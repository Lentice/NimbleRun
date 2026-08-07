# NR-046 — Drag pinned cells to reorder them in the grid

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §4.2（空白查詢狀態）／§4.8（滑鼠操作）／§4.9（視窗外觀）／§FR-011（釘選與排序）／§10.2（持久化）

## Why

design-spec §FR-011 promises "釘選項目可用拖曳調整順序" and nothing implements it.
`PinStore` has `Pin`/`Unpin`/`Reconcile` but no way to change the order, so the
pin order is permanently the order the user happened to pin things in.

This item adds drag-and-drop reordering **inside the pinned region of the grid
state only**: press a pinned cell, drag, and the pinned cells reflow around a
dashed rounded placeholder that marks where the item will land, with a
semi-transparent copy of the dragged icon following the cursor. Release commits
the new order to `favorites.txt`.

## Decisions already made — do not reopen

Confirmed by the user for this item:

1. **Ghost icon is required.** A dashed placeholder alone is not acceptable; the
   dragged item's icon must visibly follow the cursor at reduced opacity.
2. **Dropping outside the pinned region cancels.** The stored order does not
   change and nothing is written. "Move to the end" is reachable by dropping on
   the last pinned cell, so no functionality is lost.
3. **No cross-region drag.** Dragging a *recent* cell into the pinned region does
   **not** pin it. Pinning stays on the NR-018 context menu. A future item may
   add it; this one must not.

Decided while writing this item (rationale in Non-goals):

4. Pinned cells launch on **button release**, not on press — a drag threshold
   cannot exist otherwise. Recent cells and list rows keep launching on press.
5. No `Esc` cancel path, no auto-scroll while dragging, no animation, no
   keyboard "move forward/backward" fallback.

## Binding constraints — quoted, do not go looking for them

design-spec §FR-011:

> - 項目右鍵可釘選或取消。
> - 釘選項目可用拖曳調整順序；MVP 若拖曳延誤開發，可先提供「向前／向後移動」。
> - App 暫時不存在時保留 pin 紀錄 30 天；若重新安裝且 stable ID 相同，自動恢復。

design-spec §4.8 (**this item extends it — see Scope §7**):

> - 單擊清單列或格子立即啟動。
> - 格狀狀態下指標停在某格時，該格顯示淡填色並在 footer 顯示其路徑；不改變鍵盤選取。
> - 右鍵提供「釘選／取消釘選」及「開啟檔案位置」（適用時）。
> - 點擊面板外，面板自動隱藏。
> - 不要求雙擊，避免速度慢與行為不一致。

design-spec §4.2:

> - 釘選與常用項目共用同一種格子外觀，不加分組標題或分隔線；順序本身即為區隔。
> - 名稱限一行，超出寬度以尾端省略號截斷；不換行、不因文字長度改變格子高度。
> - hover 只改變 path bar 內容與該格的淡填色，**不改變選取**。

design-spec §10.2:

> `favorites.txt`：UTF-8，每行一個經 escaping 的 stable ID，行序即 pin 順序。

> 所有持久資料寫入應先寫 `.tmp`，flush 成功後以 replace 方式提交。

design-spec §4.9 clauses that stay true: grid is 6×4 cells of 101×96 DIP, icon
40×40 DIP centered in the upper half, animations are off by default.

AGENTS.md:

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Do not overwrite user data in place. Use temporary files and atomic
  replacement for persistent writes.
- New non-trivial logic needs one focused runnable test or self-check.
- NimbleRun application UI text must be English.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.

## Files to read and trace first

Line numbers are navigation hints from the **working tree** copy of
`src/app_host/main.cpp` and `src/app_host/panel_model.cpp`, which both had
uncommitted changes when this item was written. Run
`git diff src/app_host/main.cpp src/app_host/panel_model.cpp` first; the code
snippets and function names below are the specification, the line numbers are
not.

- `src/pins/pin_store.h:50-84` — the whole `PinStore` API. Note there is **no**
  reorder entry point today; §1 adds exactly one.
- `src/pins/pin_store.cpp:179-216` — `Pin`, `Unpin`, `IsPinned`, `OrderedPins`.
  `pins_` is a `std::vector<PinRecord>` whose order *is* the pin order.
