# NR-064 — Hit-testing must not select invisible rows: footer / margins launch nothing

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §4.8（滑鼠操作：單擊啟動可見列）／§4.9（footer band）
- Origin: 2026-08-07 第三次全 repo 稽核（main.cpp hit-test 與 Render 幾何比對）

## Why

`CellAtPoint`（`src/app_host/main.cpp:542-573`）的命中範圍**大於** Render 實際繪製
的範圍，三處可證明的錯誤命中：

1. **footer band（兩種版面）**：`CellAtPoint` 只檢查 `y < layout.list_top`，沒有
   `y >= footer_top` 的下界。list 模式 `(y - list_top) / row_height + FirstVisibleRow()`
   在 y∈[456,488) 算出 `first + 8`——第 9 筆結果，**未繪製**；搜尋結果超過 8 筆時
   幾乎必然命中（`main.cpp:2471-2504` 的 `WM_LBUTTONDOWN` → `SelectRow` +
   `ActivateRow` 直接啟動）。grid 模式 y∈[456,488) 算出 `first + 4*columns + col`，
   `Rows()` 超過一頁（釘選＋常用超過 24）時同樣啟動看不見的 cell。
2. **list 右緣**：`x < layout.list_left` 有檢查，`x >= list_right` 沒有；點視窗右緣
   空白帶照樣命中該列。
3. **grid 左緣**：`(x - grid_left) / cell_width` 對 `x < grid_left` 因 C++ 整數除法
   向零取整得到 `col = 0`（例如 x=10、grid_left=17 → -7/112 = 0），`col < 0` 檢查
   擋不住 → 點左邊 17 DIP 空白帶命中第 0 欄。

同一個函式還供 `WM_RBUTTONDOWN`（項目選單）、grid hover、NR-046 拖曳 gap 使用，
所以右鍵也會對錯誤的列開出 Pin/Unpin 選單，hover 高亮也會出現在看不見的格上。

後果是「點一下 footer／邊緣空白，啟動了一個使用者看不到的 App」——這比「點錯列」
更糟，因為使用者完全不知道啟動了什麼（可能是有副作用的程式）。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **修在 `CellAtPoint` 這個唯一入口**，不修各呼叫端。所有消費端（left click、
   right click、hover、drag gap）自動修正；Render 一字不改（幾何是對的，錯的是
   hit-test 的界限）。
2. **三個檢查都是界限補齊，不是重寫**：`y >= footer_top_px → -1`（兩種版面共用，
   footer band 從 `kFooterTopDip` 開始）；grid 除法前先擋 `x < grid_left_px`；
   list 補 `x >= list_right_px`。用既有的 `LayoutPx` 欄位與 `panel_layout.h` 常數，
   不新增版面常數。
3. **不加「點 footer 也不拖曳視窗」的語意變化**：`cell < 0` 的既有行為是
   「視窗可拖曳」（NR-039），空白處點擊維持可拖曳——本 item 只讓它不再**啟動**。
4. **不加單元測試**：`CellAtPoint` 吃 `g_model`＋DPI＋HWND，且 repo 既定政策是
   「不為了製造測試點而發明抽象」（NR-060 Acceptance 明載）。行為由手動驗收覆蓋。
5. **不另畫 footer 的 hover 狀態**：footer 不是項目，hover 高亮在 footer 上出現
   是錯誤命中的一部分，修好 hit-test 後自然消失。

## Binding constraints — quoted, do not go looking for them

design-spec §4.8：

> - 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、自常用清單移除、內容（交由 Shell 的 properties verb 顯示）。
> - 點擊面板外，面板自動隱藏。

design-spec §4.8（單擊啟動）：

> - 單擊啟動選取列；失敗時面板保持顯示。

design-spec §4.9：

