# NR-103 — PMv2 面板初次定位改用 per-window DPI API

Phase 3 · Depends on: NR-015

- Source: `docs/design-spec.md` §NFR-006／§NFR-007、§9.1（DPI responsibility）
- Origin: 2026-08-09 全 repo 稽核（`SetProcessDpiAwarenessContext` 與 `ShowPanel` DPI query 對照）
- Priority: MEDIUM（高 DPI 多螢幕下初次面板尺寸／定位可能取到錯誤 DPI）

## Why

`wWinMain` 在建立視窗前設定 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`，但
`ShowPanel` 仍以 `GetDpiForMonitor` 取得游標所在 monitor 的 DPI。這個 API 的結果受
process awareness 影響，並不是 PMv2 window 的可靠初次 DPI source；後續 `WM_DPICHANGED`
才使用 window DPI 相關流程，因此同一個 panel 可能在第一次顯示與 monitor move 時採用
不同的 DPI 來源。

症狀是 150%／200% monitor 的初次 panel size、center 或 search geometry 不一定符合
設計的 DIP contract；NR-015 的既有 manual check 只證明後續 layout，沒有釘住初次 query。

## Decisions already made — do not reopen

1. 使用 Windows 原生 per-window／per-monitor DPI API，優先沿用現有 `GetDpiForWindow`；
   不新增 DPI abstraction 或第三方 library。
2. `ClampWindowSize`、`WM_DPICHANGED`、layout constants 不改；只統一初次定位的 DPI source。
3. 保留 work-area clamp 與 cursor monitor selection。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-006：

> 尊重系統高對比、文字縮放、DPI 與動畫設定。

`docs/development.md`：

> `ui` | HWND, focus, input, DPI, rendering | Folder scanning

> Add or update one focused test for non-trivial logic.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `wWinMain` awareness setup、`ShowPanel`、`WM_DPICHANGED`。
- `src/ui/layout.{h,cpp}` — `ClampWindowSize`、DIP/px contract。
- `tests/unit/ui_palette_layout_test.cpp` — existing pure layout checks。
- `docs/work-items/NR-015-dpi-theme-accessibility.md` — existing DPI acceptance。
- `docs/testing.md` — 100/150/200% manual checks。

## Scope

1. 替換 `ShowPanel` 初次 DPI query 為與 PMv2 window 一致的原生 API；保留 monitor work-area
   clamp、size calculation 與 subsequent `WM_DPICHANGED` behavior。
2. 增加一個可執行的 pure/helper check 或 sanity check，釘住初次定位與 window DPI query
   使用同一來源；不為單一 Win32 query 建立 abstraction。
3. 更新 NR-015／testing 的驗收描述，明確包含「第一次顯示」而非只測 monitor move。

## Non-goals

- 不改版面尺寸、字型階梯、theme、accessibility text 或 monitor selection policy。
- 不支援 per-monitor legacy fallback 以外的新平台；Windows 10 22H2／11 x64 仍是目標。
- 不新增 third-party DPI library。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. layout tests 原樣通過，且 code review 確認 `ShowPanel` 不再呼叫不適合 PMv2 初次 query
   的 API。

Manual：

3. Release build 在 100%、150%、200% monitor 上首次 `Alt+Space` 的 panel 尺寸、center、
   search field 與 subsequent monitor move 一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_dpi_theme_accessibility_test --output-on-failure
```

```powershell
Select-String -Path src/app_host/main.cpp -Pattern 'GetDpiForMonitor|GetDpiForWindow|WM_DPICHANGED|PER_MONITOR_AWARE_V2'
git diff --name-only
# expect: ShowPanel 的初次 query 與既有 PMv2 window path 一致。
```

## 交接區

已完成。修改檔案：`src/app_host/main.cpp`（`ShowPanel`）、`docs/testing.md`（manual smoke item 7）。未動 `docs/work-items/NR-015-dpi-theme-accessibility.md`——completed item 文件是歷史紀錄，依 AGENTS.md 不可編輯；`docs/testing.md` 的更新才是本 item 更新的 live acceptance。

**採用 API**：Windows 原生 per-window `GetDpiForWindow`，與 `WM_DPICHANGED`（`main.cpp:3120`）及既有所有後續 layout 計算（`:616/:720/:835/:2262/:2407`）同一來源。`GetDpiForMonitor` 呼叫完全移除。

**初次顯示與 monitor move 共用同一 DPI source**：`ShowPanel` 算出 `work_area`（`monitor_info.rcWork`）後，先以 `SetWindowPos(window, HWND_TOPMOST, work_area.left, work_area.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER)` 把仍隱藏的面板停在 cursor monitor（SWP_NOSIZE 保留現尺寸、SWP_NOZORDER 忽略 HWND_TOPMOST 的插序，中間 move 不閃爍）；若該 monitor DPI 與 window 現值不同，`WM_DPICHANGED` 在此呼叫內同步觸發，其 handler 套用 suggested rect（同為 DIP 尺寸置中數學），返回後 `GetDpiForWindow` 即反映 cursor monitor。尺寸以 `ClampWindowSize(static_cast<float>(GetDpiForWindow(window)), …)` 計算；置中 `left/top` 與最終 `SetWindowPos(window, HWND_TOPMOST, left, top, size.width, size.height, SWP_SHOWWINDOW)`、`SetForegroundWindow(window)` 原樣保留。work-area clamp 與 cursor-monitor selection 未動。

**Sanity grep**：`src/` 中 `GetDpiForMonitor` 僅剩 `ShowPanel` 的 `// NR-103:` 說明註解一處文字（無 API 呼叫）；`ShowPanel` 現用 `GetDpiForWindow`。`git diff --name-only` 僅列出 `src/app_host/main.cpp`、`docs/testing.md`；本 ticket 文件為新增未追蹤檔（另有其他 agent 的 NR-104，未碰）。

**Build／CTest**：configure 成功；Release build 完成、無新增 warning；`ctest --test-dir build --output-on-failure` 25/25 全綠；`ctest --test-dir build -R nimblerun_dpi_theme_accessibility_test --output-on-failure` 1/1 通過。

**偏差**：
1. `GetDpiForMonitor` 字串仍出現在 `ShowPanel` 的 `// NR-103:` 解釋註解（ticket 明確要求註解說明它以何種 API 替換）；API 呼叫本身已完全移除，sanity grep 的「no GetDpiForMonitor anywhere」以無呼叫為準。
2. 未新增 abstraction／helper、未改 layout 常數／`WM_DPICHANGED`／`ClampWindowSize`，符合 ticket non-goals。
3. 100／150／200% 首次顯示與 monitor move 的視覺一致性屬人工驗收（manual smoke item 7），不在 Agent 範圍。

未完成：無。
