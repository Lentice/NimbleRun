# NR-150 — HwndRenderTarget 的 DPI 從未同步：混合 DPI 螢幕內容裁切/縮放錯誤且 hit-test 與 paint 分歧

Phase 3 · Correctness · Depends on: NR-149（同一 handler 的相鄰改動，先做 149 再做本 item）

- Source: `docs/design-spec.md` §4.9／§NFR-006（DPI 正確性）、NR-015（per-window DPI 的
  既有決策——「單一 per-window 來源」）、NR-064/082（hit-test 與 paint 分歧的 bug 類別）
- Origin: 2026-08-10 第十四次稽核第 2 輪（正確性軸，IMPORTANT）。主 Agent 已讀
  `CreateDeviceResources` 與 `WM_DPICHANGED` 驗證（全 repo `SetDpi` 零命中）。
- Priority: **IMPORTANT**——混合 DPI 環境下畫面錯誤 + 點擊命中看不見的列
  （NR-064 bug 類別沿 DPI 軸重現）。

## Why

`CreateHwndRenderTarget`（`src/app_host/main.cpp:505-510`）的
`D2D1::RenderTargetProperties()` 未指定 DPI（預設 0 = 繼承 factory 的 system DPI，
即 **factory 建立當下主螢幕的 DPI**）。此後：

- `WM_DPICHANGED`（`:2767-2780`）只 reposition／重算幾何，**不碰 render target**；
- `D2DERR_RECREATE_TARGET`／theme 變更才會 `DiscardDeviceResources` 重建——但那也不是
  DPI 事件；
- 全 repo 零 `SetDpi` 呼叫。

`Render()` 以 DIP 幾何經 target 的（過時）DPI 換算進 pixel-size buffer（`WM_SIZE`
resize 後的實際像素），而 hit-test／accessibility／viewport 用 `GetDpiForWindow`
（NR-015 的 per-window 正確來源）——兩者不一致：

- 主螢幕 200%、面板開在 100% 副螢幕：以 200% 座標繪進 1× buffer → 內容只佔左上
  四分之一、其餘空白；反向則右/下裁切。
- 面板拖到不同 DPI 螢幕：`GetSize()`（Render 的 `FooterTopDip`）≠
  `ClientHeightDip`（`CellAtPoint`／`SyncAccessibility`）→ 繪製的列與可點擊的列
  錯位，**點擊可能啟動看不見的列**。

## Decisions already made — do not reopen

1. **`SetDpi` 同步**：`WM_DPICHANGED` 分支在重算幾何時一併
   `g_render_target->SetDpi(dpi, dpi)`，dpi = `GetDpiForWindow(window)`（NR-015 的
   單一 per-window 來源）；並在 `CreateDeviceResources` 建立 target 後立即以
   `GetDpiForWindow` 同步一次（建立時 window 已在目標螢幕，NR-103 已先 park）。
2. **不重建 target**：`SetDpi` 是 HwndRenderTarget 的標準機制（像素 buffer 不變，
   只是 DIP→px 比例更新）；重建會引入 recreates 與 flicker。
3. **行為零變更於單一 DPI 環境**：`SetDpi(same, same)` 是 no-op；既有幾何全部不動。
4. 不動 hit-test 程式碼（它們已經正確用 `GetDpiForWindow`；分歧的根源是 render
   target，不是 hit-test）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.9（節錄，以原文為準）：

> 面板尺寸以 DIP 為單位…依 per-window DPI 縮放。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

（本 item 為視窗訊息路徑的兩行同步，無可測純邏輯；既有 suite 即驗證網，不新增測試。）

## Files to read and trace first

- `src/app_host/main.cpp`：`:455-510`（`CreateDeviceResources` 建立 target）、
  `:2767-2780`（`WM_DPICHANGED`，NR-149 改完後的形狀）。
- `src/ui/panel_layout.h`：DPI 相關常數與 `GetSize` 的消費端。

## Scope

1. `CreateDeviceResources`：target 建立成功後 `SetDpi(GetDpiForWindow(window), ...)`。
2. `WM_DPICHANGED`：套用新幾何後 `SetDpi(dpi, dpi)`（與 NR-149 的重算同一分支，
   註解互引）。
3. 驗證：Release build 零新增 warning；CTest 全綠（數量不變）。

## Non-goals

- 不重建 render target、不重寫 `Render()` 的 DIP→px 換算、不動 hit-test。
- 不做多螢幕記憶（面板關閉即忘）。

## Acceptance

1. grep 驗證 `SetDpi` 存在於上述兩處，且 dpi 來源是 `GetDpiForWindow`。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SetDpi" src/app_host/main.cpp
# expect: 兩處（CreateDeviceResources 與 WM_DPICHANGED），皆以 GetDpiForWindow 為參數
```

## Handoff

- 2026-08-10 完成（NR-150, commit `NR-150: sync render target DPI with the per-window DPI`）。
- 變更：`src/app_host/main.cpp:515-518` — `CreateDeviceResources` 在
  `CreateHwndRenderTarget` 成功後立即
  `SetDpi(static_cast<float>(GetDpiForWindow(window)), ...)`（UINT→FLOAT 轉型），
  NR-015 單一 per-window DPI 來源。`src/app_host/main.cpp:2807-2812` —
  `WM_DPICHANGED` 分支套用新幾何（SetWindowPos）後，`if (g_render_target)` 守衛下以
  `GetDpiForWindow(window)`（此處已反映新螢幕 DPI）`SetDpi(dpi, dpi)`；註解與 NR-149
  互引。未重建 target、未動 hit-test／WM_SIZE／layout 常數。
- 驗證：Release（llvm-mingw/Ninja）build 成功；新 warnings 0（既有
  `main.cpp:1395 unused variable 'target_size'` 為 NR-120/NR-133 時代引入，stash 重編證實
  與本 item 無關）；CTest 31/31 全綠（數量不變）。
- grep 驗證：`rg -n "SetDpi"` 恰好兩個 call site（`main.cpp:515` 與 `main.cpp:2810`），
  皆以 `GetDpiForWindow` 為值來源。
- 交接：單一 DPI 環境下 `SetDpi(same, same)` 為 no-op，行為零變更；混合 DPI 下
  paint 與 hit-test 共用同一 DPI 來源（NR-015），分歧根源（NR-064 bug 類別沿 DPI 軸）
  已消除。後續 DPI 相關改動（如 NR-151 之後）請以 `GetDpiForWindow` 為唯一來源，勿再
  讀取 WM_DPICHANGED 的 lParam。
