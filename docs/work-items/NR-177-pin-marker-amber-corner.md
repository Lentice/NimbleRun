# NR-177 — 釘選標記改為琥珀截角（grid）＋直條同步換色（list）

- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.2（icon grid）／§4.3（清單列）／§4.8（拖曳）／§FR-011（釘選）／§NFR-006（不以顏色單獨傳達狀態）
- 覆寫：NR-041 Decisions #4 與硬約束（見 §覆寫聲明）

## Goal

NR-041 的釘選標記——grid 格子左上角的**藍色實心圓點**（`main.cpp:1527-1535`）——被使用者誤讀為「新增／未讀」提示（藍色圓點是 LINE／Teams／Edge 等 unread badge 的標準形狀＋顏色）。本 item 把標記改為**左上角 ◤ 截角三角形**，並新增獨立的琥珀色：

- grid：左上角圓點 → 左上角 ◤ 琥珀截角（直角在格左上角，兩腿貼齊格子上緣與左緣，斜邊朝格內）。
- list：左緣 3 DIP 直條**形狀不變**，筆刷由 `g_selected_border_brush` 換為新的琥珀筆刷。
- 高對比（HC）模式：維持現況（`highlight_text`），不引入系統色之外的顏色。

## 覆寫聲明

本 item 覆寫 NR-041（`docs/work-items/NR-041-pinned-marker.md`）的兩處既有決策，其餘決策沿用：

1. **覆寫 Decisions #4「形狀用圓點與直條，不用三角形（FillEllipse／FillRectangle 各一次呼叫即可，不需要建立 ID2D1PathGeometry——那才是畫三角形的代價）」**。
   - 新證據 1（使用者回饋）：藍色圓點被誤讀為「新增／未讀」提示——NR-041 未預見的語意衝突，且正是形狀與顏色合力的結果；NR-041 的目標「有記號即可」已不敷使用。
   - 新證據 2（成本假設不成立）：`ID2D1Factory` 是 **device-independent**（`main.cpp:482` 的 `g_dash_style` 已示範：建立一次、跨 `D2DERR_RECREATE_TARGET` 存活、與 factory 同處釋放）。PathGeometry 採同一模式建立一次後，每幀代價只有一次 `FillGeometry`，與 `FillEllipse` 同價。NR-041 把「建立 PathGeometry」當成每次繪製的代價是誤解。
2. **覆寫硬約束「不新增筆刷」「只改 `src/app_host/main.cpp`」「`src/ui/panel_palette.*` 逐位元組不變」**。
   - 新證據：顏色是本次決策的語意核心（琥珀＝圖釘隱喻色，與藍選取／紅錯誤／灰 dim／綠成功全部錯開）；既有八支筆刷沒有琥珀色，必須新增一個 palette 欄位＋一支筆刷。`panel_layout.h` 仍逐位元組不變。
3. **未重開** `docs/work-items.md` §已否決的方向 表中的任何方向。

## 使用者已確認的決策（grilling 協議，不要重新設計）

1. **兩種狀態都換色**：grid 截角與 list 直條共用同一個新筆刷；不允許同一語義在兩種狀態出現兩種顏色。
2. **形狀為 ◤ 截角**：直角在格左上角，兩腿貼齊格子上緣／左緣，斜邊朝格內；**不是**朝右的 ▸（播放語彙）、朝上的 ▲（排序語彙）或朝右下對角的 corner-fold（「新增」語彙——正是本次要消除的誤讀）。
3. **畫在選取邊框之上**：琥珀截角直接蓋過選取邊框的左上角（選取中的釘選格視覺上「角被截掉變色」，已確認可接受）。
4. **顏色為琥珀金**：light `0xA87400`／dark `0xE0B050`；HC 模式沿用 `system.highlight_text`（與現況行為完全相同，HC 的語義是系統色、形狀已承載狀態，§NFR-006）。
5. **拖曳 ghost 不畫標記**（已查證現況本來就只畫圖示，維持不變，見 §現況事實 4）。

## 硬約束

- 修改僅限四個檔案：`src/app_host/main.cpp`、`src/ui/panel_palette.h`、`src/ui/panel_palette.cpp`、`docs/design-spec.md`（§4.2 一句）。**不新增檔案、不新增模組、不新增 helper class。**
- **不改 `src/ui/panel_layout.h`**。新常數（截角腿長）以字面值寫在繪製處旁（與既有 `kPinDotRadiusDip` 等做法一致），不新增 layout 欄位。
- 不新增 `IDWriteTextFormat`、不新增 UI 字串、不新增字型依賴。D2D 資源只新增一個 `ID2D1PathGeometry`（factory 建立一次）與一支 solid color brush。
- 不改資料流：`PanelModel`／`PinStore`／`AppEntry`／catalog／search／ranking 不動。
- 不改互動：`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN`／`WM_MOUSEMOVE`／`CellAtPoint`／`PinDragState` 不動。
- 拖曳 ghost（`main.cpp:1538-1561`）只畫圖示（cache 命中 0.6 alpha 半透明、miss 為 dim 方塊），**不得**把標記加進 ghost。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限；不寫入任何檔案（本 item 無持久化）。
- spec 增補只限 §4.2 一句，不動其他條文。