- `src/app_host/panel_model.h:78-82` — `RecentStartIndex()`: rows before it are
  the pinned region; `-1` means there is no recent region (non-empty query).
  **This is the only pinned/recent boundary in the codebase. Do not add
  another.**
- `src/app_host/panel_model.cpp:26-29,62-74` — `SetPins` calls `RefreshRows`,
  which ends with `selected_ = 0; first_visible_ = 0;`. Pin order changes
  therefore reset selection and scroll, exactly as NR-018's pin/unpin already
  do.
- `src/app_host/main.cpp:247-251` — `g_grid_hover_index` and
  `g_tracking_mouse_leave`, the existing precedent for *window-layer visual
  state that is not model state*. The drag state in §3 sits next to them and
  follows the same rules.
- `src/app_host/main.cpp:459-490` — `CellAtPoint`, the single hit-test used by
  hover, left click and right click. **Reuse it; do not write a second
  hit-test.**
- `src/app_host/main.cpp:731-754` — `DrawDecodedIcon`, which hardcodes
  `DrawBitmap(..., 1.0f, ...)`. §5 adds a defaulted opacity parameter.
- `src/app_host/main.cpp:973-1070` — the grid cell paint loop. `i` is the
  absolute row index; `slot = i - first`. §4 changes which row each slot paints.
- `src/app_host/main.cpp:1390-1394` — the panel-hide path that resets
  `g_grid_hover_index`. Drag state resets here too (§6).
- `src/app_host/main.cpp:1894-1941` — `WM_MOUSEMOVE`, `WM_MOUSELEAVE` and
  `WM_LBUTTONDOWN`. Note the `cell < 0` arm: it hands the press to NR-039's
  `WM_NCLBUTTONDOWN`/`HTCAPTION` window drag. **That arm must keep working
  unchanged.**
- `docs/work-items/NR-039-panel-drag.md` — why the empty-chrome press moves the
  window; the item you must not regress.
- `src/ui/panel_layout.h:37-47` — grid geometry (`kCellWidthDip`,
  `kCellHeightDip`, `kIconSizeDip`, `kGridLeftDip`, `kGridColumns`) and
  `kSearchCornerRadiusDip` (6 DIP), reused as the placeholder corner radius.
  **No new constant is needed in this item.**
- `tests/unit/pin_store_test.cpp:1-45` — the existing test style: plain
  `Expect(cond, "message")` + `std::exit(1)`, temp directory per case, no
  framework. The new test in §2 goes in this file.
- `tests/CMakeLists.txt:504-529` — `nimblerun_pinning_test` already builds and
  registers this file. **No CMake change is needed.**

## Scope

### 1. `PinStore`: one reorder entry point

Pin order lives in the store; the grid only knows the pins that resolved against
the current catalog. Pins for temporarily absent apps (§FR-011's 30-day
retention) are **not** in the grid, so an index-based reorder would silently
move them. The API therefore takes stable IDs, in the new visual order, and
leaves every unlisted pin at the absolute slot it already occupies.

In `pin_store.h`, after `OrderedPins()`:

```cpp
// NR-046: reorders the pins named in `order` so their relative order matches
// `order` exactly, while every pin NOT named there (a pin whose app is absent
// from the current catalog, design-spec §FR-011) keeps the absolute index it
// already has. IDs in `order` that are not pinned are ignored. Returns true when
// the stored order actually changed; call Save() to persist.
bool ReorderPresent(const std::vector<std::wstring>& order);
```

Implementation shape in `pin_store.cpp` (keep it this simple):

1. Collect the indices of `pins_` whose `stable_id` appears in `order`, in
   current ascending index order, into `slots`.
2. Collect, from `order`, only the IDs actually pinned, in `order`'s order, into
   `wanted`. If `wanted.size() != slots.size()`, some ID appeared twice or is
   unknown — take the intersection; never resize `pins_`.
3. Move the record for `wanted[k]` into `pins_[slots[k]]` for every k. Compare
   with the pre-move order to decide the return value.

`last_seen_utc` values travel with their record. `Save()` is unchanged — the
existing tmp + flush + atomic replace path already writes `pins_` in order.

### 2. One focused test

Add to `tests/unit/pin_store_test.cpp`, in the existing style, one case named
after the behavior (e.g. `ReorderKeepsAbsentPinsInPlace`):

