# NR-082 — CellAtPoint 在面板高度被 clamp 時命中未繪製的列

Phase 3 · Depends on: NR-064

- Source: `docs/design-spec.md` §4.8（「滑鼠命中僅限實際繪製的可見格／列」）／§4.9（面板高度上限）
- Origin: 2026-08-08 第五次全 repo 稽核（CellAtPoint 與「高度被 clamp 後的繪製範圍」比對）

## Why

`CellAtPoint`（`src/app_host/main.cpp:590-638`）在 NR-064 之後的上界只有
`y >= footer_top`（`main.cpp:603-607`），沒有「列號必須落在實際繪製的
viewport 內」的下界。NR-064 的假設是「grid 4 列與 list 8 列都止於
`kFooterTopDip = 456`」（`src/ui/panel_layout.h:19`）——這只在**面板高度等於
488 DIP** 時成立。當面板高度被 `ClampWindowSize`（`src/ui/panel_layout.cpp:37-43`）
依工作區縮短時，最後一列會提早結束，`footer_top` 以下的「幾何空白帶」落在
**未繪製列的範圍**，而 `CellAtPoint` 照樣回傳有效 index → 單擊／右鍵／hover／
拖曳 gap 全部命中看不見的列。

具體觸發：小螢幕＋高 DPI。例如 1280×720、150% DPI（`ClampWindowSize` 把
`min(488×1.5, work-32)` 夾到 658px）→ client 高 656px = 437 DIP →
`UpdateViewportRows` 的 grid `viewport_rows = floor((437-72)/96) = 3`，只畫
3 列（72..360 DIP）；client 到 437 DIP 都還收得到 click，而 `footer_top`
(456 DIP = 684px) 在 client 之外**永遠達不到**。`y ∈ [360, 437) DIP` 時
`(y - 72)/96 = 3` → index = `first + 18..23`，`rows.size() > 24` 時回傳
有效 index → `WM_LBUTTONDOWN`（`main.cpp:2782-2812`）直接 `SelectRow`＋
`ActivateRow` 啟動一個**螢幕上看不到的 App**。list 模式同形：只畫
`floor((437-72)/48) = 7` 列，第 8 列位置照樣命中。

這正是 NR-064 修的那類 bug 的殘留：NR-064 明載決策「不寫第二套『可見列數』
判斷，幾何界限一個就夠」（`NR-064.md` Decisions §1、`§Scope` 注意段），該決策的
前提是「兩種版面都止於 456 DIP」；本 item 以**新證據**覆寫該前提：面板高度被
clamp 時繪製範圍止於 `list_top + viewport_rows × row_height`，不是 footer。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **修在 `CellAtPoint` 唯一入口**，加一個 viewport 下界（grid：
   `row >= ViewportRows()` 即 miss；list：`(y - list_top) / row_height >=
   ViewportRows()` 即 miss）。這**覆寫** NR-064 Decisions §1「幾何界限一個就夠」
   的決策——新證據是 clamp 後繪製範圍不再止於 footer，幾何上界不足，需要
   `ViewportRows()` 當第二道界限。所有消費端（left click、right click、hover、
   拖曳 gap）自動修正，Render 一字不改。
2. **用 model 的 `ViewportRows()`，不自己重算**：`UpdateViewportRows` 已把
   client rect＋DPI 折算成 model 的 `viewport_rows_`，它就是「實際畫了幾列」
   的唯一事實來源。`CellAtPoint` 吃 `g_model`，直接讀。
3. **grid 的檢查放在 row 計算之後**（與既有 `col < 0`、`col >= columns` 同層），
   list 的檢查放在 index 計算處。不改任何版面常數、不新增 `LayoutPx` 欄位
   （沿用 NR-064 的「hit-test 獨有界限不進 `LayoutPx`」原則）。
4. **不加單元測試**：`CellAtPoint` 吃 `g_model`＋DPI＋HWND，repo 既定政策是
   「不為了製造測試點而發明抽象」（NR-060 Acceptance、NR-064 Decisions §4 先例）。
   行為由手動驗收＋sanity grep 覆蓋（與 NR-064 同標準）。
5. **不把 `y >= footer_top` 檢查刪掉**：它仍擋住「完整高度時 footer band 上」
   的命中；viewport 下界是對 clamp 情況的補強，兩者並存。

## Binding constraints — quoted, do not go looking for them

design-spec §4.8：

> - 滑鼠命中僅限實際繪製的可見格／列；footer 與邊緣空白按左鍵是拖曳面板（§4.8），不啟動任何項目。
> - 格狀狀態下指標停在某格時，該格顯示淡填色並在 footer 顯示其路徑；不改變鍵盤選取。

design-spec §4.9：

