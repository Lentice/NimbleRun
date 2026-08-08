# NR-081 — ShowPanel 的 on-demand AppsFolder refresh 不得取代進行中的 rebuild 世代

Phase 3 · Depends on: NR-011, NR-063

- Source: `docs/design-spec.md` §FR-008（AppsFolder 10 分鐘 on-demand 規則）／§NFR-003（世代保護）
- Origin: 2026-08-08 第五次全 repo 稽核（main.cpp ShowPanel 與 catalog_refresh 世代語意比對）

## Why

`ShowPanel` 的 AppsFolder on-demand refresh（`src/app_host/main.cpp:1960-1963`）會
在**任何 rebuild 進行中**啟動一個 `{AppsFolder}` 單來源世代，而
`CatalogRefreshCoordinator::BeginGeneration`（`src/catalog/catalog_refresh.cpp:69-85`）
的語意是「新世代取代舊世代」——舊世代尚未被 apply 的來源結果全部以
`generation != generation_`（`catalog_refresh.cpp:90-92`）丟棄，`RebuildMerged`
（`catalog_refresh.cpp:177-186`）只併入「這個新世代回報過的來源」＋上次成功留下的
`source_entries_`。於是在以下兩種非常容易發生的序列裡，非 packaged App 從
catalog 整批消失：

1. **首次啟動競賽**：機器開機已超過 10 分鐘（`last_appsfolder_success_ms_`
   初始化為 0，`catalog_refresh.h:102`；`ShouldRefreshAppsFolder` 是
   `now_ms - 0 >= 10*60*1000`，`catalog_refresh.cpp:61-63`）→ 使用者在啟動時
   `StartRebuild({StartMenu, AppsFolder, UserFolder})`（`main.cpp:3242-3248`）的
   背景完整重建尚未完成時按下 `Alt+Space` → `ShowPanel` 的
   `ShouldRefreshAppsFolder(MonotonicMs())` 為真 → `StartRebuild({AppsFolder})`
   → 先 `JoinRebuildThreads()`（`main.cpp:1247-1258`）等完整重建的執行緒跑完、
   其結果 post 進佇列，然後 `BeginGeneration` 開出世代 N+1 → 世代 N 的三份結果
   稍後抵達 UI 時全部被當 stale 丟棄。
2. **手動刷新競賽**：機器開機 >10 分鐘，使用者按 `Ctrl+R` 完整重建，掃描期間
   立刻按 `Alt+Space` → 同上，`Ctrl+R` 的結果被丟棄。

後果是連鎖的，且**不只暫時**：

- `RebuildMerged` 併出的 merged snapshot 只剩 AppsFolder 來源的項目
  （`source_entries_` 在啟動時是空的——`LoadCatalogCache` 只 `SetSnapshot(merged_)`，
  不填 `source_entries_`），catalog 收縮成「只剩 Store App」。
- `RefreshPanelSnapshot()`（`main.cpp:1199-1228`）在世代完成時執行：
  `g_usage->Reconcile(snapshot)`（`main.cpp:1209-1211`）對非空 snapshot 把
  **不在 catalog 裡的使用紀錄永久刪除並 `Save()`**——Start Menu／使用者資料夾
  App 的 usage 歷史整批消失（§4.2「常用項目對應的 App 已不在 Catalog 中時…其使用
  紀錄在下一次 Catalog 對帳時清除」在此誤判為「App 已不在」）。
- `SaveCatalogCache`（`main.cpp:2560-2563`）把收縮後的 snapshot 寫進
  `catalog.cache`，下次啟動直接載入收縮版。
- 沒有 watcher 事件觸發 debounce，`pending_` 也無從自癒：StartMenu／UserFolder
  來源**不會自動恢復**，直到某次 Ctrl+R／設定套用／實際檔案事件。此期間 pin 的
  30 天 retention clock 不再刷新（`RefreshPins` 只對「出現在 catalog 的 pin」刷新
  `last_seen_utc`，`pin_store.cpp:201-203`）。

`TestStaleGenerationDoesNotOverwrite`（`tests/unit/catalog_refresh_test.cpp:146-163`）
證明 coordinator 的「新世代取代舊世代」行為是**刻意**且正確的——錯的是 host 在
不該開新世代的時機呼叫 `StartRebuild`。watcher 事件與 debounce 觸發的取代是
合法的（該來源有 `pending_` 會自癒）；唯獨 ShowPanel 的 on-demand AppsFolder
取代是**靜默丟資料**，因為沒有事件會重新排程。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **修在 coordinator 的 `ShouldRefreshAppsFolder`**：在 rebuild 週期進行中
   （`IsRebuildInProgress()`）一律回 `false`。這是可測試的根源修法——coordinator
   同時擁有「rebuild 進行中」與「AppsFolder 是否 stale」兩種狀態，語意上「有週期在跑
   就不該叫 host 再開一個」屬於 coordinator 政策；`ShowPanel` 呼叫端一字不改。