- Pin `a`, `b`, `c`, `d`. `ReorderPresent({d, a, c})` → `OrderedPins()` is
  `d, a, b, c`: `b` was not named, so it keeps index 2.
- `ReorderPresent({a, b, c, d})` on an already-`a, b, c, d` store returns
  `false`.
- `ReorderPresent({z})` (unknown ID) returns `false` and changes nothing.
- `Save()` then a fresh `PinStore(dir).Load()` round-trips the new order.

No test for the drag gesture itself: it is Win32 mouse-message plumbing with no
HWND-free seam (see Acceptance).

### 3. Drag state: window-layer only

Next to `g_grid_hover_index` (`main.cpp:247-251`):

```cpp
// NR-046: pinned-cell drag-reorder state. Window-layer visual state like
// g_grid_hover_index, never model state: it is reset when the panel hides and
// when capture is lost, so there is nothing stale to reconcile.
int g_drag_row = -1;      // pressed pinned row index; -1 = no press captured
int g_drag_gap = -1;      // pinned row index the dragged item would take;
                          // -1 = cursor is outside the pinned region (= cancel)
bool g_dragging = false;  // true once the press passed the system drag threshold
POINT g_drag_origin{};    // client coords of the press, for the threshold test
POINT g_drag_cursor{};    // latest client coords, for the ghost icon
```

Plus one file-scope helper, next to `CellAtPoint`:

```cpp
// NR-046: pinned row count of the current view, 0 when there is no pinned
// region. RecentStartIndex() is the single pinned/recent boundary (NR-040).
int PinnedRowCount() {
    if (!g_model) {
        return 0;
    }
    const int recent_start = g_model->RecentStartIndex();
    return recent_start > 0 ? recent_start : 0;
}
```

and one that produces the drag preview order:

```cpp
// NR-046: paint order of the pinned region during a drag: the dragged row is
// lifted out and a gap (-1) is left where it would land, so the pinned region
// reflows and the recent region never moves -- the permutation is closed over
// the pinned region, so the total cell count is unchanged. Empty vector when no
// drag preview applies.
std::vector<int> DragPreviewOrder();
```

Build it literally, not with index arithmetic: fill `0..PinnedRowCount()-1`,
`erase` the element equal to `g_drag_row`, then `insert` `-1` at `g_drag_gap`.
Return `{}` unless `g_dragging && g_drag_row >= 0 && g_drag_gap >= 0`. The
pinned region is a handful of items and this only runs while a drag is in
progress, so the per-paint allocation is not a concern.

### 4. Paint: reflow, placeholder, ghost

In the grid loop (`main.cpp:981-1070`), take the preview once **before** the
loop and map each slot through it:

```cpp
const std::vector<int> preview = DragPreviewOrder();
const int pinned = PinnedRowCount();
...
for (int i = first; i < last; ++i) {
    const int row = (!preview.empty() && i < pinned) ? preview[static_cast<std::size_t>(i)] : i;
    ... // cell rect from `slot` exactly as today
}
```

- Every existing read of `rows[i]`, and the `selected`/`hovered` comparisons,
  becomes a read of `rows[row]` / a comparison against `row`. The cell rect
  still comes from `slot`, so geometry is untouched.
- `row == -1` is the gap: draw **only** the dashed placeholder and `continue` —
  no fill, no icon, no name, no digit box.
- While `g_dragging`, skip the hover fill entirely (the hover index is cleared
  when the drag starts, §6, and must not be recomputed during the drag).

Placeholder, using the existing selection border brush and the search box's
corner radius — no new brush, no new constant:

```cpp
// NR-046: dashed rounded outline marking where the dragged pin will land.
g_render_target->DrawRoundedRectangle(
    D2D1::RoundedRect(D2D1::RectF(cell.left + 6.0f, cell.top + 6.0f,
                                  cell.right - 6.0f, cell.bottom - 6.0f),
                      nimblerun::layout::kSearchCornerRadiusDip,
                      nimblerun::layout::kSearchCornerRadiusDip),
    g_selected_border_brush, border_width, g_dash_style);
```

`g_dash_style` is an `ID2D1StrokeStyle*` created **from the D2D factory**, so it
is device-independent: create it once where the factory is created and release
it where the factory is released, *not* in the render-target create/release
pair.

