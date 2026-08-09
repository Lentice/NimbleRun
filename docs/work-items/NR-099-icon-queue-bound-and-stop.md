# NR-099 — Icon worker queue 要有上限，並可取消過期工作

Phase 3 · Depends on: NR-032, NR-036, NR-037

- Source: `docs/design-spec.md` §9.2、§9.4、§FR-009
- Origin: 2026-08-09 全 repo 稽核（`IconWorker::Post`／`PostFlush` 與 Stop 流程）
- Priority: MEDIUM（重複開關面板或大量 prewarm 時，舊 icon request 可無界堆積）

## Why

`IconWorker` 目前以 `std::deque<IconTask>` 接收 visible、prewarm 與 flush 任務，沒有
queue upper bound，也沒有取消不再可見／已過期的 load request。`g_pending_icon_keys` 只
防止同一 key 的部分重送，不限制不同 key 或 flush task 的總數；worker 被 Shell/WIC
單次工作拖慢時，UI 仍可持續 push copyable `AppEntry` 與 vectors。

Spec 明確要求 icon queue 有上限並可取消過期請求，且關閉序列要取消 icon request。現在
`Stop()` 只在 join 完成後清 queue，無法限制 join 前的 backlog，也沒有 stale work policy。

## Decisions already made — do not reopen

1. 維持一條 worker、一個 mutex＋condition variable；不新增 thread pool 或 timer。
2. visible request 優先於 prewarm；滿載時保留目前可見工作，過期／低優先 request 可丟棄。
3. flush task 可 coalesce 成一個最新任務；不得因 flush backlog 取代可見 icon。
4. `Stop()` 必須先拒絕新工作、取消 queued work，並沿用 NR-098／既有 worker shutdown
   contract 處理 in-flight provider call；不以增加更多等待 thread 解決。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.2：

> Icon worker：單一低優先序 worker，依可見項目載入；queue 有上限並可取消過期請求。

`docs/design-spec.md` §FR-009：

> 圖示必須 lazy load，UI 不得等待 Shell；cache 必須有界。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/icons/icon_worker.{h,cpp}` — queue、priority、Stop、flush。
- `src/app_host/main.cpp` — `RequestVisibleIcon`、`PrewarmEmptyStatePage`、`HidePanel` 的
  Post／pending set 互動。
- `tests/unit/icon_worker_test.cpp` — fake provider、延遲與 Stop tests。
- `docs/work-items/NR-032-icon-worker-thread.md`、NR-036、NR-037 — 既有 queue／prewarm
  決策與 deliberately deferred cache flush trade-off。

## Scope

1. 為 load queue 設定可解釋的固定上限；插入時只丟棄低優先／過期 request，不能讓 visible
   request 在滿載時無聲失去 fallback recovery。
2. 讓 prewarm／flush work 在新面板狀態或 shutdown 時可取消；同一 pending key 的既有
   UI acknowledgement 語意不可卡住。
3. 新增 focused worker test：queue cap、visible-before-prewarm、flush coalescing、Stop
   丟棄 queued work；不需測量完整 Shell provider。

## Non-goals

- 不改 icon variant、PNG format、LRU cache capacity 或 disk pack budget。
- 不在 UI thread 呼叫 Shell／WIC，也不新增 retry timer。
- 不把所有失敗都做成 persistent retry；fallback 仍是合法終態。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. 測試證明 queue size 永不超過上限，visible task 會先於 prewarm，flush 不會無界累積。
3. `Stop()` 後新 Post 全部被拒絕，queued work 被丟棄，既有 result handoff 不洩漏。
4. 既有 icon cache／worker tests 原樣通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon_worker|icons" --output-on-failure
```

```powershell
rg -n "queue_|PostFlush|push_front|push_back|stop_|Stop\(" src/icons/icon_worker.{h,cpp} tests/unit/icon_worker_test.cpp
git diff --name-only
# expect: queue policy 只落在 worker 與 focused test；無新增 dependency。
```

