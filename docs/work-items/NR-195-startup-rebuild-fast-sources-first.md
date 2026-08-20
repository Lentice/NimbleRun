# NR-195 — 開機重建先跑快來源，UserFolder 延後成第二階段

Phase 3 · Rebuild pipeline · Depends on: NR-132、NR-081、NR-116、NR-118（皆 done）

- Source: `docs/design-spec.md` §FR-008、§9.3
- Origin: 2026-08-20 使用者回報冷開機 Alt+3 卡在「還在準備中」（`src/app_host/main.cpp` `kStillPreparing` 訊息）；`grill-with-docs`／domain-modeling 已確認範圍與機制
- Priority: **HIGH**——直接對應使用者回報的原始問題根因

## 問題與根因

實測（見 NR-194 的 benchmark 依據）確認冷開機時 `rebuild-ms startmenu` 約 4 秒、`rebuild-ms appsfolder` 約 0.9 秒、`rebuild-ms userfolder` 約 21.6 秒。`main.cpp` 目前在 `wWinMain`（`:3436-3439`）用**一次** `StartRebuild({StartMenu, AppsFolder, UserFolder})` 呼叫開機重建，這三個來源被歸在**同一個** generation：`CatalogRefreshCoordinator::GenerationComplete()`（`catalog_refresh.cpp:221-234`）要求該 generation 的**全部** `active_sources_` 都回報，才會 `RebuildMerged()` 發布新的合併 snapshot（讓 StartMenu／AppsFolder 的項目變成 `launch_verified=true`，NR-113）。

也就是說：即使 StartMenu 與 AppsFolder 早在 4 秒內就跑完，**這兩個來源的項目要等 UserFolder 的 21.6 秒也跑完，才會變成可啟動**——這正是使用者回報「開機後按 Alt+3 顯示還在準備中」的直接原因，跟 Obsidian（很可能是 StartMenu 或 AppsFolder 來源的項目）本身無關。

## Goal

只改 `wWinMain` 的開機重建路徑：先用一個 generation 跑 `{StartMenu, AppsFolder}`，等它完成、且既有的 `OnGenerationCompleteRefresh()` 完成該 generation 的既有收尾（診斷、面板刷新、cache 寫入）之後，再啟動第二個 generation 只跑 `{UserFolder}`。快來源的項目因此能在約 4 秒內變成 `launch_verified=true`，不必再等 UserFolder 的大型掃描。

## 已確認的產品決策

1. **只改開機這一個呼叫點**：`main.cpp` 目前有四個「全來源」`StartRebuild` 呼叫點（開機 `:3437`、tray／選單 `:2591`、`:2645`、以及一個在 `:1090`）。本 item 只改開機（`:3436-3439`）那一處。其餘三處維持一次性全來源 `Request`，理由：那些觸發情境通常發生在程式已跑一段時間、磁碟快取是熱的，UserFolder 掃描本來就快很多；且改動面更小，符合 `AGENTS.md`「最小改動」原則。若之後這些情境也出現同樣的體感問題，另開新 item 援用同一機制，不在本 item 範圍內擴大。
2. **機制：串接既有的 `on_complete_` 回呼，一次性旗標**：`RebuildPipeline` 的 `on_complete_` 是單一、不帶參數的 `std::function<void()>`，任何 generation 完成都會觸發它（目前接的是 `OnGenerationCompleteRefresh`）。要讓「gen1（快來源）完成後才發 gen2（UserFolder）」不影響未來任何其他 generation 完成事件，必須用一個**一次性旗標**（例如 `g_startup_userfolder_pending`，預設 `false`）：開機發 `{StartMenu, AppsFolder}` 前設為 `true`；`OnGenerationCompleteRefresh()` 檢查並清除這個旗標，只有旗標為 `true` 時才觸發第二階段。
3. **旗標檢查必須放在 `OnGenerationCompleteRefresh()` 現有收尾邏輯之後，不是之前**（見下方「關鍵風險」）。
4. **UserFolder 的 `Request` 呼叫不受節流影響**：`RebuildPipeline::Request` 的 `Explicit` 節流（`kRebuildStartMinIntervalMs = 1000ms`）以 `last_rebuild_start_ms_[source]` 判斷；UserFolder 在 gen1 沒被請求過，所以 `last_rebuild_start_ms_` 裡沒有它的記錄，`AcceptRebuildStart(kNoRebuildStart, now)` 會直接放行——不需要新的節流豁免機制，已用既有程式碼路徑驗證過。
5. **不修改 `docs/design-spec.md` §FR-008**：現行文字「重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果」描述的是**單一** generation 內的行為（該 generation 完成前不發布任何一部分),不規定一次 generation 必須涵蓋幾個來源——NR-081 的 on-demand AppsFolder-only refresh 已經是「一個 generation 只含一個來源」的既有先例。本 item 只是把「開機時的全來源 generation」拆成兩個依序的 generation，每個 generation 各自仍然是「完成後才整批替換」，不違反這句話。

