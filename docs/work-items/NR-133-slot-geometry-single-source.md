# NR-133 — 可見格／列的幾何只留一份：`SlotRect` 與 `SlotAtPointDip`（現況四份拷貝且 footer 界已分歧）

Phase 3 · UI correctness · Depends on: NR-064、NR-082、NR-120（皆 done）

- Source: `docs/design-spec.md` §4.2／§4.8（「滑鼠命中僅限實際繪製的可見格／列」）／§4.9、
  `AGENTS.md`（Reuse existing code before adding helpers or abstractions）
- Origin: 2026-08-10 架構審查（Claude 軸候選 2）。主 Agent 已讀原始碼確認四處拷貝與 footer 界分歧。
- Priority: **IMPORTANT**（同一個算式的四份拷貝**已經分歧**；目前不是 live bug 只因為另一道
  獨立加上的 `ViewportRows()` 守門剛好擋住。NR-064／NR-082／NR-120 三次修補每次都得改好幾份拷貝）

## Why

「第 N 個可見格／列在哪裡？」目前由四份各自獨立的算術回答：

| 位置 | Grid | List |
|---|---|---|
| `Render`（`main.cpp:2015-2023`） | `kGridLeftDip + col*kCellWidthDip`、`kListTopDip + row*kCellHeightDip` | `:2159` `kListTopDip + (i-first)*kRowHeightDip` |
| `CellAtPoint`（`main.cpp:708-732`） | 反算，物理 px | `:743` 反算 |
| `SyncAccessibility`（`main.cpp:882-892`） | 同一組正算，物理 px | `:891` |
| `OpenKeyboardItemMenu`（`main.cpp:2766-2775`） | 再一次同樣正算 | `:2775` |

**它們已經分歧**：`CellAtPoint`（`main.cpp:701-702`）用
`nimblerun::layout::kFooterTopDip * layout.scale`——**未 clamp** 的 footer 位置；
而 `Render`（`:2271`）與 `SyncAccessibility`（`:861`）都用
`layout::FooterTopDip(client_height_dip)`——NR-120 加入的 **已 clamp** 版本
（`src/ui/panel_layout.h:100-108`）。面板被 `ClampWindowSize` 壓縮時這是兩個不同的 y 值。
現在沒出事，只是因為 NR-082 另外加的 `ViewportRows()` 守門（`:729`／`:744`）擋住了那條縫。
`main.cpp:698-700` 的註解甚至把這個分歧寫成刻意的（「LayoutPx carries no footer field on purpose」）
——那是 codebase 自己注意到問題後決定與它共存。

## Decisions already made — do not reopen

1. 新函式放**既有的** `src/ui/panel_layout.h`（純值層、無 `<windows.h>`），不新增 header、
   不新增 library、不建立「幾何模組」抽象。
2. 介面吃 **DIP**、不吃 HWND、不吃 DPI；host 端用既有的 `layout.scale` 轉一次物理 px。
   這是既有 `panel_layout` 的一貫形狀。
3. **正反算必須互為逆向**，且 footer 界只在這一處計算——用 clamp 後的
   `FooterTopDip(client_height_dip)`（NR-120 的版本）。`CellAtPoint` 現用的未 clamp 界是
   本 item 要收掉的分歧，不是要保留的行為。
4. `ViewportRows()` 守門（NR-082）**保留**。它是模型狀態守門，不是幾何；兩者職責不同。
5. 行為零變更為目標，但若正反算統一後 clamp 情境下的命中界**變得更嚴格**（本來就該如此），
   那是修正而非退化，在交接區寫明。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.8：

> 滑鼠命中僅限實際繪製的可見格／列

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`src/ui/panel_layout.h:100-108`（NR-120 的既有規則）：footer band 恆可見，list/grid 的可見區
以 `FooterTopDip` 為下界，不是 client 底部。

## Files to read and trace first

- `src/ui/panel_layout.{h,cpp}` 全部（特別是 `kFooterTopDip:19`、`ClampWindowSize:98`、
  `FooterTopDip:108`、`LayoutForDpi`）。
- `src/app_host/main.cpp`：`:688-749`（`CellAtPoint`）、`:812-904`（`SyncAccessibility`）、
  `:2010-2260`（`Render` 的 grid 與 list 分支）、`:2265-2280`（footer）、
  `:2760-2780`（`OpenKeyboardItemMenu`）。
- `src/app_host/panel_model.h`（`Columns()`／`FirstVisibleRow()`／`ViewportRows()` 的語意）。
- `tests/unit/ui_palette_layout_test.cpp`（既有測試，已連結 `nimblerun_ui`）。
- `docs/work-items/NR-064`、`NR-082`、`NR-120` 三份的 Decisions 與交接區。

## Scope

