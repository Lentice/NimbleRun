# NR-156 — main.cpp 生命週期 sequel：逾時 release 後的 g_diag 懸空指標＋設定套用後的舊 watch 索引錯位

Phase 2 · Robustness · Depends on: —（NR-146 的兩個邊角；同檔）

- Source: `AGENTS.md`（Prefer the smallest working change…）、NR-146（逾時 detach 的
  生命週期契約——本 item 補其呼叫端殘餘）
- Origin: 2026-08-10 第十四次稽核第 2 輪（正確性軸，兩件皆 LOW；g_diag 高信心、
  watch 索引中高信心）。主 Agent 已讀兩處驗證。
- Priority: **LOW**——都是窄窗口的邊角；各一行修法。

## Why

**（a）`g_diag` 指向 stack local，逾時 release 後 detached worker 可能解參考已銷毀的 log**
（`main.cpp:3219-3228` 一帶）：NR-146 讓逾時路徑 `release()` pipeline 保持 `this`
存活，但 detached worker 的 `on_exception_` 回呼（`rebuild_pipeline.cpp:129-137,
172-176`）最終解參考 `g_diag`——它指向 `wWinMain` 的 stack local
`DiagnosticLog diag`，`wWinMain` 返回後銷毀，指標未置空。detached worker 在
process 退出前的微秒級窗口內碰到 catch 路徑（`bad_alloc`／post 失敗）即懸空解參考。

**（b）設定套用後舊 watch 索引錯位**（`main.cpp:2401-2408` 與 `SetRoots`／
`SetWatchSources`）：`kWatchChangedMessage` 的 `w_param` 是 1-based 索引
（`SourceForIndex`）。設定變更 root 集合時，佇列中**舊 watch set** 發出的訊息在
`SetWatchSources` 換表後被分派——舊索引映射到新表的不同 source → 一次錯誤來源的
重建。窗口限於「設定改變 root 集合時佇列中已有 watcher 事件」。

## Decisions already made — do not reopen

1. （a）`wWinMain` 在 message loop 結束、pipeline 收尾之後 `g_diag = nullptr;`
   （一行＋註解引用 NR-146/本 item）。detached worker 在 process 退出前再碰
   `g_diag` 只會得到 null 回呼（on_exception_ 內建 null 檢查，確認過）。
2. （b）`SetWatchSources` 換表後、`SetRoots` 之前，drain 佇列中的
   `kWatchChangedMessage`（沿用 WM_DESTROY 的 `PeekMessageW` 排空形狀，
   `main.cpp:2821-2824`）；**不加** watch-generation 戳記（drain 已關閉窗口，
   戳記是為更複雜的競爭付費）。
3. 不為此加測試目標（兩處皆一行防護）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`：`:3200-3231`（`wWinMain` 收尾）、`:1262-1299`
  （`StartWatchers`／`SetWatchSources`）、`:2395-2410`（`kWatchChangedMessage`
  消費端）、`:2815-2824`（WM_DESTROY 的 drain 範本）。
- `src/app_host/rebuild_pipeline.cpp`：`:129-137,172-176`（`on_exception_` 呼叫端，
  確認 null 檢查）。

## Scope

1. `g_diag = nullptr` 於收尾（在 pipeline 收尾之後、其他解構之前，依現有順序）。
2. `SetWatchSources` 後 drain `kWatchChangedMessage`。
3. 驗證：Release build + CTest 全綠；正常路徑（無設定變更、無 hung）行為與先前
   完全一致。

## Non-goals

- 不重排 `wWinMain` 收尾順序、不加 watch-generation 戳記。
- 不為 hung 路徑的 detached worker 加其他保護（NR-146 已決策：release 即契約）。

## Acceptance

1. grep 驗證 `g_diag = nullptr` 存在於收尾；`SetWatchSources` 後有 drain。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_diag = nullptr|kWatchChangedMessage" src/app_host/main.cpp
# expect: g_diag = nullptr 在收尾一處；kWatchChangedMessage 的 drain 在 SetWatchSources 後
```

## Handoff（2026-08-10，NR-156 done）

實作（2026-08-10）：

- **（a）`g_diag` 收尾置空**：`wWinMain` 的 pipeline 收尾塊（NR-146 的 timeout
  leak／正常 reset）之後、`com.reset()` 之前，`main.cpp:3280` 新增
  `g_diag = nullptr;`（NR-156 註解引用 NR-146 契約）。置空點在 message loop 之後
  的**最後一處 UI 執行緒使用**（`g_diag->Write` 最晚出現於 loop 內 `main.cpp:3228`）
  之後，正常路徑無後續 `g_diag` 消費者。`on_exception_` 回呼
  （`main.cpp:3072-3074`）內建 `if (g_diag)` null 檢查（已逐點確認
  `rebuild_pipeline.cpp` 全數 `on_exception_()` 呼叫皆有 `if (on_exception_)` 守門，
  回呼本體再守 `g_diag`），detached worker 晚到的 catch 路徑因此變 no-op。
- **（b）舊 watch 索引 drain**：`StartWatchers` 簽名改為 `void StartWatchers(HWND
  window)`（兩呼叫端 `main.cpp:2442` 設定套用路徑與 `main.cpp:3147` 啟動路徑都傳
  `window`）。`SetWatchSources` 換表後、`g_watcher->SetRoots` 之前（`main.cpp:1303-1314`）
  依 WM_DESTROY 形狀以 `PeekMessageW` 排空 `kWatchChangedMessage`（`MSG leftover{}`
  + PM_REMOVE，同 `main.cpp:2831-2833` 的 icon drain）。drain 點與換表之間只可能由
  仍持舊 root 的 watcher thread post 訊息，故排空的恰是舊 set 的訊息；`SetRoots` 之後
  新訊息帶新表索引。未加 watch-generation 戳記（依決策 2）。
- **驗證**：Release Ninja llvm-mingw configure＋build 通過，零新增 warning（唯一
  warning 為既有 `main.cpp:1395` 未使用 `target_size`，stash 對照確認與本 item 無
  關，本 item 後行號位移至 1405）；`ctest --test-dir build --output-on-failure`：
  31/31 全綠（數量不變）。grep 驗證：`g_diag = nullptr` 於 `main.cpp:3280`；
  drain loop 於 `main.cpp:1310-1313`（`SetWatchSources` 後、`SetRoots` 前）。
- **未完成風險**：無。兩處皆一行級防護，正常路徑（無設定變更、無 hung）零行為變更。
  已知窄窗口：逾時路徑下 detached worker 在 `wWinMain` 已置空 `g_diag` 但 process
  退出前仍會碰到 null 回呼——該行為是 NR-146 已決策的契約（release 即契約）。
