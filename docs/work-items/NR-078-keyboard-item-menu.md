# NR-078 — `Context Menu` key / `Shift+F10` must open the item menu for the selected row (spec §4.7, NFR-006)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §4.7（鍵盤表）／§NFR-006（鍵盤可完成全部核心操作）／§4.8（右鍵選單內容）
- Origin: 2026-08-08 第四次全 repo 稽核（main.cpp 全檔，spec 對照軸）

## Why

design-spec §4.7 鍵盤表明文：

| 按鍵 | 行為 |
|---|---|
| `Context Menu`／`Shift+F10` | 開啟項目選單 |

§NFR-006：「鍵盤可完成全部核心操作。」而 `main.cpp` 全檔**沒有任何** `VK_APPS`、
`WM_CONTEXTMENU` 或 `Shift+F10` 處理（grep 確認）。焦點永遠在搜尋 EDIT 上，按下
`Context Menu`／`Shift+F10` 走 EDIT 預設 → 出現**剪貼簿選單**，而不是項目選單。
結果：釘選／取消釘選、自常用清單移除、開啟檔案位置、內容（Properties）四個操作
**只能滑鼠觸發**，違反 §4.7 鍵盤表與 §NFR-006。稽核新增發現。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **攔在 `SearchEditProc` 的鍵盤層**（而非父視窗 `WM_CONTEXTMENU`）：`WM_CONTEXTMENU`
   會送給擁有焦點的子視窗（EDIT），而 EDIT 的滑鼠右鍵**必須維持剪貼簿選單**
   （§4.8/§4.9：搜尋輸入框內的右鍵維持系統剪貼簿選單）——若攔父視窗的
   `WM_CONTEXTMENU` 會同時壞掉滑鼠右鍵。鍵盤路徑在 subclass 的 `WM_KEYDOWN`
   （`VK_APPS`，以及帶 `MK_SHIFT` 的 `VK_F10`）攔截並直接開啟項目選單，**不觸發
   EDIT 的剪貼簿選單**；滑鼠右鍵仍走 EDIT 預設，一字不改。
2. **目標是「目前選取列」**：`g_model->SelectionIndex()`（含 grid 與 list 兩態，
   搜尋欄為空時是格狀選取格）。選取無效（`< 0` 或 `>= RowCount()`）時不開選單，
   `Shift+F10` 交還 EDIT 預設（無選取時按鍵無作用）。
3. **重用 `WM_RBUTTONDOWN` 的既有項目選單**：把 `main.cpp:2669-2750` 的項目分支
   （建選單＋`TrackPopupMenu`＋指令分派）抽成一個檔案範圍 helper，例如
   `void ShowItemMenu(HWND window, int cell, POINT screen_pos)`，兩個呼叫點（滑鼠
   命中列／鍵盤選取列）共用同一份邏輯與同一組指令常數。`WM_RBUTTONDOWN` 的
   `cell < 0`（面板空白處 tray 選單）分支不動。
4. **選單定位**：`screen_pos` 由呼叫端給——滑鼠路徑用 `GetCursorPos`（現狀），鍵盤
   路徑用選取列的 screen rect 左上角（格狀＝該 cell 矩形、list＝該列矩形；幾何由
   既有 `LayoutForDpi` 與 `FirstVisibleRow` 推算）。若推算成本過高，鍵盤路徑可退回
   `GetCursorPos`，由實作決定並在交接區載明。
5. **不新增選單結構、不新增指令、不新增字串**：`context_menu_strings` 已含全部文案。
6. **測試策略**：`WM_RBUTTONDOWN` 的選單邏輯不可單元測試（吃 HWND＋TrackPopupMenu），
   由 sanity grep＋手動驗收覆蓋（NR-060 先例）。`SearchEditProc` 的攔截屬 Win32
   subclass 行為，無法單元測試。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- UI strings are English and should be centralized when more than one screen needs them.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

design-spec §4.7：

- `Context Menu`／`Shift+F10` → 開啟項目選單。

design-spec §NFR-006：

- 鍵盤可完成全部核心操作。

design-spec §4.8：

