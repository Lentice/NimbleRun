# NR-136 — 釘選拖曳重排的狀態機收斂為 `PinDragState`（六個全域、五個 handler、四份重置三連）

Phase 3 · Code structure · Depends on: NR-046（釘選區，done）、NR-039（視窗拖曳，done）

- Source: `AGENTS.md`（Keep search, ranking, scoring... independent of HWND and Shell COM
  objects where practical；New non-trivial logic needs one focused runnable test）、
  `docs/design-spec.md` §4.3（釘選重排）
- Origin: 2026-08-10 架構審查（Claude 軸候選 7）。主 Agent 已 grep 逐行確認全域與四份重置。
- Priority: **LOW**（無已知 bug，但 `DragPreviewOrder()` 是純排列邏輯**且零測試**，
  其中含一段微妙的「拖曳中途 catalog 換掉」守門；重置三連被逐字寫了四次）

## Why

六個全域（`main.cpp:417-422` 的 `g_drag_row`／`g_drag_gap`／`g_dragging`／`g_drag_origin`／
`g_drag_cursor`，加上 `g_grid_hover_index`）構成一個小狀態機，由
`WM_LBUTTONDOWN`（`:3368-3373`）、`WM_MOUSEMOVE`（`:3291-3308`）、`WM_LBUTTONUP`（`:3383-3396`）、
`WM_CAPTURECHANGED`（`:3429-3432`）與 `HidePanel`（`:2472-2474`）五處驅動。

重置三連 `g_drag_row = -1; g_drag_gap = -1; g_dragging = false;` 被**逐字寫了四次**
（`:2472`、`:3394`、`:3430`，以及 `:3368-3370` 的反向設定）。

`DragPreviewOrder()`（`:766-783`）是真正的純排列邏輯，包含 `:774` 的
`if (g_drag_row >= pinned || g_drag_gap >= pinned)` ——拖曳進行中釘選區縮小時的守門——
而它**沒有任何測試**，因為它讀全域且只編進 `.exe`。

## Decisions already made — do not reopen

1. 抽成 `src/ui/pin_drag_state.{h,cpp}`（純值層），**HWND-free**：
   系統拖曳門檻（`GetSystemMetrics(SM_CXDRAG)`／`SM_CYDRAG`，`main.cpp:3298-3299`）由
   host 讀好後**當參數傳入**，不在模組內讀。這是 `AGENTS.md` 的硬規則。
2. 介面四個成員，不多：
   ```cpp
   void OnPress(int cell, POINT-like point, int pinned_count);
   void OnMove(POINT-like point, int hit_cell, int pinned_count, int threshold_x, int threshold_y);
   std::optional<std::vector<int>> OnRelease(int pinned_count); // nullopt = 不是一次重排
   void Cancel();                                  // 四份重置三連的唯一落點
   std::vector<int> PreviewOrder(int pinned_count) const;  // Render 用
   ```
   `POINT` 是 `<windows.h>` 型別——用自有的 `struct PointPx { int x, y; };` 以維持純值層。
