# NR-024 — Alt+digit quick select with per-row key hints

- Status: `ready`
- Phase: 3
- Depends on: NR-020、NR-021
- Source: `docs/design-spec.md` §4.7、§4.9、§NFR-006、AC-002

## Goal

讓使用者用 `Alt` ＋數字直接啟動當前可見的任一清單列，不必用 `↑`／`↓` 走位。鍵序固定為 `1 2 3 4 5 6 7 8 9 0`，依序指派給當前可見列（可見 8 列時用到 `Alt+1`~`Alt+8`）。每個清單列最右方畫一個圓角按鍵方塊顯示該列的數字，修飾鍵 `Alt` 只在 footer 說明一次。

## 必讀

實作前必須讀完：`AGENTS.md`、`docs/development.md`（Product boundary／Architecture rules／UI language／Build configuration／Change workflow 全部五節）、`docs/design-spec.md` §4.2／§4.7／§4.8／§4.9／§NFR-006、`docs/work-items.md`（使用方式與 Agent 交付規則）、`docs/work-items/NR-020-list-panel-restore.md`、`docs/work-items/NR-021-paged-navigation-footer.md`、本文件。

## 與既有 item 的關係（重要）

- **本 item 覆寫 NR-021 的「footer 是固定內容、不隨狀態變動」**：footer 右側在 `Scroll` 組左邊新增一組 `Launch` ＋ `Alt+1~<最後一個數字>` 指引，該字串隨當前可見列數組出。其餘 footer 規則（只放按鍵指引、不放狀態／版本／更新文字）不變。
- **不回頭修改** `NR-020`／`NR-021`／`NR-023` 文件；覆寫指示只寫在本文件。
- 若 NR-023 尚未實作，本 item 仍可實作：兩者只在 `panel_layout` 加常數、在 `Render()` 不同區塊繪製，沒有衝突。列數由 NR-020 的 `UpdateViewportRows()` 實算，本 item 不寫死 8。

## 來自 spec 與開發指南的硬約束

- 產品行為以 `docs/design-spec.md` 為準；本文件若與 spec 衝突，以 spec 為準並回報。
- App UI 文字一律英文；新字串加入既有的集中式字串表（`footer_strings`／`list_strings`），不散落於 render 迴圈。
- 核心邏輯不得依賴 HWND 或 Shell COM：「數字鍵 → 第幾個可見列」與「第幾個可見列 → 絕對列索引」必須是純值運算，可在不建視窗的情況下測。
- 顏色一律取自 `src/ui/panel_palette.h` 的 `PanelColors`（沿用 `card`／`dim`／`text`），不新增色欄、不寫死色碼。
- 不新增第三方依賴、網路存取、遙測、服務、driver 或管理員權限；不新增 timer（不得為了偵測 `Alt` 是否按住而輪詢按鍵狀態）。
- 不安裝低階鍵盤 hook，也不註冊任何新的全域 hotkey：`Alt`＋數字只在面板已顯示、焦點在搜尋 EDIT 時處理（§FR-002 的邊界）。
- 最小可行改動與重用（ponytail）：列內按鍵方塊與 footer 按鍵方塊必須共用同一個繪製函式，不得複製第二份。

## Scope

### 1. 純值鍵序對應（新檔 `src/ui/quick_select.h`，header-only）

放進既有 `nimblerun_ui` 庫的 include 路徑，**不得** `#include <windows.h>`。

- `inline constexpr wchar_t kQuickSelectDigits[] = L"1234567890";`（10 個字元，順序即 slot 順序）
- `inline constexpr int kQuickSelectSlotCount = 10;`
- `int QuickSelectSlotForKey(int key_code)`：`key_code` 為 ASCII／VK 共通的 `'1'`~`'9'`、`'0'`（主鍵區數字的 VK 值與其 ASCII 相同，故不需 Win32 標頭）。回傳 0-based slot（`'1'`→0 … `'9'`→8、`'0'`→9），其餘回傳 `-1`。
- `const wchar_t* QuickSelectLabelForSlot(int slot)`：回傳長度 1 的靜態字串（`L"1"` … `L"0"`），slot 越界回傳 `nullptr`。實作可用 10 個 `constexpr` 單字元字串陣列，避免回傳指向暫存物件的指標。

### 2. 純值列對應（`src/app_host/panel_model.{h,cpp}`）

新增：

```cpp
// Absolute row index for the slot-th visible row (0-based slot), or -1 when
// the slot is outside the current viewport or past the end of the list.
int RowForVisibleSlot(int slot) const;
```

語意：`slot < 0 || slot >= viewport_rows_` → `-1`；`first_visible_ + slot >= RowCount()` → `-1`；否則 `first_visible_ + slot`。不改變任何狀態（`const`）。翻頁後 slot 0 永遠是新的第一可見列。

### 3. 輸入處理（`src/app_host/main.cpp` 的 `SearchEditProc`）

`Alt`＋按鍵在子類化 EDIT 上到達的是 `WM_SYSKEYDOWN`，且 `TranslateMessage` 會另外產生 `WM_SYSCHAR`（未吞掉會發出系統嗶聲）。兩者都要處理：

