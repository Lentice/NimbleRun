# NR-178 — 格狀 hover 的 cell tooltip：截斷名稱顯示完整名稱（bootstrap 樣式自繪浮動視窗）

- Phase: 3
- Depends on: NR-029、NR-015、NR-133
- Source: `docs/design-spec.md` §4.2（icon grid 名稱）／§4.8（滑鼠操作）／§4.9（視窗外觀）／§NFR-001（資源預算）／§NFR-002（待機模型）／§NFR-006（無障礙）；`CONTEXT.md`（cell tooltip／truncated display name／hover）
- 覆寫：NR-029 Non-goals「不做 tooltip」（`docs/work-items/NR-029-empty-state-grid.md:84`）
- 決策紀錄：`docs/adr/0001-cell-tooltip-custom-popup.md`

## Goal

格狀狀態（搜尋欄空白）下，指標停留的名稱被省略號截斷的格子，於約 150 ms 後以 bootstrap 樣式的深色浮動視窗顯示該格的**完整顯示名稱**；名稱未被截斷的格子不顯示。tooltip 只跟隨滑鼠 hover、不跟隨鍵盤選取；footer path bar 的既有 hover 行為（顯示路徑）不變。

## 覆寫聲明

本 item 覆寫 NR-029 Non-goals 的「不做 tooltip」既有決策，其餘決策沿用：

- 新證據（2026-08-11 使用者決策，grilling 協議）：使用者明確要求 hover 顯示完整名稱並指定 bootstrap 樣式。tooltip 在此語境有明確存在理由：格寬只有 101 DIP、名稱被截斷後，完整名稱沒有其他顯示位置——footer path bar 顯示的是**路徑**（§4.9），不是名稱。
- 技術選擇以 `docs/adr/0001-cell-tooltip-custom-popup.md` 記錄（自繪 Direct2D 浮動視窗取代原生 TOOLTIPS_CLASS，理由：原生控制項在 Win10 目標上做不出 bootstrap 圓角＋箭頭）。
- 未重開 `docs/work-items.md` §已否決的方向 表中的任何方向。

## 使用者已確認的決策（grilling 協議，不要重新設計）

1. **只在名稱被截斷時顯示**：名稱放得下就沒有 tooltip；截斷判定以文字排版量測為準，不是字元數。
2. **只做格狀狀態**：搜尋結果的清單態不加。
3. **只跟隨滑鼠 hover**：鍵盤選取（active）不顯示；鍵盤使用者的完整名稱經既有 UIA provider（`PanelModel::AccessibleNameFor`，`panel_model.cpp:261-264`）取得，螢幕閱讀器可唸出，本 item 不另做鍵盤出口。
4. **外觀一律深色底白字**：不跟隨淺色／深色主題；高對比（HC）模式改用系統語意色（§NFR-006）。
5. **工程定案**：顯示在格子正上方、水平置中，頂排格子上方空間不足時翻轉到下方；指標停留 150 ms 後出現（一次性 timer）；離開格子、滑鼠離開面板、面板隱藏、按下滑鼠鍵、視窗捲動／翻頁、拖曳釘選期間皆立即隱藏；單行文字、最寬不超過面板內容寬度；不透明（略過 bootstrap 的 90% alpha）；tooltip 視窗與其 Direct2D 資源只在顯示期間存在、隱藏即釋放。
6. **記憶體預算不調整**（本 session 已確認）：NFR-001 維持原值；tooltip 顯示時約 150–200 KB、待機時為零。

## 硬約束

- 產品行為以 `docs/design-spec.md` 為準。**本 item 一併執行下列 spec 增補（§Scope 1），不得只改程式不更新 spec**（使用者 2026-08-11 指示：spec 修正在此 work item 內執行）。
- App UI 文字一律英文（tooltip 內容是 catalog 的 `display_name` 資料，不是 UI 字串）。
- 選取、可見範圍等狀態屬 `PanelModel` 的純值狀態；hover 是純視覺狀態，留在視窗層（沿用 NR-029 契約）。tooltip 只讀 `PanelModel`，不寫入。
- 顏色一律取自 `src/ui/panel_palette.h` 的 `PanelColors`，不寫死色碼（沿用 NR-029 硬約束；「一律深色」由 palette 三分支的 light／dark 兩支填同一色值達成）。
- 待機路徑保持事件驅動（§NFR-002）：**禁止常駐 timer**；150 ms 是一次性 hover timer，只在指標停留在截斷格上時存在。
- 最小可行改動、重用既有程式碼；不為單一用途新增抽象層。不新增依賴、網路、遙測、服務、driver 或管理員權限。本 item 無持久化，不寫任何使用者資料檔。
- 新非平凡邏輯需要一個 focused runnable test（AGENTS.md）。

