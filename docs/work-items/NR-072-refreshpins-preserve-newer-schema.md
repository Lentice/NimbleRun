# NR-072 — A newer-schema or partially corrupt `favorites.txt` must not be overwritten by `RefreshPins`

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.4（較新 schema 不覆寫原檔）／§11（設定損壞處置）／FR-011（pin 30 天保留）
- Origin: 2026-08-08 第四次全 repo 稽核（catalog/settings/pins/usage/storage 子系統）

## Why

`RefreshPins()`（`src/app_host/main.cpp:1113-1145`）在 `Load()` 之後**無條件**呼叫
`g_pins->Reconcile(...)` 與 `g_pins->Save()`（`:1141-1143`）：

```cpp
g_pins->Reconcile(g_refresh->Snapshot(),
                  static_cast<std::int64_t>(std::time(nullptr)));
g_pins->Save();
```

而 `PinStore::Load()`（`src/pins/pin_store.cpp:27-42`）對非 `Loaded` 結果的回傳狀態
是：`NewerSchema` 回傳時 `pins_` 已被 `:27` 清空；`Corrupt` 回傳時 `pins_` 保留**部分
解析結果**（`favorites.txt` 中排在損壞列之前的合法列早已 `push_back` 進 `pins_`，
見 `:45-72` 迴圈）。兩者結合造成兩條使用者資料損失路徑：

1. **較新 schema 被覆寫成空檔**：使用者先用較新版本（schema=3）寫入 `favorites.txt`，
   再執行這個較舊 build。`Load()` 回 `NewerSchema`、`pins_` 為空，`RefreshPins` 的
   `Save()` 立刻把整份釘選清單覆寫成**空的 schema=2 檔**。design-spec §10.4 明文
   「不覆寫原檔」。`RefreshPins` 在每次面板顯示（`main.cpp:1890` ShowPanel →
   RefreshPanelSnapshot）與每次 rebuild 完成（`:2342`）都會跑，所以使用者按一次
   `Alt+Space` 就毀掉釘選。**Pins 是有 30 天保留契約的使用者資料（§FR-011），不是
   可重建快取**；`load_notice.h` 對 `NewerSchema` 顯示的「They were left unchanged」
   文案也變成謊言。
2. **損壞檔的部分解析結果被當成新真相保存**：`favorites.txt` = 8 筆有效 pin + 1 列
   損壞 → `Load` 回 `Corrupt`（原檔被 `PreserveCorrupt` 改名為 `.corrupt`），但
   `pins_` 仍含那 8 筆；`RefreshPins` 的 `Save()` 把那 8 筆寫成新的 live `favorites.txt`，
   原始檔中損壞列**之後**的合法 pin 從 live store 永久消失（只能在 `.corrupt` 裡找回）。
   `pin_store.h:24-25` 的契約明寫「For anything other than Loaded the store is empty」。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **host 端守門**：`RefreshPins` 只在 `Load()` 結果為 `Loaded` 或 `Missing` 時才
   `Reconcile`＋`Save`。`Corrupt`／`NewerSchema` 只做既有的日誌＋一次性 balloon
   （NR-058 機制），不碰磁碟。`Missing` 仍要 Save（首次執行建立空檔、之後 Pin 才有
   落點）——「略過 Save」只針對「這個 build 讀不懂」的兩種結果。
2. **store 端契約兌現**：`PinStore::Load` 的 `Corrupt` 回傳前 `pins_.clear()`，讓
   「非 Loaded 則 store 為空」不再是註解而是事實。`NewerSchema` 本來就空（`:27`），
   不需改。
3. **`SetPins` 照舊**：非 `Loaded` 時 `pins_` 為空 → model 不顯示任何 pin，符合
   §10.4「退回安全預設」。下次使用者主動 Pin 才建立新的 live 檔。
4. **測試只落在 `pin_store_test`**（temp 目錄 fixture）：host 端 `RefreshPins` 的守門
   沒有測試 seam（吃全域 pointer＋HWND），由手動驗收與 sanity grep 覆蓋（NR-060
   先例：不為測試點發明抽象）。
