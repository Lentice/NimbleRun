# NR-029 — Empty-state icon grid and footer path bar

- Status: `done`
- Phase: 3
- Depends on: NR-020、NR-021、NR-023、NR-024
- Partially restores: [NR-016](NR-016-matrix-panel.md)（該 item 維持 `superseded`；本 item 只恢復「空白狀態用 icon grid」這一項意圖，不恢復其 `MatrixModel` 實作）
- Source: `docs/design-spec.md` §4.2、§4.3、§4.7、§4.8、§4.9、§12.3、AC-002、AC-002b

## Goal

空白查詢狀態改用 **icon grid**（6 欄 × 4 列、一頁 24 格、只有圖示與單行名稱），並在 footer band 左側顯示當前 active 或 hover 項目的完整路徑。使用者一輸入非空白字元就切回 NR-020 的單欄清單。

動機：清單一頁只放得下 8 個項目，對釘選項目而言容量太小；grid 一頁 24 格，`recent_count` 的預設值 20 可以一次看完。

## 必讀

`AGENTS.md`、`docs/development.md`（五節全部）、`docs/design-spec.md` 上列章節、`docs/work-items.md`、`docs/work-items/NR-020-list-panel-restore.md`、`docs/work-items/NR-021-paged-navigation-footer.md`、`docs/work-items/NR-024-quick-select-digits.md`、本文件。

## 來自 spec 與開發指南的硬約束

- 產品行為以 `docs/design-spec.md` 為準；本文件若與 spec 衝突，以 spec 為準並回報。
- App UI 文字一律英文，多處共用的字串集中管理。
- 選取、可見範圍、欄數等狀態屬 `PanelModel` 的純值狀態，不得依賴 HWND 或 Shell COM；渲染只讀。hover 是純視覺狀態，留在視窗層。
- 顏色一律取自 `src/ui/panel_palette.h` 的 `PanelColors`，不寫死色碼。
- 待機路徑保持事件驅動：不得為 hover 或捲動新增 timer；hover 只在**命中的格子改變時**才 invalidate。
- 不新增依賴、網路、遙測、服務、driver 或管理員權限。
- 最小可行改動、重用既有程式碼；不為單一用途新增抽象層。

## Scope

### 1. `PanelModel` 的欄數（純值）

**不要復活 `MatrixModel`。** 兩種版面共用同一套 viewport／捲動／選取程式碼，差別只是欄數：

- 新增 `void SetGridColumns(int columns)`（clamp `>= 1`）與 `int Columns() const`。
  `Columns()` 在 `query_` 為空時回傳 `grid_columns_`，否則回傳 `1`。清單狀態就是「欄數＝1」，所有既有行為原地成立。
- 可見項目數 = `ViewportRows() * Columns()`。`ClampFirstVisible()` 夾在 `[0, max(0, RowCount() - ViewportRows() * Columns())]`，**夾住後再向下取整到 `Columns()` 的倍數**，確保可見範圍永遠對齊整列、不出現半列。
- `EnsureSelectionVisible()` 以「整列」為單位位移（一次 `Columns()` 個項目）。
- `ScrollBy(int delta_rows)` 語意不變（單位仍是列），內部乘上 `Columns()`；`PgUp`／`PgDn` 仍傳 `±ViewportRows()`。
- `MoveSelection(delta)` 完全不改，仍是項目單位並在頭尾環繞。grid 的上下移動由呼叫端傳 `±Columns()`。
- `RowForVisibleSlot(slot)` 語意不變（slot 是可見項目序），NR-024 的 `Alt+1~0` 因此原封不動可用於 grid 的前 10 格。

### 2. Layout 常數

`src/ui/panel_layout.h` 加回 NR-020 刪除的名字（沿用原名，不另創）：

```cpp
constexpr float kCellWidthDip = 101.0f;
constexpr float kCellHeightDip = 96.0f;
constexpr float kIconSizeDip = 40.0f;
constexpr int kGridColumns = 6;
```

結果區 `kListTopDip`(72) ~ `kFooterTopDip`(456) 高 384 → grid 列數 = `384 / 96` = **4**，一頁 24 格；由 client rect 實際高度計算，DPI／視窗尺寸變更時一併更新 `SetViewportRows()`。

### 3. Grid 渲染（`main.cpp` 的 `Render()`）

`Columns() == 1` 時走現有清單路徑，一個像素都不改。`Columns() > 1` 時：