## 現況事實（已查證，不需重新推導）

1. **hover 追蹤**：`g_grid_hover_index`（`main.cpp:365-368`，純視覺狀態）＋ `g_hover_brush`（`:347`）；`WM_MOUSEMOVE`（`:2612-2666`，`g_pin_drag_state.Active()` 時 hover 臂被取代）、`TrackMouseEvent(TME_LEAVE)`（`:2664`）、`WM_MOUSELEAVE`（`:2667-2673`）→ hover 索引清 -1 並重繪。清除點：`EN_UPDATE` 版面切換（`:2571-2573`）、`ShowPanel`（`:1917`）、拖曳 promote（`:2629`）。
2. **footer path bar**（`:1812-1839`）：hover 優先於選取；`IsMissingPin` → `kMissingApp`；`IsDisplayablePath` → `source_path` 否則 `kWindowsApp`。**本 item 不改它。**
3. **格幾何**：`SlotRect`（`src/ui/panel_layout.cpp:69-85`，`kGridLeftDip + col*kCellWidthDip, kListTopDip + row*kCellHeightDip`）、`SlotAtPointDip`（`:90-110`）、`CellAtPoint` 包裝（`main.cpp:677-703`）；`kCellWidthDip=101`、`kCellHeightDip=96`（`panel_layout.h:40-47`）。格名繪製矩形 `cell.left+4 .. cell.right-4`（約 93 DIP，`main.cpp:1524-1530`）。
4. **截斷現況**：`g_grid_name_format` 為 `NO_WRAP`＋`CreateEllipsisTrimmingSign` trimming（`main.cpp:614-630`），名稱用 `DrawText` 繪製（內部建 layout）；**程式不知道名稱是否真的被截斷**，全檔沒有該格名稱的 `GetMetrics`。截斷判定需本 item 新增。
5. **原生 tooltip 零使用**：repo grep `TOOLTIPS_CLASS`／`TTM_` 零命中（tray 的 `NIF_TIP` 是 NOTIFYICONDATA，非 tooltip 控制項）。
6. **無障礙**：每個可見 cell 的 accessible name = 完整 `display_name`（`panel_model.cpp:261-264`、`panel_accessibility.cpp:265-284`）。
7. **拖曳守門**：`PinDragState::Dragging()`（`src/ui/pin_drag_state.h:22-25`，超過 `SM_CXDRAG` 門檻才 true）；hover 淡填色以 `!g_pin_drag_state.Dragging()` 凍結（`main.cpp:1486-1487`）。tooltip 用同一守門。
8. **色彩**：`PanelColors`（`src/ui/panel_palette.h:26-39`）；`ResolveColors` 三分支 light／dark／HC（`panel_palette.cpp:6-36`）；HC 全用系統語意色；筆刷以 `&&` 鏈建立、`g_brush_colors` 驅動主題變更重建（`main.cpp:612-635`、`:464-468` 早退守門）。
9. **視窗 chrome**：主窗 `WS_POPUP | WS_BORDER | WS_CLIPCHILDREN` ＋ `WS_EX_TOOLWINDOW | WS_EX_TOPMOST`（`main.cpp:3015-3034`）；DWM 圓角（`:3052-3054`，Win10 失敗維持方形）；除 search EDIT 外無其他 child popup。
10. **測試接法**：既有 UI 純值測試在 `tests/unit/ui_palette_layout_test.cpp`（palette＋layout 共用一檔）；測試 target 清單在 `tests/CMakeLists.txt`（NR-055 收斂後的迴圈）。`png_codec_test` 示範了在單元測試中用真實系統元件（WIC）。

## Scope

### 1. design-spec 增補（本 item 執行，三處）

a. **§4.2**「名稱限一行，超出寬度以尾端省略號截斷；不換行、不因文字長度改變格子高度。」子彈後段新增：

> 格內名稱被截斷時，指標停留該格約 150 ms 後以 cell tooltip 顯示完整顯示名稱；名稱未被截斷時不顯示。tooltip 只跟隨滑鼠 hover（§4.8），不跟隨鍵盤選取。

