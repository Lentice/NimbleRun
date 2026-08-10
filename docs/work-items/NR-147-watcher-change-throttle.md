# NR-147 — watcher Change 路徑補限流：NR-130 只蓋了 FullRescan 的半邊

Phase 2 · Robustness · Depends on: —（NR-130 的續集；同一個限流家族）

- Source: `AGENTS.md`（Keep the idle path event-driven: no busy loops and no high-frequency
  timers）、NR-130（「同 user DoS 面」：full-rescan 限流＋single-instance 靜默退出——
  本 item 是它沒蓋到的另一半）、`docs/design-spec.md` §FR-008（watcher／debounce 行為）
- Origin: 2026-08-10 第十四次全 repo 稽核（正確性軸，LOW；medium confidence——agent
  逐行確認 debounce 再武裝行為後給出脈衝式觸發情境）。主 Agent 已讀
  `Request`／`OnDebounceTimer` 驗證機制。
- Priority: **LOW**——同 user DoS、無資料損壞；但它是 NR-130 明文接受的那個 DoS 面的
  未蓋半邊，補上後該家族才算收口。

## Why

`RebuildPipeline::Request`（`src/app_host/rebuild_pipeline.cpp:56-85`）：

- `FullRescan`（`:65-76`）：走 `AcceptFullRescan`（`:17-19`，per-source 1 秒 throttle）
  → `MarkSourceFullRescan`（kNever，隨時 due）→ `Start`。
- `Change`（`:77-79`）：直接 `NotifySourceEvent(source, now)` → 500 ms debounce 後
  `Start`（`OnDebounceTimer`，`:228-235`）。

`Change` 路徑**沒有速率上限**。debounce 只併入「連續不間斷」的事件流（每次事件重置
timer，`main.cpp:2391-2398` 的 `kWatchChangedMessage` → `Request(Change)`）；
但**脈衝式**事件（~600 ms 間隔）可以讓每個 debounce 週期都開一次單來源 rebuild
（spawn 執行緒 + 全目錄走訪 + dedup + 寫 catalog.cache）——持續 ~1.6 次/秒的 CPU／
磁碟 churn。任何同 user 程序都能 `PostMessageW(WM_APP+7, idx, 0)` 維持這個速率。
NR-130 接受 full-rescan 的解法是 1/s 限流；`Change` 是同一 DoS 面的未蓋半邊。

## Decisions already made — do not reopen

1. **同一把限流器**：把 `AcceptFullRescan` 泛化為 per-source 的「距上次重建啟動 ≥ 1 s」
   閘門，`Change` 與 `FullRescan` 共用。被限流的 `Change` 事件**仍然標記 pending**
   （事件不能丟——NR-065/105 的語意），只是不觸發立即 `Start`；`OnDebounceTimer` 的
   既有再檢查機制（`ShouldStartRebuild` 每 500 ms 一次）會在閘門放行後自然啟動。
   不得在 `Request` 的 `Change` 分支丟棄事件。
2. **改 `Start` 的入口守門還是 `Request` 的？** 一律放 `Request` 的 `Change` 分支
   （與 `FullRescan` 同位置），`Start` 本身不加閘（`Explicit` 與 `Start` 的直接呼叫端
   （`rebuild_pipeline_test`）不受影響）。
3. 合法使用者的行為不變：單一事件叢集 → 一次 rebuild（debounce 照舊）；每秒超過
   一次的分離重建才被閘住，而那種速率只有偽造訊息或病態檔案系統才有。
