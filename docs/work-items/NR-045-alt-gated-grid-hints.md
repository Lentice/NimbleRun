# NR-045 — Grid quick-select hints appear only while Alt is held

Phase 3 · Depends on: —

## Why

In the grid state (empty query) all 24 visible cells paint an `Alt+N` digit box in
the top-right corner. Twenty-four small boxes at once make the grid look noisy,
and the digits are unusable information until the user actually holds `Alt`.

Decision: in the **grid state only**, the per-cell digit boxes are hidden until
`Alt` is physically down. The footer, while `Alt` is up, replaces the
`[Alt+1~N]` + `Launch` group with one plain grey sentence that says how to
reveal them.

The list state (non-empty query) is **unchanged**: its boxes stay resident,
because design-spec §4.9 requires the list's hint column to occupy constant
width so that app name and second-line text never reflow.

## Binding constraints — quoted, do not go looking for them

design-spec §4.9 (current text, **this item overrides it — see Scope §5**):

> 數字快選指引顯示於對應項目上：清單狀態在列最右方，格狀狀態在格子右上角，皆為只含數字的圓角按鍵方塊（修飾鍵 `Alt` 在 footer 說明一次，不重複）。清單狀態的方塊寬度固定並常駐佔位，App 名稱與第二行文字的可用寬度不因指引有無而變動。

design-spec §4.9, footer:

> footer 右側顯示當下適用的按鍵指引，左側在格狀狀態顯示 active／hover 項目的完整路徑（清單狀態留空）；不顯示狀態或版本資訊。路徑超出可用寬度時以尾端省略號截斷，且不得覆蓋右側指引。

design-spec §4.9, other clauses that stay true: grid is 6×4 cells of 101×96 DIP;
the results area geometry is identical in both states; animations are off by
default.

AGENTS.md:

- NimbleRun application UI text must be English.
- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- UI strings are English and should be centralized when more than one screen
  needs them.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.

## Files to read and trace first

All line numbers are navigation hints taken from the **working tree** copy of
`src/app_host/main.cpp` (which had uncommitted changes when this item was
written). Run `git diff src/app_host/main.cpp` first; the code snippets and
function names below are the specification, the line numbers are not.

- `src/app_host/main.cpp:110-117` — `footer_strings`, the centralized footer
  strings (`kScroll`, `kPageUp`, `kPageDown`, `kLaunch`, `kAltOnePrefix`).
- `src/app_host/main.cpp:901` — `DrawKeyBox`, shared by the footer boxes, the
  list-row boxes and the grid-cell boxes. **Do not change it in this item.**
- `src/app_host/main.cpp:1059-1069` — the grid-cell digit box (the boxes to
  hide). Guarded today by `QuickSelectLabelForSlot(slot)` only.
- `src/app_host/main.cpp:1186-1202` — the list-row digit box. **Out of scope.**
- `src/app_host/main.cpp:1240-1305` — the footer's right-to-left advance:
  `draw_key_box`, `draw_right_label`, `hints_left`, the `PgDn`/`PgUp`/`Scroll`
  group, then the `Alt+1~N` + `Launch` group.
- `src/app_host/main.cpp:1307-1333` — the grid path bar, which ends
  `kFooterHintGapDip` before `hints_left`. Whatever the footer draws on the
  right must keep feeding `hints_left`, or the path will run under it.
- `src/app_host/main.cpp:1552-1621` — `SearchEditProc`, the search EDIT
  subclass. The panel's keyboard focus lives on the EDIT, so `Alt` key
  transitions arrive here, not in the panel's `WndProc`. `WM_SYSKEYDOWN`
  (`:1593`) already implements NR-024 Alt+digit launch; `WM_SYSCHAR` (`:1614`)
  swallows the beep.
- `src/app_host/panel_model.h:64` — `int Columns() const { return query_.empty() ? grid_columns_ : 1; }`.
  **Grid state is exactly `g_model->Columns() > 1`.** Do not add a new
  "is the query empty" test anywhere.
- `src/ui/panel_layout.h:22-36` — footer geometry constants. **No new constant
  is needed in this item.**

## Scope

### 1. One new footer string