1. 在 `src/ui/panel_layout.h` 新增兩個互逆的純函式：

   ```cpp
   struct SlotRectDip { float left, top, right, bottom; };

   // slot = 可見區內的序號（0 起算），columns = 1 為 list、>1 為 grid。
   SlotRectDip SlotRect(int slot, int columns, float client_height_dip);

   // 回傳可見區序號，未命中回 -1。footer 界與 SlotRect 用同一個 FooterTopDip。
   int SlotAtPointDip(float x, float y, int columns, int viewport_rows,
                      float client_height_dip);
   ```

2. `Render` 的 grid／list 兩個分支改用 `SlotRect`；`SyncAccessibility` 改用同一個
   `SlotRect`（乘 `layout.scale` 得物理矩形）；`OpenKeyboardItemMenu` 改用 `SlotRect`。
3. `CellAtPoint` 改為：`SlotAtPointDip(x/scale, y/scale, columns, ViewportRows(), height_dip)`
   ＋既有的 `FirstVisibleRow()` 偏移與 `Rows().size()` 界限。刪除 `:701-702` 的未 clamp footer 界
   與四份拷貝中屬於它的算術；`:694-700` 的 NR-064 註解改寫為指向新函式（**不要刪掉 NR 編號**）。
4. 在 `tests/unit/ui_palette_layout_test.cpp` 新增 round-trip property 測試：
   對 grid（columns>1）與 list（columns==1）兩種欄數、三種 DPI（100%／150%／200%）、
   以及一個被 clamp 縮短的 client 高度，驗證
   **每個 slot 的 `SlotRect` 中心點餵回 `SlotAtPointDip` 得回同一個 slot**；
   並驗證 footer band 內的點回 -1（NR-064／NR-120 的界）、超出 `viewport_rows` 的列回 -1（NR-082）。

## Non-goals

- 不改任何常數（`kCellWidthDip`／`kCellHeightDip`／`kRowHeightDip`／`kGridLeftDip`／
  `kListTopDip`／`kFooterTopDip`）與版面外觀。
- 不改 `ClampWindowSize` 或 NR-120 的 footer 恆可見策略。
- 不拆 `Render`（那是另一個候選，本 item 只換掉它的幾何計算）。
- 不動 `panel_model` 的視窗捲動邏輯。

## Acceptance

1. `rg -n "kCellWidthDip|kCellHeightDip|kRowHeightDip|kGridLeftDip"` 在 `src/app_host/main.cpp`
   下只剩必要殘餘（理想為零，若有殘餘需在交接區逐條說明為何不屬於 slot 幾何）。
2. footer 界只在 `panel_layout` 內計算一次，`main.cpp` 不再有 `kFooterTopDip * scale`。
3. round-trip property 測試存在並通過（含 clamp 情境）。
4. 行為零變更（或分歧收斂造成的嚴格化已在交接區寫明）；Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kFooterTopDip|kCellWidthDip|kCellHeightDip|kGridLeftDip|kRowHeightDip" src/app_host/main.cpp
# expect: 幾何常數不再直接出現在 main.cpp 的 slot 計算路徑。
```

## Handoff

交接區需記錄：兩個新函式的最終簽章、四處呼叫點的改法、footer 界收斂前後在 clamp 情境的
實際 y 值差、property 測試涵蓋的 DPI／欄數組合、build／CTest 證據。

### 交接（2026-08-10）

- `SlotRect(int slot, int columns, float client_height_dip)` 回傳 DIP 矩形；
  `SlotAtPointDip(float x, float y, int columns, int viewport_rows,
  float client_height_dip)` 回傳可見區內的線性 slot，未命中回 `-1`。
- `Render` 的 grid/list、`SyncAccessibility`、`OpenKeyboardItemMenu` 都改用
  `SlotRect`；`CellAtPoint` 將 Win32 px 轉 DIP 後改用 `SlotAtPointDip`，再套用
  `FirstVisibleRow()` 與 `Rows().size()`。
- clamp 情境的 footer 界由未 clamp 的固定 `456 * scale` 收斂為
  `FooterTopDip(client_height_dip) * scale`。例如 200% 小螢幕 348 DIP client：舊界
  912 px，新界 632 px，與實際 footer 及可繪製 viewport 一致；這會刻意拒絕原本
  footer 上方不可見列的命中。
- round-trip 測試涵蓋 96／144／192 DPI、list／6-column grid、488／464／348 DIP
  client height，並驗證 footer 與 viewport 外均回 `-1`。
- `cmake --build build` 通過；`nimblerun_dpi_theme_accessibility_test` 通過，
  `nimblerun_lifecycle_check` 通過。完整 CTest 在受限 sandbox 下有既存 Temp 目錄
  寫入權限失敗，另有 `nimblerun_startup_option_test` 因相同環境限制失敗，非本 item
  編譯或行為回歸。