4. 限流狀態用現有 `last_full_rescan_ms_` map 的同一型別（或更名為
   `last_rebuild_start_ms_`，若實作時發現命名誤導）；不得新增第二份 map。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp`：`:13-21`（`AcceptFullRescan`）、`:56-85`（`Request`）、
  `:228-235`（`OnDebounceTimer`）。
- `src/app_host/main.cpp`：`:2391-2398`（`kWatchChangedMessage`）。
- `tests/unit/rebuild_pipeline_test.cpp`（既有 FullRescan throttle 測試的形狀，NR-130 產出）。

## Scope

1. per-source 閘門套到 `Change` 分支：被閘時 `NotifySourceEvent` 照常（pending 保留、
   debounce timer 由既有機制再武裝），但**不**因這次事件觸發立即 `Start`；
   閘門放行後由 `OnDebounceTimer` 的自然再檢查啟動。
   （實作細節以「同一把限流器、事件不丟」兩條決策為約束，形狀由 agent 依測試網決定。）
2. `tests/unit/rebuild_pipeline_test.cpp` 新增：脈衝式 `Request(Change, ...)`（間隔 < 1 s）
   → 每秒至多一次實際 rebuild 啟動；間隔 ≥ 1 s → 每次都會啟動；被閘事件在下一個
   debounce 週期仍會啟動（事件不丟）。既有 FullRescan throttle 測試保持綠燈。

## Non-goals

- 不改 debounce 常數（500 ms）、不改 `MarkSourceFullRescan` 語意。
- 不為偽造訊息加 sender 驗證（NR-139 的範圍與 same-user threat model 結論不變）。
- 不碰 watcher 執行緒本身的速率（那是檔案系統事件流，不是重建速率）。

## Acceptance

1. 新 throttle 測試通過；既有 `rebuild_pipeline_test` 全綠。
2. 合法行為不變：一次事件叢集一次 rebuild（既有 debounce 測試保持綠燈即證明）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "rebuild_pipeline" --output-on-failure
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff（2026-08-10）

- 實作內容（3 檔，~35+ / 8- 行）：
  - `src/app_host/rebuild_pipeline.h`：`AcceptFullRescan` 對應的常數改名為
    `kRebuildStartMinIntervalMs`／`kNoRebuildStart`；map 改名為
    `last_rebuild_start_ms_`（決策 4，改名語義為「距上次重建啟動」）。
  - `src/app_host/rebuild_pipeline.cpp:17-19`：`AcceptFullRescan` 改名為
    `AcceptRebuildStart`（同一 per-source ≥1 s 閘門，Change 與 FullRescan 共用）。
  - `rebuild_pipeline.cpp` `Request` 的 Change 分支（原 `:77-79`）：套同一閘門；
    被閘時仍呼叫 `NotifySourceEvent`（pending 保留、事件不丟），且底部的
    `schedule_debounce_` 照常武裝計時器。
  - `rebuild_pipeline.cpp` `OnDebounceTimer`（原 `:228-235`）：每次 tick 對
    due sources 逐 source 重查閘門，放行者才 `Start` 並記錄啟動時刻；被閘或
    rebuild 進行中則 `schedule_debounce_` 再武裝——被閘事件在下一個週期、閘門
    放行後自然啟動（不丟）。
- 決策覆核：決策 2「閘門在 `Request` 的 Change 分支、不在 `Start`」照辦
  （`Start` 與 `Explicit` 路徑零改動）；但 Change 事件經 `NotifySourceEvent`
  後永遠不會在 `Request` 內立即 due（debounce 剛重置），實際的啟動守門點在
  `OnDebounceTimer`——item 文件「`OnDebounceTimer` 的既有再檢查在閘門放行後
  啟動」一句即指此處，實作時補上再武裝以維持「不丟」語意。`ShouldStartRebuild`
  語意、500 ms debounce 常數、`MarkSourceFullRescan` 語意皆未動（non-goals）。
- 測試：`tests/unit/rebuild_pipeline_test.cpp` 新增 `TestChangeThrottle`
  （沿用 FullRescan throttle 測試的實時 sleep 模擬）：
  - 事件叢集 → 一次 rebuild（debounce 照舊）；
  - 上次啟動後 <1 s 的脈衝 Change → 閘門內 debounce tick 不啟動（≤1 次/秒）；
  - 閘門放行後（Sleep 1100）→ 被閘事件在下一個 debounce 週期啟動（不丟）；
  - ≥1 s 分隔的事件 → 各啟動一次。
- 驗證：Release Ninja llvm-mingw 建置零新增 warning（`main.cpp:1389` 的
  `target_size` unused warning 為既有，stash 對照確認）；CTest 31/31 全綠
  （數量不變）；`-R rebuild_pipeline` 單測通過（~4.2 s）。
