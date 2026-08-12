# NR-180 — cell tooltip 改用 Windows 原生 tooltip（覆寫 NR-178 技術路線與 ADR-0001）

- Phase: 3
- Depends on: NR-178, NR-179
- Source: `docs/design-spec.md` §4.8（現文 `design-spec.md:257`）／§4.9（現文 `design-spec.md:277`）；`docs/adr/0001-cell-tooltip-custom-popup.md`；`docs/work-items/NR-178-cell-tooltip.md` 決策 #4／#5；`docs/work-items/NR-179-cell-tooltip-below-and-arrow-fix.md`
- 覆寫：NR-178 決策 #4（深色底白字）與 #5 的「自繪 Direct2D 浮動視窗」技術部分、`docs/adr/0001-cell-tooltip-custom-popup.md` 的技術選擇、NR-179 的「修復自繪箭頭」目標；**不**覆寫 NR-178 決策 #1/#2/#3 與 NR-179 的「下方優先」放置規則

## Goal

格狀狀態（搜尋欄空白）下，指標停留的名稱被省略號截斷的格子，於約 150 ms 後以 **Windows 原生 tooltip**（`TOOLTIPS_CLASS` track tooltip）顯示該格的**完整顯示名稱**；名稱未被截斷的格子不顯示。tooltip 只跟隨滑鼠 hover、不跟隨鍵盤選取；footer path bar 的既有 hover 行為（顯示路徑）不變。

## 覆寫聲明

本 item 覆寫下列既有決策，其餘沿用（NR-178 文件與 ADR-0001 維持原樣，是歷史紀錄）：

- **覆寫 NR-178 決策 #4「外觀一律深色底白字」**：查證確認（2026-08-11，MS Learn）`TTM_SETTIPBKCOLOR`／`TTM_SETTIPTEXTCOLOR` 在啟用視覺樣式的 Win10/11 上**無效**，原生 tooltip 無法自訂底色與文字色 → 外觀改為系統主題外觀。
- **覆寫 NR-178 決策 #5 的技術部分「自繪 Direct2D 浮動視窗」與 ADR-0001**：自繪路線的箭頭在 NR-178 實作後不可見（「沒有尖角」）、NR-179 修正 transform 後使用者仍回報「尖角的處理還是有問題」（2026-08-11）→ 放棄自繪，改用原生 `TOOLTIPS_CLASS`。
- **覆寫 NR-179 的「修復箭頭不可見」目標**：原生 tooltip 無箭頭，該目標取消；NR-179 的「下方優先」放置規則**保留**（track tooltip 支援精確座標）。
- 未重開 `docs/work-items.md` §已否決的方向 表中的任何方向。

## 使用者已確認的決策（grilling 協議，2026-08-11，不要重新設計）

1. **只在名稱被截斷時顯示**（沿用 NR-178 決策 #1）：截斷判定以文字排版量測為準（`NameIsTruncated`）。
2. **只做格狀狀態**（沿用 NR-178 決策 #2）：清單態不加。
3. **只跟隨滑鼠 hover**（沿用 NR-178 決策 #3）：鍵盤選取不顯示；鍵盤使用者的完整名稱經既有 UIA provider（`PanelModel::AccessibleNameFor`）取得。
4. **原生 tooltip 標準樣式**：Win11 小圓角、Win10 方角；**允許兩平台外觀差異**；無箭頭。
5. **外觀一律系統主題**：不再深色底白字；高對比（HC）模式由系統自動使用語意色（§NFR-006）。
6. **放置規則保留 NR-179 下方優先**：track tooltip 以 `TTM_TRACKPOSITION` 顯示於格子下方，下方放不下（最後一列）翻轉上方；水平 clamp 到面板內容寬度。
7. **點擊穿透保留**：`TOOLINFO::uFlags` 設 `TTF_TRANSPARENT`，tooltip 範圍內滑鼠事件轉發給 parent。
8. **150 ms 一次性 timer 沿用**（main.cpp 既有 `kTooltipTimerId`／`kTooltipDelayMs`）：track tooltip 由 `TTM_TRACKACTIVATE` 啟動即立即顯示，`TTM_SETDELAYTIME` 不適用，顯示時機由自持 timer 控制。
9. **tooltip 控制項常駐**：一個 `TOOLTIPS_CLASS` 視窗（約數 KB），打破 NR-178 決策 #5 的「資源只在顯示期間存在」字面；記憶體預算**不調整**（NFR-001 維持，常駐幾 KB 遠小於原自繪的 150–200 KB）。
10. **長名稱 wrap 多行**：`TTM_SETMAXTIPWIDTH` 設面板內容寬度，過長自動換行，完整顯示長名稱（取代自繪版的「單行超寬裁掉」）。

