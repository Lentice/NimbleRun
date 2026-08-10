# NR-130 — 同 user 可用性 DoS 面：rebuild storm 無限流＋single-instance rendezvous 靜默退出

Phase 1 · Host hardening · Depends on: NR-002, NR-077, NR-110（皆 done）

- Source: `docs/design-spec.md` §11（不受信訊息）、§NFR-002；NR-077 的同 integrity 威脅模型先例
- Origin: 2026-08-10 第十三次全 repo 稽核（安全性軸）；主 Agent 已驗證 `main.cpp` 的 WM_APP
  處理與 single-instance 路徑
- Priority: **LOW**（同 integrity 威脅模型下的可用性問題；不跨完整性邊界、不 crash——NR-077
  已封 crash 向量；但 rebuild storm 與靜默不啟動都可用一行觸發，值得最小緩解）

## Why

同 user 可達的兩個可用性面（window class name `kWindowClass` 公開，任何同 desktop process
可 `FindWindowW` 後 `PostMessage`）：

1. **rebuild storm 無限驅動**：`kWatchChangedMessage`（`main.cpp:2944-2963`）的 full-rescan
   marker（`w_param=來源索引`、`l_param≠0`）不驗證 sender——任一 process 反覆 post 即可讓
   host 不斷 `MarkSourceFullRescan`＋啟動 rebuild（CPU＋磁碟持續消耗）。`kRefreshMessage`
   （`:2931-2942`）同形。NR-077 的 token registry 只保護帶 payload 指標的訊息
   （`kRebuildDoneMessage`／`kIconReadyMessage`）；無 payload 的命令訊息是同一家族裡被漏掉的
   一半。註：Win32 的 PostMessage 沒有 sender 認證，「偽造退出訊息（`kExitMessage` `:3101`）」
   與偽造 `WM_CLOSE` 同級、是 Windows 應用固有的，本 item 不試圖消除，只文件化。
2. **single-instance rendezvous 靜默退出**（`main.cpp:3517-3540`）：mutex（`Local\NimbleRun.
   SingleInstance`）與 event 名可預測。同 session process 先 `CreateMutexW` 佔住 → NimbleRun
   啟動拿到 `ERROR_ALREADY_EXISTS` → `FindWindowW` 找不到既有視窗 → 等 5 s event → 仍找不到
   → **`return 0` 靜默退出**（無任何提示）。常駐 launcher 無法啟動、使用者無回饋；同名 event
   被先建立且永不 set 也會造成固定 5 s 延遲。

## Decisions already made — do not reopen

1. **不重開 NR-077 的「不驗 PID」決策**（跨 integrity 判斷成本與誤報）；PostMessage sender
   認證在 Win32 不可行，接受「偽造退出訊息」與 `WM_CLOSE` 同級並文件化。
2. **rebuild storm 採限流而非驗證**：full-rescan marker 加最小間隔（如 1 s 內同源重複的
   full-rescan 標記併入既有 debounce），不新增 timer／執行緒（沿用 NR-074/NR-118 的既有
   debounce 形狀；限流只併「重複 full-rescan 標記」，真實檔案事件不受影響）。
3. **single-instance 逾時採「給使用者回饋」而非「以 primary 繼續啟動」**：逾時後找不到既有
   視窗時，顯示一次 `MessageBoxW`（說明 NimbleRun 似乎已在執行但視窗無法聯繫）再退出——
   **不**搶回 mutex 繼續啟動，避免重開 NR-110 的雙實例競賽。
4. 限流常數單一來源；不改 coordinator 的 `ShouldStartRebuild` 語意（NR-118 不動）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-002：

> 主執行緒阻塞於 GetMessage／MsgWaitForMultipleObjectsEx，沒有工作時不使用 busy loop；禁止固定小於 60 秒的 timer。

`docs/work-items/NR-110`（single-instance startup race）既有決策：

> Single-instance wake-up 不得有 HWND 建立前競態。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `kRefreshMessage`（`:2931-2942`）、`kWatchChangedMessage`
  （`:2944-2963`）、`kExitMessage`（`:3101-3105`）、single-instance（`:3517-3540`）、
  tray callback（`:2924-2929`）。
- `docs/work-items/NR-077-message-payload-token-registry.md`、NR-074、NR-110、NR-118 —
  既有決策與 debounce/限流先例。

## Scope

1. full-rescan marker 限流：host 端對同來源的 full-rescan 標記加最小間隔（建議 1 s，
   超間隔的直接併入既有 debounce 路徑）；`NotifySourceEvent` 的 normal-event 路徑與
   watcher 本體（`catalog_watcher.cpp`）一字不改。
2. single-instance：rendezvous 逾時且找不到既有視窗時，`MessageBoxW` 提示後再 `return 0`；
   訊息的英文文案與集中字串表（`dialog_strings` 或既有落點）一致。
3. 若限流邏輯可抽成純函式（如 `ShouldAcceptFullRescan(source, now_ms)`）則加 focused 測試；
   否則以 sanity grep＋lifecycle 覆蓋。
4. 文件化：spec §11 或 development.md 補一句「同 user process 可偽造 WM_APP 命令訊息，
   屬 Windows 固有模型；退出訊息等同 WM_CLOSE」。