## 現況事實（已查證，不需重新推導）

1. grid 標記：`main.cpp:1527-1535`——`FillEllipse`，圓心 `(cell.left + kPinDotInsetDip, cell.top + kPinDotInsetDip)`（`kPinDotRadiusDip = 4.0f`、`kPinDotInsetDip = 8.0f`），筆刷 `g_selected_border_brush`；圓點 bbox (4,4)–(12,12)，不與圖示（`cell_top + 12.0f` 起）或右上角快選框重疊。
2. list 標記：`main.cpp:1602-1608`——`FillRectangle`，3 DIP 直條貼列左緣，筆刷 `g_selected_border_brush`。
3. `selected_border` 現值：light `0x2E6DB4`／dark `0x3A5A8C`／HC `system.highlight_text`（`src/ui/panel_palette.cpp:17,30,33`）。
4. 拖曳 ghost：`main.cpp:1538-1561`——只畫 `DrawDecodedIcon(…, 0.6f)`（cache 命中）或 dim 方塊（miss）；`PinDragState` 只碰游標位置與列索引，與標記無關。來源格在拖曳期間仍正常繪製（含標記），行為不變。
5. 筆刷建立：`CreateDeviceResources`（`main.cpp:612-635`）——八支筆刷以 `&&` 鏈建立，`g_brush_colors = g_colors`（`:633`）驅動主題／HC 變更時的重建；早退守門 `main.cpp:464-468`。新筆刷併入同一鏈即自動涵蓋。
6. device-independent 資源模式：`g_dash_style`（`main.cpp:485-491`）——factory 建立一次、`DiscardDeviceResources` 不釋放、訊息迴圈後與 factory 同處釋放。三角形幾何採同一模式。
7. palette 結構：`PanelColors`（`src/ui/panel_palette.h:26-39`）；`ResolveColors` 三分支（light／dark／HC，`panel_palette.cpp:6-35`）。
8. 對比驗證（計算值，實作時以目視微調 ±0x001000）：light `0xA87400` 對 card `0xFFFFFF` ≈ 4.1:1、對 selected_fill `0xE0E0E0` ≈ 3.1:1；dark `0xE0B050` 對 card `0x2B2B2B` ≈ 7:1。皆為實心形狀（非文字）可接受。

## Scope

### 1. palette：`PanelColors` 新增 `pin_marker` 欄位（`src/ui/panel_palette.h:26-39`）

- 欄位宣告位置：`selected_border` 之後；註解：`pinned marker color; shape, not color, carries the state (design-spec §NFR-006)`。
- `ResolveColors`（`panel_palette.cpp`）三分支填值：
  - light：`0xA87400`；
  - dark：`0xE0B050`；
  - HC：`system.highlight_text`（維持現況行為）。
- `operator==` 為 default（`:38`），新欄位自動涵蓋，無需改動。

### 2. 筆刷：`CreateDeviceResources` 新增 `g_pin_marker_brush`

- 檔案作用域宣告與既有筆刷並列（`main.cpp` 頂部區域）。
- 建立：併入 `main.cpp:615-631` 的 `&&` 鏈：`SUCCEEDED(g_render_target->CreateSolidColorBrush(D2D1::ColorF(c.pin_marker), &g_pin_marker_brush))`。
- 早退守門（`:464-468`）與 `g_brush_colors` 機制（`:633`）不另改——併入鏈後自動涵蓋。

### 3. 幾何：三角形建立一次（仿 `g_dash_style` 模式，`main.cpp:485-491`）

- 位置：`CreateDeviceResources` 內 `g_dash_style` 區塊之後；`g_d2d_factory->CreatePathGeometry`（`g_d2d_factory` 為 device-independent，幾何存活 `D2DERR_RECREATE_TARGET`）。
- 頂點（以格左上角為原點）：`(0, 0)`、`(kPinMarkerSizeDip, 0)`、`(0, kPinMarkerSizeDip)`；`kPinMarkerSizeDip ≈ 10.0f`，以 constexpr 寫在建立處旁。
- 建立失敗容許為 `nullptr`：`Render()` 判空略過標記（標記是裝飾，不比照必要資源的早退）。
- 釋放：與 `g_dash_style` 相同位置（factory 釋放處）。

### 4. grid 繪製：替換 `main.cpp:1527-1535` 的 `FillEllipse` 區塊

```cpp
// NR-177: pinned marker -- an amber corner-cut triangle in the cell's
// top-left corner (overrides NR-041's dot; the dot read as an unread
// badge). Drawn last so it sits above the selection border; the legs sit
// on the cell's top and left edges, hypotenuse inside the cell. Shape,
// not color, carries the state (design-spec §NFR-006).
if (g_pins && g_pins->IsPinned(rows[i].stable_id) && g_pin_geometry) {
    g_render_target->FillGeometry(
        g_pin_geometry, g_pin_marker_brush,
        D2D1::Matrix3x2F::Translation(cell.left, cell.top));
}
```