5. **不新增列舉值、不換 schema、不改其他 store。**

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Never simplify away: input validation at trust boundaries, error handling that prevents data loss.
- Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep all user data under `%LOCALAPPDATA%\NimbleRun`.

design-spec §10.4：

- 遇到較新且不支援的版本：**不覆寫原檔**。將功能退回安全預設。
- 快取類檔案遇到較新 schema 時不覆寫原檔、停用該快取；**使用者資料的既有規則不變**。

design-spec §11：

- 設定損壞 → 採預設值並通知；原檔改名保存，不靜默覆寫。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:1108-1145` — `RefreshPins`。主場之一（`:1141-1143` 的
  無條件 `Reconcile`＋`Save`）。呼叫端：`main.cpp:1184`（RefreshPanelSnapshot）、
  `:1890`（ShowPanel）。
- `src/pins/pin_store.cpp:26-74` — `PinStore::Load`。主場之二（`:40-42`／`:53-56`／
  `:59-62` 三個 `Corrupt` 回傳點都帶著部分 `pins_`）。`Save`（`:76-94`）與
  `Reconcile`（`:185-215`）只讀，確認語意即可。
- `src/pins/pin_store.h:24-31` — `PinLoadResult` 列舉與「非 Loaded 則 store 為空」契約。
- `src/diagnostics/load_notice.h` — NR-058 的 balloon 文案（`NewerSchema` 顯示
  "They were left unchanged"）；本 item 不改它，但要確認修好後文案才為真。
- `tests/unit/pin_store_test.cpp` — 新 case 的家；照既有「手寫 fixture 檔 → Load →
  斷言」寫法。

## Scope

### 1. `RefreshPins` 守門（`src/app_host/main.cpp:1141-1143`）

把無條件區塊包進結果守門：

```cpp
// NR-072: a newer-schema file is another build's data -- never touch it
// (design-spec §10.4); a corrupt load must not let a partial parse become
// the live file. Only a Loaded (or Missing) store may be reconciled and
// persisted. This also stops an empty pins_ from clobbering favorites.txt.
if (result == nimblerun::PinLoadResult::Loaded ||
    result == nimblerun::PinLoadResult::Missing) {
    g_pins->Reconcile(g_refresh->Snapshot(),
                      static_cast<std::int64_t>(std::time(nullptr)));
    g_pins->Save();
}
g_model->SetPins(g_pins->Records());
```

`SetPins` 留在守門外：`Corrupt`／`NewerSchema` 時 `Records()` 為空（配合 §Scope 2），
model 收到空清單即「無 pin 顯示」，正是 §10.4 的退回預設。

### 2. `PinStore::Load` 兌現「非 Loaded 則為空」契約

`src/pins/pin_store.cpp` 的三個 `Corrupt` 回傳點（`:42`、`:55`、`:61`）在
`return PinLoadResult::Corrupt;` 之前各加 `pins_.clear();`（或把整段搬成
`pins_.clear(); return ...`）。`NewerSchema` 回傳點不需改（`:27` 已清）。

### 3. 測試（`tests/unit/pin_store_test.cpp`）

- **較新 schema**：手寫 `schema=3` 的 `favorites.txt` → `Load` 回 `NewerSchema`；
  斷言 `Records()` 為空、且**檔案內容 byte-for-byte 不變**（這是 §10.4「不覆寫」的
  直接守門）。
- **損壞列在中段**：手寫 `schema=2` 檔，含 2 筆有效列＋1 列 3-field 錯誤 → `Load`
  回 `Corrupt`、`Records()` 為空（不是 2 筆）；斷言 `.corrupt` 改名發生。
- **既有案例回歸**：`Loaded`／`Missing`／`OlderSchema` 升級路徑的既有斷言一字不改。

### 4. 更新 spec？

不需。§10.4／§11 描述的行為層級未動——本次是讓實作符合既有規格。

## How this stays maintainable

`RefreshPins` 是 pins 的唯一載入出口（grep 確認 `g_pins->Save()` 只在 `:1143`），守門
放這裡一次保護所有「載入後隱含持久化」的路徑（ShowPanel／rebuild／設定套用）。
`PinStore` 的「非 Loaded 則空」契約現在與 usage／settings 兩個 store 的 header 契約
（`usage_store.h:21`、`settings_store.h:49-50`）一致——本 item 只收 pins，其餘兩個
store 的部分狀態洩漏由 **NR-080（settings/usage Load 亦洩漏部分狀態）** 處理。

## Non-goals

- **不改 `UsageStore`／`SettingsStore`**（各自的部分狀態洩漏屬另一個 item）。
- **不新增列舉值、不換 `favorites.txt` schema、不寫遷移碼。**
- **不為 `RefreshPins` 發明測試 seam**（NR-060 先例：不為測試點抽抽象）。
- **不改 balloon 文案與通知機制（NR-058 範圍）。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋新增 case）。
2. §Scope 3 兩個新 case 通過（較新 schema 檔案 byte 不變；中段損壞 → 空 store）。

Manual：

3. 手寫 `schema=3` 的 `favorites.txt`（含幾筆假 pin），啟動較舊 build、按 `Alt+Space`
   開面板：檔案內容不變、無 crash、tray balloon 顯示較新 schema 通知一次、面板釘選區
   為空。
4. 手寫含中段損壞列的 `favorites.txt`，開面板：`favorites.txt` 被改名 `.corrupt`
   保留、live 檔**不再**被寫出部分 pin（下次主動 Pin 才建立新檔）、balloon 通知一次。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# RefreshPins 的 Save 被守門包住：
Select-String -Path src/app_host/main.cpp -Pattern "g_pins->Save"
# expect: 1 處，且位於 NR-072 守門區塊內

# PinStore::Load 的 Corrupt 回傳點在回傳前清空：
Select-String -Path src/pins/pin_store.cpp -Pattern "pins_\.clear\(\)"
# expect: 至少 4 處（Load 開頭 1 + Corrupt 回傳 3）

# 改動範圍：
git diff --name-only
# expect: src/app_host/main.cpp、src/pins/pin_store.cpp、
#         tests/unit/pin_store_test.cpp
```

