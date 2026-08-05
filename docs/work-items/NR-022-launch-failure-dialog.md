# NR-022 — Launch failure dialog and one-shot catalog refresh

- Status: `done`
- Phase: 3
- Depends on: NR-008、NR-011、NR-020
- Source: `docs/design-spec.md` §5（啟動）、§11 錯誤處理、§12.5、AC-002

## Goal

把啟動失敗的呈現從面板底部那行紅字改成一次性對話框，並在失敗時於背景觸發一次 Catalog refresh。

## 必讀

`AGENTS.md`、`docs/development.md` 全五節、`docs/design-spec.md` §5、§11、§12.5、`docs/work-items.md`、`docs/work-items/NR-020-list-panel-restore.md`、本文件。

## 來自 spec 與開發指南的硬約束

- §11 明文：錯誤提示**不得使用會搶焦點的連續 MessageBox**。本 item 允許的是使用者主動按 Enter／單擊所觸發的**單次**對話框；同一次失敗只能出現一個對話框，且不得在無使用者動作時自行彈出。
- §11 同一列要求系統側「觸發一次 Catalog refresh」，並註明對失敗來源「不高頻 retry」。
- §5 要求啟動失敗時**面板保持顯示**。
- 對話框文字為 App UI，必須英文，字串集中管理。
- 不新增依賴：`MessageBoxW` 屬 user32，既有連結即可。
- 不改 `src/launch/shell_launch.*` 的錯誤映射邏輯，只改呈現與後續動作。

## Scope

- 移除 `main.cpp` 的 `g_last_launch_error` 變數、其渲染區塊與 `g_error_brush`（若無其他使用者）。
- `ActivateRow()` 的失敗分支改為：
  1. 先在背景觸發**一次** Catalog refresh，走既有 `src/catalog/catalog_refresh.*` 的路徑（沿用 `Ctrl+R` 手動 refresh 的同一個入口，勿另寫一條）。若已有 refresh 進行中則**合併**，不重複排程（避免使用者連點多個失效項目排出多次全量掃描）。
  2. 顯示 `MessageBoxW`，`MB_OK | MB_ICONWARNING`，owner 為面板 HWND；標題 `NimbleRun`，內容為簡短英文訊息，含 App 顯示名稱與來自 `shell_launch` 的簡短原因。
  3. 對話框期間與關閉後**面板保持顯示**，且**不執行 hide-after-launch**。
  4. 對話框關閉後把焦點設回搜尋 EDIT 控制項。
- 對話框是 modal loop：需比照 NR-018 context menu 的既有做法，以 flag 抑制期間的 `WM_KILLFOCUS` 自動隱藏，避免對話框一開面板就消失。
- 成功路徑完全不變（usage 更新、依設定隱藏）。

## Non-goals

- 不做 `TaskDialogIndirect`、不加 Refresh／Retry 按鈕（refresh 已自動執行）。
- 不做 in-panel toast、不做 tray balloon。
- 不做失敗統計、診斷 UI 或錯誤分類頁面（`src/diagnostics/` 的既有記錄照舊）。
- 不改 refresh 的節流參數、snapshot 世代或 debounce 行為。

## Acceptance

- 啟動失敗時出現一個對話框，且同一次失敗只出現一次。
- 對話框顯示期間與關閉後，面板仍然可見且焦點回到搜尋欄。
- 失敗後有且僅有一次 Catalog refresh 被觸發；已有 refresh 進行中時不新增第二次。
- 連續讓多個項目啟動失敗不會排出多次全量掃描。
- 成功啟動的行為（隱藏、usage 更新）不變。
- repo 內已無 `g_last_launch_error`。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R "catalog_refresh|shell_launch" --output-on-failure
ctest --test-dir build --output-on-failure
```

對話框本身不需自動化操作視窗。以純值／可測層驗證「觸發一次且會合併」的決策：把「是否該觸發 refresh」抽成一個不依賴 HWND 的小判斷（輸入：啟動是否失敗、目前是否有 refresh 進行中），在 `tests/unit/catalog_refresh_test.cpp` 新增 case：

- 啟動失敗且無 refresh 進行中 → 觸發一次。
- 啟動失敗但已有 refresh 進行中 → 不觸發。
- 連續兩次失敗（第一次已排程）→ 總共只觸發一次。
- 啟動成功 → 不觸發。

## 交接區

- Start: 2026-08-05
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/main.cpp`（`ActivateRow()`、`g_last_launch_error`、`Ctrl+R` refresh 入口、NR-018 抑制 `WM_KILLFOCUS` 的 flag 作法）、`src/launch/shell_launch.{h,cpp}`（錯誤映射）、`src/catalog/catalog_refresh.{h,cpp}`、`src/diagnostics/diagnostic_log.h`、`tests/unit/catalog_refresh_test.cpp`。實作上述 Scope，不改 launch 錯誤映射與 refresh 節流參數。回報修改檔案、測試命令、結果與未完成事項。
- Result: 2026-08-05 完成。移除 `g_last_launch_error`、Render 的錯誤提示區塊與 `g_error_brush`（含 `CreateDeviceResources`／`DiscardDeviceResources` 的清理；palette `PanelColors::error` 欄位保留，為聚合顏色值一員，無其他副作用）；`OpenFileLocation` 失敗改為記錄診斷＋同一單次對話框（§11 允許使用者主動動作觸發的單次對話框），不觸發 refresh。`ActivateRow` 失敗分支改為：`LaunchFailureRefreshGate`（新增於 `catalog_refresh.{h,cpp}`，純值無 HWND）判斷 `OnLaunchAttempt(false, g_refresh->IsRebuildInProgress())` 為真才經 Ctrl+R 同一個 `StartRebuild` 全來源入口觸發一次背景 refresh（進行中則合併，不另寫 refresh 路徑），再顯示 `MessageBoxW`（`MB_OK|MB_ICONWARNING`、owner 面板 HWND、標題 `NimbleRun`、`Failed to launch "<name>". <reason>`，reason 由 `LaunchErrorReason` 依 shell_launch 錯誤碼映射為英文短語，字串集中於 `dialog_strings`）；新增 `g_dialog_active` flag 比照 NR-018 抑制 `WM_KILLFOCUS` 自動隱藏，對話框關閉後 `SetFocus` 回搜尋 EDIT，面板保持顯示、不執行 hide-after-launch；成功路徑不變並呼叫 gate 重置。`CatalogRefreshCoordinator` 新增 `IsRebuildInProgress()`（有 active generation 且未全回報）；`kRebuildDoneMessage` 於世代完成時呼叫 `OnRefreshComplete()`，讓下一次失敗可再排新的 refresh。`catalog_refresh_test` 新增 4 case（無 rebuild 進行中失敗→觸發一次；rebuild 進行中失敗→不觸發；連續兩次失敗只觸發一次；成功→不觸發）。Agent checks：`cmake --build build` 無新增 warning；`ctest -R "catalog_refresh|shell_launch" --output-on-failure` 2/2 通過；`ctest --test-dir build --output-on-failure` 全套件 18/18 通過。未完成：無（對話框不需自動化操作視窗；視覺人工驗證不屬本追蹤表範圍）。