## 硬約束

- 產品行為以 `docs/design-spec.md` 為準。**本 item 一併執行下列 spec 修正（§Scope 1），不得只改程式不更新 spec**（使用者 2026-08-11 指示：spec 修正在此 work item 內執行）。
- App UI 文字一律英文（tooltip 內容是 catalog 的 `display_name` 資料，不是 UI 字串）。
- tooltip 只讀 `PanelModel`，不寫入；hover 是純視覺狀態，留在視窗層（沿用 NR-029 契約）。
- 待機路徑保持事件驅動（§NFR-002）：150 ms 是一次性 hover timer，無常駐 timer。
- 最小可行改動、重用既有程式碼；不為單一用途新增抽象層。不新增依賴、網路、遙測、服務、driver 或管理員權限。本 item 無持久化。
- 新非平凡邏輯需要一個 focused runnable test（AGENTS.md）。

## 現況事實（已查證，不需重新推導）

1. **原生 tooltip 行為查證（2026-08-11，MS Learn）**：
   - `TTM_SETTIPBKCOLOR`／`TTM_SETTIPTEXTCOLOR`：啟用視覺樣式時**無效**（Win10/11 皆然）。
   - track tooltip 定位：`TTF_TRACK`＋`TTF_ABSOLUTE`＋`TTM_TRACKPOSITION` 會把 tooltip 放在**給定的螢幕座標**，不自行翻面；滑鼠移開不自動隱藏（需 `TTM_TRACKACTIVATE(FALSE)`）。
   - 點擊：原生 tooltip 預設吞掉其範圍內的滑鼠事件；`TTF_TRANSPARENT` 轉發給 parent。
   - 延遲：track tooltip 由 `TTM_TRACKACTIVATE` 啟動即顯示，`TTM_SETDELAYTIME`（`TTDT_INITIAL`）不適用。
   - 外觀：Win10 方角、Win11 小圓角（comctl32 v6 主題自繪）；balloon（`TTS_BALLOON`）的 stem 方向由系統決定、**不可控**。
2. **現行自繪模組**：`src/ui/cell_tooltip.{h,cpp}`（348 行）。純函式 `ComputeTooltipGeometryDip`（`cell_tooltip.cpp:108-138`，NR-179 下方優先語意）與 `NameIsTruncated`（`:140-158`）**保留**；`CellTooltip` 視窗類（自繪 D2D：`Show` `:160-314` ／`Hide` `:316-322`）**刪除重寫**為原生封裝。
3. **接線**：`main.cpp` `UpdateTooltipTimer`（`:731-746`）、`HideCellTooltip`（`:750-753`）、`ShowTooltipForHoverCell`（`:758-787`）；`g_cell_tooltip` 全域（`:384`）；`kTooltipTimerId=3`（`:100`）。隱藏點已齊（HidePanel、WM_MOUSELEAVE、按鍵、滾輪、EN_UPDATE、拖曳、ShowPanel、WM_DESTROY），**全部沿用**。
4. **顏色**：`PanelColors` 新增的 `tooltip_bg`／`tooltip_text`（`panel_palette.{h,cpp}`）**刪除**（原生 tooltip 用系統色，不再需要）。
5. **測試**：`tests/unit/cell_tooltip_test.cpp` 的幾何案例（NR-179 下方優先 6 例）與截斷案例（4 例＋邊界）**保留**——它們測的純函式不動。
6. **幾何參數**：格狀最後一列底緣＝`kFooterTopDip`(456)、面板高 `kPanelHeightDip`(488)（`src/ui/panel_layout.h:11-12`）；`ShowTooltipForHoverCell` 以 `ClientHeightDip(window, scale)` 傳 `max_bottom_dip`。
7. **DIP→螢幕座標**：`TTM_TRACKPOSITION` 需要螢幕座標；現行自繪用 `ClientToScreen(panel)`＋DIP×scale（`cell_tooltip.cpp:242-247`），原生封裝沿用同一轉換。

## Scope

### 1. design-spec 修正（本 item 執行）