b. **§4.8**「格狀狀態下指標停在某格時，該格顯示淡填色並在 footer 顯示其路徑；不改變鍵盤選取。」子彈後段新增：

> hover 中的格子若名稱被截斷，指標停留約 150 ms 後於格子上方顯示 cell tooltip（完整顯示名稱），上方空間不足時顯示於下方；指標離開該格、按下滑鼠鍵、開始拖曳釘選、面板隱藏或視窗捲動／翻頁時立即消失。

c. **§4.9** 視窗外觀新增一條：

> cell tooltip 為自繪浮動視窗：深色底、白字、小圓角、指向格子的箭頭，單行文字且最寬不超過面板內容寬度；一律深色、不隨主題變色，高對比模式改用系統語意色。tooltip 視窗與其 Direct2D 資源只在顯示期間存在、隱藏即釋放，不計入待機資源預算（§NFR-001）。

### 2. 新模組 `src/ui/cell_tooltip.{h,cpp}`

純函式（無 HWND、可測試）與視窗實作（視窗層）放同一模組，仿 `panel_palette` 的 lib＋test 接法。建議簽名（實作可微調，但語意不變）：

```cpp
// 純幾何：tooltip 相對於格子的位置。above=true → tooltip 在格子上方、箭頭在底緣朝下。
struct TooltipGeometry { float left_dip; float top_dip; bool above; };
TooltipGeometry ComputeTooltipGeometryDip(
    const D2D1_RECT_F& cell_dip, float tip_width_dip, float tip_height_dip,
    float gap_dip, float min_top_dip, float panel_left_dip, float panel_right_dip);

// 純量測：name 以 format 排版（NO_WRAP、不帶 trimming）的自然寬度是否大於 max_width_dip。
bool NameIsTruncated(IDWriteFactory& factory, IDWriteTextFormat& format,
                     const wchar_t* name, float max_width_dip);
```

- `NameIsTruncated`：用**不帶 trimming** 的排版量自然寬度（`CreateTextLayout` 以極大 maxWidth 建立 → `GetMetrics` → `metrics.width > max_width_dip + epsilon`），避免測量用 layout 與繪製 layout 的 trimming 鉗制混淆；量測 layout 的字型參數（字級、粗細、NO_WRAP）須與 `g_grid_name_format` 相同來源。
- 視窗實作（視窗層，不測試）：`Show`（計算文字寬高 → 幾何 → 建立 popup HWND → 建立 HwndRenderTarget 並繪製）、`Hide`（銷毀視窗＋釋放 D2D 資源）、`IsVisible()`。
  - 視窗樣式：`WS_POPUP` ＋ `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE`；以 layered（`WS_EX_LAYERED` ＋ `SetLayeredWindowAttributes` alpha=255）＋ `WS_EX_TRANSPARENT` 達成滑鼠點擊穿透——tooltip 不攔截任何點擊，下方格子的行為與無 tooltip 時相同。含 DWM 圓角（仿 `main.cpp:3052-3054`；Win10 失敗維持方形）。
  - 文字字型取系統 message font（`DEFAULT_GUI_FONT` 經 GDI interop 轉 `IDWriteTextFormat`），字級約 12 DIP；白字、深底。
  - 顏色不寫死：`PanelColors` 新增 `tooltip_bg`／`tooltip_text`（見 Scope 3）。
  - DPI：`Show` 時由 main.cpp 傳入目前面板的 scale，幾何由 DIP 換算物理像素後 `SetWindowPos`。
  - 繪製：圓角矩形（`FillRoundedRectangle`，半徑約 4 DIP）＋ 指向格子的實心三角（`FillGeometry` 或 `FillTriangle` 等價呼叫；幾何以 `g_dash_style` 同模式建立一次）＋ 單行白字（`DrawTextLayout`）。HC 模式改用 `system.window`／`system.window_text`。
  - 不建立也不觸碰 `PanelModel`；名稱字串由 main.cpp 傳入。

### 3. `PanelColors` 新增 `tooltip_bg`／`tooltip_text`（`src/ui/panel_palette.{h,cpp}`）

- light：`tooltip_bg = 0x212529`、`tooltip_text = 0xFFFFFF`；dark：同值（「一律深色」的載體）；HC：`system.window`／`system.window_text`。
- `operator==` 為 default（`:38`），新欄位自動涵蓋。

### 4. `main.cpp` 接線（最小改動）

