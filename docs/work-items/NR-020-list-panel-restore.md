# NR-020 — List panel replaces icon matrix

- Status: `done`
- Phase: 3
- Depends on: NR-010、NR-012、NR-015、NR-018
- Supersedes: NR-016
- Source: `docs/design-spec.md` §4.1、§4.2、§4.3、§4.7、§4.8、§4.9、§12.3、§19.1、AC-002

## Goal

把面板從 icon matrix 改回單欄垂直清單，全狀態（空白查詢與搜尋結果）共用同一套清單版面：每列左側圖示，右側上行 App 名稱、下行來源路徑。NR-016 的 matrix 程式碼與測試在本 item 一併移除。

## 必讀

實作前必須讀完：`AGENTS.md`、`docs/development.md`（Product boundary／Architecture rules／UI language／Build configuration／Change workflow 全部五節）、`docs/design-spec.md` 上列章節、`docs/work-items.md`（使用方式與 Agent 交付規則）、本文件。

## 來自 spec 與開發指南的硬約束

- 產品行為以 `docs/design-spec.md` 為準；本文件若與 spec 衝突，以 spec 為準並回報。
- App UI 文字一律英文（`AGENTS.md` Language rules、`docs/development.md` UI language）。多處共用的字串集中放，不要散在 render 迴圈裡。
- 搜尋、排名、選取狀態等核心邏輯不得依賴 HWND 或 Shell COM（`AGENTS.md` Engineering rules）。清單的選取與可見範圍屬於 `PanelModel` 的純值狀態，渲染只讀它。
- App Catalog 資料保持可複製的普通值（`AppEntry`），UI 不持有 Shell COM pointer。
- 不新增第三方依賴、網路存取、遙測、服務、driver 或管理員權限。
- 待機路徑保持事件驅動：不得為捲動或重繪新增高頻 timer。
- 優先最小可行改動、優先重用既有程式碼（ponytail 原則）；不要為單一用途新增抽象層。

## Scope

### 1. 移除 matrix

- 刪除 `src/app_host/matrix_model.h`、`src/app_host/matrix_model.cpp`、`tests/unit/matrix_model_test.cpp`，以及 `tests/CMakeLists.txt` 的 `nimblerun_matrix_test` 目標與 `add_test`。
- 刪除 `src/ui/panel_layout.h` 的 `kCellWidthDip`、`kCellHeightDip`、`kIconSizeDip` 與 `GridColumns()`（含 `.cpp` 實作）。
- `src/app_host/main.cpp`：移除 `g_matrix`、`MatrixCellRect()`、`MatrixCellAtPoint()`，所有呼叫改走 `PanelModel`。

### 2. `PanelModel` 可見範圍（純值）

- 新增 `void SetViewportRows(int rows)`（clamp `>= 1`）與 `int FirstVisibleRow() const`。
- `first_visible_` 夾在 `[0, max(0, RowCount() - viewport_rows_)]`；`RefreshRows()`、`SetQuery()`、`Reset()`、`SetCatalog()`、`SetRecent()`、`SetPins()` 之後一律重新夾住並回到 0。
- `MoveSelection(delta)` 維持現有頭尾**環繞**行為；移動後若選取落在可見範圍外，`first_visible_` 以最小位移把它帶回可見範圍（上界 / 下界各移一列）。
- 本 item **不**實作 PageUp／PageDown 與滾輪（NR-021 負責）。只要 viewport 狀態與 `MoveSelection` 的連動正確即可。

### 3. 清單渲染（`main.cpp` 的 `Render()`）

- `src/ui/panel_layout.h` 新增 `constexpr float kFooterTopDip = 400.0f;`（footer band 400~432 DIP，本 item 只保留空間，不畫內容）。
- 清單區為 `kListTopDip`(60) ~ `kFooterTopDip`(400)，列高沿用 `kRowHeightDip = 48`，可見列數 = `(400 - 60) / 48` = **7**；由 client rect 實際高度計算，並在 DPI／視窗尺寸變更時呼叫 `SetViewportRows()`。
- 只渲染 `[FirstVisibleRow(), FirstVisibleRow() + viewport)` 的列。圖示請求（NR-012 的 `need_icon_request` 判斷）也只針對這些列。
- 每列版面（DIP，相對列矩形）：
  - 圖示：沿用 `kTileSizeDip = 30`，左邊距 `kTileInsetDip = 8`，垂直置中。
  - 名稱：`kTextFontDip = 14`，起點在圖示右側 `8` DIP，佔列上半。
  - 第二行：`kSmallFontDip = 11`，與名稱左緣對齊，佔列下半，用 `dim` 色。
- 選取列：`selected_fill` 填滿整列，並沿用 NR-015 的 `selected_border` 邊框（非色彩的第二訊號，§NFR-006）。未選取列沿用 `card` 或背景色，維持與現行一致的視覺層次。
- 第二行內容：`entry.source == AppSource::AppsFolder` 顯示集中管理的英文字串 `Windows app`；其他來源顯示 `entry.source_path`。
- 文字截斷：對名稱與小字兩個 `IDWriteTextFormat` 各呼叫一次 `SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)` 與 `SetTrimming()`（`DWRITE_TRIMMING_GRANULARITY_CHARACTER` ＋ `…` 的 `IDWriteInlineObject`，用 `CreateEllipsisTrimmingSign`）。不得因文字長度改變列高。
- 空狀態：`Rows()` 為空時在第一列位置以 `dim` 色畫一行英文字 —— catalog 快照為空（或指標為 null）顯示 `Building app catalog…`，否則顯示 `No matching apps`。

