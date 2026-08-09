# NR-098 — Catalog rebuild 在關閉與新世代前必須具備可控取消路徑

Phase 3 · Depends on: NR-049, NR-063, NR-090, NR-091, NR-092

- Source: `docs/design-spec.md` §9.2、§9.4、§NFR-003
- Origin: 2026-08-09 全 repo 稽核（`JoinRebuildThreads` 與三個 source enumerator 的 shutdown trace）
- Priority: HIGH（Shell extension 或大型／卡住目錄可能讓 UI 關閉與 refresh 卡住）

## Why

`StartRebuild` 每次先 `JoinRebuildThreads()`，`WM_DESTROY` 也直接 join 所有 scan workers。
目前 Start Menu、AppsFolder、UserFolder enumerator 沒有 cancellation token 或可控中止入口；
只要 `FindNextFileW`、Shell COM enumeration 或 extension call 長時間不返回，UI thread 就
會等待。現有 NR-049 解決了 detached thread 的 lifetime／UAF，但刻意留下的註解也承認：
若掃描慢到可見，下一步應改成 cancellable scan。

這違反 spec 對「關閉不得因等待 Shell extension 無限卡住」的要求，也會把一次 refresh
變成使用者感知的同步阻塞。

## Decisions already made — do not reopen

1. 保留 owned `std::thread` 與 immutable／generation result contract；不 detach、不強殺 thread。
2. 取消後不得提交 partial source snapshot；該 source 走既有 failure／保留舊結果語意。
3. 先使用最小 cooperative cancellation：在目錄遞迴與 Shell item iteration 的安全邊界
   檢查 stop；不可中斷的單次 OS call 必須被隔離在可控的 worker boundary，並記錄實際限制。
4. 新世代開始前先取消並等待上一代完成；取消不能讓舊結果繞過 generation guard。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4：

> 關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑。

`docs/design-spec.md` §9.2：