## 關鍵風險：`on_complete_` 回呼不能直接同步呼叫 `StartRebuild`

`RebuildPipeline::Start()`（`rebuild_pipeline.cpp:118-190`）在極早期的例外情況下（`settings_()` 快照或 `workers_.reserve`／`failures_.reserve` 失敗，通常是 OOM）會在**自己的呼叫堆疊內**同步呼叫 `CompleteIfReady(generation)` → `on_complete_()`（`:136-143`）。如果 `on_complete_`（即改動後的 `OnGenerationCompleteRefresh`）在旗標為真時直接同步呼叫 `StartRebuild({UserFolder})` → `Start({UserFolder})`，就會在**外層 `Start()` 尚未返回、仍在自己的呼叫堆疊上時**重入 `RebuildPipeline::Start()`（它沒有重入防護，直接操作 `workers_`／`cancel_`／`failures_`／`refresh_` 的成員狀態）。這是本 item 引入的新風險，不是既有行為（今天 `on_complete_` 從不呼叫回 `StartRebuild`）。

**必須**避免這個同步重入：第二階段的 `StartRebuild({UserFolder})` 呼叫不能從 `OnGenerationCompleteRefresh()` 內直接呼叫，而要透過既有的「post 一個 `WM_APP+N` 訊息，在下一次訊息迴圈處理時才執行」模式（`main.cpp` 已用同一模式處理 `kRebuildDoneMessage`／`kRebuildDeliveryFailedMessage`／`kSettingsMessage` 等）——`PostMessageW` 一個新的私有訊息常數,在視窗處理程序收到該訊息時才呼叫 `StartRebuild({UserFolder})`，確保呼叫發生在全新的呼叫堆疊上，徹底避開重入問題。新常數的數值由實作者 grep 現有 `WM_APP+` 定義後選一個未使用的號碼。

## `OnGenerationCompleteRefresh()` 內的操作順序（第二個關鍵風險）

`OnGenerationCompleteRefresh()`（`main.cpp:1390-1410`）目前依序：寫入 `RebuildDiagnosticLines(g_refresh->LastGenerationDiagnostics())`、`g_launch_failure_refresh.OnRefreshComplete()`、`RefreshPanelSnapshot()`、`SaveCatalogCache(...)`。而 `CatalogRefreshCoordinator::BeginGeneration()`（`catalog_refresh.cpp:76-93`）會把 `generation_diagnostics_` 重設為 `{}`。

