# NR-143 — §4.8 搜尋結果列缺「自常用清單移除」：RecentStartIndex 在搜尋態為 -1

Phase 3 · Spec compliance · Depends on: —（使用者可見的行為缺口，規格側無歧義）

- Source: `docs/design-spec.md` §4.8（binding clause，全文引於下）、
  `AGENTS.md`（Keep search, ranking, scoring… independent of HWND and Shell COM objects
  where practical）
- Origin: 2026-08-10 第十四次全 repo 稽核（spec 軸，MEDIUM）。主 Agent 已讀
  `ShowItemMenu` 與 `panel_model.h` 驗證。
- Priority: **MEDIUM**——規格明列的功能在搜尋態整段消失，使用者看得見；修法小。

## Why

`docs/design-spec.md` §4.8（原文）：

> 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、自常用清單移除、內容（交由 Shell 的 properties verb 顯示）。

實作：`ShowItemMenu`（`src/app_host/main.cpp:2043-2078`）把「自常用清單移除」gate 在
`:2064-2070` 的 `in_recent`：

```cpp
const int recent_start = g_model->RecentStartIndex();
const bool in_recent = recent_start >= 0 && cell >= recent_start
                       && cell < g_model->RecentEndIndex();
```

而 `RecentStartIndex()` 在搜尋態（query 非空）是 **-1**（`src/app_host/panel_model.h:107-110`
：「is no recent region (a non-empty query produces search results, which belong to neither
region)」）。因此搜尋結果列**永遠**拿不到「自常用清單移除」——§4.8 明列的搜尋結果選單
功能缺了一半。NR-040 的原始理由（「pinned row 上這個命令靜默無效」）只涵蓋釘選列，
不涵蓋搜尋結果。

## Decisions already made — do not reopen

1. 判定規則（純函式，可測）：
   `ShouldOfferRemoveFromRecent(recent_start, recent_end, cell, pinned, has_usage)`：
   - 列在 recent 區內（`recent_start >= 0 && cell >= recent_start && cell < recent_end`）→ true；
   - 否則**搜尋態**（`recent_start == -1`）且**非釘選**且該 stable_id 有 usage 紀錄 → true；
   - 其餘（pinned 列、empty-query grid 的非 recent 列）→ false。
   保留 NR-040 的「不提供靜默無效命令」精神：搜尋態下 pinned 列不提供（移除 usage
   紀錄不會改變那列的顯示）；沒有 usage 紀錄的列不提供。
2. `UsageStore` 加一個唯讀 `bool HasRecord(std::wstring_view stable_id) const`
   （`usage_store.h` 的既有儲存結構是 map，一行查詢；不碰 `Forget`／`Reconcile`）。
3. 判定函式放進可測試的純值位置（依既有測試連結，首選 `panel_model.h` 內聯函式或
   `src/usage/usage_store.h`；不得放 `main.cpp`）。`ShowItemMenu` 改呼叫它。
4. **行為其餘零變更**：命令處理端（`kCmdForgetRecent` 的 `g_usage->Forget(...)`＋
   `UpdateSnapshotRanking`）不動；empty-query grid 的既有選單行為不動。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.8（binding clause）：

