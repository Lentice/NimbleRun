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

實作者需記錄每一個 setup failure 的 completion path、ownership table、stale generation／
shutdown 結果、focused test、build／CTest 與任何未完成的 OS-only 驗證。