- 只渲染可見範圍內的格子；圖示請求（NR-012 的 `need_icon_request`）也只針對這些格子，請求尺寸為 `kIconSizeDip` 換算後的物理像素。
- 每格版面（DIP，相對格子矩形）：圖示 40×40 水平置中、置於上半；名稱 `kTextFontDip` 水平置中於下半，單行 `NO_WRAP` ＋ `CreateEllipsisTrimmingSign()`（沿用 NR-020 建好的 trimming）。名稱長度不得改變格子幾何。
- 選取格：`selected_fill` ＋ NR-015 的 `selected_border`（非色彩的第二訊號，§NFR-006）。hover 格：只用 `card` 級的淡填色，且**不畫 `selected_border`**，兩種狀態必須可同時存在且可分辨。
- 前 10 格右上角畫 NR-024 的圓角數字方塊（`kRowKeyBoxWidthDip`），內容取 `QuickSelectLabelForSlot()`。
- 空狀態：`Rows()` 為空時在第一格位置以 `dim` 色畫既有英文提示（`Building app catalog…` / `No matching apps`），沿用清單狀態的同一份字串。

### 4. Footer path bar

- 位置：`kFooterTopDip`(456) ~ `kPanelHeightDip`(488) 的**左半**；右側 `Scroll` / `PgUp` / `PgDn` / `Alt+1~N` 指引與分隔線完全維持 NR-021/024 現狀。
- 內容：hover 中的項目優先，否則為選取項目；`entry.source == AppSource::AppsFolder` 顯示既有集中字串 `Windows app`，其餘顯示 `entry.source_path`。
- 只在 `Columns() > 1` 時繪製；清單狀態該區留空。
- 用 `kSmallFontDip` ＋ `dim` 色，`NO_WRAP` ＋省略號，右界固定在指引區左緣減去 `kFooterHintGapDip`，任何長度都不得覆蓋指引。

### 5. 輸入

- EDIT 子類化的 search proc：`VK_LEFT`／`VK_RIGHT` **只在 `Columns() > 1` 時**攔截並呼叫 `MoveSelection(∓1)`，否則 `return DefSubclassProc(...)` 交還 EDIT（NR-020 的驗收條件必須保持成立）。`VK_UP`／`VK_DOWN` 改呼叫 `MoveSelection(∓Columns())`。`VK_HOME`／`VK_END` 維持不攔截。
- `WM_LBUTTONDOWN`／`WM_RBUTTONDOWN` hit-test：`Columns() > 1` 時改為
  `FirstVisibleRow() + (row_index * Columns()) + col_index`，超出 `RowCount()` 視為未命中。左鍵維持選取後立即啟動，右鍵維持 NR-018 的 context menu。
- 新增 `WM_MOUSEMOVE`：算出命中格索引（未命中為 `-1`），**只有在值改變時**才更新視窗層的 hover 索引並 `InvalidateRect`。同時以 `TrackMouseEvent(TME_LEAVE)` 註冊 `WM_MOUSELEAVE`，收到後把 hover 索引清成 `-1` 並重繪。
- 版面切換（query 空↔非空）時清除 hover 索引。

## Non-goals

- 不改清單狀態的任何呈現或輸入行為。
- 不做拖曳排序、捲軸、動畫、tooltip、雙擊、grid 內的分組標題或 pin/recent 分隔線。
- 不改 catalog、dedup、usage、pin、icon cache 的邏輯或持久化格式。
- 不改面板尺寸、搜尋欄或 footer 右側指引。
- 不新增設定項（欄數與格子尺寸是固定常數，不進 `settings.ini`）。

## Acceptance

