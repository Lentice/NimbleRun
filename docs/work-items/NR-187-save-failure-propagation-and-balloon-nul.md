# NR-187 — 錯誤與持久化路徑的兩個缺口：Save 失敗被靜默忽略、ShowInfoBalloon 對 string_view 讀越界

Phase 3 · Error propagation · Depends on: —（兩項獨立、同主題「失敗要看得見」）

- Source: `docs/design-spec.md` §10.2（現文 `design-spec.md:760`，原子寫入規則）
- Origin: 2026-08-12 第十七次全 repo 稽核（codex 報告 M5；claude 報告 I-5b）
- Priority: **MEDIUM**（跨 restart 靜默遺失狀態；helper 契約在下次呼叫者傳 substring 時越界讀）

## Why

1. **持久化寫入失敗被忽略（codex M5）**：
   - `main.cpp:1094` `g_usage->Save()` 在 `RecordLaunch` 後呼叫，回傳值未檢查也不記錄。磁碟滿／權限／AV lock 時，本次使用紀錄在 restart 後消失，且診斷不可見。
   - `main.cpp:1376` `SaveCatalogCache(...)` 回傳值被丟棄；`catalog_cache.cpp:81-87` 的 `AtomicWriteUtf8Text()` 回傳值也被丟棄。cache 可重建，但失敗完全無聲。
   - 對照：pins／settings 的 Save 多數有檢查（NR-096 守門）；這兩條路徑是漏網。
2. **`ShowInfoBalloon` 對 `wstring_view` 做 `wcsncpy`（claude I-5b）**：`main.cpp:2054-2065` 對 `text.data()` 呼叫 `wcsncpy(..., sizeof(...) - 1)`。`wstring_view::data()` **不保證** NUL 結尾；目前兩個呼叫點傳 `std::wstring` 與字面字串恰好有結尾，不會炸，但簽名承諾的是 view——下一個呼叫者傳 `substr()` 就是讀越界。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.2（現文 `design-spec.md:760`）：

> 所有持久資料寫入應先寫 `.tmp`，flush 成功後以 replace 方式提交。

`AGENTS.md`：

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

## Files to read and trace first

- `src/app_host/main.cpp` — `ActivateRow` 的 `g_usage->Save()`（:1094）、`SaveCatalogCache` 呼叫（:1376）、`ShowInfoBalloon`（:2054-2065）與其全部呼叫點（:2070、:2079、:1334、:3374 一帶）、`g_diag` 的既有失敗記錄先例。
- `src/catalog/catalog_cache.cpp` — `SaveCatalogCache`（:81-87 一帶）的回傳值使用。
- `src/usage/usage_store.{h,cpp}` — `Save()` 的回傳語意與既有錯誤分類。
- `src/storage/atomic_text_file.h` — `AtomicWriteUtf8Text` 的回傳語意。

## Scope

1. **usage Save**：`main.cpp:1094` 檢查回傳值，失敗時 `g_diag->Write(L"usage", L"save failed ...")` 一行（沿用 `:1077-1081` 的 launch-failure 記錄形式；不 retry、不通知——磁碟滿時 retry 無意義，但診斷必須可見）。
2. **catalog cache Save**：讓失敗沿既有路徑記錄（`SaveCatalogCache` 回傳 bool 或直接在 cache 模組內寫 diag；**不改成 in-place write**）。選最短路徑：呼叫點檢查回傳並寫一行 diag，或 cache 模組內記錄——實作者依「不新增抽象」原則挑小的。
3. **`ShowInfoBalloon`**：參數改 `const std::wstring&`（兩個呼叫點都已是 `wstring` 或可隱式轉換），或保留 view 但用 `text.copy()` 後手動補 NUL。二選一，改小的。
4. 測試：非平凡新邏輯（若有）加 focused 測試；純改回傳檢查的話以 build＋既有 CTest 覆蓋（磁碟滿不可注入）。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-187 列。

## Non-goals

- 不改成 in-place write；不動 atomic replace 機制。
- 不新增 retry／佇列／重試 timer。
- 不改 pins／settings 既有 Save 檢查。
- 不改 UI 文字（訊息都走 `g_diag` 記錄檔，非 UI）。