- **§4.2**：不動（行為不變）。
- **§4.8**：blockquote（現文 `design-spec.md:257`）改為：

  > hover 中的格子若名稱被截斷，指標停留約 150 ms 後以 Windows 原生 tooltip 於格子下方顯示 cell tooltip（完整顯示名稱），下方空間不足時（如最後一列）顯示於上方；指標離開該格、按下滑鼠鍵、開始拖曳釘選、面板隱藏或視窗捲動／翻頁時立即消失。

- **§4.9**：現文 `design-spec.md:277` 整條改為：

  > cell tooltip 使用 Windows 原生 tooltip 外觀（Win11 小圓角、Win10 方角，允許平台外觀差異）：系統主題底色與文字色、最寬不超過面板內容寬度、過長自動換行、無箭頭。tooltip 控制項常駐（約數 KB），顯示時依格子幾何定位於格子下方或上方；不計入待機資源預算（§NFR-001）。

### 2. `src/ui/cell_tooltip.{h,cpp}` 重寫（保留純函式，刪自繪）

- **保留**：`ComputeTooltipGeometryDip`（含 `TooltipGeometry`）與 `NameIsTruncated` 簽名與實作不變。
- **刪除**：自繪視窗類（`TooltipWndProc`、`RegisterTooltipClass`、`CreateTooltipTextFormat`、`Show` 的 D2D 建立／繪製／箭頭／DWM 圓角、`ReleaseResources`、layered 相關）；`kTooltipClass`、`kTooltipFontDip`、`kPaddingXDip/YDip`、`kGapDip`、`kRadiusDip`、`kArrowWidthDip/HeightDip`、`kMeasureSizeDip`（若 `NameIsTruncated` 仍用則保留後者）。
- **新增**原生封裝（視窗層，不測試；建議 API，實作可微調但語意不變）：
  ```cpp
  class CellTooltip {
   public:
    void EnsureCreated(HWND panel, HWND tooltip_owner /* or reuse panel */);
    void Show(HWND panel, float scale, const D2D1_RECT_F& cell_dip,
              float min_top_dip, float max_bottom_dip, float panel_left_dip,
              float panel_right_dip, const wchar_t* name);
    void Hide();
    bool IsVisible() const;
   private:
    HWND window_ = nullptr;
  };
  ```
  - `window_`：`CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, ...)`，常駐；`TTM_ADDTOOL` 一次（`TOOLINFO`：`uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT`，`hwnd`＝panel，`uId`＝面板，`lpszText`＝名稱緩衝）；`TTM_SETMAXTIPWIDTH` 設面板內容寬度（面板寬度 DIP × scale）；`TTM_SETDELAYTIME(TTDT_AUTOPOP, ...)` 不必（track 模式無自動隱藏）。
  - `Show`：`TTM_SETTIPTEXTW` 更新名稱 → `TTM_TRACKACTIVATE(TRUE)` → 用 `ComputeTooltipGeometryDip` 算 DIP 幾何 → `ClientToScreen(panel)`＋DIP×scale 得螢幕座標 → `TTM_TRACKPOSITION`。
  - `Hide`：`TTM_TRACKACTIVATE(FALSE)`。
  - 不建立也不觸碰 `PanelModel`；名稱字串由 main.cpp 傳入。
- **`main.cpp` 呼叫端**：`ShowTooltipForHoverCell` 改傳面板指標＋scale＋cell DIP＋四邊界＋名稱；`HideCellTooltip` 改呼叫 `Hide()`。timer 邏輯、截斷 gate、隱藏點全不動。

### 3. `PanelColors` 刪除 `tooltip_bg`／`tooltip_text`（`src/ui/panel_palette.{h,cpp}`）

- 移除兩欄位與三分支（light/dark/HC）對應行；`ResolveColors` 同步。`operator==` 為 default 不需動。

### 4. 測試

- `tests/unit/cell_tooltip_test.cpp` 案例**保留不變**（純函式未動）。若 `cell_tooltip.h` 的 include 或函式簽名有整理，確保測試仍編譯通過。
- 原生封裝為視窗層，不測試（與 NR-178 的自繪視窗層相同政策）。

### 5. 追蹤

完成後更新本文件 交接區 與 `docs/work-items.md` 的 NR-180 列（狀態與依賴只在追蹤表）。

## Non-goals