- 搜尋欄為空時面板為 6×4 icon grid，一頁 24 格，每格只有圖示與單行名稱。
- 輸入第一個非空白字元後切為 NR-020 清單，清空後切回 grid；兩次切換後可見範圍都回到頂端。
- `←`／`→`／`↑`／`↓` 在 grid 內移動且頭尾環繞；清單狀態下 `←`／`→`／`Home`／`End` 仍可在搜尋欄編輯文字。
- `PgDn` 使可見範圍前進正好一頁（4 列 / 24 格），滾輪前進一個 grid 列；兩者皆不越界、不出現半列。
- hover 改變 footer 路徑與該格填色，但不改變選取邊框；此時按 `Enter` 啟動的仍是有邊框的那格。指標移出面板後 footer 顯示選取項的路徑。
- packaged App 在 footer 顯示 `Windows app`，不顯示 Shell parsing name；超長路徑截斷且不覆蓋按鍵指引。
- `Alt+1~0` 啟動 grid 可見範圍的前 10 格。
- 清單為空時 grid 不是一片空白。
- 淺色／深色／高對比三種情況下，選取與 hover 兩種格子狀態都可分辨。
- 建置無新增警告；repo 內不得出現 `MatrixModel`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/panel_model_test.cpp` 新增 case（純值，不需操作視窗）：

- `SetGridColumns` clamp 到 `>= 1`；`Columns()` 在 query 非空時恆為 1、空時為設定值。
- `FirstVisibleRow()` 恆為 `Columns()` 的倍數（含捲到底、清單長度非整列倍數的情況）。
- grid 下 `MoveSelection(+Columns())` 等同下移一列；在最後一列下移會環繞回頂端。
- grid 下 `ScrollBy(+ViewportRows())` 使可見範圍前進 `ViewportRows() * Columns()` 個項目，到底被夾住。
- 總項目數少於一頁時，任何 `ScrollBy` 都不改變 `FirstVisibleRow()`。
- query 由空變非空再變回空後，`FirstVisibleRow()` 回到 0 且 `Columns()` 正確切換。
- `RowForVisibleSlot(0..9)` 在 grid 下對應可見範圍的前 10 個項目。

## 交接區

- Start: 2026-08-05
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/panel_model.{h,cpp}`、`src/app_host/main.cpp`（`Render()`、search proc、`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN`／`WM_MOUSEWHEEL`、`VisibleRowCount()`、footer 繪製、icon 請求路徑）、`src/ui/panel_layout.{h,cpp}`、`src/ui/panel_palette.h`、`src/ui/quick_select.h`、`src/icons/icon_cache.h`、`src/catalog/app_entry.h`、`tests/unit/panel_model_test.cpp`。實作 Scope 1~5，不越界改動清單狀態。回報修改檔案、測試命令、結果與未完成事項。
- Result: 2026-08-05 完成。
  - 修改檔案：`src/app_host/panel_model.h`／`.cpp`（`SetGridColumns`＋`Columns()`、`ClampFirstVisible` 夾 `[0, max(0, RowCount()-ViewportRows()*Columns())]` 後向下取整 `Columns()` 倍數、`EnsureSelectionVisible` 以整列位移、`ScrollBy` 內部乘 `Columns()`、`RowForVisibleSlot` 上界改 `ViewportRows()*Columns()`；`MoveSelection` 未動）、`src/ui/panel_layout.h`（加回 `kCellWidthDip=101`／`kCellHeightDip=96`／`kIconSizeDip=40`／`kGridColumns=6`，新增置中 `kGridLeftDip`）、`src/ui/panel_palette.h`／`.cpp`（`PanelColors` 新增 `hover_fill`：light/dark＝card、HC＝system.highlight）、`src/app_host/main.cpp`（grid Render 分支、footer path bar、`CellAtPoint` 取代 `RowAtPoint`、`VisibleItemCount` 取代 `VisibleRowCount`、`IconKeyFor` 依狀態取 30/40 DIP、`UpdateViewportRows` 依 `Columns()` 選列高、search proc 攔 `VK_LEFT`／`VK_RIGHT`（僅 grid）與 `∓Columns()` 的 `VK_UP`／`VK_DOWN`、`WM_MOUSEMOVE`／`WM_MOUSELEAVE`、`EN_UPDATE` 版面切換重算 viewport＋清 hover、ShowPanel 順序調整、`SetGridColumns` 於啟動設定）、`tests/unit/panel_model_test.cpp`（7 case）、`tests/unit/ui_palette_layout_test.cpp`（2 case）、`docs/work-items.md`（NR-029 → `done` 並記決策）。
  - Agent checks（Release x64，PATH 含 CMake/LLVM/Ninja）：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`（configure 成功）；`cmake --build build --clean-first`（無 error、無新增 warning）；`ctest --test-dir build --output-on-failure` → 19/19 Passed。repo 內 `rg "MatrixModel" src tests` 無命中。
  - 未完成事項：無。grid 的視覺呈現、三主題下 hover／選取可分辨性、hover 不改變 `Enter` 目標等屬人工驗證，不列入 Agent 交付；footer 右側 `Alt+1~N` 指引沿用 NR-024 公式（grid 下顯示 `Alt+1~4`），依規格「右側指引維持現狀」不更動，實際綁定仍為可見前 10 格。
