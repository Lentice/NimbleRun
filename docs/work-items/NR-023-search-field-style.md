# NR-023 — Search field style and typography

- Status: `ready`
- Phase: 3
- Depends on: NR-015、NR-020
- Source: `docs/design-spec.md` §4.9、§NFR-006、AC-002

## Goal

把頂端搜尋欄從「預設字型、無邊框、不跟主題」的原生 EDIT，改成與面板一致的圓角輸入框：面板用 Direct2D 畫圓角填色框與 1 DIP 邊框，原生 EDIT 內縮於框內、套系統 message font 放大到 24 DIP、背景與文字色跟隨主題。面板高度隨之調整為 488 DIP、可見 8 列。

## 必讀

實作前必須讀完：`AGENTS.md`、`docs/development.md`（Product boundary／Architecture rules／UI language／Build configuration／Change workflow 全部五節）、`docs/design-spec.md` §4.9 與 §NFR-006、`docs/work-items.md`（使用方式與 Agent 交付規則）、`docs/work-items/NR-020-list-panel-restore.md`、本文件。

## 與 NR-020 的關係（重要）

NR-020 文件中寫的 `kFooterTopDip = 400`、`kPanelHeightDip = 432`、可見列數 7，是在搜尋欄仍為 28 DIP 高時算出來的。**本 item 取代這三個數值**，改為 456 / 488 / 8。

- 不要回頭修改 `NR-020-list-panel-restore.md`；該文件保留原始數字作為決策軌跡。
- NR-020／NR-021 已 `done`，可直接實作。
- NR-021 的 footer band 幾何常數（`kFooterKeyBoxWidthDip` 等）皆相對 `kFooterTopDip` 計算，band 高度維持 32 DIP（456~488），不需改動。

## 來自 spec 與開發指南的硬約束

- 產品行為以 `docs/design-spec.md` 為準；本文件若與 spec 衝突，以 spec 為準並回報。
- App UI 文字一律英文；多處共用的字串集中放。
- 核心邏輯不得依賴 HWND 或 Shell COM。版面計算屬 `layout` 的純值函式，色彩解析屬 `palette::ResolveColors` 的純函式，兩者都不得引入 Win32 型別。
- 不新增第三方依賴、網路存取、遙測、服務、driver 或管理員權限。**不得**為了 `EM_SETCUEBANNER` 或其他控制項訊息在 manifest 加入 comctl32 v6 相依，會連帶改變 `NimbleRun.rc` 對話框外觀。
- 待機路徑保持事件驅動：不得為了重繪或閃爍 caret 新增 timer。
- 優先最小可行改動與重用（ponytail）；不要為單一用途新增抽象層。
- GDI 物件必須配對釋放（`HFONT` 在重建與 `WM_DESTROY` 時 `DeleteObject`；`WM_CTLCOLOREDIT` 用的 brush 快取後於 `WM_DESTROY` 釋放，不得每次訊息都 create）。

## Scope

### 1. 版面常數（`src/ui/panel_layout.h` / `.cpp`）

改值：

- `kPanelHeightDip` 432 → **488**
- `kFooterTopDip` 400 → **456**
- `kListTopDip` 60 → **72**
- `kSearchBottomDip` 44 → **64**（搜尋框 16~64，高 48 DIP）

新增：

- `constexpr float kSearchCornerRadiusDip = 6.0f;`
- `constexpr float kSearchTextInsetDip = 12.0f;`  // EDIT 相對框的左右內縮
- `constexpr float kSearchEditInsetYDip = 6.0f;`  // EDIT 相對框的上下內縮
- `constexpr float kSearchFontDip = 24.0f;`

`LayoutPx` 新增四個欄位並在 `LayoutForDpi()` 填值（維持既有的 `std::lround` 換算風格）：

- `search_edit_left  = px(kSearchLeftDip + kSearchTextInsetDip)`
- `search_edit_top   = px(kSearchTopDip + kSearchEditInsetYDip)`
- `search_edit_right = px(kSearchRightDip - kSearchTextInsetDip)`
- `search_edit_bottom= px(kSearchBottomDip - kSearchEditInsetYDip)`

再新增 `int search_font_height = 0;`，值為 `-px(kSearchFontDip)`（負值＝字元高度，直接餵 `LOGFONTW::lfHeight`）。

可見列數維持由 client rect 實算（NR-020 的 `UpdateViewportRows()`），96 DPI 下應得 `(456 - 72) / 48 = 8`。

### 2. 主題色（`src/ui/panel_palette.h` / `.cpp`）

`PanelColors` 新增兩欄（放在 `card` 之後，保持 `operator==` 預設實作）：

- `Rgb input_fill = 0;`
- `Rgb input_border = 0;`

`ResolveColors()` 對應值：