## 交接區

（實作者填寫：守門的實際形狀、三個 Corrupt 回傳點的清理方式、兩個新 case 的 fixture
寫法與 byte-compare 斷言、建置與 CTest 結果、sanity greps、偏差、未完成事項。）

實作（2026-08-08）：

- **守門形狀**：`main.cpp` 的 `RefreshPins` 把無條件 `Reconcile`＋`Save` 包進
  `if (result == Loaded || result == Missing) { ... }` 區塊，`SetPins` 留在守門外
  （`Corrupt`／`NewerSchema` 時 `Records()` 為空 → model 收到空清單）。NR-072 註解
  四行照 item 正文。
- **Corrupt 回傳點清理**：`pin_store.cpp` 三個 `Corrupt` 回傳點（`default` arm 與
  迴圈內兩處）在 `PreserveCorrupt` 之後、`return` 之前各加 `pins_.clear()`；
  `NewerSchema` 回傳點未改（`:27` 開頭已清）。
- **測試**：新增 `TestCorruptMidFileClearsPins`（schema=2 檔含 2 筆有效列＋1 列
  1-field 錯誤）——斷言回 `Corrupt`、`Records()` 為空、原檔改名 `.corrupt` 且內容
  verbatim 保留。較新 schema 的 byte-unchanged 守門由既有 `TestNewerSchema` 覆蓋
  （已斷言 `Records()` 空＋檔內容不變＋無 `.corrupt`），故未另立 duplicate case。
  既有案例一字未改。
- **建置與 CTest**：Release build 無新增警告；`ctest` 23/23 全綠。
- **sanity greps**：`g_pins->Save` 在 `RefreshPins` 內僅 1 處且位於守門區塊
  （另有 2 處為選單/拖曳重排的顯式 Save，非載入後隱含持久化路徑，屬預期）；
  `pins_.clear()` 4 處（Load 開頭 1＋Corrupt 回傳 3）；`git diff --name-only`＝
  main.cpp、pin_store.cpp、pin_store_test.cpp。
- **偏差**：item 正文「`g_pins->Save()` 只在 `:1143`」的審計筆記與現況不符
  （選單與拖曳重排各有一處顯式 Save），守門語意不受影響；未完成事項：無。