- 順序不變：仍在快選框（`:1508-1518`）與選取邊框之後；`cell`／`rows`／`i` 皆在該迴圈作用域內。
- 腿長 10 DIP：三角形占 (0,0)–(10,10)，與圖示（左緣約 30.5、`cell_top + 12.0f` 起）與右上角快選框不重疊。

### 5. list 繪製：`main.cpp:1602-1608` 只換筆刷

- `g_selected_border_brush` → `g_pin_marker_brush`；形狀（3 DIP 直條）、尺寸、位置不變。
- 更新區塊註解：直條與 grid 截角共用 `pin_marker` 色。

### 6. spec 增補：`docs/design-spec.md` §4.2 一句

- 位置：§4.2「釘選與常用項目共用同一種格子外觀，不加分組標題或分隔線」該句之後（`docs/design-spec.md:152` 一帶）。
- 內容大意（以現行 §4.2 語體定稿）：「釘選項目以左上角琥珀色截角標示，搜尋結果清單列以左緣琥珀色直條標示；形狀承載狀態、不以顏色單獨傳達（§NFR-006），高對比模式使用系統色。」
- 理由：NR-041 刻意不碰 spec（純繪製）；本 item 新增的琥珀色是使用者可見的語意色，值得一行視覺契約，避免日後其他狀態誤用同色或回歸圓點。
- 只加一句，不動其他條文。

## Non-goals

- 不改 list 直條形狀（維持 3 DIP 長條，僅換色）。
- 不把標記加進拖曳 ghost；不改拖曳行為。
- 不為 recent 區或任何其他狀態新增標記。
- 不做無障礙朗讀（`AccessibleNameFor` 不變，與 NR-041 相同）；無障礙樹不暴露 pin 狀態。
- 不新增動畫、不新增 hover 時的標記變化。
- 不為此新增單元測試（理由同 NR-041：純 D2D 繪製，抽出可測純函式只會測到常數——畫錯位置／被蓋住／高對比不可見時不會失敗；改以手動驗收覆蓋）。
- 不新增設定選項（顏色不可自訂，屬 MVP 範圍外）。

## Acceptance

自動部分：

- 修改僅限：`src/app_host/main.cpp`、`src/ui/panel_palette.h`、`src/ui/panel_palette.cpp`、`docs/design-spec.md`（§4.2 一句）；無新增檔案。
- Release 建置無新增警告；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。
- `git diff` 檢視：grid 區塊 `FillEllipse` 消失、`FillGeometry` 出現；list 區塊僅筆刷識別字變更；palette 三個值如 §Scope 1 所訂；`panel_layout.h` 逐位元組不變。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **grid 截角**：釘選 2 個 App、清空查詢 → 前兩格左上角各有一個琥珀色 ◤ 截角（兩腿貼齊格子上緣／左緣、斜邊朝格內），其餘格子沒有。圖示與名稱位置與改動前相同。
2. **不與快選框衝突**：Alt 按住顯示數字框時，前 10 格右上角數字框與左上角截角不重疊。
3. **list 直條換色**：搜尋已釘選 App → 結果列左緣為琥珀直條；未釘選列無。同一語義在 grid（截角）與 list（直條）顏色一致。
4. **選取狀態**：方向鍵把選取移到已釘選格 → 琥珀截角蓋過選取邊框左上角（已確認可接受），邊框其餘部分完整；hover 同樣可見。list 選取列直條可見。
5. **Unpin 後消失**：右鍵 → Unpin → 記號立刻消失（grid 與 list 各驗一次）。
6. **高 DPI**：150%（或 200%）縮放螢幕 → 截角與直條等比、邊緣不糊成一團。
7. **高對比主題**：切到 Windows 高對比 → 截角／直條以系統 `highlight_text` 色顯示，與格子底色對比可辨。
8. **深色／淺色主題**：各看一次，琥珀皆可辨識（淺色 `0xA87400`、深色 `0xE0B050`）。
9. **拖曳不受影響**：拖曳釘選格 → ghost 仍只有半透明圖示（無標記），來源格標記原地顯示，放開後順序套用、標記正常。
10. **spec 一致**：§4.2 增補句子與實作行為一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的十條手動步驟。**不要**只憑編譯成功就回報完成——本 item 的行為驗證是那十條，且第 1、4、6、7、8、9 條需要實際目視。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。

## 交接區

- 修改：`src/ui/panel_palette.h/.cpp`（新增 `pin_marker` 欄位與三分支值）、`src/app_host/main.cpp`（新筆刷＋三角形幾何建立一次＋grid `FillGeometry` 替換＋list 換筆刷）、`docs/design-spec.md`（§4.2 一句）。`panel_layout.h` 逐位元組未動。
- 建置與 CTest：Release configure＋build 無新增 warning；全套件 CTest 全綠。
- 手動驗收：屬視覺人工驗證（AGENTS.md 明列不屬於追蹤表），未由 Agent 操作；十條步驟留待人工以 Release build 目視確認。
- 覆寫紀錄：NR-041 Decisions #4（形狀）與硬約束（不新增筆刷／只改 main.cpp／palette 不變）由本 item 覆寫，新證據見 §覆寫聲明。
- 未完成：無。