> Scan worker 只在啟動、目錄變更或手動刷新時存在。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp` — `StartRebuild`、`JoinRebuildThreads`、`WM_DESTROY`。
- `src/catalog/start_menu_catalog.{h,cpp}` — recursive Win32 enumeration。
- `src/catalog/appsfolder_catalog.{h,cpp}` — Shell AppsFolder enumeration。
- `src/catalog/user_folder_catalog.{h,cpp}` — configured-root recursion。
- `tests/integration/lifecycle_check.ps1` — real shutdown acceptance path。
- `tests/unit/*catalog*_test.cpp` — existing source success/failure contracts。

## Scope

1. 將取消狀態以最小 copyable／thread-safe token 傳到三個 enumerator 的 safe iteration
   邊界；取消時回傳明確的 cancelled/source-failure outcome，不提交 prefix entries。
2. `StartRebuild` 在新 generation 與 `WM_DESTROY` 使用同一套 cancel→join 順序，保留
   `g_rebuild_handoffs` 清理與既有 stale-generation 行為。
3. 新增一個可重現的 focused check：取消中的 directory／fake Shell iteration 不會提交
   partial snapshot，且 lifecycle shutdown 不會留下 joinable worker。

## Non-goals

- 不新增常駐 thread pool、timer、polling、detach 或 `TerminateThread`。
- 不改 source priority、dedup、snapshot merge 或 AppsFolder 10 分鐘政策。
- 不在本 item 解決 icon worker 的 queue 上限；那是 NR-099。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. cancellation test 證明 partial source result 不會進入 coordinator。
3. lifecycle check 在正常 rebuild／shutdown 下通過；code review 能指出每個 join 前的
   cancel signal 與每個 enumerator 的安全檢查點。

Manual：

4. 用含大型／受限目錄的 Release build，在 rebuild 中退出，程序於可接受時間內結束，
   不需等下一次完整掃描。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_lifecycle_check --output-on-failure
```

```powershell
rg -n "JoinRebuildThreads|cancel|Cancellation|EnumerateStartMenuCatalog|EnumerateAppsFolderCatalog|EnumerateUserFolderCatalog" src/app_host/main.cpp src/catalog tests
git diff --name-only
# expect: cancellation 變更集中於 host、三個 source 與 focused tests。
```

## 交接區

（實作者填寫：token／outcome 設計、每個 enumerator 的檢查點、partial-result 保護、lifecycle
結果、建置／CTest、偏差與不可中斷 OS call 的實測限制。）

### Token／outcome 設計

- 最小 cooperative token：`std::atomic<bool>* cancel`，三個公開 enumerator 各加一個
  `= nullptr` 的預設參數（nullptr ＝不取消），既有呼叫端零改動即可編譯。
  `src/app_host/main.cpp` 新增檔案範圍 `std::atomic<bool> g_rebuild_cancel{false};`
  （`g_rebuild_threads` 旁）；只由 UI thread 寫入（`StartRebuild` 與 `WM_DESTROY` 都在
  UI thread，且舊 thread join 完才 reset），除 atomic 本身外不需額外鎖。
- outcome：取消一律回傳 failure outcome，走既有 `source_ok = false` → worker
  `result->failed = !res.source_ok` → coordinator `ApplySourceFailure`（NR-063 既有路徑）
  保留舊 entries。partial prefix 永不提交。

### 每個 enumerator 的檢查點

- `start_menu_catalog`：
  - `EnumerateStartMenuCatalog`：function top（已取消立即 return false）、每個 root walk
    後若 `cancel` 已設 → `source_ok = false` 並 return（停在下一 root 之前）。
  - `EnumerateProgramsDirectory`：top（COM guard 之前）。
  - `EnumerateDirectoryRecursive`：function top（每層遞迴進入即檢查）＋ `do{...}while(FindNextFileW)`
    loop 每圈頂端（取消時 `failed = true; break`，不進 `ProcessFile`，不提交該 prefix）。
- `user_folder_catalog`：
  - `EnumerateUserFolderCatalog`：top＋每個 root walk 後若已取消 → `source_ok = false` 並
    return（停在下一 root 之前）。
  - `ScanDirectory`：top＋`do{...}while(FindNextFileW)` loop 每圈頂端（與 Start Menu 相同）。
- `appsfolder_catalog`：
  - `EnumerateAppsFolderCatalog`：top＋`for(;;) enumerator->Next()` loop 每圈頂端
    （取消時 `source_ok = false; break`；`enumerator->Release()` 仍會執行）。
- 遞迴 site 不需獨立檢查點：每次遞迴進入子目錄的 function top 檢查已涵蓋。

### Partial-result 保護

取消在 loop 頂端 break 前已先 `failed = true`，之後 `return !failed` 為 false，因此
`EnumerateProgramsDirectory`／`ScanDirectory` 回傳 false → 對應 enumerator 設
`source_ok = false` → worker 只轉送，coordinator 走 `ApplySourceFailure`（NR-065 條件式
clear pending 不動）。partial prefix 存在於未提交的 `result.entries`，永不進入 merged
snapshot。

### 測試

- `TestCancellationBeforeWalk`（`tests/unit/start_menu_catalog_test.cpp`）：fixture 含子目錄
  與 `.exe`/`.lnk`；`cancel` 預設 true → `EnumerateProgramsDirectory` 回傳 false 且 `out`
  保持空；同 fixture 以預設 nullptr 控制組 → 回傳 true 且 populate entries。
- `TestCancellationMidWalk`：40 子目錄 × 25 `.lnk` shortcut（共 1000 個，每個走過
  `ProcessFile` 的 Shell resolution，建立與走訪都夠慢，使 walk 可靠跨越多次 OS timeslice，
  輪詢才能觀察到 mid-flight；用純 `.exe` 會在單一 timeslice 內走完造成 flaky）。
  在 `std::thread` 上跑 `EnumerateProgramsDirectory(..., &cancel)`，主 thread 以
  `Sleep(0)`＋5 s deadline 輪詢 `out.size()`（test-only benign concurrent read，有註解說明）
  直到 > 0，`cancel.store(true)`、join，斷言回傳 false、`0 < out.size() < total`。
  兩者皆已註冊在 `wmain`。既有 Start Menu 測試全用預設 nullptr，未改動。
- UserFolder／AppsFolder 取消不另做 unit test（AppsFolder 需真實 Shell），僅 code review 驗證。

### Lifecycle 結果

- `ctest -R nimblerun_lifecycle_check`：Passed（正常 rebuild／shutdown 路徑不受影響）。
- `WM_DESTROY` 的 drain/clear（`kRebuildDoneMessage`、`g_handoff_mutex` registry）與 NR-077
  handoff 流程未動；stale-generation 由 coordinator generation guard 照舊丟棄。

### 建置／CTest

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`
  ＋ `cmake --build build --clean-first`：0 warning、0 error。
- `ctest --test-dir build --output-on-failure`：24/24 全綠（含 `start_menu_catalog` 與
  `lifecycle_check`）。

### 偏差

- **不可中斷 OS call 的實測限制**：單次 `FindNextFileW`、`IEnumShellItems::Next()` 或
  Shell extension call 一旦進入就必須等它返回；取消只在上述 safe check point 生效，因此
  單個極慢 call 仍會短暫阻塞 join，但下一個 check point 即停。這是刻意的最小
  cooperative 設計（Decision 3），非缺陷。
- `main.cpp` 的 NR-097 push_back 失敗（bad_alloc）例外路徑仍直接 `worker.join()` 單一
  fresh worker，未先設 cancel：該 worker 剛建立（尚未開始掃描或僅數微秒），且 StartRebuild
  迴圈後續 source 仍需新 worker，設 cancel 會誤殺同 generation 的其他 worker，故保留原樣。
- lambda 以 `&g_rebuild_cancel`「capture」在 C++ 中對 namespace-scope 變數不合法
  （無 automatic storage duration），改為直接引用全域（等價意圖，worker 讀同一 flag）。