```cpp
D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                            D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                            D2D1_DASH_STYLE_DASH, 0.0f)
```

Ghost icon, drawn **after** the whole cell loop so it is above every cell, and
only while `g_dragging`: a `kIconSizeDip`-square rect centered on
`g_drag_cursor` (converted from client pixels to DIPs with the same
`dpi_x / kDpi96` scale the rest of the paint uses), holding the dragged row's
icon at reduced opacity.

```cpp
// NR-046: the dragged item must remain visible under the cursor (user
// requirement). Cache miss -> a dim square, so something always follows the
// cursor; the icon request was already issued by the cell paint.
```

Use `g_icon_cache->Peek(IconKeyFor(rows[g_drag_row], grid_icon_needed_px).Encode())`
— the same key the cell paint builds. On a hit call `DrawDecodedIcon(..., 0.6f)`;
on a miss `FillRectangle(ghost_rect, g_dim_brush)`. Do not call
`RequestVisibleIcon` from the ghost path.

### 5. `DrawDecodedIcon` gains an opacity parameter

```cpp
void DrawDecodedIcon(const nimblerun::IconBitmap& icon,
                     const D2D1_RECT_F& tile,
                     float dpi_x,
                     float dpi_y,
                     float opacity = 1.0f);
```

and pass it through to the existing `DrawBitmap` call in place of the hardcoded
`1.0f`. Every existing call site stays byte-identical.

### 6. Messages

All in the panel's `WndProc`, around `main.cpp:1894-1941`:

**`WM_LBUTTONDOWN`** — three arms, in this order:

1. `cell < 0` → unchanged NR-039 `ReleaseCapture()` +
   `WM_NCLBUTTONDOWN`/`HTCAPTION` window drag.
2. Grid state (`g_model->Columns() > 1`) **and** `cell < PinnedRowCount()` →
   `SetCapture(window)`, `g_drag_row = cell`, `g_drag_gap = cell`,
   `g_dragging = false`, `g_drag_origin = g_drag_cursor = {x, y}`, `return 0`.
   **Do not launch here.**
3. Otherwise (recent cell, or list state) → the existing `SelectRow` +
   `ActivateRow`, unchanged.

**`WM_MOUSEMOVE`** — when `g_drag_row >= 0`, this arm replaces the hover arm
entirely (`return 0`; no hover update, no `TrackMouseEvent`):

- Not yet dragging: promote to `g_dragging = true` once
  `abs(x - g_drag_origin.x) >= GetSystemMetrics(SM_CXDRAG)` or the `SM_CYDRAG`
  equivalent holds, and clear `g_grid_hover_index` when promoting.
- Dragging: store `g_drag_cursor`, recompute
  `const int cell = CellAtPoint(...)` and set
  `g_drag_gap = (cell >= 0 && cell < PinnedRowCount()) ? cell : -1`, then
  `InvalidateRect(window, nullptr, FALSE)`.

When `g_drag_row < 0` the existing hover arm runs exactly as today.

```cpp
// ponytail: the ghost follows the cursor, so every WM_MOUSEMOVE during a drag
// invalidates the whole panel. The repaint rate is bounded by the mouse message
// rate -- no timer, no busy loop -- and a drag is a brief, explicitly
// user-driven gesture. Narrow it to the two dirty cell rects only if a drag
// ever measures as a problem.
```

**`WM_LBUTTONUP`** (new) — only acts when `g_drag_row >= 0`:

1. `ReleaseCapture()`, then take copies of `g_drag_row`, `g_drag_gap`,
   `g_dragging` and **clear all drag state before doing anything else**, so no
   arm can leave it half-set.
2. `!dragging` → the press was a click: `SelectRow(row)` + `ActivateRow(row, window)`.
   This is the deferred launch from §6 arm 2.
3. `dragging && gap >= 0 && gap != row` → commit: build
   `std::vector<std::wstring>` from `DragPreviewOrder()` with `-1` replaced by
   `g_drag_row`, taking `g_model->Rows()[k].stable_id` for each entry (capture
   the vector *before* clearing the state, or rebuild the same permutation from
   the copies). Then `g_pins->ReorderPresent(order)`, and on `true`
   `g_pins->Save()`; on a successful save `g_model->SetPins(g_pins->OrderedPins())`.
   Mirror NR-018's existing failure handling: a failed `Save()` leaves the file
   untouched and the view is simply not refreshed.