- hover 命中格改變的分支（`:2650-2656`）：新格 `>= 0` 且該格名稱被截斷（`NameIsTruncated`，量測 layout 沿用 `g_grid_name_format` 的字型設定）→ 啟動一次性 `SetTimer` 150 ms；否則 `KillTimer`＋`Hide()`。
- `WM_TIMER`（新 id）：觸發時若 hover 格仍存在且仍截斷 → `Show`；`KillTimer`（一次性）。
- 隱藏點（一律 `KillTimer`＋`Hide()`）：`WM_MOUSELEAVE`（`:2667`）、`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN`（格狀）、`WM_MOUSEWHEEL` 與 `PgUp`／`PgDn` 捲動、`EN_UPDATE` 版面切換（`:2571-2573`）、`ShowPanel`（`:1917`）、拖曳 Active 分支（`:2616`，含按壓即拖曳的過渡）。
- 不用 `WM_MOUSEMOVE` 逐事件更新 tooltip 內容；只在 hover 格改變與 timer 觸發時動作。
- 主題／DPI 變更不即時處理：tooltip 下次 Show 時自然取得新色值與新 scale（惰性，與 `ResolveColors` 每幀解析的模式一致）。
- `Render()` 完全不動。

### 5. 測試

`tests/unit/cell_tooltip_test.cpp`（新增 target，併入 `tests/CMakeLists.txt` 的既有迴圈）：

- `ComputeTooltipGeometryDip`：上方空間足夠 → `above=true` 且水平置中；頂排（上方不足）→ `above=false`；水平 clamp 到 `[panel_left, panel_right]`；`gap_dip` 有被遵守。
- `NameIsTruncated`：短名稱 → false；長名稱 → true；恰好等寬（含 epsilon 邊界）→ false；空字串 → false。測試內以真實 DWrite 物件（`CoInitializeEx` ＋ `CreateDWriteFactory` ＋ `CreateTextFormat`，仿 `png_codec_test` 使用系統元件的先例）。

### 6. 追蹤

實作完成後更新本文件 交接區 與 `docs/work-items.md` 的 NR-178 列（狀態與依賴只在追蹤表）。

## Non-goals

- 清單狀態（搜尋結果）不加 tooltip。
- 鍵盤選取（active）不顯示 tooltip；不新增其他鍵盤出口（既有 UIA accessible name 已含全名）。
- 名稱未被截斷時不顯示；不做「永遠顯示」。
- 不跟隨淺色／深色主題（一律深色）；不做 90% 半透明 alpha。
- 不做動畫、不做 MSAA／UIA tooltip role、不加設定項、不持久化。
- 不改 footer path bar、不改 hover 淡填色、不改 `Render()`、不改 `PanelModel`／catalog／search／icons 任何邏輯。
- 不採用原生 `TOOLTIPS_CLASS`（ADR-0001）。

## Acceptance

