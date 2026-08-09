# NR-109 — Icon worker 的 store lifecycle／flush 不能逃出例外邊界

Phase 3 · Icon worker resilience

- Source: `docs/design-spec.md` §11、§NFR-003、§FR-009
- Origin: 2026-08-09 全 repo 稽核；重新追查 NR-097 完成後 `IconWorker::Run` 的每一個 `IconStore` call site
- Priority: HIGH（背景 thread 例外可直接 `std::terminate`，或讓 icon request 永遠留在 pending）

## Why

NR-097 已包住 `IconResult` allocation、provider task body 與 handoff registry insert，但
`src/icons/icon_worker.cpp::Run` 仍有未包住的 store 路徑：

- `store_->Open()` 在 `CoInitializeEx` 後、主 task loop 前直接呼叫（約 `:142-146`）；
- Flush task 的 `store_->Flush(...)` 在約 `:159-164` 沒有 exception boundary；
- queue drain flush（約 `:252-260`）與 shutdown final flush（約 `:263-271`）也直接呼叫。

`IconStore::Open` 的 header comment 宣稱 never throws，但實作包含 vector/string/map 配置、
diagnostic log 與 Win32 path 操作，並沒有 `noexcept` 或自身完整 catch。任一例外逃出
`std::thread` entry 都會終止 process，違反 §11。

另外，`new IconResult` 失敗或 `g_icon_handoffs` insert 失敗時目前直接 `continue`。host
已在 `RequestVisibleIcon`（`src/app_host/main.cpp` 約 `:1009-1017`）先加入
`g_pending_icon_keys`，而只有 `kIconReadyMessage` case（約 `:2806-2837`）會移除它；
因此沒有 result 的 setup failure 會讓 key 跨越多次 ShowPanel 永久 pending，fallback 會留著
但不再重試。

這是 NR-097 completion table 與實際 code 的落差；不重開其已完成的 task-body catch 或
token registry，而是補 store lifecycle 與「每個 UI request 都有終點」的剩餘契約。

## Decisions already made — do not reopen

1. Worker 失敗時 process 必須存活；store 是可重建 cache，失敗可降級為 fallback/no-cache。
2. 每個 visible icon request 必須以 bitmap、empty-result/fallback 或明確 drop-and-clear
   pending 結束；不得以永久 pending 代替 completion。
3. 保留一條常駐 icon worker、既有 queue cap、token registry 與 event-driven flush timing。
4. 不把 `IconStore` 的所有 allocation 假設成不會拋出；使用最小 catch／failure path，並
   讓 diagnostic failure 本身不再把 exception 帶出 worker entry。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> Worker 發生例外：捕捉邊界、記錄並丟棄該次結果；UI 不崩潰。

`docs/design-spec.md` §NFR-003：

> 任一 App 圖示失敗不得使 Catalog 建立失敗；任一損壞捷徑不得造成崩潰或卡住整體掃描。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/icons/icon_worker.{h,cpp}` — `Start`、`Run`、所有 `Open`／`Flush`、queue stop/flush。
- `src/icons/icon_store.{h,cpp}` — exception-capable allocations and state degradation semantics。
- `src/app_host/main.cpp` — `RequestVisibleIcon`、`g_pending_icon_keys`、`g_requested_icon_keys`、
  `kIconReadyMessage`、`WM_DESTROY`。
- `src/icons/png_codec.cpp`、`src/icons/shell_icon_provider.cpp` — existing provider/codec
  failure behavior used by the worker。
- `tests/unit/icon_worker_test.cpp`、`tests/unit/icon_store_test.cpp` — existing fake provider,
  handoff and store seams。
- `docs/work-items/NR-076-worker-exception-boundary.md`、NR-097、NR-099 — existing catch,
  ownership and queue decisions。

## Scope

1. Put store open, normal flush, queue-drain flush and shutdown flush behind a boundary that
   cannot escape `IconWorker::Run`; log through the existing sanitized diagnostic route and leave
   the cache in a safe disabled/read-only state as appropriate.
2. Make setup/allocation/handoff failure produce the smallest existing UI completion signal or
   clear the pending key through an explicit failure path; fallback remains visible and a later
   ShowPanel can retry.
3. Add a focused injectable self-check for store/open or flush failure and for a dropped icon
   request; verify the worker remains joinable/stoppable and pending state does not stick.

## Non-goals

- 不重寫 PNG codec、Shell provider、IconStore pack format 或 NR-077 token validation。
- 不新增 thread pool、retry timer、telemetry 或另一個 icon worker。
- 不把 low-memory OS injection 寫成 production-only abstraction；優先使用既有 test seam。

## Acceptance

1. An exception from `Open` or any `Flush` call cannot escape the worker thread or terminate the
   process; the worker either continues safely or stops with fallback behavior and a bounded log。
2. A visible request that cannot allocate/register/post still clears or acknowledges its pending
   key; the next panel show may retry it, and the fallback remains usable。
3. Existing successful cache hit/miss, provider failure, queue bound, shutdown and handoff tests
   pass unchanged。
4. Release build has no new warnings; the focused test catches both unhandled store lifecycle
   exceptions and permanent pending-key behavior。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon_worker|icon_store" --output-on-failure
```

```powershell
Select-String -Path src/icons/icon_worker.cpp -Pattern 'store_->Open|store_->Flush|catch \(\.\.\.\)|pending_puts_'
Select-String -Path src/app_host/main.cpp -Pattern 'g_pending_icon_keys|kIconReadyMessage|RequestVisibleIcon'
git diff --name-only
# expect: worker/host seam 與 focused tests；不改 catalog 或 UI layout。
```

## Handoff

實作者需記錄每個 store call site 的 catch／state policy、request completion table、failure
self-check、worker shutdown、build／CTest 與未能注入的 OS-only branch。

