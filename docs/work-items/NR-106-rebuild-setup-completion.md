# NR-106 — Rebuild setup／handoff failure 必須完成 generation

Phase 3 · Catalog refresh worker boundary

- Source: `docs/design-spec.md` §FR-008、§NFR-003、§11
- Origin: 2026-08-09 全 repo 稽核；沿 `BeginGeneration`、每 source worker 建立、handoff registry 到 coordinator completion 追蹤
- Priority: HIGH（少一個 source completion 就會讓 panel refresh、cache write 與後續 refresh gate 永遠等待）

## Why

`StartRebuild` 在 `BeginGeneration(sources)` 時把所有 source 的 `received_` 設為 false。
但目前只有成功送達 `kRebuildDoneMessage` 或 NR-100 已處理的 `PostMessageW` failure 才會
呼叫 coordinator 的 source completion。下列分支都直接跳出，沒有 `ApplySourceFailure` 或
等價的 UI-owned completion signal：

- `RebuildResult` 配置失敗（`src/app_host/main.cpp` `StartRebuild` lambda 的
  `result == nullptr` catch）。
- `g_rebuild_handoffs` insert 失敗（該 catch `delete result; return`）。
- `std::thread` 建構失敗（外層 catch `continue`）。
- `g_rebuild_threads.push_back` 失敗（join 新 thread 後 `continue`）。

NR-097 的交接區明確把「stuck-generation completion」留給 NR-100，但 NR-100 只涵蓋
「payload 已登記後，`PostMessageW` 失敗」；上述路徑有些甚至沒有 payload、沒有 queued
message，因此不會進入 NR-100。這個 item 以新 code-trace evidence 擴充 NR-097/NR-100
的 completion contract，不重開 token registry 的安全邊界。

## Decisions already made — do not reopen

1. 每個 generation 中的每個 source 必須恰好以 success 或 `ApplySourceFailure` 完成一次；
   不得以「該 source 沒回報」當作可接受的 degraded state。
2. Coordinator 仍由 UI thread 擁有；worker 不直接修改 snapshot 或呼叫未同步的 UI-owned
   方法。
3. 保留 NR-077 token registry、NR-098 cancellation、NR-100 的 delivery-failure cleanup。
4. Failure completion 使用既有 event/message handoff 或等價的 event-driven path；不加
   busy wait、無界 retry 或第二個 rebuild worker。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-003：

> Catalog 重建可取消或具世代編號；舊工作完成後不得覆蓋較新的結果。

`docs/design-spec.md` §11：

> Worker 發生例外：捕捉邊界、記錄並丟棄該次結果；UI 不崩潰。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `StartRebuild`、`JoinRebuildThreads`、`g_rebuild_handoffs`、
  `g_rebuild_delivery_failures`、`kRebuildDoneMessage`、`WM_DESTROY`。
- `src/catalog/catalog_refresh.{h,cpp}` — `BeginGeneration`、`ApplySourceResult`、
  `ApplySourceFailure`、`GenerationComplete`。
- `tests/unit/catalog_refresh_test.cpp` — generation／failure isolation tests。
- `tests/integration/lifecycle_check.ps1` — window teardown and message lifecycle。
- `docs/work-items/NR-097-worker-setup-exception-boundary.md`、NR-100、NR-077 — existing
  ownership and stale-generation decisions; do not duplicate or weaken them.

## Scope

1. Close every setup／handoff branch between `BeginGeneration` and source completion, including
   allocation, registry insertion, thread creation and thread-vector ownership failure.
2. Preserve old source entries on setup failure and allow other source results to complete the
   same generation; stale generations remain harmless.
3. Review registry token, result payload and failure-notification ownership so each is released
   exactly once on success, failure and `WM_DESTROY`.
4. Add one deterministic coordinator/handoff self-check that proves a setup failure completes a
   generation; cover OS-only allocation/thread errors with a code-level invariant if no safe
   injection exists.

## Non-goals

- 不重寫 source enumerator、dedup、cache format 或 AppsFolder staleness policy。
- 不把 coordinator pointer 交給 worker，不把 raw pointer 放回 `lParam`。
- 不新增 thread pool、detach、`TerminateThread` 或固定頻率 retry。

## Acceptance

1. No source remains `received_ == false` when its worker was never started, its result could not
   be allocated/registered, or its handoff could not be queued.
2. A setup failure preserves the previous source entries, lets healthy sources publish together,
   and eventually makes `IsRebuildInProgress()` false for the generation.