In `footer_strings` (`main.cpp:110-117`), next to `kLaunch`/`kAltOnePrefix`:

```cpp
// NR-045: shown in the grid state while Alt is up, in place of the
// Alt+1~N / Launch group (design-spec §4.9).
constexpr wchar_t kHoldAltHint[] = L"Hold Alt to show shortcuts";
```

Exact wording, exact capitalization. Do not add a period.

### 2. One predicate, used twice

At file scope, next to `DrawKeyBox` (`main.cpp:901`):

```cpp
// NR-045: the grid's per-cell digit boxes and the footer's Alt+1~N group are
// revealed only while Alt is physically down; the list state is unaffected.
// Queried per paint instead of tracked in a flag, so there is no stale state to
// clear on Alt+Tab, focus loss or panel hide.
bool AltHeld() { return GetKeyState(VK_MENU) < 0; }
```

No flag, no timer, no `RegisterHotKey`, no low-level keyboard hook.

### 3. Grid cells: hide the boxes while Alt is up

At `main.cpp:1061`, extend the existing condition — the box either paints
exactly as it does today, or is not painted at all:

```cpp
if (AltHeld()) {
    if (const wchar_t* key_label = nimblerun::ui::QuickSelectLabelForSlot(slot)) {
        ... unchanged ...
    }
}
```

Grid cell geometry must not change: the box is an overlay in the cell's top-right
corner, nothing reserves space for it, so hiding it must not move the icon or the
name by one DIP.

### 4. Footer: swap the Alt group for the sentence while Alt is up

Replace the `Alt+1~N` + `Launch` block (`main.cpp:1288-1305`) with a branch.
Keep the `right -= kFooterHintGapDip` that precedes it, and keep feeding
`hints_left` in **both** arms:

- Grid state (`g_model && g_model->Columns() > 1`) and `!AltHeld()`:
  draw only `footer_strings::kHoldAltHint` through the existing
  `draw_right_label` lambda, right-aligned at `right`, and fold its returned
  width into `hints_left`. No key box is drawn.
- Otherwise (list state, or Alt held): the existing code, unchanged —
  `alt_label` built per frame from `kAltOnePrefix` + `QuickSelectLabelForSlot(last_slot)`,
  `draw_key_box(..., kFooterWideKeyBoxWidthDip)`, then the `Launch` label.

The sentence uses `g_small_format` and `g_dim_brush` — i.e. whatever
`draw_right_label` already uses. Do not add a text format, a brush, or a
right-aligned `IDWriteTextFormat`.

`PgUp`/`PgDn`/`Scroll` are untouched in both arms.

### 5. Repaint on the Alt transitions

In `SearchEditProc`, repaint the panel when `Alt` goes down and when it comes
back up. Both must fall through to default processing (`break`), so NR-024's
Alt+digit path and `Alt+Space` keep working:

- `case WM_SYSKEYDOWN:` — before the existing NR-024 digit handling, if
  `w_param == VK_MENU` **and the auto-repeat bit is clear** (`(l_param & (1 << 30)) == 0`),
  call `InvalidateRect(GetParent(edit), nullptr, FALSE)` and `break`. The repeat
  guard is required: holding `Alt` auto-repeats at the keyboard repeat rate, and
  repainting on every repeat is exactly the high-frequency repaint AGENTS.md
  forbids.
- `case WM_SYSKEYUP:` (new) and `case WM_KEYUP:` — if `w_param == VK_MENU`,
  `InvalidateRect(GetParent(edit), nullptr, FALSE)`, then `break`. Releasing
  `Alt` normally produces `WM_SYSKEYUP`; the `WM_KEYUP` arm is there because a
  release that follows a swallowed `Alt+digit` can arrive as either.

Known and accepted: opening the panel with `Alt+Space` while still holding
`Alt` shows the hints immediately and hides them on release — correct behavior,
no special case. If a release is ever missed (e.g. `Alt+Tab` away), nothing
sticks: the panel hides, and the next paint re-reads `GetKeyState`.

### 6. Update the spec

Rewrite the §4.9 clause quoted above so it states the new behavior:

