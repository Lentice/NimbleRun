# NR-181 — 提交 tooltip 修正與 manifest，並補 InitCommonControlsEx（cell tooltip 顯示）

Phase 3 · Host lifecycle · Depends on: —（獨立；建議最先做，它把 HEAD 的失效功能修回可用）

- Source: `docs/design-spec.md` §4.8（現文 `design-spec.md:257`）／§4.9；`docs/work-items/NR-180-cell-tooltip-native.md`（交接區）
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 稽核報告 I-1／I-2／H-2；codex 報告 M1／M2）
- Priority: **CRITICAL**（HEAD 上 cell tooltip 功能完全失效，且送錯訊息構成越界讀；NR-180 已標 done 但其驗收只存在於未提交工作樹）

## Why

HEAD（`1ea6fff`）的 `src/ui/cell_tooltip.cpp` 在 `Show()` 裡送出 `TTM_SETTIPTEXTW`（定義為 `WM_USER + 52`）並把 `wchar_t*` 直接當 `LPARAM`。Windows SDK 中 `WM_USER + 52` 是 **`TTM_NEWTOOLRECTW`**（lParam 必須是 `TOOLINFOW*`，不存在 `TTM_SETTIPTEXTW` 這個訊息），因此：

1. comctl32 把名稱字串緩衝當 `TOOLINFOW` 解讀（先讀 `cbSize`＝名稱前 4 個 wchar、再往後讀 ~72 bytes）——短名稱（SSO，16 wchar 緩衝）→ **越界讀**，沒炸只是運氣。
2. tooltip 文字從未更新：`TTM_ADDTOOL` 在 `name_` 仍為空字串時送出，文字已被複製走 → **tooltip 永遠顯示空字串**。

工作樹已有一份未提交的修正（`TTM_UPDATETIPTEXTW`＝`WM_USER + 57` ＋ 完整 `TOOLINFOW`，以及 manifest 的 `Microsoft.Windows.Common-Controls` v6 相依），但它不在 HEAD：任何冷讀 clone 的 agent 會拿到一個 tooltip 壞掉的 HEAD，並讀到一份說它已完成的 tracker（AGENTS.md 禁止的狀態）。

此外全 repo 沒有 `InitCommonControlsEx` 呼叫。comctl32 v5.82 的 common control class 只在 `InitCommonControls*` 之後註冊，`CreateWindowExW(TOOLTIPS_CLASS, …)` 可能直接失敗且 `EnsureCreated` 靜默返回——即使 manifest 已載入 v6，MS 文件仍要求顯式初始化。NR-180 政策「原生封裝屬視窗層、不測試」的代價就在這 30 行：唯一沒測試的區塊就是唯一壞掉的區塊。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.8（現文 `design-spec.md:257`）：

> hover 中的格子若名稱被截斷，指標停留約 150 ms 後以 Windows 原生 tooltip 於格子下方顯示 cell tooltip（完整顯示名稱），下方空間不足時（如最後一列）顯示於上方；…

`AGENTS.md`：

> Anything a later session needs must live in the repository, not in a scratchpad handoff.

## Files to read and trace first

- `src/ui/cell_tooltip.{h,cpp}` — 工作樹 diff（`git diff` 即可見）＋ `Show()`／`EnsureCreated()`。
- `src/resources/NimbleRun.manifest` — 工作樹 diff。
- `src/app_host/main.cpp` — `wWinMain`（`InitCommonControlsEx` 需在建立任何 common control 前呼叫）、`g_cell_tooltip` 全域、`WM_DESTROY` 對 `HideCellTooltip` 的呼叫。
- `docs/work-items/NR-180-cell-tooltip-native.md` — 交接區（原生封裝的建議 API 與決策）。

## Scope

1. **提交**工作樹既有修正（不重寫、不重設計）：
   - `src/ui/cell_tooltip.cpp`：`TTM_SETTIPTEXTW`→`TTM_UPDATETIPTEXTW`（`WM_USER + 57`，MinGW 缺此常數所以 `#ifndef` 補定義），`Show()` 送出完整 `TOOLINFOW`。
   - `src/resources/NimbleRun.manifest`：加入 `Microsoft.Windows.Common-Controls` 6.0.0.0 相依。
2. **補 `InitCommonControlsEx`**：在 `wWinMain` 建立任何視窗前呼叫一次 `INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);`（約 3 行；與 manifest 一起，把「靠隱式註冊」這個假設消掉）。
3. **`CellTooltip` 收尾（Claude 報告 I-1 殘留，一行）**：`WM_DESTROY` 隱藏後把 `window_` 置空（`Hide()` 或 `EnsureCreated` 內的清理），避免面板銷毀後 dangling HWND 留著「只是目前沒人碰」的推理。不新增解構子以外的機制；若 `Show()` 的 `tool_owner_ != panel` 分支在 `EnsureCreated` 首行 `if (window_) return;` 下實際不做事，把條件簡化為 `if (!window_)`（一行）。
4. **提交訊息**：`NR-181: use TTM_UPDATETIPTEXTW, require comctl32 v6, init common controls`（或更簡短同義句）。**commit 是本 item 的一等交付物**。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-181／NR-180 列（NR-180 的驗收條件自此成立於 HEAD）。

## Non-goals

