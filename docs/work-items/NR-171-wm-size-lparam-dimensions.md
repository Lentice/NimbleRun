# NR-171 — WM_SIZE 直接採用偽造 lParam 尺寸呼叫 Resize，可要求 65535×65535 render target

Phase 3 · Host hardening · Depends on: —

- Source: `docs/design-spec.md` §11（不受信訊息）、NR-149 先例（HWND 層
  untrusted-input 邊界：WM_ 訊息參數不可信）
- Origin: 2026-08-11 第十六次稽核第 3 輪（codex backend，MINOR）。主 Agent
  已重讀 `main.cpp:2783-2789` 驗證。
- Priority: **LOW**——不 crash（lParam 是尺寸不是指標），但同 integrity process
  可 `SendMessageW(hwnd, WM_SIZE, 0, MAKELPARAM(65535, 65535))` 要求約
  4.29 billion pixels 的 D2D surface；`Resize` 的 HRESULT 被忽略，失敗後
  render target 狀態未檢查，後續繪製可能持續使用失敗的 target。

## Why

`main.cpp:2783-2789` 的 `WM_SIZE` 分支直接取 `LOWORD(l_param)`／`HIWORD(l_param)`
呼叫 `ID2D1HwndRenderTarget::Resize`，且不回傳值檢查。真實的 `WM_SIZE` 由系統
在視窗尺寸改變時送出（尺寸受 `ClampWindowSize` 工作區限制）；但任何同 integrity
process 都能 `SendMessageW`／`PostMessageW` 偽造該訊息（window class name 公開，
NR-130 已確認此訊息面）。偽造的 `65535 × 65535` 尺寸：

- 讓 D2D 嘗試配置約 17 GB 的 surface（4B pixels × 4 bytes），或依 driver 上限
  失敗——兩者都是資源壓力面；
- `Resize` 失敗的 HRESULT 被忽略，之後 `Render()` 持續使用可能已失效的
  render target（繪製失敗、device loss）。

這是 NR-149（WM_DPICHANGED lParam 不可信）同一 HWND untrusted-input 邊界的
尺寸資料版本：不該相信訊息帶來的任何參數，應以視窗實際狀態為準。

## Decisions already made — do not reopen

1. **忽略 `lParam` 尺寸，改以 `GetClientRect(window)` 取得實際 client size**
   再呼叫 `Resize`——系統送出的真 `WM_SIZE` 其 lParam 本就等於 client rect，
   行為不變；偽造訊息不再能指定尺寸。
2. **檢查 `Resize` 的 HRESULT**：失敗時記錄既有 sanitized diagnostic（沿用
   `on_exception_`／既有 render 錯誤處理形狀），不崩潰；render 的既有
   `D2DERR_RECREATE_TARGET` 復原路徑（NR-067 一帶）繼續負責 target 重建。
3. 不驗證 sender（NR-077／NR-130 決策不重開）；`UpdateViewportRows`／
   `RepositionSearchEdit` 照舊以實際視窗尺寸運作（它們已用 GetClientRect，
   不受此修改影響）。
4. 不新增測試（訊息層行為，依 NR-060 先例以 sanity grep＋手動驗收覆蓋）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11（不受信訊息）：

> 同 user process 可偽造 WM_APP 命令訊息… 只對可被無限重複驅動的路徑限流。

`docs/work-items/NR-149`（WM_DPICHANGED lParam 驗證）——同族先例：

> 該訊息以 SendMessageW 送出、lParam 跨處理序原樣傳遞…根本不解參考不可信指標。

## Files to read and trace first

- `src/app_host/main.cpp:2783-2789`（WM_SIZE 分支）。
- `main.cpp` 的 `Render()` 與 `D2DERR_RECREATE_TARGET` 復原路徑（NR-067 一帶，
  確認 Resize 失敗與既有復原機制的互動）。
- `src/ui/panel_layout.h`（`ClampWindowSize`——真視窗尺寸的既有守門）。

## Scope

1. `WM_SIZE` 分支改為 `GetClientRect(window)` 取尺寸，檢查 `Resize` HRESULT，
   失敗走既有診斷路徑。
2. 行為不變證明：真 `WM_SIZE`（系統送出）的 lParam 尺寸 == client rect，
   此修改對正常路徑零影響。

## Non-goals

- 不驗證 sender、不做 token、不加 timer。
- 不為 render target 加重建邏輯（既有 D2DERR_RECREATE_TARGET 路徑已存在）。
- 不加測試 seam（依 NR-060 先例，訊息層行為不為測試發明抽象）。

## Acceptance

1. `WM_SIZE` 分支不再解參考 lParam（code review 斷言）。
2. 既有渲染／DPI／viewport 測試全綠；Release build 零新增 warning；CTest 全綠
   （數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -A5 "case WM_SIZE" src/app_host/main.cpp
# expect: 使用 GetClientRect；Resize 的 HRESULT 被檢查
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

- 2026-08-11 完成（NR-171, commit `1ed4248`：`NR-171: size the render target
  from GetClientRect, not forged WM_SIZE lParam`）。
- 變更：`src/app_host/main.cpp:2783-2805` — `WM_SIZE` 分支不再解參考 lParam，
  改以 `GetClientRect(window)` 取實際 client size 呼叫
  `g_render_target->Resize`；`Resize` 的 HRESULT 以 `FAILED` 檢查，失敗時沿用
  既有診斷形狀寫入 `g_diag->Write(L"resize", L"error " + ...)`（同
  `open-location`／`properties` 模式），不崩潰。既有
  `UpdateViewportRows`／`RepositionSearchEdit` 順序不變。NR-171 註解說明
  lParam 尺寸不可信（同 integrity 可 `SendMessageW` 偽造 65535×65535）、
  真 WM_SIZE 的 lParam 本就等於 client rect 故正常路徑零影響。
- 驗證：Release（llvm-mingw/Ninja）build 成功；新 warnings 0（既有
  `main.cpp:1410 unused variable 'target_size'` 為 NR-120/NR-133 時代引入，與
  本 item 無關）；CTest 31/31 全綠（數量不變）。
- grep 驗證：`case WM_SIZE` 分支使用 `GetClientRect`，`Resize` HRESULT 被檢查；
  分支內無 `LOWORD(l_param)`／`HIWORD(l_param)`。
- 交接：無。未加 sender 驗證／token／timer／測試 seam（依 item Decisions 與
  NR-060 先例）；render target 重建仍由既有 `D2DERR_RECREATE_TARGET` 路徑
  （NR-067）負責。