- 清單狀態：方塊在列最右方，寬度固定且常駐佔位，App 名稱與第二行文字的可用寬度不因指引有無而變動。
- 格狀狀態：方塊在格子右上角，僅在按住 `Alt` 時顯示；未按住時 footer 右側以一句灰色說明文字取代 `Alt+1~N` 指引群組。
- 修飾鍵 `Alt` 仍只在 footer 說明一次，不在每個方塊上重複。

Keep it Traditional Chinese, keep the surrounding bullet style, and do not touch
any other §4.9 clause.

## Non-goals

- The list state (non-empty query). Its boxes stay always-on and keep reserving
  `kRowHintReserveDip`.
- `DrawKeyBox` itself — its centering/color/weight are NR-043's subject.
- Any animation, fade or transition on reveal (design-spec §4.9: animations off
  by default).
- Partial invalidation of the panel on the Alt transition. Two full repaints per
  Alt press is not a measured problem; `EN_UPDATE`'s per-keystroke full
  invalidate (`main.cpp:1814`) is the larger one and is out of scope here.
- Tracking Alt with a global flag, a keyboard hook, `WM_ACTIVATE` bookkeeping,
  or a timer.
- New layout constants, new brushes, new text formats.
- The settings window.

## Interaction with the other open items

- **NR-043 §3** fixes a bug in the same footer block: `draw_right_label` returns
  a measured width that the call sites never subtract from `right`, so the
  `Alt+1~N` box overlaps the `Scroll` label. The two items touch adjacent lines.
  Land NR-043 first if both are queued; if this item lands first, keep the
  `hints_left` feed on the new branch intact so NR-043's fix still applies to it.
- NR-041/NR-042/NR-044 do not touch the footer or the grid cell paint.

## Acceptance

No unit test. The whole change is a paint-time branch on `GetKeyState(VK_MENU)`
plus two `InvalidateRect` calls; there is no HWND-free, COM-free seam to test,
and a test that re-asserted the branch condition would be checking its own
duplicate of the code, not the behavior. Manual acceptance instead:

1. Release build, `Alt+Space`, empty query. The grid shows **no** digit boxes.
   The footer right side reads `Hold Alt to show shortcuts   PgUp PgDn Scroll`.
2. Hold `Alt`. Digit boxes appear on the first 10 cells (top-right corners); the
   footer sentence is replaced by `[Alt+1~0] Launch`. Icons and names do not
   move by a pixel between the two states.
3. Release `Alt`. Both revert immediately, no leftover boxes.
4. Hold `Alt` for five seconds. The panel does not flicker (proves the
   auto-repeat guard).
5. With `Alt` held, press `1`. The first cell launches, exactly as NR-024 —
   no beep.
6. Type one character. The list state appears with its digit boxes **always**
   visible and the footer showing `[Alt+1~8] Launch` — no `Hold Alt` sentence,
   with or without `Alt` held.
7. In the grid, hover a cell with a long path. The path bar truncates with an
   ellipsis and never runs under the `Hold Alt to show shortcuts` sentence.
8. `Alt+Tab` away while holding `Alt`, then reopen the panel: no stuck boxes.
9. At 200% DPI, repeat steps 1–3.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Build must produce no new warnings and the existing suite must stay green — this
item adds no test of its own, so a regression in the shared footer/grid paint
would only surface in the manual steps above.

```powershell
# No new layout constant was introduced:
git diff src/ui/panel_layout.h        # must be empty
# Alt is read in exactly one place:
Select-String -Path src/app_host/main.cpp -Pattern 'VK_MENU'
# expect: the AltHeld() definition plus the WM_SYSKEYDOWN / WM_SYSKEYUP / WM_KEYUP guards
```

## 交接區

（實作者填寫：修改的位置、建置與 CTest 結果、九條手動驗收是否為人工驗證、未完成事項。）