- **不修復自繪箭頭**（該路線已放棄）。
- 不加 `TTS_BALLOON`、不控制箭頭方向（系統不可控）。
- 不調記憶體預算（NFR-001 維持；常駐控制項幾 KB）。
- 不改觸發時機、截斷 gate、只做格狀、只跟隨 hover、隱藏點、150 ms、`ComputeTooltipGeometryDip` 語意與測試。
- 不做「tooltip 超出面板顯示在桌面上」的選項（維持收在面板邊界內）。
- 不編輯 NR-178／NR-179 文件、不編輯 ADR-0001（歷史紀錄）。

## Acceptance

- 格狀狀態下，指標停留在名稱被截斷的格子上約 150 ms → 原生 tooltip 出現並顯示該格完整顯示名稱；名稱未被截斷的格子永遠不出現。
- 原生外觀（Win11 圓角／Win10 方角皆為系統樣式）；HC 模式為系統語意色；無箭頭。
- 中間列 hover → tooltip 顯示於格子**下方**；最後一列 → 顯示於**上方**；不超出面板左右邊界。
- 點擊 tooltip 區域的行為與沒有 tooltip 時完全相同（`TTF_TRANSPARENT`）。
- 長名稱在面板內容寬度內 wrap 多行完整顯示。
- 指標離開格子／離開面板、按下滑鼠鍵、滾輪或翻頁捲動、面板隱藏、拖曳釘選開始 → tooltip 立即消失。
- tooltip 控制項常駐一個 HWND；150 ms timer 只存在於 hover 期間，無常駐 timer（§NFR-002）。
- 建置無新增警告；CTest 全綠（既有 `nimblerun_cell_tooltip_test` 案例不變）；自繪殘留檢查：`rg "tooltip_bg|tooltip_text|kArrowWidthDip|kRadiusDip|FillRoundedRectangle" src` 零命中。
- design-spec §4.8／§4.9 兩處修正完成（本 item 的一等交付物）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg "tooltip_bg|tooltip_text|kArrowWidthDip|kRadiusDip|FillRoundedRectangle" src
```

驗證：build 無 error／新增 warning；CTest 全數 Passed（含既有 `nimblerun_cell_tooltip_test`）；`rg` 零命中（exit 1）；design-spec §4.8／§4.9 修正與 ADR-0002 在 git diff 中可見。

## 交接區

- Start: 2026-08-11
- Subagent scope: 讀本文件＋`docs/work-items/NR-178-cell-tooltip.md`（決策 #4/#5、交接區自繪細節僅作對照）＋`docs/adr/0001-cell-tooltip-custom-popup.md`；trace `src/ui/cell_tooltip.{h,cpp}`、`src/app_host/main.cpp`（`:100`、`:384`、`:731-787` 與全部 `HideCellTooltip` 呼叫點）、`src/ui/panel_palette.{h,cpp}`、`tests/unit/cell_tooltip_test.cpp`、`docs/design-spec.md:257/277`。實作 Scope 1~5，不越界。回報修改檔案、測試命令與結果、spec diff、未完成事項。
- Result（2026-08-11 實作完成，Scope 1~5 全數落地，未 commit；工作樹同時含 NR-177/NR-178/NR-179 的未 commit 修改，本 item 未動其檔案區塊）：

  1. **design-spec 兩處修正**照 Scope 1 文字逐字寫入：§4.8 blockquote（`design-spec.md:257`）加入「以 Windows 原生 tooltip」，§4.9 該條（`:277`）整條改為原生外觀描述。`git diff docs/design-spec.md` 可見。
  2. **`src/ui/cell_tooltip.{h,cpp}` 重寫**：純函式 `ComputeTooltipGeometryDip`／`NameIsTruncated` 簽名與實作一字未改（含 `TooltipGeometry`、`kMeasureSizeDip`、`kTruncationEpsilonDip`）；自繪視窗類（`TooltipWndProc`／`RegisterTooltipClass`／`CreateTooltipTextFormat`／D2D 建立繪製／箭頭／DWM 圓角／layered／`ReleaseResources`）與 `kTooltipClass`、`kTooltipFontDip`、`kPaddingXDip/YDip`、`kGapDip`、`kRadiusDip`、`kArrowWidthDip/HeightDip` 全刪。`CellTooltip` 改為原生封裝：`EnsureCreated`（`CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS)` 常駐、owner=panel；`TOOLINFOW`：`uFlags=TTF_TRACK|TTF_ABSOLUTE|TTF_TRANSPARENT`、`hwnd/uId=panel`、`lpszText`＝`name_` 緩衝；`TTM_ADDTOOL` 一次）、`Show`（`name_`=name → `TTM_SETTIPTEXTW` → `TTM_SETMAXTIPWIDTH`=內容寬度 DIP×scale（每次 Show 重設，DPI 變更惰性生效）→ `TTM_TRACKACTIVATE(TRUE)` → `ComputeTooltipGeometryDip`（width=內容寬度、height 估 40 DIP、gap 6 DIP，原生控制項自算 wrap 高度，僅供下方優先／最後一列翻轉判定）→ `ClientToScreen(panel)`＋DIP×scale → `TTM_TRACKPOSITION`）、`Hide`（`TTM_TRACKACTIVATE(FALSE)`）；`IsVisible`＝`visible_` 旗標。MinGW `commctrl.h` 無 `TTM_SETTIPTEXTW`（WM_USER+52），於 cpp 內 `#ifndef` 補定義。
  3. **`main.cpp` 呼叫端**：`ShowTooltipForHoverCell` guard 移除 `g_d2d_factory`，`Show` 改傳 `(window, scale, cell DIP, kListTopDip, ClientHeightDip, kListLeftDip, kListRightDip, display_name)`；`HideCellTooltip` 仍呼叫 `Hide()`。timer 邏輯、截斷 gate、隱藏點全不動。`g_cell_tooltip` 註解更新為常駐原生 tooltip。
  4. **palette**：`PanelColors` 刪除 `tooltip_bg`／`tooltip_text`（`panel_palette.h` 兩欄、`panel_palette.cpp` 三分支值），`ResolveColors` 同步，`operator==` default 未動。
  5. **建置**：`CMakeLists.txt` `NimbleRun` link 清單新增 `comctl32`（系統 DLL，確保 `TOOLTIPS_CLASS` 可用；非新增第三方依賴）。測試 target 不需動（純函式不引用 comctl32 符號）。
  6. **測試**：`tests/unit/cell_tooltip_test.cpp` 案例一字未改，target 未動，編譯通過。

  **Agent checks（全部通過）**：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：done，無 error。
  - `cmake --build build`：成功，唯一 warning 為 `main.cpp:1517` 的 `target_size` unused（grid 分支，HEAD 即存在，非本 item 引入，NR-178 交接區已有記錄）。
  - `ctest --test-dir build --output-on-failure`：32/32 Passed（含既有 `nimblerun_cell_tooltip_test` #32）。
  - `rg "tooltip_bg|tooltip_text|kArrowWidthDip|kRadiusDip|FillRoundedRectangle" src`：tooltip 相關符號（tooltip_bg／tooltip_text／kArrowWidthDip／kRadiusDip）零命中；唯一 2 個 `FillRoundedRectangle` 命中為 `main.cpp` `Render()` 的既有卡片框與搜尋框填色（NR-021/NR-023 程式碼，非 tooltip 殘留，NR-178 之前即存在）。
  - design-spec §4.8／§4.9 兩處修正與 `CMakeLists.txt`、`cell_tooltip.*` 新檔在 git diff 中可見。

  **未完成／注意**：無阻礙。未 commit（依指示）。視覺行為（原生外觀、win11 圓角／win10 方角、wrap、穿透）屬人工驗證，不在本追蹤表；track tooltip 由 `TTM_TRACKACTIVATE` 顯示、由自持 150 ms timer 控制時機（`TTM_SETDELAYTIME` 不適用，符合本 item 決策 #8）。

  **主 Agent 驗證後修正（2026-08-11）**：agent 初版以「面板內容全寬 608 DIP」當 tip 尺寸估算，導致兩個視覺偏差：(1) 中間格 tooltip 水平位置被 clamp 到面板左緣，非格子正下方置中；(2) 高度估算 40 DIP 使最後一列 tooltip 過早翻上（單行實際約 24 DIP，456+6+24=486 ≤ 488 其實放得下）。修正：`Show` 在 `TTM_TRACKACTIVATE(TRUE)` 後以 `TTM_GETBUBBLESIZE`（`TTF_TRACK|TTF_ABSOLUTE` 下有效，MS Learn 明示用於精確定位）量實際像素尺寸，換算 DIP 後再算幾何，置中與翻轉判定皆以真實尺寸為準；量測失敗時 fallback 內容寬度／估算（仍收在面板邊界內）。修正後重跑：`cmake --build build` 無新 warning、`ctest` 32/32 Passed。