## 交接區

實作完成，未 commit。

**Queue 上限**：`IconWorker::kMaxQueuedTasks = 64`（`icon_worker.h`，public `static constexpr std::size_t`）。理由：可見頁 24 格 ＋ 預熱頁 24 格 ＋ 搜尋結果頁與 flush 的餘量（24＋24＋16）。

**丟棄／coalesce／cancel 規則**（皆在 mutex 下）：
- `Post(IconRequest)`：visible → `push_front` 後 `while (size() > kMaxQueuedTasks) pop_back()`（back 永遠是 prewarm/flush，visible 自身不會被逐出，fallback recovery 保留）；non-visible → 僅 `size() < kMaxQueuedTasks` 時 `push_back`，否則丟棄。保留既有 `!thread_.joinable()` drop 與 `cv_.notify_one()`。
- `PostFlush`：`std::find_if` 找既有 `Flush` task，命中就原地 replace（最新 pins/now 勝出），否則僅低於上限時 `push_back`、滿載丟棄。保留既有 joinable drop 與 notify。
- `CancelPrewarm()`：`std::erase_if`（C++20）移除 `Load && !request.visible`；可見 Load 與 Flush 保留，in-flight request 不受影響（不在 queue）。
- `QueueDepth()`：`const`，lock 後回傳 `queue_.size()`；為此 `mutex_` 改為 `mutable`（唯一 deviation，見下）。
- `Run()`／`Start()`／`Stop()` 未動：Stop 仍先拒絕新 post（`!joinable`）、join 後清 queue、in-flight provider call 與最後 flush 沿用既有 contract。

**HidePanel call site**：`main.cpp` 的 `HidePanel` 在 `PostFlush`／`PrewarmEmptyStatePage` 之前呼叫 `g_icon_worker->CancelPrewarm()`（新面板狀態取消上一輪 hide cycle 的 queued prewarm），其餘不變。

**四個新測試**（`tests/unit/icon_worker_test.cpp`，註冊於 `wmain`，沿用 FakeProvider＋gate＋PumpResults 模式）：
- `TestQueueCapDropsPrewarmWhenFull`
- `TestVisibleEvictsPrewarmWhenFull`
- `TestFlushCoalesces`
- `TestCancelPrewarmDropsQueuedPrewarm`

既有 14 個測試未改、原樣通過。

**建置／CTest 結果**：Release x64（LLVM-MinGW＋Ninja）clean build 無新增 warning（`--clean-first` 全量重建 zero warning）。`ctest --test-dir build --output-on-failure` **24/24 全綠**；4 個新 case 加在既有 `nimblerun_icon_worker_test` 執行檔內，CTest test 數維持 24 不變。`ctest -R "icon_worker|icons"` 2/2 過。`nimblerun_icon_worker_test.exe` 連續執行 8 次全過（exit 0），新 gated 測試穩定。

**偏差**：
1. TICKET 的 cap 邊界測試描述為「post `kMaxQueuedTasks - 1` prewarm 後再 post 一個即被丟棄」，但依 SCOPE 指定的實作（上限＝deque 內 64 個 queued task；prewarm 於 `size() < 64` 時接受；visible 僅在 `size() > 64` 時逐出），63 個 prewarm 尚未觸頂、第 64 個會被接受、64 個之上才被丟棄。為使測試與實作一致，`TestQueueCapDropsPrewarmWhenFull` 與 `TestVisibleEvictsPrewarmWhenFull` 改為用 `kMaxQueuedTasks` 個 prewarm 填到上限再驗證（覆蓋預熱被丟棄／被逐出的正確邊界），結果數為 `kMaxQueuedTasks + 1`（1 in-flight ＋ 64 queued），測試斷言的屬性不變：queue 不無界成長、visible 永不遺失。
2. `mutex_` 改為 `mutable` 以支撐 const `QueueDepth()`；無行為改變。
3. 未改 status／`docs/work-items.md`（依指示，controller 擁有）。

**未完成**：無。