4. `dragging && (gap < 0 || gap == row)` → cancel: nothing is written.
5. `InvalidateRect(window, nullptr, FALSE)` in every case.

**`WM_CAPTURECHANGED`** (new) — clear all drag state and invalidate. This is the
single escape hatch that covers `Alt+Tab`, the panel hiding under the drag, and
anything else taking capture; no `WM_ACTIVATE` or `Esc` bookkeeping is added.

**Panel hide** (`main.cpp:1390-1394`) — reset the drag state next to the
existing `g_grid_hover_index = -1`.

Known and accepted: a committed reorder resets selection to the first cell and
scrolls to the top, because `SetPins` → `RefreshRows` already does that for
pin/unpin (NR-018). Do not add selection preservation here.

The icon cache capacity does **not** need re-deriving: the pin *count* is
unchanged by a reorder, so no `IconCacheCapacityFor` call belongs in this path.

### 7. Update the spec

`docs/design-spec.md` §4.8, after 「單擊清單列或格子立即啟動。」 add:

> - 格狀狀態的釘選格可用左鍵拖曳調整順序：拖曳中以圓角虛線框標示落點，被拖曳項目的圖示以半透明跟隨指標，其餘釘選格即時讓位；放開即套用並寫入 `favorites.txt`。放在釘選區之外放開視為取消，順序不變。
> - 為了與拖曳共存，釘選格在放開左鍵時啟動（未超過系統拖曳門檻即視為單擊）；常用格與清單列仍在按下時啟動。

§FR-011, replace 「釘選項目可用拖曳調整順序；MVP 若拖曳延誤開發，可先提供「向前／向後移動」。」 with:

> - 釘選項目可在格狀狀態以拖曳調整順序（§4.8）；不提供「向前／向後移動」的鍵盤替代。
> - 拖曳只在釘選區內重排，不能藉拖曳釘選或取消釘選。

Keep Traditional Chinese and the surrounding bullet style; do not touch any other
clause.

## Non-goals

- **Cross-region drag.** Dragging a recent cell does nothing (it launches on
  press, as today). Pinning stays on the NR-018 context menu.
- **Reordering in the list state.** A non-empty query has no pinned region
  (`RecentStartIndex() == -1`), so `PinnedRowCount()` is 0 and no drag can start.
- **Auto-scroll while dragging near an edge.** It needs a timer, which AGENTS.md
  forbids on the idle path; one page holds 24 cells and the pinned region rarely
  exceeds it. Dropping outside the pinned region cancels, so nothing breaks.
- **`Esc` to cancel a drag.** Keyboard focus is on the search EDIT, so `Esc`
  arrives in `SearchEditProc` and would need cross-proc plumbing. Dropping
  outside the pinned region already cancels, and `WM_CAPTURECHANGED` covers the
  abnormal exits.
- **Animation, easing or a fade on the reflow** (design-spec §4.9: animations off
  by default).
- **A drag cursor (`SetCursor`) or a drag-image API (`ImageList_BeginDrag`,
  OLE drag-drop).** The ghost is one `DrawBitmap` in the existing paint.
- **Partial invalidation during the drag** (see the `ponytail:` note in §6).
- **Selection preservation across a reorder** (see §6).
- **Reordering absent pins**, a settings-page pin manager, or the §FR-011
  30-day cleanup UI — separate items.
- **New layout constants, new brushes, new text formats, new CMake targets.**

## Interaction with the other open items

Everything below touches `main.cpp`; land them one at a time and re-check line
numbers.

- **NR-041 (pinned marker)** paints inside the same grid cell loop. Its marker
  must key off the same `row` variable this item introduces, not `i`. Whichever
  lands second must adapt.
- **NR-045 (Alt-gated grid hints)** wraps the cell digit box in `if (AltHeld())`
  in the same loop. No conflict beyond adjacency: the gap slot's `continue`
  skips the digit box either way.
- **NR-040 (context menu)** already introduced `RecentStartIndex()`, which this
  item depends on; it is `done`-adjacent in the same `WM_RBUTTONDOWN` block but
  does not touch `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`.
