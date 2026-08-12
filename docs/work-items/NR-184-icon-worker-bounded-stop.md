# NR-184 — IconWorker::Stop() 無界 join：Shell 圖示擷取卡住即關閉無限延遲

Phase 3 · Icon lifecycle · Depends on: NR-099（已 done，queue 停止語意的先例）；獨立於 NR-181/182/183

- Source: `docs/design-spec.md` §9.4（現文 `design-spec.md:731`）
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 報告 I-4；codex 報告 H2）
- Priority: **HIGH**（按下 Exit 後 process 可能永不退出）

## Why

`IconWorker::Stop()`（`src/icons/icon_worker.cpp:87-100`）：設 `stop_`、`cv_.notify_all()` 後直接 `thread_.join()`——**無 timeout**。`Stop()` 只從 `WM_DESTROY`（`main.cpp:3006` 一帶）與解構子呼叫。

worker 的迴圈體會呼叫 `provider_.Load()`（Shell 圖示擷取——第三方 shell extension 可任意慢）與 `store_->Flush()`（磁碟）。這兩者都無法被 `stop_` 中斷 → 結束被按下後 process 可能無限期不退。`kStopFlushMaxPending`（:20）只限制 pending 數量，不限制單次 Shell／I/O 的時間。

這與 §9.4「等待有界，超時即繼續退出」明文衝突；`RebuildPipeline` 的對應問題已由 NR-123／NR-182 有界化，icon worker 是同一型的最後一塊。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4（現文 `design-spec.md:731`）：

> 關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑。等待有界，超時即繼續退出。

## Files to read and trace first

- `src/icons/icon_worker.{h,cpp}` — `Stop()`（:87-100）、`Run()` 迴圈（確認哪幾段不可中斷）、`store_`／`provider_` 成員的生命週期。
- `src/app_host/main.cpp` — `WM_DESTROY` 對 `Stop()` 的呼叫（:3000-3008）與關機順序；NR-123 的 `g_rebuild_shutdown_timed_out`「刻意洩漏、不銷毀」先例（同形狀可直接照抄）。
- `src/icons/icon_store.{h,cpp}` — detach 後 `Flush()` 仍會碰 `store_`：逾時 detach 時 `IconStore` 的生命週期必須安全（與 NR-123 的 worker 捕獲清單論證同型）。
- `src/app_host/handoff_registry` 或 `g_icon_handoffs` — detach 後遲到的 icon 結果訊息（NR-077 token 語意）與 `g_icon_dropped_keys` 的清空時機。

## Scope

1. `Stop()` 加 bounded wait：`WaitForSingleObject(thread_.native_handle(), timeout)`，逾時值沿用既有常數風格（建議 5 s，與 `kJoinTimeoutMs` 同值）。
2. 逾時 → `detach()`（不可直接清 joinable thread）並保留既有「已停止」語意（後續 `Post()` 回 false drop 請求）；若逾時後清空 `queue_` 的既有行為與 detach 的 worker 衝突，確保 detach worker 不會碰已釋放的記憶體（worker 持有的 `store_`／`provider_` 都必須在 process 退出前有效——目前 `Stop()` 只從退出路徑呼叫，安全論證可沿 NR-123：detach 後 process 立即退出，OS 回收執行緒）。
3. **安全論證必須寫進交接區**：detach 後誰還活著、誰被 OS 回收、遲到訊息（icon result、dropped-key）如何被忽略（NR-077 token 語意）或無害。
4. 測試：沿用 NR-123 先例（sanity grep＋既有 `nimblerun_lifecycle_check`；hang 注入不可自動化）。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-184 列。

## Non-goals

- 不用 `TerminateThread`；不中斷已卡住的 Shell 擷取本體。
- 不改 `Post()`／queue／`kStopFlushMaxPending` 語意。
- 不重開 NR-099（queue bound 與 stale cancellation）的既有決策。
- 不動 rebuild worker 的路徑（NR-123／NR-182 已處理）。

