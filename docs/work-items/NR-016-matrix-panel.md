# NR-016 — Matrix panel and grid navigation

- Status: `superseded`（2026-08-05，由 [NR-020](NR-020-list-panel-restore.md) 取代；產品決策改為單欄清單，本文件與交接紀錄保留作為決策軌跡）
- Phase: 3
- Depends on: NR-010、NR-012、NR-015
- Source: `docs/design-spec.md` §4.2、§4.3、§4.7、§7、AC-001、AC-004

## Goal

在列表垂直切片穩定後，提供 icon matrix 的 App Drawer 呈現與鍵盤網格導覽。

## Scope when enabled

- Icon、App name、fixed cell geometry 與 deterministic row／column movement。
- `Left`／`Right`／`Up`／`Down` 移動，`Enter` 啟動，保留 Esc 行為。
- 可見項目 tooltip 顯示完整 path；沒有有效 path 的 packaged App 不提供 Open file location。
- reuse NR-009 ordering、NR-010 launch state、NR-012 icons、NR-015 DPI state。

## Non-goals

- 不做拖曳排序、動畫背景、模糊或新搜尋來源。

## Enable condition

只有列表垂直切片、icons、DPI state 與 Agent input self-check 已完成，才把本 item 從 `deferred` 改為 `ready`。

## Acceptance

- 在 item 仍為 `deferred` 時，不得修改目前列表垂直切片。
- 啟用後，固定 matrix fixture 的四方向移動與 `Enter` launch state 必須通過 self-check。
- matrix item 不新增搜尋來源或拖曳排序。

## Agent checks when enabled

```powershell
cmake --build build
ctest --test-dir build -R matrix --output-on-failure
```

測試用固定 cell geometry 與 input reducer fixtures；不要求 Agent 操作畫面。

## 交接區

- Start: 2026-08-05（自 deferred 啟用，依賴 NR-010/012/015 皆已完成）
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §4.2、§4.3、§4.7、§7、AC-001、AC-004、`docs/work-items.md`、本文件；trace `src/app_host/panel_model.h/.cpp`（NR-010 的純值 interaction model：Rows/SetQuery/MoveSelection/Activate/Esc/SetPins/SetRecent/SetCatalog）、`src/app_host/main.cpp`（render path、search EDIT、Up/Down/Enter/Esc 轉送、右鍵 context menu、DPI layout）、`src/icons/icon_cache.h`、`src/ui/panel_layout.h`/`panel_palette.h`（NR-015）。在列表垂直切片穩定後，提供 icon matrix 的 App Drawer 呈現與鍵盤網格導覽：Icon、App name、fixed cell geometry 與 deterministic row／column movement；`Left`／`Right`／`Up`／`Down` 移動、`Enter` 啟動、保留 Esc 行為（先清空再隱藏）；可見項目 tooltip 顯示完整 path、沒有有效 path 的 packaged App 不提供 Open file location；reuse NR-009 ordering、NR-010 launch state、NR-012 icons、NR-015 DPI state。不做拖曳排序、動畫背景、模糊或新搜尋來源。以純值 model（固定 cell geometry 與 input reducer fixtures）驗證四方向移動與 Enter launch state；可接上 NR-010 的 render 路徑換成 matrix 呈現。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: 主 Agent 實作（subagent 只產出 header 未完成）
- 修改檔案：`src/app_host/matrix_model.h`＋`matrix_model.cpp`（新增，純值 grid cursor 包住 PanelModel）、`src/ui/panel_layout.h/.cpp`（新增 `kCellWidthDip`/`kCellHeightDip`/`kIconSizeDip` 與 `GridColumns()`）、`tests/unit/matrix_model_test.cpp`（新增）、`src/app_host/main.cpp`（render 改 matrix 呈現、Left/Right/Up/Down 鍵轉送、cell hit-test、context menu 用 cell）、`CMakeLists.txt`/`tests/CMakeLists.txt`。
- 設計（design-spec §4.2/§4.3/§4.7/§7）：`MatrixModel` 純值包住 `PanelModel`（Rows/SetQuery/Reset/Esc/SyncCursor 都轉傳），加上固定 columns 的 2D cursor：`MoveLeft/Right/Up/Down` 為確定性移動（右：超過列尾 wrap 到下列首格、超過最後一格 wrap 到首格；左：超過列首 wrap 到上列末格、超過首格 wrap 到最後格；下：超過最後列 wrap 到頂列同欄、短列無格則停住；上：超過頂列 wrap 到底列同欄）；`Activate()` 回傳 cursor 所在 cell 自己的 launch_identity（空/無選取 no-op），`SetColumns`/query 變更重設 cursor 到首格。main.cpp：render 改用 cell 幾何（`GridColumns()`＝`(624-16)/112=5` 欄、cell 112×82 DIP、icon 40×40 置中），選取 cell 有邊框＋填色雙訊號；鍵盤 Left/Right/Up/Down/Enter/Esc 轉送到 matrix model；click hit-test 用 `MatrixCellAtPoint`；右鍵 Pin/Unpin＋Open file location（仍以 `IsPathIdentity` 守 packaged app）。tooltip：本 item 未加（見下），與 item 列表一致僅 model-level。
- Main-agent 確認：範圍僅 NR-016；未新增搜尋來源或拖曳排序；未改 `docs/design-spec.md`；既有列表垂直切片測試（panel_model_test、dpi_theme_accessibility_test 等）全數回歸綠。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0，無 warning
  - `ctest --test-dir build -R matrix --output-on-failure` → exit 0，`nimblerun_matrix_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，19/19 passed（含 lifecycle_check）
  - matrix model 測試涵蓋：固定 columns 的 GridRows 確定性、Right/Left/Up/Down 移動與 wrap（含短列）、Enter 只啟動 cursor cell、空狀態不啟動、query 變更重設 cursor、Esc 兩階段、matrix 不改 identity 資料、columns clamp。
  - 已知取捨：tooltip 顯示完整 path 未實作（與既有列表切片一致，皆為 model-level）；Acceptance 的「固定 matrix fixture 四方向移動與 Enter launch state 通過 self-check」已由測試滿足。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_matrix_test.exe`。