- **NR-039 (panel drag)** owns the `cell < 0` arm of `WM_LBUTTONDOWN`. Leave it
  first in the chain and unchanged.

## Acceptance

Automated: the `PinStore::ReorderPresent` test from §2 must pass in
`nimblerun_pinning_test`.

Manual (Release build), because the gesture is Win32 mouse-message plumbing with
no HWND-free, COM-free seam:

1. Pin four apps. `Alt+Space` with an empty query. Drag the first pinned cell
   onto the third pinned cell's position: the intervening pinned cells shift
   left/up to make room, a dashed rounded outline sits at the target cell, and a
   semi-transparent copy of the dragged icon follows the cursor. **The recent
   cells after the pinned region do not move at all.**
2. Release. The new order is applied. Reopen the panel: the order persists.
   Restart NimbleRun: it still persists.
3. Inspect `%LOCALAPPDATA%\NimbleRun\favorites.txt`: line order matches the
   visible order, the `schema=1` first line is intact, no `.tmp` file is left
   behind.
4. Drag a pinned cell and release it over a **recent** cell — nothing changes.
   Release it over the empty area below the grid — nothing changes, and the
   panel does **not** start a window drag.
5. Click (press and release without moving) a pinned cell: it launches, exactly
   as before. Click a recent cell: it launches on press, exactly as before.
6. Press a pinned cell, drag, and press `Alt+Tab` mid-drag: no stuck placeholder
   or ghost when the panel comes back.
7. Type a character to enter the list state and try to drag a row: nothing
   drags, the click still launches on press.
8. Press and drag on the panel's empty chrome (not on a cell): the window still
   moves (NR-039 unbroken).
9. Pin an app, then rename/move its target so it drops out of the catalog but
   stays within the 30-day retention. Reorder the remaining pinned cells,
   restart, and confirm the absent pin is still in `favorites.txt` at its
   original line index.
10. At 200% DPI, repeat steps 1–2: the placeholder outline and the ghost icon
    are correctly scaled and centered on the cursor.