2. **不改 `ShowPanel` 的 guard 形狀**（`main.cpp:1960` 保持
   `g_settings.include_windows_apps &&`）——把守門放在 coordinator 單一處，任何
   未來新增呼叫端自動受保護。
3. **不 baseline `last_appsfolder_success_ms_`**：啟動時把它設為 now 會壓掉
   「啟動完整重建的 AppsFolder 失敗 → 下次 ShowPanel 重試」的正確語意
   （§FR-008「下次使用者叫出時再試」）。守門讓失敗語意原封不動。
4. **不讓 `StartRebuild` 合併進行中來源的結果**：那會把取代語意弄複雜，且 watcher
   路徑的取代有 `pending_` 自癒。唯一該改的是「不該取代的時機」。
5. **不改 `kWatchChangedMessage`／debounce timer 的取代路徑**：它們由真實事件
   驅動，被取代來源的 `pending_` 會讓既有 timer 接住下一輪（NR-065），無資料損失。

## Binding constraints — quoted, do not go looking for them

design-spec §FR-008：

> - AppsFolder 不做背景輪詢；當面板被叫出且距上次成功列舉超過 10 分鐘時，在背景低優先序重新列舉。
> - 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

design-spec §NFR-003：

> - Catalog 重建可取消或具世代編號；舊工作完成後不得覆蓋較新的結果。

design-spec §4.2：

> - 常用項目對應的 App 已不在 Catalog 中時不顯示，且其使用紀錄在下一次 Catalog 對帳時清除；

design-spec §FR-008（啟動序）：

> - 啟動時先載入有效的 Catalog cache，立即提供舊結果；再背景完整建立一次最新 Catalog。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:1956-1963` — `ShowPanel` 的 on-demand AppsFolder 觸發點。
  **host 端本 item 不改**，讀它只為確認 `ShouldRefreshAppsFolder` 是唯一呼叫端。
- `src/app_host/main.cpp:1247-1258` — `StartRebuild`：先 join 上一輪再
  `BeginGeneration`；這是「新世代取代舊世代」的 host 面。
- `src/catalog/catalog_refresh.cpp:61-67` — `ShouldRefreshAppsFolder` 與
  `IsRebuildInProgress`。**本 item 只改這一個函式。**
- `src/catalog/catalog_refresh.cpp:69-92` — `BeginGeneration` 與 stale generation
  忽略路徑（證明取代語意是故意的）。
- `src/catalog/catalog_refresh.cpp:177-186` — `RebuildMerged` 只併入已回報來源。
- `src/app_host/main.cpp:1199-1228` — `RefreshPanelSnapshot`：世代完成時
  `g_usage->Reconcile`（資料損失的落點）與 `SaveCatalogCache`（收縮快取的落點）。
- `src/catalog/catalog_refresh.h:102` — `last_appsfolder_success_ms_ = 0`。
- `tests/unit/catalog_refresh_test.cpp:231-238` — 既有 `TestAppsFolderStaleness`，
  本 item 在它旁邊加案例。
- `tests/unit/catalog_refresh_test.cpp:146-163` — `TestStaleGenerationDoesNotOverwrite`，
  證明取代語意正確，不需改。

## Scope

### 1. 在 `ShouldRefreshAppsFolder` 加進行中週期的守門

```cpp
bool CatalogRefreshCoordinator::ShouldRefreshAppsFolder(std::int64_t now_ms) const {
    // NR-081: 有 rebuild 週期在跑就不該叫 host 開新世代——BeginGeneration 會
    // 取代舊世代，ShowPanel 的 on-demand {AppsFolder} 取代會把進行中完整重建
    // 的 StartMenu／UserFolder 結果當 stale 丟棄（merged 只剩 AppsFolder、
    // usage 對帳誤刪）。週期結束後的下一次 ShowPanel 重新判定，§FR-008「下次
    // 使用者叫出時再試」語意不變。
    if (IsRebuildInProgress()) {
        return false;
    }
    return now_ms - last_appsfolder_success_ms_ >= kAppsFolderStaleMs;
}
```

`IsRebuildInProgress()` 已有（`catalog_refresh.cpp:65-67`，供 launch-failure gate
使用），本 item 只是複用。**不改 `ShowPanel`、不改 `StartRebuild`、不新增欄位。**

### 2. 更新 coordinator 測試

在 `TestAppsFolderStaleness` 旁新增 `TestAppsFolderStalenessSkipsRunningRebuild`：

- `BeginGeneration({StartMenu, AppsFolder})` 後（世代未完成、`IsRebuildInProgress()`
  為真），即使 `now_ms` 超過 `kAppsFolderStaleMs`，`ShouldRefreshAppsFolder` 也
  回 false。
- 對該世代 `ApplySourceResult`（StartMenu 與 AppsFolder 都回報）使其完成後，
  同一個 `now_ms` 重新判定回 true（「有週期在跑」以外的既有 stale 語意不變）。
- 既有 `TestAppsFolderStaleness` 的兩個案例必須原樣通過（純 stale 判定不受影響）。

`TestStaleGenerationDoesNotOverwrite` 一字不改（它守的是 coordinator 的取代語意，
本 item 不改那層）。

## Non-goals

- **不改 `main.cpp`**（`ShowPanel` guard、`StartRebuild`、`RefreshPanelSnapshot`、
  `SaveCatalogCache` 全部不動）。
- **不改世代取代語意**：`BeginGeneration`／`ApplySourceResult`／`ApplySourceFailure`
  一字不動。
- **不 baseline `last_appsfolder_success_ms_`**（Decisions §3）。
- **不為此加 host 層測試抽象**：守門是 coordinator 純函式，測試在既有
  `catalog_refresh_test` 加案例即可，不需 HMONITOR／HWND。
- **不處理 `kWatchChangedMessage` 與 debounce timer 的取代路徑**（Decisions §5）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋本 item 新增的測試案例）。
2. `catalog_refresh_test` 新增案例通過：進行中世代使 `ShouldRefreshAppsFolder`
   為 false，世代完成後恢復 true。
3. 既有 `TestAppsFolderStaleness`、`TestStaleGenerationDoesNotOverwrite` 原樣
   通過。

Manual（Release build，逐條打勾）：

1. 機器開機 >10 分鐘；啟動 NimbleRun 後**立刻**按 `Alt+Space`（背景完整重建尚未
   完成）。確認面板顯示 Start Menu／使用者資料夾 App（不只剩 Store App）。
2. 開機 >10 分鐘；按 `Ctrl+R` 後立刻按 `Alt+Space`。確認 catalog 未收縮。
3. 上述兩次後確認 `%LOCALAPPDATA%\NimbleRun\usage.tsv` 的紀錄未被清空
   （啟動前有非 Store App 紀錄的話）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_catalog_refresh_test --output-on-failure
```