- 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、自常用清單移除、
  內容。搜尋輸入框內的右鍵維持系統剪貼簿選單（§4.9）。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2630-2751` — `WM_RBUTTONDOWN`（`cell < 0` tray 分支、
  `cell >= 0` 項目分支）。項目分支抽 helper 的主場。
- `src/app_host/main.cpp` `SearchEditProc`（grep 找其 `WM_KEYDOWN` 段，約
  `:2450-2580`）— 鍵盤攔截主場。先讀既有 `VK_ESCAPE`／`VK_RETURN`／方向鍵的分派形狀，
  照樣式加。
- `src/app_host/main.cpp` — `PanelModel::SelectionIndex()`／`RowCount()`（
  `panel_model.h`），以及 `LayoutForDpi` 幾何（grid cell rect／list row rect）。
- `src/app_host/main.cpp:2716-2749` — 指令分派（`kCmdPin`…`kCmdForgetRecent`），
  隨 helper 一起搬。

## Scope

### 1. 抽 `ShowItemMenu` helper

從 `WM_RBUTTONDOWN` 的 `cell >= 0` 分支（`main.cpp:2669-2750`）抽成：

```cpp
// NR-078: shared by the right-click handler (hit cell) and the keyboard
// Context Menu / Shift+F10 path (selected cell). design-spec §4.8 lists the
// commands; §NFR-006 requires the keyboard to reach them.
void ShowItemMenu(HWND window, int cell, POINT screen_pos);
```

函式內容＝原分支逐字搬移（`g_model`/`g_pins` null guard、建選單、`IsMissingPin`
縮排、`RecentStartIndex` 判斷、`TrackPopupMenu(..., screen_pos, ...)`、
`g_context_menu_active` 包覆、`DestroyMenu`、指令分派）。`WM_RBUTTONDOWN` 的
`cell >= 0` 分支改為一行 `ShowItemMenu(window, cell, cursor);`。

### 2. 鍵盤攔截（`SearchEditProc` 的 `WM_KEYDOWN`）

- `VK_APPS`：`const int sel = g_model->SelectionIndex(); if (sel >= 0 && sel < (int)g_model->RowCount()) { 推算 screen_pos；ShowItemMenu(window, sel, pos); } return 0;`（吞掉，不送 EDIT 預設）。
- `VK_F10` 且 `(GetKeyState(VK_SHIFT) & 0x8000)`：同上；非 shift 的 `F10` 維持既有
  行為（menu bar 慣例，不攔）。
- 兩者共用一個檔案範圍小 helper（如 `OpenKeyboardItemMenu(HWND window)`），避免
  重複推算。

### 3. 選單定位推算（鍵盤路徑）

格狀：`cell` 轉列／欄（`FirstVisibleRow()`＋`Columns()`），cell rect 由
`LayoutForDpi(GetDpiForWindow(window))` 的 `kCellWidthDip/kCellHeightDip/kGridLeftDip`
與 `kListTopDip` 算出左上角，`ClientToScreen`。list：列 rect 由 `kRowHeightDip` 推算。
兩者都要 clamp 到面板 client rect 內。

### 4. 更新 spec？

不需。§4.7 鍵盤表本就要此行為；本次是讓實作符合既有規格。

## How this stays maintainable

滑鼠與鍵盤兩條路徑收斂到同一個 helper、同一組指令常數與字串；日後選單內容變更
只改一處。`g_context_menu_active` 的 `WM_KILLFOCUS` 豁免沿用，鍵盤開出的選單同樣
不會讓面板在選單期間隱藏。

## Non-goals

- **不改 EDIT 的滑鼠右鍵（剪貼簿選單）行為。**
- **不改選單結構、指令、字串。**
- **不動 `cell < 0` 的面板空白處 tray 選單。**
- **不新增設定或 accessibility 以外的鍵盤捷徑。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項）。
2. sanity grep：`VK_APPS` 與 shift+F10 分支存在；`ShowItemMenu` 宣告 1＋定義 1＋
   呼叫 2（滑鼠＋鍵盤）。

Manual：

3. 格狀狀態（搜尋欄空）有選取格時按 `Context Menu`／`Shift+F10`：出現該格的項目
   選單，Pin／Unpin／Remove from recent／Properties（依項目型別）運作正常，與滑鼠
   右鍵同內容；選單期間面板不隱藏。
4. list 狀態（已打字）選取列時同上。
5. 無選取時按 `Shift+F10`／`Context Menu`：不開選單、無 crash。
6. 搜尋輸入框內**滑鼠**右鍵仍是剪貼簿選單（回歸）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 鍵盤攔截存在：
Select-String -Path src/app_host/main.cpp -Pattern "VK_APPS"
# expect: 1 處

# 項目選單只有一份 helper，滑鼠與鍵盤都走它：
Select-String -Path src/app_host/main.cpp -Pattern "ShowItemMenu"
# expect: 宣告 1 + 定義 1 + 呼叫 2

# 改動範圍：
git diff --name-only
# expect: 只有 src/app_host/main.cpp
```

## 交接區

（實作者填寫：helper 的實際簽名與搬移範圍、鍵盤 screen_pos 推算方式（cell/row rect
vs 退回 cursor）、`VK_F10` shift 判定、建置與 CTest 結果、sanity greps、手動驗收
3/4/5/6 的實際觀察、偏差、未完成事項。）
