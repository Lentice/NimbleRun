# NR-115 — Rebuild failure wake-up 失敗仍必須完成 generation

Phase 3 · Catalog refresh completion · Depends on: NR-100, NR-106

- Source: `docs/design-spec.md` §FR-008、§NFR-003、§11
- Origin: 2026-08-09 第十次全 repo audit；追蹤 `QueueRebuildSourceFailure` 的 record／wake-up 與 UI message loop
- Priority: IMPORTANT（failure record 可已寫入，但 wake-up message 失敗後 generation 沒有保證的 drain 事件）

## Why

NR-100／NR-106 已把 delivery／setup failure 轉成 UI-owned `g_rebuild_delivery_failures`，但目前
`src/app_host/main.cpp:1506-1520` 的 `QueueRebuildSourceFailure()` 在 vector append 成功後忽略
`PostMessageW(window, kRebuildDeliveryFailedMessage, 0, 0)` 的回傳值並直接返回。若 queue 已滿，
failure record 留在 mutex-protected vector，卻沒有 message 會進入 `case kRebuildDeliveryFailedMessage`
（約 `:2998-3016`）呼叫 `DrainRebuildDeliveryFailures()`。只有另一個成功送達的 rebuild result
（約 `:2988-2994`）才會順便 drain；若所有 sibling result／wake-up 都送失敗，
`CatalogRefreshCoordinator::received_` 永遠少一項，`IsRebuildInProgress()` 可永久為 true。

這會阻止 generation completion、完整 snapshot/cache refresh、AppsFolder on-demand 與 launch-failure
gate reset，違反「source failure 必須完成」的既有 contract。NR-100 handoff 曾假設「entries 仍在
vector 內等下次 drain」；新證據是 wake-up 本身失敗時沒有下一個事件，因此本 item 明確**覆寫 NR-100
與 NR-106 交接區的 best-effort wake-up 假設**，但不否定它們的 token ownership、UI coordinator
ownership 或 event-driven/no-retry 決策。不得只把 `PostMessageW` failure 寫 log 後當成完成。

## Decisions already made — do not reopen

1. 每個 generation/source 必須恰好以 `ApplySourceResult` 或 `ApplySourceFailure` 完成一次；stale
   generation 不得覆蓋新 snapshot，source failure 保留舊 entries。
2. Worker 不直接呼叫 coordinator；failure record 仍由 UI-owned completion path 消費，避免 data race。
3. Wake-up 必須是可靠的 event-driven UI signal（例如既有 message pump 可監聽的受控 signal）；不得
   加 1 Hz timer、busy loop、無界 retry、detach thread 或第二個 rebuild worker。
4. 保留 NR-077 token registry、NR-100 result cleanup、NR-106 setup-failure ownership 與 WM_DESTROY
   teardown semantics；只補「record 已入列但喚醒失敗」的完成保證。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-003：

> Catalog 重建可取消或具世代編號；舊工作完成後不得覆蓋較新的結果。

`docs/design-spec.md` §11：

> Worker 發生例外：捕捉邊界、記錄並丟棄該次結果；UI 不崩潰。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

## Files to read and trace first

- `src/app_host/main.cpp` — `StartRebuild`、`QueueRebuildSourceFailure`、`DrainRebuildDeliveryFailures`、
  `CompleteRebuildSourceFailure`、`kRebuildDoneMessage`、`kRebuildDeliveryFailedMessage`、message loop、
  `WM_DESTROY`／`JoinRebuildThreads`。
- `src/catalog/catalog_refresh.{h,cpp}` — `BeginGeneration`、`ApplySourceResult`、`ApplySourceFailure`、
  `GenerationComplete`、stale generation guard。
- `tests/unit/catalog_refresh_test.cpp` — NR-100／NR-106 coordinator self-checks and generation assertions。
- `tests/integration/lifecycle_check.ps1` — teardown, queue and window lifecycle coverage。
- `docs/work-items/NR-077-message-payload-token-registry.md`、NR-100、NR-106、NR-098 — preserve ownership,
  failure and cancellation decisions; do not edit completed item documents。

## Scope

1. Make a recorded failure wake-up reliable even when the normal `PostMessageW` returns false; integrate the
   smallest event-driven fallback with the existing UI message loop and lifecycle teardown.
2. Cover both paths: failure record appended successfully, and the direct generation/source fallback used
   when recording cannot allocate. Each source must reach the UI-owned coordinator exactly once.
3. Preserve stale-generation filtering, old source entries, sibling source completion, token cleanup, and
   shutdown ordering. Clear or consume any signal/record safely during `WM_DESTROY` after workers join.
4. Add one deterministic focused seam/self-check that forces the wake-up post to fail and proves the record
   is eventually drained, the generation completes, and no duplicate `ApplySourceFailure` occurs. If the OS
   message queue cannot be safely injected, keep the pure coordinator assertion plus the smallest host seam
   that validates the signal path rather than claiming OS coverage.

## Non-goals

- 不改 source enumeration、dedup、AppsFolder staleness、cache format、search、UI layout 或 launch policy。
- 不重新設計 NR-077 raw-token 防護，不把 raw pointer 放回 `lParam`，不把 coordinator pointer 交給 worker。
- 不新增 polling timer、busy wait、無界 retry、常駐 thread pool、detach 或 `TerminateThread`。
- 不把「目前沒有下一個 Windows message」當成可接受的 degraded state；完成保證必須來自可達的 event-driven signal。