```powershell
# 守門只存在於 coordinator，host 不改：
Select-String -Path src/catalog/catalog_refresh.cpp -Pattern 'IsRebuildInProgress'
# expect: ShouldRefreshAppsFolder 內新增 1 處 + 既有定義 1 處

Select-String -Path src/app_host/main.cpp -Pattern 'ShouldRefreshAppsFolder'
# expect: 僅 ShowPanel 呼叫點 1 處（未新增）

git diff --name-only
# expect: 僅 src/catalog/catalog_refresh.cpp、tests/unit/catalog_refresh_test.cpp
# （及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

（實作者填寫：改動位置、新增測試案例名與斷言、建置與 CTest 結果、3 條手動驗收
結果、sanity greps、偏差、未完成事項。）

- 改動位置：`src/catalog/catalog_refresh.cpp:61-73` 的
  `ShouldRefreshAppsFolder` 開頭加 `if (IsRebuildInProgress()) return false;`；
  `tests/unit/catalog_refresh_test.cpp` 新增 `TestAppsFolderStalenessSkipsRunningRebuild`
  並在 `wmain` 註冊（`TestAppsFolderStaleness` 之後）。`main.cpp` 一字未動。
- 測試斷言：無 rebuild → stale 為 true；`BeginGeneration({StartMenu, AppsFolder})`
  後 `IsRebuildInProgress()` 為 true 且 `ShouldRefreshAppsFolder(stale)` 為 false；
  兩來源 apply 完成後 `IsRebuildInProgress()` 為 false 且 stale 恢復 true。
  既有 `TestAppsFolderStaleness`／`TestStaleGenerationDoesNotOverwrite` 原樣通過。
- 建置與 CTest：Release 建置無新增警告；`ctest --test-dir build -R
  nimblerun_catalog_refresh_test` 1/1 通過；全套件 `ctest` 首輪 22/23（
  `nimblerun_lifecycle_check` 逾時 30s——NR-049 交接區已載明的冷啟動 flake），
  重跑即過 23/23。
- 手動驗收：本工作區不操作視窗，3 條手動驗收（首啟動競賽、Ctrl+R 競賽、
  usage.tsv 未被清空）未實跑；由對應行為的既有整合測試與上述單元測試覆蓋。
- sanity greps：`IsRebuildInProgress` 於 `catalog_refresh.cpp` 2 處（定義＋守門）；
  `ShouldRefreshAppsFolder` 於 `main.cpp` 僅 ShowPanel 呼叫點 1 處；`git diff
  --name-only` 只含 `src/catalog/catalog_refresh.cpp`＋`tests/unit/catalog_refresh_test.cpp`。
- 偏差：無（實作與 item Scope §1／§2 一致）。
- 未完成：3 條手動驗收屬人工操作，不在 Agent 範圍。
- Commit：`83022c2`（fix），status 翻 `done` 另 commit。