如果檢查旗標並觸發第二階段的邏輯放在 `OnGenerationCompleteRefresh()` 的**開頭**（在讀取 `LastGenerationDiagnostics()` 之前),UserFolder 的 `BeginGeneration` 會在 gen1 的診斷還沒被寫進 log 之前就把它清空,導致 gen1 的 `rebuild-ms startmenu`／`rebuild-ms appsfolder` 診斷行永遠遺失。**旗標檢查與觸發第二階段必須放在 `OnGenerationCompleteRefresh()` 現有收尾邏輯（診斷寫入、`RefreshPanelSnapshot`、`SaveCatalogCache`）全部執行完之後**，確保 gen1 的診斷、面板刷新與 cache 寫入都以 gen1 自己的資料完成,才觸發 gen2。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §9.3（啟動序列，第 7-8 步，本 item 不改變這個順序，只改變背景重建內部怎麼分階段）：

> 7. 載入可選 Catalog cache，若有效可先提供結果。
> 8. 背景建立最新 Catalog。

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`、`docs/work-items.md` 的使用方式、Agent 交付規則、Item 總覽與「已否決的方向 — 不要重開」。
- `docs/design-spec.md` §FR-008、§9.3。
- `docs/work-items/NR-132-rebuild-pipeline-module.md`、`NR-081-appsfolder-on-demand-supersedes-rebuild.md`、`NR-116-cold-start-cache-source-failure-retention.md`、`NR-118-watcher-supersede-full-rebuild.md`、`NR-113-catalog-cache-launch-provenance.md`、`NR-185-cold-start-cache-honest-message.md`；完成 item 文件只讀取，不回頭修改歷史紀錄。
- `src/app_host/rebuild_pipeline.h/.cpp`：`Request()`（`:63-116`，`Explicit` 節流邏輯）、`Start()`（`:118-190`，含同步例外路徑 `on_complete_` 呼叫）、`CompleteIfReady()`（`:239-245`）。
- `src/catalog/catalog_refresh.h/.cpp`：`BeginGeneration()`（`:76-93`，`generation_diagnostics_` 重設處）、`GenerationComplete()`（`:221-234`）、`RebuildMerged()`（`:237-250`）。
- `src/app_host/main.cpp`：`OnGenerationCompleteRefresh()`（`:1390-1410`）、`StartRebuild()`（`:1412-1416`）、`wWinMain` 的開機呼叫（`:3434-3439`）、`RebuildPipeline` 建構時 `on_complete_` 的注入（`:3297`）、既有 `WM_APP+` 訊息常數（`kRebuildDoneMessage`、`kRebuildDeliveryFailedMessage`、`kSettingsMessage` 等）與其 `PostMessageW`／window proc 處理慣例。
- `tests/unit/rebuild_pipeline_test.cpp`、`tests/unit/catalog_refresh_test.cpp`：既有 `on_complete_`／generation 完成的測試手法。

## Scope

1. **一次性旗標**：在 `main.cpp` 新增一個檔案範圍全域 `bool g_startup_userfolder_pending = false;`（比照既有 `g_*` 全域的命名與註解慣例）。
2. **開機呼叫點改寫**（`main.cpp:3434-3439`）：
   ```cpp
   if (g_refresh) {
       g_startup_userfolder_pending = true;
       StartRebuild({nimblerun::CatalogSource::StartMenu,
                     nimblerun::CatalogSource::AppsFolder});
   }
   ```
3. **新增一個私有 `WM_APP+N` 訊息常數**（grep 現有 `WM_APP+` 定義後選未使用的號碼），在主視窗處理程序（window proc）的既有 `switch` 內新增一個 `case`，收到後呼叫 `StartRebuild({nimblerun::CatalogSource::UserFolder})`。
4. **`OnGenerationCompleteRefresh()` 尾端**（現有收尾邏輯**全部執行完之後**,見上方「關鍵風險」的順序要求)：
   ```cpp
   if (g_startup_userfolder_pending) {
       g_startup_userfolder_pending = false;
       PostMessageW(g_window /* 或既有等價全域 */, kStartupUserFolderMessage, 0, 0);
   }
   ```
5. 若 `g_refresh` 在開機時為 null（既有 guard，理論上不應發生但維持既有防禦），旗標維持 `false`，不觸發第二階段——維持現狀行為。

## Non-goals

- 不改其餘三個「全來源」`StartRebuild` 呼叫點（`main.cpp:1090`、`:2591`、`:2645`）；只改開機路徑。
- 不修改 `CatalogRefreshCoordinator::BeginGeneration`／`GenerationComplete`／`RebuildMerged` 的簽名或語意；完全重用既有函式。
- 不修改 NR-113 的 `launch_verified` 語意或 catalog.cache 信任模型。
- 不修改 `docs/design-spec.md` §FR-008（見上方決策 5，現行文字已涵蓋這個改動）。
- 不處理 NR-192（thread priority）、NR-193（max depth）、NR-194（拿掉開檔 probe）；技術上互不依賴，可獨立完成，但若一起落地，UserFolder 的第二階段掃描會因 NR-194／NR-192 而更快、對開機體感更有利。
- 不新增第二個節流機制或第二個 debounce timer；沿用 `Request`／`kRebuildStartMinIntervalMs` 既有邏輯。

## Acceptance

1. 開機時只用 `{StartMenu, AppsFolder}` 開始第一個 generation；該 generation 完成（診斷寫入、`RefreshPanelSnapshot`、`SaveCatalogCache` 皆執行完）後，才啟動只含 `{UserFolder}` 的第二個 generation。
2. 第二階段的觸發**不**是從 `on_complete_`／`OnGenerationCompleteRefresh()` 同步呼叫 `StartRebuild`，而是透過 `PostMessageW` 到下一次訊息迴圈——驗證方式：grep 確認 `OnGenerationCompleteRefresh()` 內沒有直接呼叫 `StartRebuild`／`Request`，而是呼叫 `PostMessageW`。
3. gen1 的診斷行（`rebuild-ms startmenu`、`rebuild-ms appsfolder`）在 gen2（`BeginGeneration({UserFolder})` 會重設 `generation_diagnostics_`）開始之前已經寫入 log，不會遺失。
4. StartMenu／AppsFolder 的項目在 gen1 完成後即變成 `launch_verified=true`（可透過既有 `nimblerun_catalog_refresh_test` 或 `nimblerun_rebuild_pipeline_test` 的既有測試手法驗證，不需要真的等 UserFolder 掃描完成）。
5. 第二階段的 `Request({UserFolder}, Explicit)` 不受 `kRebuildStartMinIntervalMs` 節流影響，正常啟動（既有邏輯，行為驗證即可，非新機制）。
6. 其餘三個全來源呼叫點（`:1090`、`:2591`、`:2645`）行為完全不變。
7. Release build 無新增 warning；完整 CTest 通過，含既有 `nimblerun_rebuild_pipeline_test`、`nimblerun_catalog_refresh_test`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "rebuild_pipeline|catalog_refresh" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_startup_userfolder_pending|kStartupUserFolderMessage" src/app_host/main.cpp
rg -n "OnGenerationCompleteRefresh" src/app_host/main.cpp
```

Focused runnable coverage 必須包含：一個測試或 self-check 證明「兩階段 generation」的順序（gen1 只含 StartMenu/AppsFolder 完成後才 BeginGeneration({UserFolder})），可用既有 `RebuildPipeline`／`CatalogRefreshCoordinator` 的注入點（`ThreadFactory`、`EnumerateSource`、`Complete on_complete`）在測試裡模擬,不需要真的操作 UI 或磁碟。

## Handoff requirements

交接時記錄：

- `g_startup_userfolder_pending` 與新 `WM_APP+N` 常數的確切位置（檔案／行號）。
- `OnGenerationCompleteRefresh()` 內新邏輯放置的精確位置，與「診斷寫入在 `BeginGeneration({UserFolder})` 之前完成」的驗證證據。
- 同步重入風險的驗證方式（例如：確認 `Start()` 的例外路徑不會在旗標為真時觸發同步 `StartRebuild`）。
- Agent checks 的完整命令與結果。