- `case WM_SYSKEYDOWN:`
  - 條件：`(l_param & (1 << 29)) != 0`（Alt 按住）、`GetKeyState(VK_CONTROL) >= 0`（Ctrl 未按）、`g_model != nullptr`。
  - `const int slot = nimblerun::ui::QuickSelectSlotForKey(static_cast<int>(w_param));`
  - `slot < 0` → 不處理，落到 `CallWindowProcW`（`Alt+Space` 等系統行為不受影響）。
  - `slot >= 0`：`const int row = g_model->RowForVisibleSlot(slot);`
    - `row >= 0`：`g_model->SelectRow(static_cast<std::size_t>(row));` 後呼叫既有的 `ActivateRow(row, GetParent(edit))`（沿用 NR-020／NR-018／NR-022 既有的 usage 更新、hide-after-launch、失敗對話框與 refresh 路徑，不另寫一條啟動流程）。
    - `row < 0`：不啟動、不改選取，但**仍 `return 0`**（該數字已綁定，不應發出嗶聲）。
  - 處理完 `return 0;`
- `case WM_SYSCHAR:`：`(l_param & (1 << 29))` 且 `QuickSelectSlotForKey(w_param) >= 0` 時 `return 0;`（只吞掉這 10 個字元的嗶聲），其餘落到預設。

### 4. 列內按鍵方塊（`src/ui/panel_layout.h` 與 `main.cpp` 的 `Render()`）

`panel_layout.h` 新增：

- `constexpr float kRowKeyBoxWidthDip = 20.0f;`（只放單一數字，比 footer 的 44 窄）
- `constexpr float kRowKeyRightInsetDip = 8.0f;`  // 方塊右緣距 `kListRightDip`
- `constexpr float kRowKeyGapDip = 8.0f;`  // 方塊左緣與列文字右緣的間距
- `constexpr float kRowHintReserveDip = kRowKeyBoxWidthDip + kRowKeyRightInsetDip + kRowKeyGapDip;`（= 36）

`Render()` 的列迴圈：

- 名稱與第二行的 `text_right` 由 `kListRightDip - 8.0f` 改為 `kListRightDip - kRowHintReserveDip`。**無條件保留**這塊寬度（即使某列沒有指引），文字寬度才不會跳動（§4.9）。既有的 `NO_WRAP` ＋尾端省略號設定不變。
- 對第 `i` 列（`slot = i - first`）取 `QuickSelectLabelForSlot(slot)`；非 `nullptr` 時在列的垂直中線畫一個高度 `kFooterKeyBoxHeightDip`、寬 `kRowKeyBoxWidthDip`、圓角 `kFooterKeyRadiusDip` 的方塊，右緣位於 `kListRightDip - kRowKeyRightInsetDip`。
- `nullptr`（viewport 超過 10 列的情形）時不畫，也不改文字寬度。

### 5. 共用按鍵方塊繪製 + footer 新增一組指引（`main.cpp`）

- 把 NR-021 目前寫在 footer 區塊內的 `draw_key_box` lambda 抽成檔案範圍的靜態函式，簽名如 `void DrawKeyBox(const wchar_t* label, D2D1_RECT_F box_rect)`（內部沿用 `FillRoundedRectangle`＋`card`、`DrawRoundedRectangle`＋`dim`、`kSmallFontDip`＋`text`、`kFooterTextInsetDip` 的文字上內距）。footer 與列內共用它；footer 端保留原本「由右往左推進」的 `right` 計算。
- `footer_strings` 新增 `kLaunch = L"Launch"`。
- footer 指引順序（由右至左）：`Scroll` `PgUp` `PgDn` 組不動，其左方隔 `kFooterHintGapDip` 再放 `Launch` ＋一個寬方塊，內容為 `Alt+1~` 接上最後一個可用數字，即 `QuickSelectLabelForSlot(std::min(ViewportRows(), kQuickSelectSlotCount) - 1)`（可見 8 列 → `Alt+1~8`；可見 10 列以上 → `Alt+1~0`）。字串在 render 內以 `std::wstring` 組出，格式片語 `L"Alt+1~"` 放進 `footer_strings`。
- 新增 `constexpr float kFooterWideKeyBoxWidthDip = 56.0f;` 給這個較長的方塊用；`Launch` 標籤沿用 footer 既有的「量測一次後靠右對齊」寫法（`CreateTextLayout` ＋ `GetMetrics`），不要複製第三份量測程式碼——若能與 `Scroll` 共用一個 lambda 就共用。
- 清單為空（`rows.empty()`）時不畫任何列內方塊；footer 的 `Launch` 組固定顯示（與 `Scroll` 組一致，維持 band 內容穩定）。

## Non-goals