## Acceptance

- usage Save 失敗至少留下一行診斷記錄（grep 可見 `g_usage->Save()` 結果被檢查）。
- catalog cache Save 失敗有診斷（grep 可見回傳值被檢查或 cache 模組內有記錄）。
- `ShowInfoBalloon` 不再對無 NUL 保證的緩衝做 `wcsncpy`（grep 確認 `text.data()` 不再進入 `wcsncpy`）。
- Release build 無 error／新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "g_usage->Save|SaveCatalogCache|ShowInfoBalloon|wcsncpy\(nid\.szInfo" src/app_host/main.cpp src/catalog/catalog_cache.cpp
```

驗證：build 無 error／新增 warning；CTest 全 Passed；兩個 Save 的失敗都有診斷路徑；`ShowInfoBalloon` 簽名或複製方式已無越界風險。

## 交接區

實作（2026-08-12）：

**1. usage Save 的失敗記錄形狀** — `main.cpp:1115`（`ActivateRow`）：

```cpp
if (!g_usage->Save() && g_diag) {
    g_diag->Write(L"usage", L"save failed");
}
```

沿用 `:1094-1098` launch-failure 先例的形狀（stage／detail 一對、null-check 的 `g_diag`）。不 retry、不通知，只有一行診斷。detail 不帶 stable_id：item 範例即 `L"usage" / L"save failed"`，最小的可用形狀。

**2. catalog cache Save 的失敗記錄形狀** — 選「呼叫點檢查回傳」而非「cache 模組內記錄」：cache 模組不持有 logger，模組內記錄等於新增依賴；`SaveCatalogCache` 回傳 bool（`catalog_cache.h:25`、`catalog_cache.cpp:86`，`WriteCache` 直接回傳 `AtomicWriteUtf8Text` 的結果，不吞錯誤）即可讓失敗沿既有路徑浮出，無新抽象。呼叫點 `main.cpp:1404`（`OnGenerationCompleteRefresh`）：

```cpp
if (!nimblerun::SaveCatalogCache(g_user_data_directory, g_refresh->Snapshot()) &&
    g_diag) {
    g_diag->Write(L"catalog_cache", L"save failed");
}
```

**3. ShowInfoBalloon 修法** — 二選一取「參數改 `const std::wstring&` ＋ bounded copy」：改簽名一行、兩個呼叫點（literal 隱式轉換、`const std::wstring&`）零改動；同時把 `wcsncpy(nid.szInfo, text.data(), ...)` 換成 `text.copy(..., cap) + 手動補 NUL`（`main.cpp:2089-2098`）。理由：`wstring&` 已保證 NUL 結尾（C++11 data() 語意），copy＋cap＋NUL 再移除對 `text.data()` 的 `wcsncpy`，即使未來有人改回 view 也不復發越界。現在 `nid` 上唯一的 `wcsncpy` 是 `szInfoTitle` 配編譯期 literal，無風險。

**驗證證據**（LLVM-MinGW Release、Ninja）：

- `cmake -S . -B build ... && cmake --build build --clean-first`：無 error、無新增 warning。唯一 warning 為 `main.cpp:1548 unused variable 'target_size'`，以 stash 後重編 HEAD 確認是既有警告，非本次引入。
- `ctest --test-dir build --output-on-failure`：32/32 Passed（75.98 s）。
- Agent-check grep：`g_usage->Save()` 結果被檢查（`main.cpp:1115`）；`SaveCatalogCache` 回傳被檢查（`main.cpp:1404`）；`ShowInfoBalloon` 簽名為 `const std::wstring&`（`main.cpp:2089`）；`wcsncpy(nid.szInfo` 已不存在。
- 接受準則四項全過：usage Save 失敗有診斷、catalog cache Save 失敗有診斷、`text.data()` 不再進入 `wcsncpy`、Release build 無 error／新增 warning 且 CTest 全綠。
- 既有測試對 bool 回傳的相容性：`catalog_refresh_test.cpp`（6 處）、`settings_store_test.cpp:555` 皆為丟棄回傳的呼叫，語意不變，未改測試。
