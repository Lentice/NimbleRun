# NR-021 — Paged navigation and footer hint band

- Status: `done`
- Phase: 3
- Depends on: NR-020
- Source: `docs/design-spec.md` §4.2、§4.7、§4.8、§4.9、§12.3、§NFR-006

## Goal

在 NR-020 的清單上加入以可見列數為單位的翻頁（`PgUp`／`PgDn` 與滾輪），並在面板最下方畫出固定的按鍵指引列。

## 必讀

`AGENTS.md`、`docs/development.md` 全五節、`docs/design-spec.md` 上列章節、`docs/work-items.md`、`docs/work-items/NR-020-list-panel-restore.md`、本文件。

## 來自 spec 與開發指南的硬約束

- footer 文字是 App UI，必須英文；字串集中管理（`AGENTS.md` Language rules）。
- 捲動狀態屬 `PanelModel` 的純值狀態，不得放進視窗層；渲染只讀。
- 不新增 timer、不新增依賴、不加動畫。
- 顏色一律取自 `src/ui/panel_palette.h` 的 `PanelColors`，不寫死色碼，才能跟著 NR-015 的淺／深色與高對比切換。
- 最小可行改動：翻頁與滾輪共用同一個函式，不要各寫一份。

## Scope

### 1. `PanelModel::ScrollBy(int delta_rows)`（純值）

- 語意：`first_visible_ += delta_rows`，夾在 `[0, max(0, RowCount() - viewport_rows_)]`；**永不越過清單頭尾，也不環繞**。
- 夾住後把**選取設為新的第一可見列**（`selected_ = first_visible_`）；清單為空時不改選取。
- `PgDn` = `ScrollBy(+viewport_rows_)`，`PgUp` = `ScrollBy(-viewport_rows_)`。
  例：可見列數 5、目前顯示第 7~11 列 → `PgDn` 後顯示 12~16 列、選取第 12 列。
- 這是唯一的捲動入口：`PgUp`／`PgDn` 與滾輪都呼叫它。

### 2. 輸入

- EDIT 子類化的 search proc 新增 `VK_NEXT`（PgDn）與 `VK_PRIOR`（PgUp），呼叫 `ScrollBy` 並 invalidate。
- 視窗新增 `WM_MOUSEWHEEL`：捲動列數以 `SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, ...)` 讀取使用者設定（回傳 `WHEEL_PAGESCROLL` 時用可見列數；讀取失敗時退回 3），乘上 `WHEEL_DELTA` 的方向與倍數後呼叫 `ScrollBy`。不寫死 3。

### 3. Footer

- 位置：`kFooterTopDip`(400) ~ `kPanelHeightDip`(432)，NR-020 已保留這塊空間。左側留白。
- band 上緣畫一條 1 DIP 分隔線（`DrawLine`，`dim` 色）。
- 右側靠右對齊一組指引：說明字 `Scroll`，其後兩個圓角按鍵方塊，內容分別為 `PgUp`、`PgDn`。
  - 方塊用 `DrawRoundedRectangle`（框線 `dim`）＋內填 `card`，內文用 `kSmallFontDip` 與 `text` 色。
  - 指引文字集中在一處字串表，勿散落於 render 迴圈。
- footer 只放按鍵指引；不放狀態文字、版本資訊或更新提示（無網路存取）。

## Non-goals

- 不做捲軸、捲動動畫、慣性捲動、拖曳。
- 不做隨狀態變動的指引（例如有選取時才顯示 Enter）；本 item 的 footer 是固定內容。
- 不改啟動失敗呈現（NR-022）。
- `PgUp`／`PgDn` 不環繞；不新增 `Home`／`End` 的清單導航（那兩鍵屬搜尋欄文字編輯）。

## Acceptance

- 可見列數為 n 時，`PgDn` 使可見範圍前進正好 n 列，選取落在新的第一可見列。
- 在清單尾端 `PgDn`、在開頭 `PgUp` 皆無效果且不越界；可見範圍不會出現空白列。
- 總列數少於可見列數時，`PgUp`／`PgDn` 與滾輪都不改變可見範圍。
- 滾輪捲動量跟隨系統「一次捲動幾行」設定。
- footer 固定佔 400~432 DIP，清單不會畫進 footer；淺色／深色／高對比三種情況下分隔線與按鍵方塊都可見。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R list_vertical_slice --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/panel_model_test.cpp` 新增 case（純值，不需操作視窗）：

- `ScrollBy(+n)` 使 `FirstVisibleRow()` 前進 n 且選取等於新的第一可見列。
- 尾端 `ScrollBy(+n)` 被夾住：`FirstVisibleRow() == RowCount() - viewport`，且不再前進。
- 開頭 `ScrollBy(-n)` 被夾住在 0。
- `RowCount() < viewport` 時任何 `ScrollBy` 都不改變 `FirstVisibleRow()`。
- 空清單 `ScrollBy` 不產生選取、不越界。
- 連續 `ScrollBy(+n)` 直到底再 `ScrollBy(-n)` 可回到可預期位置（不環繞）。

## 交接區

- Start: 2026-08-05
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/panel_model.{h,cpp}`（NR-020 的 viewport 狀態）、`src/app_host/main.cpp`（search proc、視窗 proc、`Render()`）、`src/ui/panel_layout.h`、`src/ui/panel_palette.h`、`tests/unit/panel_model_test.cpp`。實作 Scope 1~3。不動 catalog／usage／pin／icon 邏輯，不做 NR-022。回報修改檔案、測試命令、結果與未完成事項。
- Result: 2026-08-05 完成。
  - 修改檔案：`src/app_host/panel_model.h`（新增 `ScrollBy(int delta_rows)` 宣告與文件註解）、`src/app_host/panel_model.cpp`（實作 ScrollBy：`first_visible_ += delta` 後 `ClampFirstVisible()` 夾在 `[0, max(0, RowCount()-viewport)]`、夾住後 `selected_ = first_visible_`、空清單 early-return 不改選取）、`src/ui/panel_layout.h`（新增 footer band 幾何常數：分隔線寬、key 方塊寬高與圓角半徑、label/方塊間距、文字 inset）、`src/app_host/main.cpp`（集中式 `footer_strings` 字串表；EDIT 子類化攔 `VK_PRIOR`/`VK_NEXT` 呼叫 `ScrollBy(±ViewportRows())` 並 invalidate；`WM_MOUSEWHEEL` 讀 `SPI_GETWHEELSCROLLLINES`、`WHEEL_PAGESCROLL` 時用可見列數、讀取失敗退回 3，乘 `WHEEL_DELTA` 方向倍數後呼叫 ScrollBy；Render 畫 footer band：1 DIP dim 分隔線＋右對齊 `Scroll` 與兩個圓角按鍵方塊，內文 `kSmallFontDip`、`text`/`card`/`dim` 全取自 `PanelColors`）、`tests/unit/panel_model_test.cpp`（新增 6 case）。
  - Agent checks（PowerShell，PATH 含 CMake/LLVM/Ninja）：`cmake --build build` 無 error 無新增 warning；`ctest --test-dir build -R list_vertical_slice --output-on-failure` → 1/1 Passed；`ctest --test-dir build --output-on-failure` → 18/18 Passed。
  - 未完成事項：無。滾輪焦點在搜尋 EDIT 時，單行 EDIT 不吞 `WM_MOUSEWHEEL`、經 `DefWindowProc` 轉送 parent 由 WindowProc 處理（NR-021 文件 §Scope 2 的「視窗新增 WM_MOUSEWHEEL」即此路徑）；NR-022（啟動失敗對話框）為後續 item。
