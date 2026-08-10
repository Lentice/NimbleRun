# NR-149 — WM_DPICHANGED 的 lParam 指標未驗證：同 user 程序 SendMessageW 任意指標即可 AV 殺死常駐 process

Phase 3 · Security · Depends on: —（無依賴，本輪最先做——與 NR-139 同類的偽造訊息 crash 向量）

- Source: `AGENTS.md`（Keep changes scoped…）、NR-139 同類（「偽造訊息不可使常駐程式崩潰」家族）、
  MSDN WM_DPICHANGED（lParam 為「suggested rect」指標，系統以 SendMessage 送出）
- Origin: 2026-08-10 第十四次稽核第 2 輪（安全軸，IMPORTANT）。主 Agent 已讀
  `main.cpp:2767-2775` 驗證。
- Priority: **IMPORTANT**——NR-139 只蓋了 WM_APP 訊息；這是同家族的系統訊息漏網。

## Why

`WindowProc` 的 `WM_DPICHANGED` 分支（`src/app_host/main.cpp:2770-2775`）：

```cpp
if (const RECT* suggested = reinterpret_cast<const RECT*>(l_param)) {
    SetWindowPos(window, nullptr, suggested->left, suggested->top, ...);
}
```

`WM_DPICHANGED` 以 `SendMessageW` 送出，`lParam` 是**跨處理序原樣傳遞的指標值**，
接收端在自己的位址空間解參考。任何同 integrity、同 session 的程序可用公開的窗體
類別名（`NimbleRun.Phase0Probe`，`main.cpp:65`）`FindWindowW` 拿到 HWND，再
`SendMessageW(hwnd, WM_DPICHANGED, 0, (LPARAM)0x1)`——0x1 不是任何位址空間的有效
指標，接收端 `suggested->left` 直接 AV。`lParam == 0` 會被 null check 擋下，但任何
非空無效值都保證崩潰。tray、hotkey、watcher 全部消失，需手動重啟。

## Decisions already made — do not reopen

1. **完全不使用 lParam**：suggested rect 改由 `GetDpiForWindow(window)`＋
   `MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)`＋`GetMonitorInfoW`＋
   `ClampWindowSize` 重算（與 `ShowPanel`／NR-146 同一套邏輯，`main.cpp:1785-1808`）。
   行為等價：保持目前視窗位置（`GetWindowRect` 的 left/top），尺寸改為新 DPI 的
   clamp 後尺寸，並夾回 work area。面板每次顯示本來就會重新置中（`ShowPanel`），
   位置保留只是次要細節。
2. **不用 `IsBadReadPtr`**：它無法防 races/ownership 且對跨處理序指標本就不該信任；
   正確的 lazy 解是「根本不解參考不可信指標」。
3. `GetMonitorInfoW` 失敗 → 沿用 NR-146 的處置（不移動視窗，只重算幾何；或直接
   `return 0`——以不 crash、不讀未定義記憶體為唯一硬性要求）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`：`:2767-2780`（`WM_DPICHANGED`）、`:1785-1808`（`ShowPanel` 的
  monitor/DPI/clamp 邏輯，重算範本）、`:65`（class 名）。
- `src/ui/panel_layout.h`：`ClampWindowSize` 簽名。

## Scope

1. `WM_DPICHANGED` 分支刪除 lParam 解參考：以 `GetWindowRect` 取目前位置、
   `GetDpiForWindow` 取新 DPI、monitor work area clamp 重算位置與尺寸，
   `SetWindowPos` 套用。NR-149 註解（「lParam 不可信，重算；同 integrity 偽造
   SendMessageW 可送任意指標」）。
2. 既有 `UpdateViewportRows`／`RepositionSearchEdit`／`UpdateSearchFont`／
   `InvalidateRect` 順序不變。
3. 驗證：Release build 零新增 warning；CTest 全綠（數量不變）。

## Non-goals

- 不碰 `ShowPanel`、不碰 hit-test、不碰 render target（DPI 同步是 NR-150）。
- 不加測試目標（此為視窗訊息處理常式；既有 suite 即驗證網）。

## Acceptance

1. grep 驗證 `WM_DPICHANGED` 分支不再有 `reinterpret_cast`／`l_param` 解參考。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -A 12 "case WM_DPICHANGED" src/app_host/main.cpp
# expect: 無 reinterpret_cast，無 suggested-> 解參考
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