## Non-goals

- 不驗證 sender（PID／token）；不改 `kExitMessage` 的既有處理語意。
- 不加 timer／執行緒／mutex；不改 debounce 的 500 ms 常數；不改 NR-118 的
  `ShouldStartRebuild` 守門。
- 不重開 NR-110 的啟動競賽決策；不試圖在逾時後成為 primary。

## Acceptance

1. 連續 post full-rescan 標記時，rebuild 啟動頻率被限流（實測記錄進交接區）；真實檔案事件
   行為不變（watcher 測試全綠）。
2. 佔住 mutex 的情境：NimbleRun 啟動後至多約 5 s 顯示提示訊息框再退出，不再靜默。
3. 正常 single-instance 路徑（wake-up、雙擊圖示）行為與現況相同（NR-110 語意不變）。
4. Release build 無新增 warning；完整 CTest 與 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|watcher|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "FullRescan|kWatchChangedMessage|kInstanceMutex|kStartupRendezvous" src/app_host
git diff --name-only
# expect: 只動 main.cpp（＋測試）與 docs；watcher 本體零變更。
```

## Handoff

### 限流形狀與間隔

- 純函式 `nimblerun::ShouldAcceptFullRescan(last_accepted_ms, now_ms)` 與常數
  `kFullRescanMinIntervalMs = 1000`（ms）、`kFullRescanNever = -1` 置於新檔
  `src/app_host/full_rescan_throttle.h`（沿用 NR-117 header-only seam 慣例）。
  語意：同來源距上次接受滿 1 s 才接受（= 立即 full-rescan）；未接受過一律接受；
  間隔內的重複標記不產生立即 rebuild。
- host 端 `main.cpp` 的 `kWatchChangedMessage` full-rescan branch（`:3021-3059`）：
  接受者照舊走 `MarkSourceFullRescan`＋`ShouldStartRebuild`（NR-118 語意未動），
  並以 `g_last_full_rescan_ms[source] = now` 蓋章；被限流者走
  `NotifySourceEvent(source, now)`＋`ScheduleDebouncedRebuild`（併入既有 500 ms
  debounce）。`kRefreshMessage`（Ctrl+R／tray）是使用者的顯式命令、無「來源」，
  不在限流範圍。
- 純函式已加 focused 測試 `tests/unit/full_rescan_throttle_test.cpp`（CTest
  `nimblerun_full_rescan_throttle_test`），涵蓋「未接受過即接受」「間隔內拒絕」
  「恰滿 1 s 接受」「間隔後接受」。
- 實測（直接編譯 `full_rescan_throttle.h` 模擬）：同源 10 s 內以 1000 標記/ms 的
  風暴（1000 萬筆）只接受 10 筆 → 立即 rebuild 頻率 1.000/s（恰為 1 s 上限）；
  單一 1 ms 內 50 筆 burst → 0 筆接受。

### 逾時提示文字與落點

- 新字串 `dialog_strings::kRendezvousTimeout`（`main.cpp:162-166`，集中字串表）：
  `L"NimbleRun appears to be already running, but its window could not be contacted."`
- `wWinMain` 的 `ERROR_ALREADY_EXISTS` branch（`:3628-3642`）：rendezvous 逾時且
  `FindWindowW` 仍找不到時 `MessageBoxW(nullptr, ..., MB_OK | MB_ICONWARNING)` 再
  `return 0`。不搶 mutex 續跑（NR-110 不重開）。

### 正常路徑不變的驗證

- 接受路徑與改動前的 full-rescan branch 行為逐一對照：`MarkSourceFullRescan` 不受
  `now` 影響、`ShouldStartRebuild(now)`／`DueSources(now)` 用同一個 `now`，接受時
  語意與舊碼完全一致。watcher 本體 `catalog_watcher.cpp` 零變更；`NotifySourceEvent`
  的 normal-event 路徑一字未動；500 ms debounce 常數（`kDebounceMs`／
  `ScheduleDebouncedRebuild` 的 `SetTimer(..., 500, ...)`）未動；`ShouldStartRebuild`
  語意未動（NR-118 守門保留）。`kExitMessage` 處理語意未改。

### 文件化句子

- `docs/design-spec.md` §11 末尾補一句：「同 user process 可偽造 `WM_APP` 命令訊息，
  屬 Windows 固有模型（`PostMessage` 無 sender 認證）；偽造退出訊息與偽造 `WM_CLOSE`
  同級，NimbleRun 不試圖消除，只對可被無限重複驅動的重掃描路徑限流（NR-130）。」

### build／CTest 證據

- Release（LLVM-MinGW＋Ninja）build 無新增 warning（-Wall -Wextra -Wpedantic）。
- 完整 CTest 27/27 通過（原 26＋新 `nimblerun_full_rescan_throttle_test`），含
  lifecycle_check、catalog_refresh、catalog_watcher。`git diff --name-only` 僅
  `src/app_host/main.cpp`、`tests/CMakeLists.txt`、`docs/design-spec.md`（＋新檔
  `full_rescan_throttle.h`、`full_rescan_throttle_test.cpp`）。

