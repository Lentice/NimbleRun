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
