# NR-134 — Snapshot 組裝的順序契約要有擁有者：`CatalogSnapshotAssembler`

Phase 3 · Code structure · Depends on: NR-061、NR-072、NR-083、NR-116（皆 done）

- Source: `AGENTS.md`（Keep search, ranking, scoring, persistence formats, and other core
  logic independent of HWND and Shell COM objects where practical；New non-trivial logic
  needs one focused runnable test）、`docs/design-spec.md` §4.5／§4.6／§10.2
- Origin: 2026-08-10 架構審查（Claude 軸候選 4）。主 Agent 已讀 `main.cpp:1420-1467` 逐行確認。
- Priority: **MEDIUM**（`PanelModel`／`PinStore`／`UsageStore` 各自測試充分，**組合零測試**，
  而 NR-061／NR-072／NR-083／NR-116 四次修補全落在這個組合上，其中 NR-061 是資料遺失級）

## Why

`RefreshPanelSnapshot`（`src/app_host/main.cpp:1422-1467`）用 45 行、透過五個全域
（`g_model`、`g_refresh`、`g_usage`、`g_pins`、`g_settings`）以**嚴格順序**編排五個模組，
而那些順序約束只以函式內的散文註解存在：

- `:1440`「Pins are loaded first because they feed the is_pinned stamp.」
- `:1460`「SetRecent is last, so its RefreshRows is the one that decides the visible rows」
- `:1426-1438`／`:1463-1465` `snapshot_index` 是 borrowed view：在 `RefreshPins → SetPins`
  之前 `SetCatalogIndex(&snapshot_index)`、在 `SetRecent` 之後 `SetCatalogIndex(nullptr)`。
  這是 dangling `wstring_view` 風險，**唯一的保護就是這兩個呼叫剛好在同一個函式裡**。
- `:1444-1446`「Guarded on a non-empty snapshot: reconciling against an empty one during
  startup would wipe every record.」——NR-061，一個資料遺失 bug。

每一條都是跨模組的真實不變式，而每一條都**無法執行、無法測試**：函式讀全域，且只編進 `.exe`。

## Decisions already made — do not reopen

1. 抽成一個小 class，四個協作者由**建構子以 reference 注入**（coordinator、usage store、
   pin store、panel model；`Settings` 以值或 const ref 傳入）。不做 DI 容器、不做 interface、
   不做 registry。
2. **編成 library**，讓測試能在記憶體中構造協作者。這是它目前零測試的機械原因。
3. 介面就兩個成員：`Refresh()` 與 `OnPinsChanged(bool refresh_rows = true)`（drag-reorder
   路徑）；可選參數只保留 launch 原本的 stamp-only 行為，不增加第三個 public member。
   不要為了讓 `main.cpp` 少改而加第三、第四個。
4. **pin load notice 分支（`main.cpp:1358-1376`）改為回傳值**，由 host 轉成 balloon。
   那是這個函式裡唯一碰視窗的部分（`g_tray_icon_active`／`g_main_window`），必須移出去，
   否則模組不是 HWND-free（違反 `AGENTS.md`）。
5. `SyncAccessibility(g_main_window)`（`:1466`）**留在 host**，由 host 在 `Refresh()` 之後呼叫。
6. **行為零變更**，四條註解裡的不變式（順序、borrowed view 生命週期、非空 snapshot 守門）
   原封搬移，且**註解連同 NR 編號一起帶走**。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`main.cpp:1444-1446`（NR-061 的既有規則，不得反悔）：

> Guarded on a non-empty snapshot: reconciling against an empty one during startup would
> wipe every record.

## Files to read and trace first

- `src/app_host/main.cpp:1349-1467`（`RefreshPins`、`StampRankingFields`、
  `RefreshPanelSnapshot`）與所有呼叫 `RefreshPanelSnapshot()` 的位置。
- `src/app_host/panel_model.h`（`SetCatalogIndex`／`SetPins`／`SetRecent`／`RefreshRows` 語意）。
- `src/pins/pin_store.h`、`src/usage/usage_store.h`（`Reconcile`／`Recent`／`Save`）。
- `src/catalog/catalog_refresh.h`（`Snapshot()`／`MutableSnapshot()` 的生命週期）。
- `docs/work-items/NR-061`、`NR-072`、`NR-083`、`NR-116` 的 Decisions 與交接區。