> 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、自常用清單移除、內容（交由 Shell 的 properties verb 顯示）。

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp`：`:2043-2110`（`ShowItemMenu` 與命令處理）。
- `src/app_host/panel_model.{h,cpp}`：`:107-116`（`RecentStartIndex`／`RecentEndIndex`）。
- `src/usage/usage_store.{h,cpp}`：紀錄儲存結構（map？vector？）、`Forget`。
- `tests/unit/panel_model_test.cpp`、`tests/unit/usage_store_test.cpp`（測試落點）。

## Scope

1. `UsageStore::HasRecord`（唯讀、const、一行查詢；需確認現有儲存結構與測試）。
2. 純函式 `ShouldOfferRemoveFromRecent`（或等價命名），含規則註解引用本 item 與 NR-040。
3. `ShowItemMenu` 的 `in_recent` 改呼叫該函式；`has_usage` 由 `g_usage`（若存在）提供，
   不存在時視為 false（與現況一致）。
4. 測試：
   - 純函式測試：搜尋態＋非釘選＋有紀錄 → true；搜尋態＋釘選 → false；
     search 態＋無紀錄 → false；recent 區內 → true（釘選與否無關）；recent 區外（grid
     非 recent 列）→ false；`recent_start == -1` 且非搜尋態（不可能，防呆）→ false。
   - `UsageStore::HasRecord` 測試：有紀錄 true、無紀錄 false、`Forget` 後 false。

## Non-goals

- 不改「移除」的執行路徑（`Forget`、`UpdateSnapshotRanking`、重繪）與其既有行為。
- 不加「從搜尋結果移除該列」的 UI 語意——§4.8 只要求命令存在；移除後列仍在
  （它是搜尋結果，不是 recent 列）。
- 不改 `RecentStartIndex`／`RecentEndIndex` 的既有語意。
- 不順手修 `ShowItemMenu` 的其他項目。

## Acceptance

1. 搜尋態下對「有 usage 紀錄且非釘選」的結果列右鍵，出現「自常用清單移除」且可執行。
2. 釘選列（任何態）與無紀錄列不出現該命令。
3. 新測試通過；既有 `panel_model_test`／`usage_store_test`／全部 CTest 綠燈。
4. Release build 零新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "usage_store|panel_model" --output-on-failure
```

```powershell
rg -n "ShouldOfferRemoveFromRecent" src/app_host/main.cpp src/app_host/panel_model.h
# expect: ShowItemMenu 內呼叫它，panel_model.h（或 chosen location）定義它
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff（2026-08-10，NR-143 done）

- 純函式放在 `src/app_host/panel_model.h:189-200`（namespace scope inline 函式），
  而非 `usage_store.h`：參數（recent_start／recent_end／cell／pinned）全是
  PanelModel 的區域語意，且 `tests/unit/panel_model_test.cpp` 已 include 此 header
  （測試目標 `nimblerun_list_vertical_slice_test` 只鏈 `nimblerun_panel_model`），
  零新增連結。`main.cpp` 已 include 同 header。
- 判決 2 的實作事實：`UsageStore` 的實際儲存是 `std::vector<UsageRecord>`
  （`usage_store.h:87`），不是 map——item 正文寫「map、一行查詢」是假設。
  `HasRecord` 照既有 `Forget` 的 `std::find_if` 風格實作（`usage_store.cpp:155-158`），
  語意不變（一行容器查詢、const、不碰 Forget／Reconcile／Load）。
- 測試落點：純函式 8 案例進 `panel_model_test.cpp`（`TestShouldOfferRemoveFromRecent`，
  `:897-922`，含防呆案例 `(-1, 3, 1, false, false)`——沒有 `recent_start >= 0` 守門的
  位置判斷會誤判成「在 recent 區內」）；`TestHasRecord` 進 `recent_usage_test.cpp`
  （`:404-423`，有紀錄 true／無紀錄 false／空 id false／Forget 後 false／不增紀錄）。
  未新增測試目標、未改 `tests/CMakeLists.txt`、測試總數維持 31。
- `ShowItemMenu`（`main.cpp:2060-2074`）：以
  `nimblerun::ShouldOfferRemoveFromRecent(RecentStartIndex(), RecentEndIndex(), cell,
  pinned, has_usage)` 取代原 inline `in_recent`；`has_usage = g_usage ? g_usage->
  HasRecord(entry.stable_id) : false`（g_usage 為 null 時視為 false，與現況一致）。
  `kCmdForgetRecent` 執行路徑（`:2110-2118` 的 `g_usage->Forget`＋
  `RefreshPanelSnapshot`）一字未動。NR-040 註解保留並補本 item 引註。
- 驗證：Release Ninja llvm-mingw 重配置＋建置零新 warning（23/23 targets）；
  `ctest` 31/31 全綠；聚焦測試 2/2（`ctest -R "recent_usage|vertical_slice"`）。
- 偏差：item Agent check 的 `ctest -R "usage_store|panel_model"` 找不到任何測試——
  CTest 名是 `nimblerun_recent_usage_test`（不含 "usage_store"）與
  `nimblerun_list_vertical_slice_test`（NR-055 例外，執行檔名與 CTest 名不同，
  不含 "panel_model"）。等價聚焦命令為 `ctest -R "recent_usage|vertical_slice"`。
- 未完成：無。
