# NR-179 — cell tooltip 優先顯示在格子下方＋修復箭頭不可見

- Phase: 3
- Depends on: NR-178
- Source: `docs/design-spec.md` §4.8（現文 `design-spec.md:257`）／§4.9（外觀）；`docs/work-items/NR-178-cell-tooltip.md` 決策 #5 與交接區
- 覆寫：NR-178 使用者決策 #5 的放置規則「顯示在格子正上方、水平置中，頂排格子上方空間不足時翻轉到下方」與 design-spec §4.8 對應文字

## Goal

1. **放置偏好改為下方優先**：tooltip 盡量顯示在格子**下方**（箭頭朝上指向格子）；下方放不下時（最後一列下方是 footer，空間不足）才翻轉到**上方**。
2. **修復箭頭不可見**：NR-178 實作的「tooltip 在格子上方」時箭頭整支畫在視窗範圍外，使用者實機確認「tooltip 沒有尖角」。

## 覆寫聲明

本 item 覆寫 NR-178 決策 #5 的放置規則與 design-spec §4.8 的對應文字（NR-178 文件與交接區維持原樣，是歷史紀錄）：

- 新證據（2026-08-11 使用者回饋）：「tooltip盡量顯示在icon的下方」——下方優先的放置偏好；以及「tooltip沒有尖角」——箭頭不可見（root cause 見 §現況事實 1）。
- 其餘 NR-178 決策全部沿用：只在截斷時顯示、只做格狀、只跟隨 hover、深色底白字、150 ms、隱藏點、點擊穿透、資源只在顯示期間存在。
- 未重開 `docs/work-items.md` §已否決的方向 表中的任何方向。

## 現況事實（已查證，不需重新推導）

1. **箭頭不可見的 root cause**：`src/ui/cell_tooltip.cpp:276-279`「above」分支的 transform 乘法順序錯誤。Direct2D 採 **row-vector 慣例**（`x' = x·m11 + y·m21 + dx`；`y' = x·m12 + y·m22 + dy`），`A * B` 組出的矩陣等於「先套 A 再套 B」。現文 `Translation(arrow_cx, body_bottom + kArrowHeightDip) * Scale(1, -1)` 依 `Matrix3x2F::operator*` 公式組出 `m22 = -1`、`dy = -(body_bottom + 6)`，點變換為 `y' = -y - ty`：
   - 箭頭頂點 `(0, 0)` → `y' = -(body_bottom + 6)`（負值，視窗頂緣之上）；
   - 底邊 `(±half, 6)` → `y' = -(body_bottom + 12)`（更上方）。
   - 整支箭頭落在視窗範圍外被裁掉 → **預設「上方放置」時箭頭完全不可見**。「下方放置」分支只用 `Translation(arrow_cx, 0)`，正確，但該分支只在頂排出現。
   - 修正：swap 成 `Scale(1, -1) * Translation(arrow_cx, body_bottom + kArrowHeightDip)` → `y' = -y + ty`：頂點 → `(arrow_cx, body_bottom + 6)`（窗底邊）、底邊 → `(arrow_cx ± half, body_bottom)`（貼齊 body 底緣）✓。
2. **放置函式**：`ComputeTooltipGeometryDip`（`cell_tooltip.cpp:108-128`）。現邏輯：`above_top = cell.top - gap - tip_height; if (above_top >= min_top_dip) → above`，否則 below。簽名為 `(cell_dip, tip_width_dip, tip_height_dip, gap_dip, min_top_dip, panel_left_dip, panel_right_dip)`。
3. **幾何**：格狀最後一列底緣 = `kFooterTopDip`(456)、面板高 `kPanelHeightDip`(488)（`src/ui/panel_layout.h:11-12`）。最後一列下方空間：456 + 6 + tooltip 高（約 30） = 492 > 488 → 下方放不下 → 翻轉上方；其餘三列下方都放得下。故「下方優先」在現行 6×4 幾何下的實際效果：第 1~3 列在下方、最後一列在上方。
4. **測試**：`tests/unit/cell_tooltip_test.cpp` 的幾何案例（上方置中／頂排翻轉／左右 clamp／gap）目前驗證「上方優先」語意，必須改寫為「下方優先」語意。