## Scope

1. 新增 `src/app_host/snapshot_assembler.{h,cpp}`（或放 `src/catalog/`，擇一並寫明理由），
   建成 library。介面：

   ```cpp
   class CatalogSnapshotAssembler {
   public:
       CatalogSnapshotAssembler(CatalogRefreshCoordinator&, UsageStore&, PinStore&,
                                PanelModel&, const Settings&);
       struct Result { bool pin_load_notice = false; /* 需要時再加，不預先加 */ };
       Result Refresh();
       Result OnPinsChanged(bool refresh_rows = true);
   };
   ```

2. `RefreshPins`／`StampRankingFields`／`RefreshPanelSnapshot` 的邏輯搬入，五個全域的讀取
   改為成員 reference。`main.cpp` 只留一個 assembler 實例＋`SyncAccessibility` 與 balloon 呼叫。
3. 新增 `tests/unit/snapshot_assembler_test.cpp`，**必測案例**（每條對應一次修過的 bug）：
   - 空 snapshot 時**不得** `Reconcile`、不得 `Save`（NR-061，資料遺失）
   - 損壞的 favorites 載入後不得被 reconcile 或回寫（NR-072）
   - pin 在 `SetRecent` 之前完成，`is_pinned` 標記正確（順序契約）
   - `SetCatalogIndex` 在 `Refresh()` 返回時已被清為 `nullptr`（borrowed view 生命週期）
   - recent 清單只收得進 snapshot 的項目，數量受 `recent_count` 限制
   依 NR-055 的 list-plus-loop 註冊到 `tests/CMakeLists.txt`，依 NR-129 用 `test_util.h`。

## Non-goals

- 不改 §4.6 的排序／評分公式、不改 favorites／usage 的 schema 或檔案格式。
- 不改 `PanelModel`、`PinStore`、`UsageStore` 的公開介面（除非搬移暴露必要的最小調整）。
- 不合併 pins 與 usage 兩個 store，不動它們的持久化路徑。
- 不順手處理 rebuild 編排（NR-132）或 slot 幾何（NR-133）。

## Acceptance