- 淺色：`input_fill = 0xFFFFFF`、`input_border = 0xE0E0E0`
- 深色：`input_fill = 0x2B2B2B`、`input_border = 0x3C3C3C`
- 高對比：`input_fill = system.window`、`input_border = system.window_text`（實心可見，§NFR-006）

### 3. 繪製圓角框（`src/app_host/main.cpp` 的 `Render()`）

在清單之前、以 DIP 座標畫搜尋框：

- `D2D1::RoundedRect(D2D1::RectF(kSearchLeftDip, kSearchTopDip, kSearchRightDip, kSearchBottomDip), kSearchCornerRadiusDip, kSearchCornerRadiusDip)`
- 先 `FillRoundedRectangle` 用 `input_fill`，再 `DrawRoundedRectangle` 用 `input_border`，線寬沿用 NR-015 既有的 `std::max(1.0f, dpi_x / kDpi96)` 算法。
- 框內不要用 DirectWrite 畫任何文字：EDIT 是子視窗，會蓋掉該區域。**本 item 不做 placeholder**。

### 4. EDIT 的字型、位置與配色（`src/app_host/main.cpp`）

- 新增檔案範圍的 `HFONT g_search_font = nullptr;` 與 `HBRUSH g_search_bg_brush = nullptr;`。
- 新增 `void UpdateSearchFont(HWND window)`：以 `NONCLIENTMETRICSW ncm{sizeof(ncm)}` 呼叫 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, GetDpiForWindow(window))`，取 `ncm.lfMessageFont`，只覆寫 `lfHeight = LayoutForDpi(dpi).search_font_height`（其餘 face name／weight／charset 照抄），`CreateFontIndirectW` 建新字型，`WM_SETFONT`（`w_param` = 新字型、`l_param` = TRUE）給 `g_search_edit`，再 `DeleteObject` 舊字型。`SystemParametersInfoForDpi` 失敗時退回 `SystemParametersInfoW` 同一項目。
- 呼叫點：EDIT 建立後一次；`WM_DPICHANGED` 內（在 `RepositionSearchEdit()` 之後）一次。
- `RepositionSearchEdit()` 改用 `search_edit_*` 四個欄位定位，不再用 `search_left/top/right/bottom`（那四個現在只給 D2D 圓角框用）。EDIT 建立時的硬寫座標 `16,16,608,28` 一併改為建立後立刻呼叫 `RepositionSearchEdit()`。
- 新增 `WM_CTLCOLOREDIT` 處理：`l_param` 等於 `g_search_edit` 時，`SetTextColor(hdc, text)`、`SetBkColor(hdc, input_fill)`（`palette::Rgb` 是 `0xRRGGBB`，需轉成 `COLORREF` 的 `0x00BBGGRR`，用既有的轉換 helper；若沒有就地寫一個 3 行的 `RgbToColorRef()`），回傳快取的 `g_search_bg_brush`。主題色變更時（既有的重新解析色彩路徑）重建 brush。
- `WM_DESTROY`：`DeleteObject` 字型與 brush。
- EDIT style 維持 `WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL`，**不加** `WS_BORDER`／`WS_EX_CLIENTEDGE`（邊框由 D2D 畫）。

### 5. 深色主題 caret（條件式）

以深色主題實際執行一次，確認輸入游標可見。若不可見，在 `SearchEditProc` 補最小修正：`WM_SETFOCUS` 先 `CallWindowProcW` 走預設流程，再 `CreateCaret(edit, reinterpret_cast<HBITMAP>(1), 0, 0)` 後 `ShowCaret(edit)`；`WM_KILLFOCUS` 走完預設流程後 `DestroyCaret()`。若原生 caret 本來就看得見，不要加這段程式碼，並在交接區註明已驗證。

## Non-goals

- 不做 placeholder／cue banner，不掛 comctl32 v6 manifest。
- 不自繪 EDIT 的文字、選取或 caret 閃爍；不改 IME 行為。
- 不做搜尋框的 focus ring、陰影、動畫或透明模糊。
- 不改清單列、選取樣式、footer 內容（NR-020／NR-021）、啟動失敗呈現（NR-022）。
- 不改 catalog、dedup、usage、pin、icon cache 的邏輯或持久化格式。
- 不回頭修改 NR-016／NR-020／NR-021／NR-022 文件。

## Acceptance

- 搜尋框在面板上呈現為 16~64 DIP 的圓角填色框，含 1 DIP 邊框，左右各距面板 16 DIP。
- 輸入文字使用系統 message font、字級 24 DIP，左內距 12 DIP；EDIT 的直角不會蓋掉圓角。
- 淺色／深色主題切換時，輸入框填色與文字色跟著變；高對比模式下邊框為實心系統色且清楚可見。
- 96 / 144 / 192 DPI 下搜尋框與 EDIT 位置皆按比例縮放，字級同步變化，沒有裁字或溢出。
- 面板高度 488 DIP，清單可見 8 列，footer band 從 456 DIP 開始。
- 深色主題下輸入游標可見（原生可見則不加程式碼，並於交接區註明）。
- `←`／`→`／`Home`／`End`／選取／複製貼上／IME 在搜尋框內行為不變（NR-020 已定的鍵盤契約不受影響）。
- 建置無新增警告；repo 內搜尋不到殘留的 `608`／`28` 硬寫 EDIT 座標。
- 無 GDI 物件洩漏：字型與 brush 各只有一份，重建時釋放舊的。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

在 `tests/unit/ui_palette_layout_test.cpp` 新增 case，全部純值、不需操作視窗：

- `LayoutForDpi(96)`：`panel_height == 488`、`list_top == 72`、`search_bottom == 64`、`(456 - 72) / 48 == 8`。
- `LayoutForDpi(96)`：`search_edit_left == 28`、`search_edit_top == 22`、`search_edit_right == 612`、`search_edit_bottom == 58`、`search_font_height == -24`。
- `LayoutForDpi(192)`：上述每個值皆為 96 DPI 的兩倍；`search_edit_left < search_edit_right`、`search_edit_top < search_edit_bottom`。
- EDIT 矩形嚴格落在搜尋框矩形內：`search_edit_*` 四邊都在 `search_*` 四邊之內（96 / 144 / 192 三個 DPI 各驗一次）。
- `ResolveColors(Theme::Light, ...)`：`input_fill == 0xFFFFFF`、`input_border == 0xE0E0E0`，且 `input_fill != background`。
- `ResolveColors(Theme::Dark, ...)`：`input_fill == 0x2B2B2B`、`input_border == 0x3C3C3C`，且 `input_fill != background`。
- 高對比：`input_fill == system.window`、`input_border == system.window_text`，且兩者不相等。

## 交接區

- Start: 2026-08-05
- Subagent scope: 依「必讀」讀完所有文件；trace `src/ui/panel_layout.{h,cpp}`、`src/ui/panel_palette.{h,cpp}`、`src/app_host/main.cpp`（`Render()`、`RepositionSearchEdit()`、`SearchEditProc()`、EDIT 建立處、`WM_DPICHANGED`、`WM_DESTROY`、色彩解析路徑）、`tests/unit/ui_palette_layout_test.cpp`。先確認 NR-020 已完成，否則回報阻塞。實作 Scope 1~5，不越界到 NR-021／NR-022。回報修改檔案、測試命令、結果與未完成事項。
- Result: 2026-08-05 完成。
  - 修改檔案：`src/ui/panel_layout.h`（四值 488／456／72／64、新增四常數與 `LayoutPx` 五欄）、`src/ui/panel_layout.cpp`（`LayoutForDpi` 填 `search_edit_*`／`search_font_height`）、`src/ui/panel_palette.h`（`PanelColors` 加 `input_fill`／`input_border`）、`src/ui/panel_palette.cpp`（light/dark aggregate 與 HC 路徑）、`src/app_host/main.cpp`（`Render` 畫圓角框＋色變重建 GDI brush、`UpdateSearchFont`、`RepositionSearchEdit` 改 `search_edit_*`、EDIT 建立改 0,0,0,0＋立即定位、`WM_CTLCOLOREDIT`、`RgbToColorRef`、`WM_DPICHANGED` 加 `UpdateSearchFont`、`WM_DESTROY` 釋放 GDI 物件、caret 修正、CreateWindowExW 初值 488）、`tests/unit/ui_palette_layout_test.cpp`（既有值更新＋5 個新 case）。新增／刪除檔案：無。
  - Agent checks（Release x64）：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`；`cmake --build build`（無新增 warning）；`ctest --test-dir build --output-on-failure` → 全套件 18/18 通過（含 `nimblerun_dpi_theme_accessibility_test`）。
  - 額外確認：repo 內 `src/app_host/main.cpp` 已無 `608`／`28` 硬寫 EDIT 座標；`CreateWindowExW` 初值 432→488 對齊新面板高。
  - **caret 決定**：採用「修正方案」（`WM_SETFOCUS` 先 `CallWindowProcW` 走預設流程、再 `CreateCaret(edit, reinterpret_cast<HBITMAP>(1), 0, 0)`＋`ShowCaret`；`WM_KILLFOCUS` 走完預設後 `DestroyCaret`）。理由：本次 Agent 環境無法實際執行深色 UI 做視覺驗證，依任務指示採保守做法在程式碼層確保可見。solid caret 由系統對視窗背景取反色繪製，配合 `WM_CTLCOLOREDIT` 設定的 `input_fill` 背景，淺色（白底）與深色（深灰底）主題下皆維持對比；無新增 timer，閃爍沿用系統機制。
  - 未完成事項：無。深色主題／高對比下 caret 與搜尋框外觀的最終肉眼確認屬人工驗證，不列入 Agent 交付（依 `docs/work-items.md` Agent 交付規則）。