### 4. 輸入

- EDIT 子類化（`main.cpp` 的 search proc）：`VK_UP`／`VK_DOWN` → `MoveSelection(-1)`／`MoveSelection(+1)`；`VK_RETURN` → `Activate()`；`VK_ESCAPE` 維持兩階段。
- `VK_LEFT`、`VK_RIGHT`、`VK_HOME`、`VK_END` **不再攔截**，交還 EDIT 做文字插入點移動。
- `WM_LBUTTONDOWN`：hit-test 改為 `(y - list_top) / row_height + FirstVisibleRow()`，超出 `RowCount()` 視為未命中；命中時**選取後立即啟動**（呼叫既有 `ActivateRow()`，沿用 hide-after-launch 設定）。
- `WM_RBUTTONDOWN` 的 context menu（NR-018）沿用，只換 hit-test。

## Non-goals

- 不做 PageUp／PageDown、滾輪與 footer 內容（NR-021）。
- 不改啟動失敗的呈現（NR-022）；本 item 保留現有 `g_last_launch_error` 行為，只把它移到 `kFooterTopDip` 之上以免與 footer band 重疊。
- 不做 tooltip、拖曳排序、捲軸、動畫、中間省略號、雙擊、新搜尋來源。
- 不改 catalog、dedup、usage、pin、icon cache 的任何邏輯或持久化格式。

## Acceptance

- 空白查詢與搜尋結果使用同一套清單版面；輸入第一個字時版面不跳動、只原地過濾。
- 選取列可由 `↑`／`↓` 移動並在頭尾環繞；選取超出可見範圍時可見範圍跟著移動一列。
- 可見範圍永不越過清單頭尾。
- packaged App 的第二行不顯示 Shell parsing name。
- 長名稱／長路徑以省略號截斷且不換行、不影響列高。
- 清單為空時面板不是一片空白。
- `←`／`→`／`Home`／`End` 可在搜尋欄內編輯文字。
- 建置無新增警告；matrix 相關檔案、測試目標與 layout 常數已完全移除（repo 內搜尋不到 `MatrixModel`、`GridColumns`、`kCellWidthDip`）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/panel_model_test.cpp`（`nimblerun_list_vertical_slice_test`）新增 case，全部以純值模型驗證，不需操作視窗：

- `SetViewportRows` clamp 到 `>= 1`。
- 列數少於可見列數時 `FirstVisibleRow()` 恆為 0。
- 選取往下移出可見範圍時 `FirstVisibleRow()` 只加 1；往上同理。
- `↑` 在第一列環繞到最後一列後，可見範圍跳到尾端且不越界。
- `SetQuery`／`Reset`／`SetPins` 後 `FirstVisibleRow()` 回到 0。
- 可見列數大於總列數時 `FirstVisibleRow()` 不會變成負數。

## 交接區

- Start: —
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/main.cpp`（`Render()`、search proc、`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN`、`VisibleRowCount()`、icon 請求路徑）、`src/app_host/panel_model.{h,cpp}`、`src/app_host/matrix_model.{h,cpp}`、`src/ui/panel_layout.{h,cpp}`、`src/ui/panel_palette.h`、`src/catalog/app_entry.h`、`tests/unit/panel_model_test.cpp`、`tests/CMakeLists.txt`。實作上述 Scope 1~4，不越界到 NR-021／NR-022。回報修改檔案、測試命令、結果與未完成事項。
- Result: 2026-08-05 完成。移除 `matrix_model.{h,cpp}`、`matrix_model_test.cpp`、`tests/CMakeLists.txt` 的 `nimblerun_matrix_test`、`panel_layout` 的 cell 常數與 `GridColumns()`；`PanelModel` 加純值 viewport 狀態（`SetViewportRows`／`FirstVisibleRow`／`ViewportRows`／`SelectRow`，`first_visible_` 於 RefreshRows 重設 0、MoveSelection 環繞後最小位移帶回可見範圍）；main.cpp Render 改單欄清單（icon 30 DIP 左緣 8 垂直置中、名稱 14 上半／小字 11 下半、AppsFolder 顯示 `Windows app`、`selected_fill`＋`selected_border` 雙訊號、空狀態 dim 色提示、名稱與小字 `NO_WRAP`＋CHARACTER 省略號）；`kFooterTopDip=400` 保留 footer 空間、launch error 移到其上；EDIT 只攔 `↑`／`↓`／Enter／Esc／Ctrl+R，`←`／`→`／Home／End 交還；左／右鍵 hit-test 改 `(y-list_top)/row_height + FirstVisibleRow()`，單擊選取後啟動；viewport 於 ShowPanel／WM_SIZE／WM_DPICHANGED 更新。`panel_model_test` 新增 7 case；`ctest` 全套件 18/18 通過、build 無新增 warning、repo 已無 `MatrixModel`／`GridColumns`／`kCellWidthDip`。未完成：無（NR-021 翻頁與 footer、NR-022 啟動失敗對話框為後續 item）。