3. No background exception escapes the thread entry; no payload or registry token leaks or double
   frees during normal delivery, failed delivery or teardown.
4. Release build has no new warnings; focused generation and lifecycle checks plus the full CTest
   suite pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure
```

```powershell
Select-String -Path src/app_host/main.cpp -Pattern 'BeginGeneration|ApplySourceFailure|std::thread|g_rebuild_handoffs|g_rebuild_delivery_failures|catch \(\.\.\.\)'
git diff --name-only
# expect: completion/ownership 變更只在 host、必要 coordinator/test seam。
```

## Handoff

實作（2026-08-09，未 commit）：

### Setup failure completion paths

| 失敗點 | completion path | ownership／結果 |
|---|---|---|
| `Settings` snapshot、`g_rebuild_threads.reserve` 或 failure-vector reserve | `StartRebuild` 在 UI thread 對每個 source 呼叫 `CompleteRebuildSourceFailure` | 沒有 worker／payload；`ApplySourceFailure` 保留舊 source entries；最後一個 source 觸發既有 `OnGenerationCompleteRefresh` |
| `std::thread` 建構 | outer catch 在 UI thread 呼叫 `CompleteRebuildSourceFailure` | worker 未啟動、沒有 join／payload；其他 source 繼續 |
| worker `RebuildResult` 配置失敗 | worker 呼叫 `QueueRebuildSourceFailure`，寫入既有 `g_rebuild_delivery_failures`，再 post `kRebuildDeliveryFailedMessage` | 無 raw result；UI drain 以 `ApplySourceFailure` 完成該 source；worker 不持有 coordinator pointer |
| `g_rebuild_handoffs` insert 失敗 | worker 釋放未登記的 `RebuildResult`，再走同一 `QueueRebuildSourceFailure` | registry 沒有 owner；payload 恰好釋放一次；UI 完成 source failure |
| `g_rebuild_threads.push_back` 失敗 | join local worker；`DiscardPendingRebuildCompletion` 移除該 generation/source 尚未處理的 registry／delivery record；UI 呼叫 `CompleteRebuildSourceFailure` | queued token 之後只會被 receiver 當 unknown token 忽略，避免 success/failure double completion；reserve 使此分支成為 invariant fallback |
| 既有 `PostMessageW(kRebuildDoneMessage)` 失敗 | 保留 NR-100 的 registry erase，`QueueRebuildSourceFailure` 記錄並喚醒 UI | payload 由 registry erase 釋放一次；UI `ApplySourceFailure` 保留舊結果 |

`QueueRebuildSourceFailure` 的 reserved vector append 若仍發生 OS-only 例外，使用
`kRebuildDeliveryFailedMessage` 的 generation/source 整數欄位作無配置 fallback；receiver
先驗證 `CatalogSource` 範圍，再進入 UI-owned coordinator。

### Ownership / stale / shutdown

- 成功 handoff：registry 持有 `RebuildResult`，UI message move＋erase；結果由
  `ApplySourceResult` 或 `ApplySourceFailure` 消費後釋放。
- setup／delivery failure：沒有 coordinator pointer 交給 worker；未登記 raw result 由
  worker `delete`，已登記結果由 registry erase／clear 釋放；failure record 由 mutex
  保護的 vector swap／erase 消費。
- stale generation：`ApplySourceFailure`／`ApplySourceResult` 維持既有 generation guard，
  舊 failure 不覆蓋新 snapshot；`DiscardPendingRebuildCompletion` 只在 push-back
  fallback 的同 generation/source 上清理。
- shutdown：既有 `WM_DESTROY` stop→join→message drain→registry clear 未改；worker
  不 detach，所有在途 payload 仍由 registry 清理一次。

### Focused check / validation

- `tests/unit/catalog_refresh_test.cpp` 新增 `TestSetupFailureCompletesGeneration`：以既有
  coordinator seam 模擬「source 沒有 result／handoff／worker」，驗證 generation 完成、
  failed source 舊 entries 保留、healthy source 新 entries 一起發佈。
- Release configure：通過。
- `cmake --build build`：通過，無新增 warning。
- `ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure`：2/2 通過。
- 完整 CTest：依主 agent 指示在長時間無輸出時停止，尚未取得結果；未 commit、未 push。

### 未完成風險

無安全的 production seam 可直接注入 OS heap exhaustion、`std::thread` 建構失敗或
Windows queue failure；本次以 reserve invariant、UI failure path 與 deterministic
coordinator self-check 覆蓋。完整 CTest 請主 agent 接手重跑。