## Scope

### 1. design-spec 修正（本 item 執行）

§4.8 hover 子彈的 blockquote（現文 `design-spec.md:257`：「…於格子上方顯示 cell tooltip（完整顯示名稱），上方空間不足時顯示於下方…」）改為：

> hover 中的格子若名稱被截斷，指標停留約 150 ms 後於格子下方顯示 cell tooltip（完整顯示名稱），下方空間不足時（如最後一列）顯示於上方；指標離開該格、按下滑鼠鍵、開始拖曳釘選、面板隱藏或視窗捲動／翻頁時立即消失。

§4.2／§4.9 的 tooltip 條文不動。

### 2. `ComputeTooltipGeometryDip` 改為下方優先（`src/ui/cell_tooltip.{h,cpp}`）

- 簽名新增 `max_bottom_dip`（tooltip 可佔用的下方邊界，呼叫端傳面板 client 高度 DIP）：
  ```cpp
  TooltipGeometry ComputeTooltipGeometryDip(
      const D2D1_RECT_F& cell_dip, float tip_width_dip, float tip_height_dip,
      float gap_dip, float min_top_dip, float max_bottom_dip,
      float panel_left_dip, float panel_right_dip);
  ```
- 放置規則（按序判斷）：
  1. `cell.bottom + gap_dip + tip_height_dip <= max_bottom_dip` → **below**（tooltip 在格下方、箭頭朝上）；
  2. 否則 `cell.top - gap_dip - tip_height_dip >= min_top_dip` → **above**（tooltip 在格上方、箭頭朝下）；
  3. 兩者皆放不下 → 選可用空間較大的一側（一行 `max` 比較；現行幾何不會走到，屬守衛）。
- 水平 clamp 邏輯不變。`TooltipGeometry::above` 語意不變（above=true → tooltip 在格上方、箭頭在底緣朝下）。
- `Show` 的呼叫端把面板 client 高度 DIP 傳入 `max_bottom_dip`（與 `min_top_dip` 同來源，皆由 main.cpp 傳入）。

### 3. 箭頭 transform 修正（`src/ui/cell_tooltip.cpp:276-279`）

- above 分支改為 `Scale(1, -1) * Translation(arrow_cx, body_bottom + kArrowHeightDip)`（乘法順序 swap）。
- 註解補一行 root cause：D2D row-vector 慣例，`Translation * Scale` 會把 dy 組出負值使箭頭落在視窗外；順序必須是 Scale 在前。
- below 分支（`Translation(arrow_cx, 0)`）不動。

### 4. 測試更新（`tests/unit/cell_tooltip_test.cpp`）

幾何案例改為「下方優先」語意：

- 中間列（下方空間足夠）→ `above == false`（tooltip 在格下方）且水平置中、gap 精確。
- 最後一列（`cell.bottom + gap + tip_height > max_bottom`）→ `above == true`。
- 頂列：下方一定放得下 → `above == false`。
- 水平 clamp 到 `[panel_left, panel_right]` 不變。
- 兩側皆放不下（`tip_height` 極大）→ 回傳可用空間較大的一側（規則 3）。

### 5. 追蹤

完成後更新本文件 交接區 與 `docs/work-items.md` 的 NR-179 列。

## Non-goals

- 不改 tooltip 觸發時機、內容、深色外觀、150 ms、隱藏點、點擊穿透、資源生命週期等其餘 NR-178 決策。
- 不重新設計箭頭形狀或尺寸（12×6 DIP 維持）。
- 不改 §4.2／§4.9 的 tooltip 條文。
- 不做「tooltip 超出面板顯示在桌面上」的選項（tooltip 維持收在面板邊界內）。

## Acceptance