1. `main.cpp` 不再直接持有 snapshot 組裝順序；`RefreshPanelSnapshot` 的五個全域讀取歸零。
2. assembler 是 HWND-free（grep：新檔不出現 `HWND`／`g_main_window`／`Shell_NotifyIcon`）。
3. 五類測試存在並通過，特別是「空 snapshot 不得 wipe usage」。
4. 行為零變更；Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "HWND|g_main_window|Shell_NotifyIcon" src/app_host/snapshot_assembler.h src/app_host/snapshot_assembler.cpp
# expect: 零命中。
```

## Handoff

### Final implementation

- 新增 `src/app_host/snapshot_assembler.{h,cpp}`，放在 `app_host` 是因為它擁有
  panel snapshot 的跨模組編排；實作仍編成 `nimblerun_snapshot_assembler` library，
  不依賴 HWND、Shell 或 COM。
- 最終介面是
  `CatalogSnapshotAssembler(CatalogRefreshCoordinator&, UsageStore&, PinStore&, PanelModel&, const Settings&)`、
  `Result Refresh()` 與 `Result OnPinsChanged(bool refresh_rows = true)`。四個協作者以 reference 注入，設定以
  `const Settings&` 注入；沒有 DI container、interface 或 registry。
- `Result` 實際欄位為 `bool pin_load_notice` 與 `PinLoadResult pin_load_result`。
  `pin_load_notice` 只在 `Corrupt`／`NewerSchema` 時為 true；`pin_load_result` 讓 host
  保留原本 Corrupt／NewerSchema 的不同 balloon 語意。
- `Refresh()` 的固定順序為：建立 borrowed `stable_id` index →
  `SetCatalogIndex(&index)` → 載入／對帳 pins → 在非空 snapshot 才 reconcile usage →
  stamp `is_pinned`／`usage_score` → `SetCatalog` → 解析並限制 recent →
  `SetRecent` → `SetCatalogIndex(nullptr)`。`OnPinsChanged()` 用於 drag reorder、Pin／Unpin
  的 derived-field／pin-region 更新；launch 以 `OnPinsChanged(false)` 只重蓋 ranking fields，
  保留原本不重建 visible rows 的行為；兩者都不重新載入 favorites.txt。

### Host notification path

- assembler 不碰視窗。`main.cpp::RefreshPanelSnapshot()` 呼叫 `Refresh()`，再把純值
  `Result` 交給 `HandlePinLoadResult()`；host 以 `StoreLoadResultName` 寫既有診斷，
  以 `StoreLoadIssueFor` 組合既有一次性 `StoreLoadNoticeText` balloon。
- tray 尚未建立時，issue 累積到 `g_store_load_issues`；tray 建立後沿用 startup send point
  顯示一次。tray 已存在時直接呼叫既有 `ShowLoadIssueNotice(g_main_window, text)`。
  `SyncAccessibility(g_main_window)` 留在 host，且在 `Refresh()` 返回後呼叫。

### Focused assertions

`tests/unit/snapshot_assembler_test.cpp` 以現有 `test_util.h` 的 `Expect` 提供五類必要案例與
一個更新模式案例，並以 NR-055 list-plus-loop 註冊為 `nimblerun_snapshot_assembler_test`：

1. `TestEmptySnapshotDoesNotWipeUsage`：空 snapshot refresh 後 usage record 仍在記憶體，
   `usage.tsv` bytes 不變，覆蓋 NR-061 的不得 `Reconcile`／`Save`。
2. `TestCorruptPinsAreNotReconciledOrSaved`：Corrupt result 與 notice 回傳、live
   `favorites.txt` 不被重建、`.corrupt` 原檔保留，覆蓋 NR-072。
3. `TestPinsAreStampedBeforeRecentRows`：pin row 先出現且 `is_pinned == true`，覆蓋
   pins 在 `SetRecent` 前完成的順序契約。
4. `TestCatalogIndexIsClearedOnReturn`：`Refresh()` 返回後
   `PanelModel::HasCatalogIndex()` 為 false，覆蓋 borrowed index lifetime。
5. `TestRecentRowsAreSnapshotBoundAndCapped`：不存在於 snapshot 的 usage record 不進入
   rows，剩餘 recent 依 `recent_count == 2` 截斷並保持 newest-first 順序。
6. `TestPinChangeRefreshMode`：`OnPinsChanged(false)` 只更新 catalog ranking、不重建
   visible rows；`OnPinsChanged(true)` 重新發布 pin rows，固定 launch 與 pin-edit 的行為分流。

### Invariant comment locations

- `src/app_host/snapshot_assembler.cpp:59-64`：NR-083 borrowed index 建立並在所有
  `RefreshRows` 觸發點完成後清除；`src/app_host/snapshot_assembler.cpp:91-92` 再次標明
  返回前不得留下 stack-owned view。
- `src/app_host/snapshot_assembler.cpp:68-69`：pins 先載入，因為它供 `is_pinned` stamp 使用。
- `src/app_host/snapshot_assembler.cpp:72-75`：NR-061 只允許非空 snapshot 做 usage reconcile，
  避免 startup 空 snapshot wipe records。
- `src/app_host/snapshot_assembler.cpp:88-89`：`SetRecent` 最後執行，讓它的 `RefreshRows`
  決定最終 visible rows。

### Validation evidence

- `cmake --build build`：Release build 成功，沒有新增 warning 輸出；新增 assembler library、
  host link 與 focused test target 均可編譯連結。
- focused sandbox 執行曾實際失敗：
  `filesystem error: in create_directories: 存取被拒。 ["C:\\Users\\lenticetsai\\AppData\\Local\\Temp\\NimbleRun_snapshot_assembler_test_6744_empty"]`
  （exit `0xc0000409`，屬環境權限，不修改測試規則）。同一 focused CTest 以 elevated
  權限重跑通過：`1/1`。
- 完整 `ctest --test-dir build --output-on-failure` 以 elevated 權限通過：`30/30`，包含
  `nimblerun_snapshot_assembler_test`、lifecycle check 與既有全部測試。
- `rg -n "HWND|g_main_window|Shell_NotifyIcon" src/app_host/snapshot_assembler.h src/app_host/snapshot_assembler.cpp`
  零命中；`git diff --check` 無輸出。