> - 預設寬度 640 DIP、高度 488 DIP；高度依內容調整，上限為目前螢幕工作區的 70%。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:590-638` — `CellAtPoint`。**本 item 只改這一個函式**
  （grid 分支在 `:622-628`，list 分支在 `:632-637`）。
- `src/app_host/main.cpp:679-693` — `UpdateViewportRows`：viewport 的唯一計算點，
  證明 `g_model->ViewportRows()` 就是「實際畫了幾列」。
- `src/ui/panel_layout.cpp:37-43` — `ClampWindowSize`：高度被工作區夾短的條件。
- `src/ui/panel_layout.h:19` — `kFooterTopDip = 456.0f`（NR-064 的幾何上界）。
- `src/app_host/main.cpp:2782-2812`（`WM_LBUTTONDOWN`）、`:2873-2906`
  （`WM_RBUTTONDOWN`）、`:2755-2770`（hover）、`:2739-2740`（拖曳 gap）—
  全部只消費 `CellAtPoint` 回傳值，**不改**。
- `docs/work-items/NR-064-hittest-visible-rows-only.md` — 本 item 覆寫其
  Decisions §1 的「幾何界限一個就夠」；其餘決策（修在唯一入口、呼叫端不改、
  不加測試抽象）沿用。

## Scope

### 1. 在 `CellAtPoint` 加 viewport 下界

grid 分支（`main.cpp:622-628` 一帶）：

```cpp
const int col = (x - grid_left) / cell_width;
const int row = (y - layout.list_top) / cell_height;
// NR-082: 面板高度被 ClampWindowSize 夾短時（小螢幕＋高 DPI），最後一列會提早
// 結束，幾何上界 footer_top 之後到 client 底邊的空白帶會命中未繪製的列（§4.8
// 「命中僅限實際繪製的可見格／列」）。ViewportRows() 是 UpdateViewportRows 依
// 實際 client rect 折算的「畫了幾列」事實來源。
if (col < 0 || col >= columns || row < 0 || row >= g_model->ViewportRows()) {
    return -1;
}
```

list 分支（`main.cpp:632-637` 一帶）：

```cpp
const int row_index = (y - layout.list_top) / layout.row_height;
if (row_index >= g_model->ViewportRows()) {
    return -1;
}
const int index = row_index + g_model->FirstVisibleRow();
return index >= 0 && index < static_cast<int>(g_model->Rows().size()) ? index : -1;
```

既有 `y >= footer_top`、`x < grid_left`、`x >= layout.list_right` 檢查**原樣保留**。
Render 一字不改（繪製範圍是對的，錯的是 hit-test 少了 viewport 下界）。

### 2. 更新 spec？

design-spec §4.8 已含「滑鼠命中僅限實際繪製的可見格／列」（NR-064 補入）。本 item
是讓該句名副其實的殘留補強，不另加條文。

## Non-goals

- **不改 `Render()`**——繪製範圍是對的。
- **不改任何呼叫端的分派邏輯**（拖曳、hover、啟動、右鍵選單）。
- **不加測試抽象**（Decisions §4）。
- **不處理「高度被 clamp 時 footer 框被裁掉」的視覺問題**：那是
  `ClampWindowSize` 與 footer 幾何的獨立問題，屬版面設計決策，非本 item 範圍。
- **不刪既有三條幾何檢查**（Decisions §5）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，本 item 不新增測試）。

Manual（Release build，逐條打勾）：

1. 在一個工作區高度不足以容納完整面板的螢幕（例如 1280×720、150% DPI），
   空白 query 且 catalog 超過 24 格，點**最後一繪製列下方到 footer 之間的
   空白帶**：不啟動任何 App、不觸發 hover 高亮。
2. 同環境輸入超過 8 筆結果的關鍵字，點**最後一繪製列下方**的空白帶：
   不啟動。
3. 完整高度（例如 1080p、100% DPI）：點 footer band、點 grid 左緣空白、
   點 list 右緣空白——與 NR-064 之後的行為一致，不啟動。
4. 正常項目上左鍵單擊仍啟動、右鍵仍開項目選單、hover 仍高亮、釘選格拖曳仍可
   重排（回歸）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# viewport 下界只存在於 CellAtPoint：
Select-String -Path src/app_host/main.cpp -Pattern 'ViewportRows\(\)'
# expect: 既有用法 + CellAtPoint 內新增（grid 1 處、list 1 處）

# 既有三條幾何檢查仍在：
Select-String -Path src/app_host/main.cpp -Pattern 'footer_top|x < grid_left|list_right'
# expect: 原樣存在

git diff --name-only
# expect: 僅 src/app_host/main.cpp（及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

（實作者填寫：改動位置、實際插入的兩段程式碼、建置與 CTest 結果、4 條手動驗收
結果、sanity greps、偏差、未完成事項。）

- 改動位置：`src/app_host/main.cpp:590-651` 的 `CellAtPoint`。
- grid 分支（`:622-633`）：`if (col < 0 || col >= columns || row < 0 ||
  row >= g_model->ViewportRows()) return -1;`——在既有 col/row 檢查上追加
  `row >= ViewportRows()`，NR-082 註解置於檢查前。
- list 分支（`:642-650`）：新增 `const int row_index = (y - layout.list_top) /
  layout.row_height;`，`row_index >= ViewportRows()` 即 miss，再以 `row_index`
  算 index（原內聯式改為具名變數）。
- 既有三條幾何檢查（`y >= footer_top`、`x < grid_left`、`x >= layout.list_right`）
  原樣保留；`Render()` 與所有呼叫端一字未改。
- 建置與 CTest：Release 建置無新增警告；`ctest --test-dir build --output-on-failure`
  **23/23 全綠**（lifecycle_check 暖機 1.83s 通過）。
- 手動驗收：本工作區不操作視窗，4 條手動驗收（clamp 螢幕下點未繪製列不啟動、
  完整高度回歸）未實跑；由既有 hit-test 消費端（WM_LBUTTONDOWN／RBTN／hover／
  拖曳）讀碼確認行為，並依 NR-064 先例以 sanity grep 守門。
- sanity greps：`ViewportRows()` 於 `main.cpp` 新增 CellAtPoint 內 grid＋list
  兩處（既有使用不變）；`footer_top`／`x < grid_left`／`list_right` 皆原樣在
  CellAtPoint 內；`git diff --name-only` 只含 `src/app_host/main.cpp`。
- 偏差：無（實作與 item Scope §1 一致）。
- 未完成：4 條手動驗收屬人工操作，不在 Agent 範圍。
- Commit：`62bd14a`（fix），status 翻 `done` 另 commit。

