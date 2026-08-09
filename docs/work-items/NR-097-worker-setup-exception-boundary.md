# NR-097 — 補完 worker setup／handoff 的例外邊界，不讓背景例外終止 process

Phase 3 · Depends on: NR-076, NR-077

- Source: `docs/design-spec.md` §11（worker exception boundary）／§NFR-003
- Origin: 2026-08-09 全 repo 稽核（NR-076 完成後重新追查 task 前後的配置與 handoff）
- Priority: HIGH（低記憶體或 thread／message 資源壓力下仍可能 `std::terminate`）

## Why

NR-076 已把 source enumeration／icon load 的主要任務本體包在 `catch (...)`，但目前仍有
例外可逃出 background entry point：

- `StartRebuild` lambda 的 `new RebuildResult` 在既有 `try` 之前；
- icon worker 的 `new IconResult` 在既有 `try` 之前；
- `g_rebuild_handoffs`／`g_icon_handoffs` 的 `unordered_map` 插入可能配置失敗，位於
  task catch 之外；
- `std::thread` 建立本身可能拋出，`CatalogWatcher::SetRoots` 也在已開 directory handle
  後直接建立 thread。

這些路徑不是 NR-076 已完成的「枚舉器拋例外」測試，而是 setup／ownership／delivery
邊界。從 thread function 逃逸仍會呼叫 `std::terminate`；若 setup 失敗只丟棄結果，還
可能讓 coordinator generation 或 icon pending key 永久等待。

## Decisions already made — do not reopen

1. 不重開 NR-076 的 task-body catch；本 item 只處理配置、thread 啟動、handoff 登記與
   delivery failure 的剩餘邊界。
2. 任何失敗都必須有既有語意的完成訊號：catalog source 走 `ApplySourceFailure`，icon
   request 清除 pending 並保留 fallback；不得以「讓 generation 永遠 in progress」代替。
3. `CatalogWatcher` 建 thread 失敗時必須關閉已開 handle 並保留其他 watch；不得讓部分
   `watches_` 進入不一致狀態。
4. 不用 detach、`TerminateThread` 或全域 catch 後靜默吞掉錯誤；診斷沿用現有出口。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> Worker 發生例外：捕捉邊界、記錄並丟棄該次結果；UI 不崩潰。

`docs/design-spec.md` §NFR-003：

> 任一 App 圖示失敗不得使 Catalog 建立失敗；任一損壞捷徑不得造成崩潰或卡住整體掃描。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

- `src/app_host/main.cpp` — `StartRebuild` lambda、`g_rebuild_handoffs`、
  `kRebuildDoneMessage`、`WM_DESTROY` cleanup。
- `src/icons/icon_worker.cpp`、`src/icons/icon_worker.h` — `Run`、`Start`、`Post`、
  `g_icon_handoffs`。
- `src/app_host/catalog_watcher.cpp`、`.h` — `SetRoots`、`WatchLoop`、handle ownership。
- `src/app_host/catalog_watcher.h`、`src/catalog/catalog_refresh.h` — source failure／generation
  contract。
- `tests/unit/icon_worker_test.cpp` — NR-076 fake provider exception seam。
- `docs/work-items/NR-076-worker-exception-boundary.md`、NR-077 — existing boundary and ownership。

## Scope

1. 讓 rebuild、icon、watcher 的 setup／handoff failure 都落在明確的 failure／cleanup
   path；既有成功路徑與 token registry 不改成 raw pointer message。
2. 確認 `RebuildResult`／`IconResult` 的 ownership 在配置、registry insert、PostMessage
   成功／失敗與 WM_DESTROY drain 的每一個分支恰好一次。
3. 在可注入的 icon worker seam 加一個 setup／handoff failure self-check；watcher／rebuild
   的 OS-only branch 以 code-level invariant grep 加上最小可測 failure path 覆蓋，不新增
   為了測試而存在的 production abstraction。

## Non-goals