- 格狀狀態下，指標停留在名稱被截斷的格子上約 150 ms → tooltip 出現並顯示該格完整顯示名稱；名稱未被截斷的格子永遠不出現。
- 頂排格子 tooltip 顯示於格子下方；任何格子 tooltip 不超出面板左右邊界。
- 外觀：深色底、白字、小圓角、指向格子的箭頭（淺色與深色主題下相同）；HC 模式為系統語意色。
- tooltip 出現期間：hover 淡填色與 footer path bar 行為不變、鍵盤選取不變、`Enter` 仍啟動選取格；點擊 tooltip 區域的行為與沒有 tooltip 時完全相同（點擊穿透）。
- 指標離開格子／離開面板、按下滑鼠鍵、滾輪或翻頁捲動、面板隱藏、拖曳釘選開始 → tooltip 立即消失。
- tooltip 隱藏後其 HWND 與 D2D 資源已銷毀（程式碼檢查 lazy create／destroy 路徑）；150 ms timer 只存在於 hover 期間，無常駐 timer（§NFR-002）。
- 建置無新增警告；CTest 全綠且新增測試通過；repo 內 `rg "TOOLTIPS_CLASS" src` 零命中。
- design-spec §4.2／§4.8／§4.9 三處增補完成（本 item 的一等交付物）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg "TOOLTIPS_CLASS" src tests
```

驗證：build 無 error／新增 warning；CTest 全數 Passed（含新增 `cell_tooltip_test`）；`rg` 零命中；design-spec 三處增補在 git diff 中可見。

## 交接區

- Start: 2026-08-11
- Subagent scope: 依「硬約束」與「現況事實」讀完文件與 trace 點；實作 Scope 1~6，不越界。回報修改檔案、測試命令與結果、spec 增補的 diff、未完成事項。
- Result（2026-08-11 實作完成，Scope 1~6 全數落地，未 commit；工作樹同時含 NR-177 的未 commit 修改，本 item 未動其檔案區塊）：

  1. **design-spec 三處增補**（§4.2「名稱限一行」子彈後段、§4.8 hover 子彈後段、§4.9 新增「cell tooltip 為自繪浮動視窗」一條）照 Scope 1 文字逐字寫入，`git diff docs/design-spec.md` 可見。
  2. **新模組** `src/ui/cell_tooltip.{h,cpp}`：純函式 `ComputeTooltipGeometryDip`（above 判定以 `min_top_dip`＝格狀區上緣 `kListTopDip` 為界，頂排翻轉下方；水平 clamp 到面板內容區）與 `NameIsTruncated`（巨大 layout 量自然寬度、epsilon 0.01 DIP、空字串 false）；`CellTooltip` 視窗類（`WS_EX_TOOLWINDOW|TOPMOST|NOACTIVATE|LAYERED|TRANSPARENT` 點擊穿透、`SetLayeredWindowAttributes` alpha=255、DWM 圓角仿 `main.cpp:3052`、message font 12 DIP 經 GDI interop、箭頭以 SetTransform＋FillGeometry 畫（此 MinGW d2d1.h 無 FillGeometry 的 transform 多載）），Show 建立／Hide 全釋放。
  3. **palette**：`PanelColors` 新增 `tooltip_bg`（light/dark 同為 `0x212529`）／`tooltip_text`（同為 `0xFFFFFF`），HC 分支用 `system.window`／`system.window_text`。
  4. **main.cpp 接線**：`kTooltipTimerId=3`＋`kTooltipDelayMs=150`＋`kTooltipNameWidthDip=93`；`UpdateTooltipTimer`（hover 改變即 Hide＋重新武裝一次性 timer）、`HideCellTooltip`、`ShowTooltipForHoverCell`；隱藏點＝`HidePanel`（涵蓋所有面板隱藏路徑）、`WM_MOUSELEAVE`、`WM_LBUTTONDOWN`/`WM_RBUTTONDOWN`、`WM_MOUSEWHEEL`、`VK_PRIOR`/`VK_NEXT`、`EN_UPDATE`、拖曳 Active 分支、`ShowPanel`、`WM_DESTROY`；`Render()` 未動。
  5. **測試**：`tests/unit/cell_tooltip_test.cpp`（真實 DWrite 物件，`CoInitializeEx`＋`CreateTextFormat` 仿 png_codec_test）：幾何四例（上方置中／頂排翻轉／左右 clamp／gap 精確）＋截斷四例（短長空 null）＋邊界回饋（恰等寬與 epsilon 內 false、窄 1 DIP true）。
  6. **CMake**：`cell_tooltip.cpp` 併入 `nimblerun_ui`；測試**不得**進 NIMBLERUN_TESTS 迴圈——該迴圈的 TAIL sublist 長度寫死 17（`list(SUBLIST ... 10 17 ...)`），尾端新增會被截斷；改照 `rebuild_pipeline_test` 先例在檔尾加獨立 block（CTest 編號不位移，為 #32）。

  **Agent checks（全部通過）**：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：done，無 error。
  - `cmake --build build --clean-first`：成功，唯一 warning 為 `main.cpp:1518` 的 `target_size` unused（grid 分支，HEAD 即存在，非本 item 引入）。
  - `ctest --test-dir build --output-on-failure`：32/32 Passed（含新增 `nimblerun_cell_tooltip_test` #32）。首輪曾見 `nimblerun_catalog_refresh_test` 0xc0000409 一次，單獨重跑與第二輪全跑皆 Passed——該測試只鏈 `nimblerun_catalog`（本 item 未觸碰），為既有平行執行下的偶發 race，非本 item 引入。
  - `rg "TOOLTIPS_CLASS" src tests`：零命中（exit 1）。
  - 視覺行為（tooltip 出現／翻轉／穿透）屬人工驗證，不在本追蹤表；程式碼路徑：timer 只在 hover 截斷格期間存在、tooltip HWND＋D2D 資源於 Show/Hide 間存活。

  **未完成／注意**：無阻礙。未 commit（依指示）。工作樹自帶 NR-177 的未 commit 修改與新文件，與本 item 範圍無交疊。