## Acceptance

- 正常路徑：`Stop()` 行為與現況等價（暖機下近 no-op）。
- worker 卡死時：Exit 後最多約 5 s process 退出，不 crash。
- detach 後任何遲到的 icon 結果訊息不 crash（token registry 語意驗證）。
- Release build 無 error／新增 warning；CTest 全綠（含 lifecycle check）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "Stop\(|WaitForSingleObject|TerminateThread|detach" src/icons/icon_worker.*
```

驗證：build 無 error／新增 warning；CTest 全 Passed；`Stop()` 的 join 為 bounded；`TerminateThread` 零命中。

## 交接區

（實作者填寫：bounded-wait 形狀與逾時值、detach 後的生命週期安全論證（worker／store／provider／遲到訊息）、queue_ 清理時機、build／CTest 證據）

## 交接區（2026-08-12，實作完成）

### bounded-wait 形狀與逾時值

`IconWorker::Stop()`（`src/icons/icon_worker.cpp:93-124`）設 `stop_`＋`notify_all()` 後以
`WaitForSingleObject(thread_.native_handle(), kStopJoinTimeoutMs)` 做有界等待：

- **逾時值**：`constexpr DWORD kStopJoinTimeoutMs = 5000;`（`icon_worker.cpp:22`，5 秒，
  與 `RebuildPipeline::kJoinTimeoutMs = 5000`（`rebuild_pipeline.h:51`）同值；icons 模組
  不 include app_host，故以同值區域常數呈現，註解互相參照）。
- **形狀**：`WAIT_OBJECT_0`（worker 已自行退出）→ `thread_.join()`，與原本逐行等價；
  逾時／wait 失敗 → `thread_.detach()`（**不可**直接 `clear()` joinable thread——會
  `std::terminate`），最後再 `thread_ = std::thread()`。兩種分支都保留既有收尾：鎖下
  `queue_.clear()`＋重置 thread handle（`Stop()` 後 `Post()`／`PostFlush()` 因
  `!thread_.joinable()` 回 false drop 請求，語意不變）。`Stop()` 維持 `void`；無
  `TerminateThread`（對持 Shell／COM apartment 的執行緒強制終止可能 deadlock 全
  process，NR-123 決策 §3 同理由）。

### detach 後的生命週期安全論證

逾時 detach 只發生在退出路徑：`Stop()` 只被 `WM_DESTROY`（`main.cpp:3006-3008`）與
解構子（`icon_worker.cpp:73`）呼叫，而解構子呼叫時 thread 若不是 joinable 就是已退出
（WM_DESTROY 是唯一會逾時的路徑）。

- **誰還活著**：detach 的 worker 執行緒本身（由 OS 持有）與 `g_icon_handoffs`／
  `g_icon_dropped_keys`（`icon_worker.h` 的 inline 全域，存活到 process 結束，各持
  自己的 mutex）。worker 迴圈體觸碰的 `store_`／`provider_`／`this` 成員
  （`icon_store`、`shell_icon_provider`、`icon_worker` 皆為 `wWinMain` 的 stack local，
  `main.cpp:3297-3317`）在退出流程中保持有效直至 process 退出；WM_DESTROY 之後的
  message loop 退出、資源釋放、wWinMain 返回、ExitProcess 是同一條連續退出路徑
  （NR-123 交接區同型論證：「逾時放棄 join 後 process 立即退出，OS 回收執行緒」）。
- **誰被 OS 回收**：detach 的執行緒與其 COM apartment（`CoUninitialize` 或 process
  結束時 OS 回收）。NR-049 的 detach 問題只在非退出路徑成立，本路徑（WM_DESTROY
  逾時→退出）不符合該情境（NR-123 交接區同句）。
- **遲到訊息（NR-077 token 語意）**：detach 的 worker 若稍後才
  `PostMessageW(kIconReadyMessage, token)`，有兩道守門——視窗已銷毀則訊息直接丟棄；
  或 `kIconReadyMessage` handler（`main.cpp:2613-2626`）以 `g_icon_handoffs.Take(token)`
  查無此 token 即回 0 忽略（`WM_DESTROY:3045` 的 `Clear()` 已清空 registry，與晚到的
  insert 以 registry 內建 mutex 序列化）。最壞情況是 registry 在 process 退出時尚有
  殘留物件，OS 回收，無 crash。dropped-key 路徑同型：`g_icon_dropped_keys` 是 inline
  全域，晚到的 push 與 UI 的 `TakeIconDroppedKeys` 以 `g_icon_dropped_keys_mutex`
  序列化；UI 已不存在時只是無人消費，無 UAF。
- **殘留窗口（誠實註記）**：NR-146 對 rebuild pipeline 指出「detach 後執行緒比
  process 晚醒」的窗口窄但真實存在，當時以 heap 物件的 `release()`（deliberate leak）
  一行關閉。icon worker 的三個物件是 wWinMain 的 stack local，無法洩漏；關閉同型
  窗口需把三者改 heap allocate 並在逾時時洩漏（NR-146 形狀，改動點
  `main.cpp:3297-3317` 建構與收尾），超出本 item 的 Scope（item 明訂安全論證沿
  NR-123）。本 item 依 spec 採 NR-123 論證；若日後要收掉該窗口，另開 item。

### queue_ 清理時機

`queue_.clear()` 維持在等待（join 或 detach）**之後**、鎖下執行，與既有行為一致。
安全論證：`stop_` 設定後 worker 的 wait predicate（`stop_ || !queue_.empty()`）必然為
true，worker 不可能再 pop 任務；idle flush 檢查（`queue_.empty() && !stop_`）也因
`stop_` 短路為 false——因此 detach 後清空 queue_ 不與仍在跑的 worker 競爭（worker 只
在上鎖時碰 queue_，且 `stop_` 後永不讀其內容）。in-flight 任務早已移出佇列，不受
影響。

### 測試覆蓋方式

依 item Scope 4 的 NR-123 先例：sanity grep＋既有 `nimblerun_lifecycle_check`（tray
Exit 正常結束即驗證暖機路徑）；hang 注入不可自動化（item 明訂）。既有
`FakeProvider.gate`（`icon_worker_test.cpp:71`）是現成注入點；日後若要自動化「Stop()
逾時 detach」，需先解決「detach 執行緒完全退出」的可觀測性（NR-182 測試用
post-to-UI 的 finished event 當信號，icon worker 的 PostMessageW 不是執行緒最後一步）。

### build／CTest 證據

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` → configure 成功。
- `cmake --build build` → 成功，0 error；唯一 warning 是既有的 `main.cpp:1518`
  `target_size` unused（NR-146 已記錄為既有，stash 對照確認，非本 item 引入）。
- `ctest --test-dir build --output-on-failure` → **32/32 全綠**（含
  `nimblerun_lifecycle_check` 3.63 s、`nimblerun_icon_worker_test` 1.41 s、
  `nimblerun_rebuild_pipeline_test` 11.67 s）。
- sanity grep（`rg -n "Stop\(|WaitForSingleObject|TerminateThread|detach" src/icons/icon_worker.*`）：
  `Stop()` 定義 1＋呼叫 2（解構子、WM_DESTROY）；`WaitForSingleObject` 1（Stop 內，
  bounded）；`detach` 1（逾時分支）；`TerminateThread` **零命中**（僅註解提及）。

### 偏差

- 無。未動其他 src 檔案、未動 `docs/design-spec.md`（§9.4 條文已含 NR-123 的補句）。
- 變更檔案：`src/icons/icon_worker.cpp`（bounded Stop）、`src/icons/icon_worker.h`
  （Stop() 註解同步）。