## Acceptance

1. 當 result delivery 與 failure wake-up 的 `PostMessageW` 都失敗時，仍會在 UI-owned path drain
   failure record；每個 generation/source 恰好完成一次，`IsRebuildInProgress()` 最終回復 false。
2. Healthy sibling results 與 failed source 會在同一完整 generation publish；failed source 保留舊 entries，
   stale generation 不覆蓋較新的 snapshot。
3. Normal delivery、recorded failure、direct fallback、stale result 與 WM_DESTROY 的 registry／record／
   signal ownership 都無 leak、double free 或 use-after-free；不增加 idle polling／高頻 wake-up。
4. Focused failure-injection／coordinator test、lifecycle check、Release build 與完整 CTest 通過，無新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "QueueRebuildSourceFailure|DrainRebuildDeliveryFailures|PostMessageW|ApplySourceFailure|IsRebuildInProgress|WM_DESTROY" src/app_host/main.cpp tests
git diff --name-only
# expect: host／必要的 coordinator or test seam；不改 NR-077 token format 或 source modules。
```

## Handoff

實作者需記錄 wake-up failure 注入方式、UI signal／message-pump integration、每一 outcome 的 ownership
表、generation/source 去重、WM_DESTROY 行為、idle path 影響、build／CTest 與無法安全注入的 OS-only failure path。

實作（2026-08-09，single clean worker ＋ controller 修正一處順序）：

- **UI signal／message-pump integration**：新增檔案層級 manual-reset event `g_rebuild_failure_event`
  （`src/app_host/main.cpp:348` 附近）。`QueueRebuildSourceFailure` 的 recorded 分支改為
  `if (!PostMessageW(window, kRebuildDeliveryFailedMessage, 0, 0) && g_rebuild_failure_event) SetEvent(...)`
  ——wake-up message 失敗（queue 滿）時改由 event 可靠喚醒，不再依賴「留著等下次 drain」。
  主 message loop（`wWinMain`）由純 `GetMessageW` 改為 `MsgWaitForMultipleObjectsEx(1, &event, INFINITE,
  QS_ALLINPUT, MWMO_INPUTAVAILABLE)`：`WAIT_OBJECT_0`（event）分支做
  `DrainRebuildDeliveryFailures()`→完成時 `OnGenerationCompleteRefresh()`＋`InvalidateRect`，然後在
  `g_delivery_failure_mutex` 下**只有 vector 空才 `ResetEvent`**（level-triggered，racing SetEvent 不丟失）；
  其餘經 `GetMessageW` 原樣。event 在**第一次 StartRebuild 之前**建立、message loop 結束後
  （`CoUninitialize` 旁）關閉；`CreateEventW` 失敗（null）時 loop 完全退化為舊 `GetMessageW` 行為、
  `SetEvent` 有 null guard。`kRebuildDeliveryFailedMessage` case、`DrainRebuildDeliveryFailures`、
  `OnGenerationCompleteRefresh`、`WM_DESTROY` 皆未改。
- **wake-up failure 注入**：OS queue-full 無法安全注入；以純 coordinator 測試模擬「多個 source 的
  result＋wake-up 都失敗、UI 單次 drain」的完成語意。`catalog_refresh_test` 新增
  `TestFailureWakeupDrainCompletesGeneration`：seed 舊 entries → 新 generation 三 source → StartMenu
  健康送達 → 依 `DrainRebuildDeliveryFailures` 形狀一次套用 AppsFolder／UserFolder 兩筆 failure →
  斷言每筆 `ApplySourceFailure` 恰好套用一次、`GenerationComplete(gen)`、`!IsRebuildInProgress()`、
  healthy 新 entry 發佈、failed source 舊 entries 保留。
- **ownership table**：成功 delivery＝registry erase（唯一 owner）＋message case 消費；recorded failure＋
  wake-up OK＝vector（mutex）＋message case drain；recorded failure＋wake-up FAILED＝vector（mutex）＋
  SetEvent→loop branch 同一 drain；no-alloc fallback（append 拋）＝無 record，message 帶 gen/source 由
  `CompleteRebuildSourceFailure` 消費；WM_DESTROY＝`JoinRebuildThreads` 後無 worker 能 SetEvent／append，
  handle 在 loop 結束後關閉，無 use-after-close。generation/source 去重：每 source 每 generation 至多一筆
  record（NR-106），drain 以 swap 一次消費。
- **idle path 影響**：`INFINITE` wait 待機、無 polling、無 timer；queue 空且 event 未 signal 時完全阻塞。
  空 event（create 失敗）退化為舊 loop。lifecycle_check 以真實 exe 驗證新 loop 下視窗/message teardown。
- **build／CTest**：Release x64（LLVM-MinGW＋Ninja）無新增 warning；focused 2/2 綠（catalog_refresh、
  lifecycle）；完整 CTest 25/25 綠。
- **未涵蓋**：no-alloc fallback 的 direct post 也失敗（heap 耗盡＋queue 滿同時發生）時無 record 可 drain——
  NR-106 的 reserve 使該分支為 invariant fallback，列為已記錄殘餘。controller 修正：event 建立移回
  第一次 `StartRebuild` 之前（原 worker 放在其後，與註解意圖不符的 startup race）。commit：`<controller fills after commit>`