- **修改**（只動 `src/app_host/main.cpp` 與 `docs/design-spec.md`；`src/ui/panel_layout.h`、`src/ui/panel_palette.*`、`DrawKeyBox`、清單列數位框、`quick_select.h` 逐位元組不變）：
  - `main.cpp:118-120`：`footer_strings` 新增 `kHoldAltHint[] = L"Hold Alt to show shortcuts"`（含 NR-045 註解，無句尾句點），加在 `kLaunch`／`kAltOnePrefix` 之後。
  - `main.cpp:916-923`：`DrawKeyBox` 前方新增檔案範圍 `bool AltHeld() { return GetKeyState(VK_MENU) < 0; }`（含 NR-045 註解；無 flag、無 timer、無 hook）。
  - `main.cpp:1088-1103`：grid cell 數位框（top-right 的 `QuickSelectLabelForSlot` 方塊）外層包 `if (AltHeld()) { ... }`，框的幾何與繪製內容一字未改；清單列數位框（`main.cpp:1246` 一帶）未動。
  - `main.cpp:1359-1384`：footer 的 `Alt+1~N`＋`Launch` 群組改為分支。`right -= kFooterHintGapDip` 保留在前；grid 狀態（`g_model && g_model->Columns() > 1`）且 `!AltHeld()` 時只以既有 `draw_right_label` 畫 `kHoldAltHint`、右對齊於 `right`，回傳寬度照 `right -= draw_right_label(...); hints_left = std::min(hints_left, right);` 折入 `hints_left`；否則（清單狀態或按住 Alt）沿用原 `alt_label`＋`draw_key_box(kFooterWideKeyBoxWidthDip)`＋`Launch` 組。兩臂都持續餵 `hints_left`。句子沿用 `g_small_format`＋`g_dim_brush`，未新增 format／brush／版面常數。
  - `main.cpp:1672-1681`（`WM_SYSKEYDOWN`）：NR-024 數字處理之前，若 `w_param == VK_MENU` 且自動重複位元未設（`(l_param & (1 << 30)) == 0`）則 `InvalidateRect(GetParent(edit), nullptr, FALSE)` 後 `break`（走預設處理）。
  - `main.cpp:1700-1708`：新增 `case WM_SYSKEYUP:`／`case WM_KEYUP:`（同一 case），`w_param == VK_MENU` 時 `InvalidateRect(GetParent(edit), nullptr, FALSE)` 後 `break`。NR-024 的 Alt+digit 路徑與 Alt+Space 未受影響。
  - `docs/design-spec.md §4.9`：重寫「數字快選指引顯示於對應項目上」該條，改為清單常駐、格狀僅按 `Alt` 顯示、未按時以灰色說明句取代 `Alt+1~N` 群組、`Alt` 仍只在 footer 說明一次。未動其他條款。
- **建置與 CTest**：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` configure 成功；`cmake --build build` 只重編 `main.cpp` 並成功連結，**無新增警告**；`ctest --test-dir build --output-on-failure` **23/23 全綠**（本 item 不新增測試，既有測試為回歸護欄）。
- **Sanity greps**：`git diff src/ui/panel_layout.h` 為空；`Select-String -Path src/app_host/main.cpp -Pattern 'VK_MENU'` 恰好三處（`AltHeld()` 定義、`WM_SYSKEYDOWN` guard、`WM_SYSKEYUP`／`WM_KEYUP` 共用 guard），工作樹原本無任何 `VK_MENU` 引用。
- **手動驗收（9 條）**：全部為人工視覺／操作驗證，依 `AGENTS.md` 交付規則與 `docs/work-items.md`「Agent 交付規則」（不要求操作視窗或人工確認畫面；視覺人工驗證不屬於本追蹤表），由人類在 Release 版上逐條執行：1) 空白查詢 grid 無數字框、footer 顯示 `Hold Alt to show shortcuts`＋`PgUp PgDn Scroll`、2) 按 `Alt` 前 10 格出現數字框且 footer 換回 `[Alt+1~0] Launch`、圖示與名稱不位移、3) 放開 `Alt` 立即復原、4) 長按五秒不閃爍（auto-repeat guard）、5) 按 `Alt` 時按 `1` 啟動第一格無嗶聲、6) 輸入字元進入清單狀態時數字框常駐且無 `Hold Alt` 句、7) grid hover 長路徑以省略號截斷不覆蓋右側句、8) 按 `Alt` 時 `Alt+Tab` 離開再重開無殘留框、9) 200% DPI 重跑 1–3。
- **未完成**：無。九條手動驗收留待人類於 Release 版逐條打勾。