- 不支援數字鍵盤（`VK_NUMPAD0`~`VK_NUMPAD9`）；只綁主鍵區數字。
- 不做「按住 `Alt` 才顯示指引」（需追蹤按鍵狀態且會造成閃爍）；指引常駐。
- 不註冊新的全域 hotkey、不裝鍵盤 hook；面板未顯示時 `Alt`＋數字與 NimbleRun 無關。
- 不改 `↑`／`↓`／`PgUp`／`PgDn`／`Enter`／`Esc`／`Ctrl+R`／右鍵選單／單擊即啟動的既有行為。
- 不改 `PanelColors` 欄位、不改列高、不改 icon 幾何、不改面板尺寸。
- 不改 catalog、dedup、usage、pin、icon cache 的邏輯或持久化格式。
- 不做 `Alt`＋數字的設定項（不可自訂、不可關閉）。
- 不回頭修改 NR-016／NR-020／NR-021／NR-022／NR-023 文件。

## Acceptance

- 面板顯示且焦點在搜尋欄時，`Alt+1` 啟動當前第一可見列、`Alt+8` 啟動第八可見列，行為與對該列按 Enter 完全相同（usage 更新、依設定隱藏、失敗走 NR-022 對話框）。
- 翻頁（`PgUp`／`PgDn`／滾輪）後，`Alt+1` 對應的是**新的**第一可見列。
- 以 `↓` 把選取推出可見範圍時（NR-020 的最小位移捲動，可見範圍前進 1 列），指引標籤仍是 `1`~`8` 不變，但 `Alt+1` 對應新的第一可見列、`Alt+8` 對應新進來的最後一列；在最後一列 `↓` 環繞回頂端後同樣自動對位。指引數字永遠等於「該列在當前畫面上的位置」，不是絕對列號。
- 清單列數少於可見列數時，指向空位的數字不啟動任何項目、不改變選取，且不發出系統嗶聲。
- 清單為空時任何 `Alt`＋數字都無效果、無嗶聲。
- `Alt+Space`（全域快捷鍵，可能在面板已顯示時再次按下）與其他 `Alt` 組合行為不變；一般文字輸入、`←`／`→`／`Home`／`End`、IME、複製貼上皆不受影響。
- 每個可見列最右方有一個圓角方塊顯示該列數字（第一列 `1`…第八列 `8`），淺色／深色／高對比三種模式下方塊邊框與數字都清楚可見。
- App 名稱過長時在指引方塊左側就以省略號截斷，不會壓到方塊；有無指引時名稱可用寬度相同。
- footer 右側同時有 `Launch` `Alt+1~8` 與 `Scroll` `PgUp` `PgDn` 兩組，靠右對齊、彼此不重疊、不超出 `kListRightDip`。
- 96／144／192 DPI 下方塊與文字皆按比例縮放，無裁字或重疊。
- 列內與 footer 的按鍵方塊由同一個函式繪製（repo 內只有一份 `FillRoundedRectangle`＋`DrawRoundedRectangle` 的按鍵方塊程式碼）。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "list_vertical_slice|dpi_theme_accessibility" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/panel_model_test.cpp` 新增 case（純值，不需操作視窗）：

- `RowForVisibleSlot(0)` 等於 `FirstVisibleRow()`；`RowForVisibleSlot(viewport-1)` 等於最後一個可見列。
- `ScrollBy(+viewport)` 後 `RowForVisibleSlot(0)` 等於新的 `FirstVisibleRow()`。
- `slot < 0`、`slot == viewport`、`slot > viewport` 皆回傳 `-1`。
- 列數少於 viewport 時，超出 `RowCount()` 的 slot 回傳 `-1`，且呼叫後 `SelectionIndex()` 未改變。
- 空清單所有 slot 回傳 `-1`。
- `RowForVisibleSlot` 為 `const`：連續呼叫不改變 `FirstVisibleRow()`／選取。

`tests/unit/ui_palette_layout_test.cpp` 新增 case：

- `QuickSelectSlotForKey('1') == 0`、`('9') == 8`、`('0') == 9`；`('A')`、`(0)`、`(VK 以外的值)` 皆為 `-1`。
- `QuickSelectLabelForSlot(0)` 為 `L"1"`、`(8)` 為 `L"9"`、`(9)` 為 `L"0"`、`(10)` 與 `(-1)` 為 `nullptr`。
- `kQuickSelectDigits` 長度為 10 且無重複字元。
- `kRowHintReserveDip == kRowKeyBoxWidthDip + kRowKeyRightInsetDip + kRowKeyGapDip`，且 `kListLeftDip + kTileSizeDip + kRowHintReserveDip < kListRightDip`（名稱仍有正寬度可用）。

## 交接區

- Start: —
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/panel_model.{h,cpp}`（`FirstVisibleRow`／`ViewportRows`／`SelectRow`／`ScrollBy`）、`src/app_host/main.cpp`（`Render()` 列迴圈與 footer 區塊、`draw_key_box` lambda、`SearchEditProc`、`ActivateRow`）、`src/ui/panel_layout.h`、`src/ui/panel_palette.h`、`tests/unit/panel_model_test.cpp`、`tests/unit/ui_palette_layout_test.cpp`。先確認 NR-020／NR-021 已完成，否則回報阻塞。實作 Scope 1~5，不越界到 NR-022／NR-023。回報修改檔案、測試命令、結果與未完成事項。
- Result: —