- 不改 tooltip 觸發時機、截斷 gate、幾何、`TTM_TRACKPOSITION`、隱藏點、150 ms timer——那些都正常。
- 不新增測試 target（視窗層政策沿用 NR-180）。
- 不動 `docs/work-items/NR-180-cell-tooltip-native.md`（歷史紀錄，不得編輯已完成 item 文件）。
- 不修 I-3/I-4（無界 join）——那是 NR-182/NR-184。

## Acceptance

- `git diff HEAD` 中 cell tooltip 訊息為 `TTM_UPDATETIPTEXTW`、manifest 含 v6 相依、`wWinMain` 含 `InitCommonControlsEx`；全部已 commit（`git status` 乾淨，除後續新 item 的檔案）。
- HEAD 上 tooltip 文字更新路徑不再把字串當 `TOOLINFOW*` 解讀（grep 確認 `TTM_SETTIPTEXTW` 零命中、`TTM_UPDATETIPTEXTW` 一命中於 cell_tooltip.cpp）。
- `rg "InitCommonControlsEx" src` 一命中。
- Release build 無 error／新增 warning；CTest 全綠。
- NR-180 驗收條件（tooltip 顯示完整名稱、原生外觀）自此可由後續人工驗證成立於已提交 HEAD。

## Agent checks

```powershell
git diff -- src/ui/cell_tooltip.cpp src/resources/NimbleRun.manifest
rg -n "TTM_SETTIPTEXTW|TTM_UPDATETIPTEXTW|InitCommonControlsEx" src
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git status --short
```

驗證：`TTM_SETTIPTEXTW` 零命中；`TTM_UPDATETIPTEXTW` 與 `InitCommonControlsEx` 各一命中；build 無 error／新增 warning；CTest 全 Passed；`git status --short` 顯示本次修改已 commit（只剩交接區文件更新）。

## 交接區

（實作者填寫：提交內容、InitCommonControlsEx 位置、window_ 清理、build／CTest 證據、commit hash）

- Start: 2026-08-12
- 提交（兩個 commit，工作樹原修正與本 item 補強各自獨立）：
  - `8dd2545` — `NR-181: use TTM_UPDATETIPTEXTW, require comctl32 v6, init common controls`：`src/ui/cell_tooltip.cpp`（`TTM_SETTIPTEXTW`→`TTM_UPDATETIPTEXTW`＝`WM_USER+57`，`#ifndef` 補定義因 MinGW 缺此常數；`Show()` 改送完整 `TOOLINFOW`）＋ `src/resources/NimbleRun.manifest`（`Microsoft.Windows.Common-Controls` v6.0.0.0 相依）。即工作樹既有未提交修正，原樣提交。
  - `05a9787` — `NR-181: init common controls in wWinMain, drop stale tooltip HWND`：`src/app_host/main.cpp`＋`src/ui/cell_tooltip.cpp`（見下兩點）。
- InitCommonControlsEx 位置：`wWinMain` 開頭、`SetProcessDpiAwarenessContext` 之後、任何視窗建立之前（`main.cpp:3079-3082`）：`INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);`。`#include <commctrl.h>` 已加至 main.cpp 系統標頭區。
- window_ 清理（I-1 殘留，Scope 3）：`EnsureCreated` 首行 guard 改為 `if (window_ && IsWindow(window_)) return;` 並在下方 `window_ = nullptr;` —— 面板 `WM_DESTROY` 銷毀常駐 tooltip（owned window 隨 owner 銷毀）後，stale HWND 在下次 `EnsureCreated` 即被丟棄重造，不再留下「只是目前沒人碰」的 dangling 推論。`Show()` 的 `tool_owner_ != panel` 分支經確認在 `if (window_) return;` 下確實不做事，簡化為 `if (!window_)`（一行）。未在 `Hide()` 置空：`Hide()` 於每次滑鼠離開／面板隱藏都會呼叫，置空會破壞 NR-180 決策 #9 的常駐控制項政策並在每次 hover 週期重建視窗。
- Agent checks（全部通過）：
  - `rg -n "TTM_SETTIPTEXTW|TTM_UPDATETIPTEXTW|InitCommonControlsEx" src`：`TTM_SETTIPTEXTW` 僅存在於註解（解釋 SDK 無此訊息，非程式碼使用）零程式碼命中；`TTM_UPDATETIPTEXTW` 一處 `SendMessageW` 呼叫（`cell_tooltip.cpp:143`，另有 `#ifndef` 補定義區塊）；`InitCommonControlsEx` 一命中（`main.cpp:3081`）。
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：done，無 error。
  - `cmake --build build`：成功，唯一 warning 為 `main.cpp:1518` `target_size` unused（HEAD 即存在的既有 warning，原 `:1517` 因本 item 新增 include 位移一行，NR-180 交接區已有記錄，非本 item 引入）。
  - `ctest --test-dir build --output-on-failure`：32/32 Passed（含 `nimblerun_cell_tooltip_test` #32）。
  - `git status --short`：本次修改已全部 commit（剩本文件與 `docs/work-items.md` 的交接更新）。
- 未完成／注意：無阻礙。tooltip 視覺行為（原生外觀、wrap、穿透）屬人工驗證，依 NR-180 政策不在此追蹤；NR-180 驗收條件自此成立於已提交 HEAD。