- 中間列 hover → tooltip 顯示在格子**下方**、箭頭朝上指向格子；最後一列 → 顯示在**上方**、箭頭朝下；兩者箭頭皆清晰可見。
- 上方放置的箭頭不再被裁掉（`y' = -y + ty` 修正生效）。
- 水平置中與 clamp、gap 6 DIP 不變。
- build 無新增 warning；CTest 全綠（含更新後的 geometry 案例）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

驗證：build 無 error／新增 warning；CTest 全數 Passed（含 `nimblerun_cell_tooltip_test` 更新後的案例）；design-spec §4.8 一行改寫在 git diff 中可見。

## 交接區

- Start: 2026-08-11
- Subagent scope: 讀 `docs/work-items/NR-178-cell-tooltip.md` 交接區（放置與箭頭實作細節）與本文件；trace `src/ui/cell_tooltip.{h,cpp}`、`tests/unit/cell_tooltip_test.cpp`、`src/app_host/main.cpp` 的 Show 呼叫端（`max_bottom_dip` 來源）、`docs/design-spec.md:257`。實作 Scope 1~5，不越界。回報修改檔案、測試命令與結果、spec diff、未完成事項。
- Result（2026-08-11 實作完成，Scope 1~5 全數落地，未 commit；工作樹同時含 NR-177 的未 commit 修改，本 item 未動其檔案區塊）：

  1. **design-spec §4.8**：blockquote 逐字改為下方優先版（「於格子下方顯示……下方空間不足時（如最後一列）顯示於上方」），`git diff docs/design-spec.md` 可見。
  2. **`ComputeTooltipGeometryDip` 下方優先**（`src/ui/cell_tooltip.{h,cpp}`）：簽名新增 `max_bottom_dip`；規則＝below 先判（`cell.bottom + gap + tip_height <= max_bottom_dip` → below）、次判 above（`cell.top - gap - tip_height >= min_top_dip`）、最後守衛「兩側皆放不下取空間較大側」（`above = 上方空間 > 下方空間` 一行比較）。水平 clamp 與 `above` 語意不變。`CellTooltip::Show` 同步新增 `max_bottom_dip` 參數（header 與 out-of-line 定義）。
  3. **箭頭 transform 修正**：above 分支改為 `Scale(1, -1) * Translation(arrow_cx, body_bottom + kArrowHeightDip)`（順序 swap），註解記錄 row-vector 根因（`Translation * Scale` 組出負 dy 使箭頭落在視窗頂緣之外）。below 分支不動。
  4. **main.cpp**：`ShowTooltipForHoverCell` 以 `ClientHeightDip(window, scale)` 傳入 `max_bottom_dip`（與 `min_top_dip` 同為 main.cpp 來源，即面板 client 高度 DIP）。
  5. **測試**：`tests/unit/cell_tooltip_test.cpp` 幾何案例全部改寫為下方優先語意：中間列 below（含置中＋gap 精確）、最後一列 flip above（456+6+30=492 > 488）、頂列仍 below、左右 clamp 不變、gap 在 below／above 兩分支皆精確、兩側皆放不下（tip 200）取空間較大側（上下各一例）。

  **Agent checks（全部通過）**：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：done，無 error。
  - `cmake --build build`：成功；唯一 warning 為 `main.cpp:1519` `target_size` unused（HEAD 即存在，非本 item 引入）。
  - `ctest --test-dir build --output-on-failure`：32/32 Passed（含 `nimblerun_cell_tooltip_test` #32 更新後的 6 個幾何案例）。
  - `rg "TOOLTIPS_CLASS" src tests`：零命中（exit 1）。
  - 視覺行為（箭頭朝上指向格子的實機外觀）屬人工驗證，不在本追蹤表；幾何數值已由單元測試覆蓋。

  **未完成／注意**：無阻礙。未 commit（依指示）。`docs/work-items.md` NR-179 列已更新為 `done`（追蹤表是本 item Scope 5 的一等交付物）。