3. `g_grid_hover_index` **不搬**（hover 與 drag 是兩件事，只是恰好都在 `WM_MOUSEMOVE`）。
4. 行為零變更；`:774` 的守門與 `:766-783` 的排列語意原封搬移，註解含 NR 編號一起帶走。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`：`:414-422`（全域）、`:753-784`（`PinnedRowCount`／`DragPreviewOrder`）、
  `:2006`／`:2129-2140`（`Render` 的 preview 與 ghost）、`:2470-2476`（`HidePanel` 重置）、
  `:3287-3312`（`WM_MOUSEMOVE`）、`:3360-3420`（`WM_LBUTTONDOWN`／`WM_LBUTTONUP`）、
  `:3427-3436`（`WM_CAPTURECHANGED`）。
- `src/pins/pin_store.h:106` 一帶（重排的落地介面）。
- `src/app_host/panel_model.h`（`RecentStartIndex()` 的釘選／最近邊界語意，NR-040）。

## Scope

1. 新增 `src/ui/pin_drag_state.{h,cpp}`，掛到既有的 `nimblerun_ui` library。
2. 六個全域中的五個搬入；五個 handler 改為呼叫對應成員；四份重置三連改為 `Cancel()`。
3. `Render` 的 preview 與 ghost 改讀模組狀態（ghost 座標仍由 host 換算成 DIP）。
4. 新增 `tests/unit/pin_drag_state_test.cpp`，**必測案例**：
   - press → move 未過門檻 → release：**不**產生重排（是一次點擊，不是拖曳）
   - press → move 過門檻 → release 在另一格：產生正確的排列
   - **拖曳中途釘選區縮小**（`pinned_count` 變小到 <= `drag_row` 或 `drag_gap`）：
     `PreviewOrder` 不越界、`OnRelease` 不產生無效重排（`:774` 的守門）
   - `Cancel()` 後 `OnRelease()` 回 `nullopt`
   - `OnMove` 命中釘選區外（`hit_cell >= pinned_count`）時 gap 為 -1
   依 NR-055 的 list-plus-loop 註冊，依 NR-129 用 `test_util.h`。

## Non-goals

- 不改拖曳的視覺（ghost 大小、位置、透明度）與重排的落地格式。
- 不改 `SetCapture`／`ReleaseCapture` 的時機或 `WM_CAPTURECHANGED` 的語意。
- 不搬 `g_grid_hover_index`，不動 hover 高亮。
- 不動 `PinStore` 的持久化。

## Acceptance

1. 五個 drag 全域在 `main.cpp` 歸零（grep 驗證）；重置三連只剩 `Cancel()` 一處。
2. 新模組 grep 不到 `HWND`／`GetSystemMetrics`／`<windows.h>`。
3. 五類測試存在並通過，特別是「拖曳中途釘選區縮小」。
4. 行為零變更；Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_drag_row|g_drag_gap|g_dragging|g_drag_origin|g_drag_cursor" src/app_host/main.cpp
rg -n "windows.h|HWND|GetSystemMetrics" src/ui/pin_drag_state.h src/ui/pin_drag_state.cpp
# expect: 兩者皆零命中。
```

## Handoff

已完成。`PinDragState` 位於 `src/ui/pin_drag_state.{h,cpp}`，以
`PointPx`（同一 header）承載像素座標；`OnPress` 記錄 pressed row，`OnMove`
由 host 傳入命中 cell 與 `GetSystemMetrics(SM_CXDRAG/SM_CYDRAG)` 門檻，
`OnRelease(pinned_count)` 以 release 當下的即時 pinned count 回傳含 `-1` gap 的排列或 `nullopt`，`Cancel` 清除狀態，
`PreviewOrder` 供 Render 使用。`Active`／`Dragging`／`PressedRow`／`Cursor`
是 host 讀取視覺與 release 分支所需的唯讀查詢。

五個 handler 的改法：`WM_MOUSEMOVE` 只在 host 讀取門檻與 hit cell 後呼叫
`OnMove`；`WM_LBUTTONDOWN` 呼叫 `OnPress`；`WM_LBUTTONUP` 將當下
`PinnedRowCount()` 傳給 `OnRelease`，再以其排列結果落地或在未拖曳時啟動；`WM_CAPTURECHANGED` 呼叫
`Cancel`；`HidePanel` 也呼叫 `Cancel`。`g_grid_hover_index` 保持在 host，
未搬入模組。拖曳中途 pinned region 縮小時，`PreviewOrder` 與 `OnRelease`
均拒絕越界排列。

五類必要 focused assertions 加一個 release-count regression 位於 `tests/unit/pin_drag_state_test.cpp`：門檻內
點擊不重排、跨格拖曳回傳 `[0,2,3,-1]`、pinned region 縮小時 preview/release
皆為空、release 重新檢查當下 pinned count、`Cancel` 後 release 為 `nullopt`、命中 pinned 區外不產生 gap。

驗證：Release configure/build 成功，完整 CTest 通過（32/32，含 lifecycle），
`rg` 驗證 `main.cpp` 無五個 `g_drag_*` 名稱，且新模組無
`windows.h`／`HWND`／`GetSystemMetrics` 命中。