> - 面板最下方為固定高度提示帶（footer）。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:542-573` — `CellAtPoint`。**本 item 只改這一個函式。**
  對照 `Render()` 的繪製範圍（grid 迴圈與 list 迴圈的上限）與 `panel_layout.h`
  的 `kFooterTopDip`／`kGridLeftDip`／list 左右界常數。
- `src/ui/panel_layout.h` — `LayoutPx` 有 `list_left`／`list_top`／`list_right` 欄位，
  但**沒有** `footer_top`／`grid_left` 欄位（後兩者由 hit-test 自行從
  `kFooterTopDip × scale`／`kGridLeftDip × scale` 計算，`CellAtPoint` 現行對
  `grid_left` 就是這樣算的）。沿用既有模式，不要新增欄位。
- `src/app_host/main.cpp:2471-2505`（`WM_LBUTTONDOWN`）、`:2506-2530`（`WM_LBUTTONUP`
  拖曳提交）、`:2411-2462`（`WM_MOUSEMOVE` hover）、右鍵分支 — 全部只消費
  `CellAtPoint` 的回傳值，**不改**。
- `grep CellAtPoint` — 確認呼叫點清單與「-1 = 沒命中」的語意。

## Scope

### 1. 補齊 `CellAtPoint` 的三個界限

```cpp
int CellAtPoint(HWND window, int x, int y) {
    if (!g_model) {
        return -1;
    }
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    if (y < layout.list_top || y >= layout.footer_top) {   // 上界＋footer 下界
        return -1;
    }
    const int columns = g_model->Columns();
    if (columns > 1) {
        const int grid_left = ...;                         // 既有計算
        if (x < grid_left) {                               // 除法前的左界
            return -1;
        }
        const int col = (x - grid_left) / cell_width;
        ... 其餘原樣 ...
    }
    if (x < layout.list_left || x >= layout.list_right) {  // 右界補上
        return -1;
    }
    ... 其餘原樣 ...
}
```

（以現場程式碼與 `LayoutPx` 的實際欄位為準。**注意 `LayoutPx` 沒有 `footer_top`
欄位**——footer 上界照 `CellAtPoint` 現行對 `grid_left` 的算法算：
`static_cast<int>(std::lround(kFooterTopDip * layout.scale))`（`panel_layout.h:19`
的 `kFooterTopDip = 456.0f`）。不要新增 `LayoutPx` 欄位：這是 hit-test 獨有的
界限，不屬於版面幾何。）

**注意**：`y >= footer_top` 同時涵蓋 grid 與 list 兩者的 footer band——grid 4 列
（72+4×96=456）與 list 8 列（72+8×48=456）都止於 `kFooterTopDip=456`。不要寫
第二套「可見列數」判斷，幾何界限一個就夠。

### 2. 更新 spec？

design-spec §4.8 已寫「單擊啟動選取列」。本 item 是修正「列」的判定使「可見列」
名副其實。在 §4.8 補一句（選用，兩句以內）：

> 滑鼠命中僅限實際繪製的可見格／列；footer 與邊緣空白按左鍵是拖曳面板（§4.8），
> 不啟動任何項目。

## How this stays maintainable

**命中幾何與繪製幾何共享同一組 `LayoutPx` 常數與同一個函式。** 日後版面常數再動
（例如 NR-021 調過 footer 高度），兩邊自動同步，不會再出現「繪製改了一格、
hit-test 忘了跟上」的靜默錯位——本 item 的三個 bug 都是這種錯位的歷史案例。

## Non-goals

- **不改 `Render()`**——繪製範圍是對的。
- **不改任何呼叫端的分派邏輯**（拖曳、hover、啟動、右鍵選單）。
- **不加測試抽象**（Decisions §4）。
- **不處理「點 footer 拖曳視窗」的行為**（NR-039 既定語意，保持）。
- **不修 grid 左緣以外的負座標**：`col < 0` 檢查保留作為 `x < grid_left` 之後的
  第二道防線，不刪。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，本 item 不新增測試）。

Manual（Release build，逐條打勾）：

1. 搜尋一個超過 8 筆結果的關鍵字，在 footer band（`Scroll PgUp PgDn` 那條）按
   左鍵：**不啟動任何東西**，面板可以被拖動。
2. 空白 query 且釘選＋常用超過 24 格（或使用 NR-061 前的填充行爲，以現況為準），
   在 grid footer 按左鍵：不啟動、可拖曳。
3. list 模式點視窗最右緣（文字截斷區右方的空白帶）：不啟動該列。
4. grid 模式點最左邊 17 DIP 空白帶：不啟動第 0 欄、hover 高亮不出現。
5. 上述四處按**右鍵**：不出現項目選單（Pin/Unpin 等）；footer 右鍵維持 NR-060
   的面板選單（若已實作）。
6. 正常項目上左鍵單擊仍啟動、右鍵仍開項目選單、拖曳釘選格仍可重排（回歸）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# footer 下界存在（兩種版面共用一個檢查）：
Select-String -Path src/app_host/main.cpp -Pattern 'footer_top'
# expect: CellAtPoint 內 1 處（加 Render 既有用法，總數與實作前一致＋1）

# grid 除法前有左界：
Select-String -Path src/app_host/main.cpp -Pattern 'x < grid_left'
# expect: CellAtPoint 內 1 處

# list 右界存在：
Select-String -Path src/app_host/main.cpp -Pattern 'list_right'
# expect: 既有用法＋CellAtPoint 內新增

# 呼叫端未被動到：
git diff --name-only
# expect: 僅 src/app_host/main.cpp（及選用的 docs/design-spec.md）
```

## 交接區

（實作者填寫：修改的位置、`LayoutPx` 實際欄位名（`footer_top` 是否存在）、
建置與 CTest 結果、6 條手動驗收結果、sanity greps、偏差、未完成事項。）