- 不重寫 NR-076 已完成的 source／provider exception catch。
- 不改 token registry 的安全邊界，不把 `lParam` 當指標解參考。
- 不新增 retry loop、thread pool、第三方 exception framework 或 telemetry。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. setup／handoff 的 injected／self-check failure 不會終止 process，且每個 catalog source
   與 icon request 都能結束於成功或既有 failure/fallback 語意。
3. `CatalogWatcher::SetRoots` 在 thread 建立失敗的分支不洩漏 directory handle，其他
   watch 仍可停止。
4. code review 確認 background thread function 不再有未處理的 setup／handoff exception
   出口。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon_worker|lifecycle" --output-on-failure
```

```powershell
Select-String -Path src/app_host/main.cpp,src/icons/icon_worker.cpp,src/app_host/catalog_watcher.cpp -Pattern 'std::thread|new .*Result|g_.*handoffs|catch \(\.\.\.\)'
git diff --name-only
# expect: setup／ownership 變更只在上述 host／worker／watcher 與 focused tests。
```

## 交接區

實作（2026-08-09）：

### 每個 setup failure 的完成語意

| 失敗點 | 處理 | 完成語意 |
|---|---|---|
| `IconWorker::Run`：`new IconResult` 或 `encoded_key` 拋出（heap 耗盡） | catch 內 `result == nullptr` → 寫 log（`store_ != nullptr` 時 `WriteLog(L"icon-worker", L"exception")`）→ `continue` | 無結果可交付；pending key 保持設定、fallback 繼續顯示（heap-exhaustion edge，NR-076 語意不適用） |
| icon handoff registry insert（`g_icon_handoffs[...]`）拋出 bad_alloc | `delete result`、寫 log、`continue`（不 post） | 該 request 無結果；pending key 保持、fallback 顯示；process 存活、物件不洩漏 |
| `IconWorker::Start` 的 `std::thread` 建構拋出 | `stop_ = true`、寫 log、不 rethrow | worker 停用（optional）：後續 `Post()`／`PostFlush()` 因 `!thread_.joinable()` 全部 drop；icons 全部走 fallback；caller 不受影響 |
| `StartRebuild` lambda：`new RebuildResult` 或 generation/source 指派拋出 | catch 內 `result == nullptr` → `g_diag->Write(L"rebuild", L"exception")` → `return` | 該來源此 cycle 無回報（coordinator 視為該來源無結果；stuck-generation 完成機制屬 NR-100，本 item 不建） |
| rebuild handoff registry insert 拋出 bad_alloc | `delete result`、寫 log、`return`（不 post） | 該來源此 cycle 無回報；process 存活、物件不洩漏 |
| `g_rebuild_threads.push_back` 拋出 bad_alloc | 寫 log、`worker.join()`、`continue` 迴圈 | 該來源的 rebuild 被跳過（thread 已啟動過，join 後 local 可安全銷毀）；其他來源繼續；process 存活 |
| `StartRebuild` 的 `std::thread worker(...)` 建構拋出 `std::system_error` | 寫 log、`continue`（worker 未啟動，無可 join） | 該來源此 cycle 不啟動；其他來源繼續；process 存活 |
| `CatalogWatcher::SetRoots`：`shared->thread = std::thread(WatchLoop, shared)` 拋出 | 關閉已開的 `shared->directory`、`continue`（不加 watch） | 該 root 不監看（同 SkipInvalidRoot 語意），其他 watch 照常運作；不洩漏 handle |
| `watches_.push_back` 拋出 bad_alloc | `shared->stop = true`、`CancelIoEx`、join thread（若 joinable）、關閉 handle、`continue` | 該 watch 完整拆除、絕不留在 `watches_`、joinable `std::thread` 成員絕不被無 join 銷毀；其他 watch 照常運作 |

### `RebuildResult`／`IconResult` ownership table（allocate → insert → post-success → post-failure → WM_DESTROY-drain）

| 階段 | IconResult（icon worker） | RebuildResult（StartRebuild lambda） |
|---|---|---|
| allocate（`new`，now inside try） | worker 持有 raw ptr；失敗→log+continue（無物件） | lambda 持有 raw ptr；失敗→log+return（無物件） |
| registry insert（`[...] = unique_ptr(...)`，now guarded） | 成功→registry 持有；失敗→`delete result`+log+continue | 成功→registry 持有；失敗→`delete result`+log+return |
| post-success（`PostMessageW` 真） | registry 持有；UI 於 `kIconReadyMessage` 以 token 找 map → move 出 → erase（解構於 UI thread） | 同左，經 `kRebuildDoneMessage`；`ApplySourceResult`/`ApplySourceFailure` 後解構 |
| post-failure（`PostMessageW` 假） | erase token（`unique_ptr` 解構刪除物件）——NR-063 既有 leak guard，原樣保留 | 同左 |
| WM_DESTROY drain | workers 全 join 後：drain queue＋`g_icon_handoffs.clear()`（解構所有 in-flight） | `JoinRebuildThreads()` 後：drain queue＋`g_rebuild_handoffs.clear()` |

每個分支恰好釋放一次；post-failure 的 erase guard 未動（NR-097 只新增 insert 的 catch，不更動 erase）。

### 新增測試

`tests/unit/icon_worker_test.cpp` → **`TestFailedPostErasesHandoffAndLeaksNothing`**（註冊於 `wmain`，介於 `TestStopDropsQueueAndSilencesNewPosts` 之後）：
- 用既有 `FakeProvider` 的 `gate` event：Start worker → post 一筆 → 等 `entered` 為真（worker 在 provider 內阻塞）→ **在請求 in-flight 時 `DestroyWindow(window)`** → `SetEvent(gate)` 放行 → worker 對已死的 `target_` post 失敗。
- 斷言：process 不 crash（到達斷言即證）；`nimblerun::g_icon_handoffs` 在 `nimblerun::g_handoff_mutex` 保護下 5 秒內輪詢至 `empty()`（失敗 post 的 erase 已刪除物件）；無殘留 entry。
- 結尾照既有樣式 `CloseHandle(gate)`＋`worker.Stop()`＋`DestroyWindow(window)`。

### 建置與 CTest

- Release build 無新增警告（`-Wall -Wextra -Wpedantic`）。
- `ctest --test-dir build --output-on-failure`：**24/24 全綠**。
- `ctest --test-dir build -R "icon_worker|lifecycle" --output-on-failure`：**2/2 全綠**。

### sanity greps（輸出摘要）

`Select-String -Pattern 'std::thread|new .*Result|g_.*handoffs|catch \(\.\.\.\)'`（main.cpp / icon_worker.cpp / catalog_watcher.cpp）：

- 所有背景 entry point 的 setup 均已入 try：`new IconResult`（icon_worker.cpp:139）在 try 內；`new RebuildResult`（main.cpp:1303）在 try 內。
- 所有 handoff insert 均有 catch：icon_worker.cpp:201、main.cpp:1369。
- 所有 thread 建構點均 guarded：icon_worker.cpp:44、catalog_watcher.cpp:105；rebuild 的 `push_back` guarded（main.cpp:1389）。
- `catch (...)`: icon_worker.cpp 3 處（:45 Start、:172 task-body、:201 insert）；main.cpp 3 處（:1338 task-body、:1369 insert、:1389 push_back）；catalog_watcher.cpp 2 處（:106 thread、:116 push_back）。

### 偏差

- `RebuildResult*` 宣告用 bare `RebuildResult`（anonymous namespace 型別，非 `nimblerun::`）；首輪編譯因此報錯，已修正，非語意偏差。
- **controller 覆核時補上 `StartRebuild` 的 `std::thread worker(...)` 建構 guard**（main.cpp:1297-1297 一帶，`std::thread worker; try { worker = std::thread([...]) } catch { log; continue; }`）：TICKET「Why」已把 thread 建構列為逃逸點之一，且 icon worker Start()／watcher SetRoots() 的 thread 建構均已 guarded，為使「所有 thread 建構點均 guarded」一致而補；lambda body 縮排維持原樣（無 clang-format，避免 90 行機械性 re-indent 噪音），功能不受影響。
- 未改動：NR-076 task-body catch、NR-077 token registry（lParam 不 dereference）、PostMessageW-failure erase guard、coordinator／catalog_refresh／icon_store.h。未完成事項：無。