11. Drag continuously for several seconds: no flicker, no visible lag.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R pinning --output-on-failure
```

Build must produce no new warnings and the whole suite must stay green.

```powershell
# No new layout constant, no new CMake target:
git diff src/ui/panel_layout.h tests/CMakeLists.txt CMakeLists.txt   # must be empty
# Exactly one hit-test:
Select-String -Path src/app_host/main.cpp -Pattern 'CellAtPoint'
# expect: the definition plus the WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_RBUTTONDOWN call sites
# One pinned/recent boundary:
Select-String -Path src/app_host/main.cpp -Pattern 'RecentStartIndex'
# expect: PinnedRowCount() and the NR-040 context-menu check only
# No timer was introduced:
Select-String -Path src/app_host/main.cpp -Pattern 'SetTimer'
# expect: no new match
```

## 交接區

（實作者填寫：修改的位置、建置與 CTest 結果、九條手動驗收是否為人工驗證、未完成事項。）

- **修改**：
  - `src/pins/pin_store.{h,cpp}`：新增 `PinStore::ReorderPresent(order)`——唯一的重排入口，接受新視覺順序的 stable ID 清單。演算法照 §1：`slots` 收集「在 order 內」的 pin 的絕對索引（升冪），`wanted` 收集 order 中實際已 pin 的 ID（去重，等價於交集，避免重複 ID 造成 resize），再將 `wanted[k]` 的 record 搬到 `pins_[slots[k]]`；未列出的 pin 永不搬動，故在 `favorites.txt` 保持原行號（acceptance 9）。回傳值以 `OrderedPins() != 搬移前` 決定（比較 id 順序，`PinRecord` 無 `operator==`）；`last_seen_utc` 隨 record 一起搬移。
  - `tests/unit/pin_store_test.cpp`：新增 `TestReorderKeepsAbsentPinsInPlace`，沿用既有 `Expect`/`exit(1)`/每 case 一個 temp dir 風格，涵蓋：`{d,a,c}` 重排後未列出的 `b` 留在絕對索引 1（結果 `[d,b,a,c]`，grid 對應顯示 `[d,a,c]`）、`Save()`→fresh `Load()` round-trip、已排好序的 store 上 identity reorder 回 `false`、未知 ID 回 `false` 且不動。已在 `wmain()` 註冊。
  - `src/app_host/main.cpp`（只動此一檔）：
    - 拖曳狀態五個 global（`g_drag_row`／`g_drag_gap`／`g_dragging`／`g_drag_origin`／`g_drag_cursor`）放 `g_grid_hover_index` 旁，同一「window-layer 視覺狀態、非 model 狀態」規則。
    - `g_dash_style`（`ID2D1StrokeStyle*`）宣告於 `g_render_target` 旁，在 `CreateDeviceResources` 中由 `g_d2d_factory` 建立（`StrokeStyleProperties(...D2D1_DASH_STYLE_DASH...)`，device-independent、隨 `DiscardDeviceResources` 存活），在 wWinMain 的 `Release(g_d2d_factory)` 前釋放；不在 render-target create/release pair。
    - `CellAtPoint` 旁新增檔案範圍 `PinnedRowCount()`（以 `RecentStartIndex()` 為唯一邊界）與 `DragPreviewOrder()`（填 `0..pinned-1`、`erase(g_drag_row)`、`insert(-1, g_drag_gap)`，回 `{}` 除非 `g_dragging && row>=0 && gap>=0`；另加一行越界防護以防拖曳途中 catalog swap 縮小 pin 區）。
    - `DrawDecodedIcon` 新增預設 `float opacity = 1.0f`，代入既有 `DrawBitmap`；既有呼叫點逐位元組不變。
    - `Render` 格狀迴圈：迴圈前取 `DragPreviewOrder()`＋`PinnedRowCount()`，每個 slot 以 `row = (preview 非空且 i<pinned) ? preview[i] : i` 決定要畫哪一列，cell rect 仍由 `slot` 算出（幾何不變）；`row==-1` 即落點——只畫 `g_selected_border_brush`＋`kSearchCornerRadiusDip` 圓角的虛線框後 `continue`；所有 `rows[i]` 讀取、selected/hovered 比較改 `rows[row]`／比較 `row`（NR-041 pin 標記、NR-045 數位框一併以 `row`/`slot` 維持）；`hovered` 加 `!g_dragging &&`（拖曳中凍結 hover）；迴圈後（仍在格狀分支內）畫 ghost——以 `dpi_x/kDpi96` 把 `g_drag_cursor` client px 轉 DIP、`kIconSizeDip` 方形置中於游標，cache 命中 `DrawDecodedIcon(..., 0.6f)`、miss `FillRectangle(g_dim_brush)`，不呼叫 `RequestVisibleIcon`，並以 `g_drag_row < rows.size()` 防越界。
    - `WndProc`：`WM_MOUSEMOVE` 當 `g_drag_row >= 0` 以拖曳臂完全取代 hover 臂（未達 `SM_CXDRAG`/`SM_CYDRAG` 門檻則 promote，promote 時清 `g_grid_hover_index`；拖曳中更新 `g_drag_cursor`、以 `CellAtPoint` 算 `g_drag_gap = (cell>=0 && cell<PinnedRowCount()) ? cell : -1`、`InvalidateRect` 全窗並附 `ponytail:` 註解）；`WM_LBUTTONDOWN` 三臂：`cell<0` 維持 NR-039 原樣 → 格狀且 `cell<PinnedRowCount()` 時 `SetCapture`＋記狀態且**不啟動** → 其餘（recent 格／清單）維持按壓即啟動；新增 `WM_LBUTTONUP`（先拷貝 `row`/`gap`/`dragging`/`preview` **再** `ReleaseCapture()`——`ReleaseCapture` 會同步觸發 `WM_CAPTURECHANGED` 清掉 globals，故拷貝必須在前，這是對 §6 文字順序的必要修正；未拖曳=click→`SelectRow`+`ActivateRow` 延遲啟動；`dragging && gap>=0 && gap!=row`→以 `preview` 換掉 `-1` 為 `g_drag_row` 組 `order`（`g_model->Rows()[entry==-1?row:entry].stable_id`）→`ReorderPresent`+`Save` 成功才 `SetPins`，失敗不動 view，不重算 icon cache cap（pin 數量不變）；其餘=取消不寫）與 `WM_CAPTURECHANGED`（清全部拖曳狀態＋invalidate，唯一逃生門）。`ShowPanel` 的 hover 重設旁一併重設拖曳狀態。
  - `docs/design-spec.md`：§4.8「單擊清單列或格子立即啟動。」後新增兩條（拖曳重排＋虛線落點/半透明 ghost/即時讓位/寫入 `favorites.txt`/區外放開取消；釘選格於放開左鍵啟動、常用格與清單列按壓即啟動）；§FR-011 把「釘選項目可用拖曳調整順序；MVP 若拖曳延誤開發，可先提供「向前／向後移動」。」換成「可在格狀狀態以拖曳調整順序（§4.8）；不提供「向前／向後移動」的鍵盤替代。」＋「拖曳只在釘選區內重排，不能藉拖曳釘選或取消釘選。」；未動其他條款。
- **建置與 CTest**：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` configure 成功；`cmake --build build` 成功（`nimblerun_pins`、`nimblerun_pinning_test`、`NimbleRun.exe` 重編），**無新增警告**（LLVM-MinGW `-Wall -Wextra -Wpedantic` 全清）；`ctest --test-dir build --output-on-failure` **23/23 全綠**；`ctest --test-dir build -R pinning --output-on-failure` **1/1 通過**（新增 reorder case 入既有 pinning test）。
- **Sanity greps**：`git diff src/ui/panel_layout.h tests/CMakeLists.txt CMakeLists.txt` 為空（無新常數、無新 target）；`CellAtPoint` 5 處＝定義＋`WM_MOUSEMOVE` 兩處（新拖曳臂＋既有 hover 臂）＋`WM_LBUTTONDOWN`＋`WM_RBUTTONDOWN`，**未寫第二個 hit-test**（item 的 expect 敘述把 WM_MOUSEMOVE 算一次，實際上它現在有兩次呼叫，定義＋5 呼叫＝預期且正確）；`RecentStartIndex` 3 處＝`PinnedRowCount()` 的註解與呼叫＋NR-040 context-menu 檢查，無第二條 pin/recent 邊界；`SetTimer` 仍只有 NR-011 既有 500ms debounce 一處，**無新增 timer**。
- **與 item 文件的偏差（1 處，文件內部矛盾之必要修正）**：§2 測試 bullet 寫「`ReorderPresent({d, a, c})` → `OrderedPins()` 是 `d, a, b, c`：b 沒被點名所以留在 index 2」。但 b 在起始 `[a, b, c, d]` 的絕對索引是 1，該結果實際把 b 從 1 移到 2，與 §1 演算法（「未列出的 pin 保留其絕對索引」）、header 註解、acceptance 9（「absent pin 仍在 `favorites.txt` 原行號」）以及本 item 的綁定約束「unlisted pins keep absolute slots」全部矛盾。實作照 §1 演算法執行（結果 `[d, b, a, c]`，b 停在絕對索引 1，grid 跳過 b 後顯示 `[d, a, c]`），測試亦改為斷言此結果並在註解說明；§1 演算法本身一字未改。其餘零偏差。
- **手動驗收（11 條）**：全部為人工視覺／操作驗證（Win32 mouse-message 無 HWND-free seam），依 `AGENTS.md` 交付規則與 `docs/work-items.md`「Agent 交付規則」由人類在 Release 版上逐條執行：1) 拖第一格到第三格位置見虛線落點＋半透明 ghost＋讓位且 recent 區不動、2) 放開後順序持久並跨重啟、3) `favorites.txt` 行序與可見順序一致且 `schema=1` 首行完整、無 `.tmp` 殘留、4) 在 recent 格上或格下空白處放開＝取消且不觸發 window drag、5) 不移動地按一下釘選格即啟動、按一下 recent 格按壓即啟動、6) 拖曳中 `Alt+Tab` 回來無殘留、7) 清單狀態拖曳列無作用且按壓即啟動、8) 空白 chrome 拖曳仍可移動視窗（NR-039）、9) 缺席 pin（30 天內）重排後仍在原行號、10) 200% DPI 虛線與 ghost 縮放正確、11) 連續拖曳數秒無閃爍/延遲。
- **未完成**：無。11 條手動驗收留待人類於 Release 版逐條打勾。
