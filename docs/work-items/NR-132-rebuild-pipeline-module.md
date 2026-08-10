# NR-132 — Rebuild 編排抽成 RebuildPipeline 模組（九個全域＋五個啟動點＋四個完成點收斂為一個物件）

Phase 3 · Code structure · Depends on: NR-131（HandoffRegistry）、NR-100、NR-115、NR-116、NR-118、NR-123、NR-130（皆 done）

- Source: `AGENTS.md`（Keep the idle path event-driven；Keep search, ranking, scoring...
  independent of HWND and Shell COM objects where practical；New non-trivial logic needs one
  focused runnable test）、`docs/design-spec.md` §FR-003／§9.4／§11
- Origin: 2026-08-10 架構審查；Claude 軸與 Codex 軸**獨立得到同一個首選建議**（Claude 候選 1
  ＝ Codex 候選 01）。主 Agent 已逐處 grep 驗證全域清單與呼叫點。
- Priority: **IMPORTANT**（近 15 次修補中 7 次落在這段膠水碼：NR-100、NR-106、NR-115、
  NR-116、NR-118、NR-123、NR-130。旁邊的純值 coordinator 有 1000+ 行測試，這段膠水碼**零測試**）

## Why

`CatalogRefreshCoordinator`（`src/catalog/catalog_refresh.{h,cpp}`）是深、純、測試充分的模組——
而**bug 不在那裡**。bug 全部落在它周圍約 400 行的編排碼，那些碼持有九個檔案範圍全域：

```
g_rebuild_threads, g_rebuild_cancel, g_rebuild_handoffs,
g_delivery_failure_mutex, g_rebuild_delivery_failures,
g_rebuild_failure_event, g_watch_sources, g_last_full_rescan_ms,
g_catalog_cache_disable_writes
```

這些全域共同守的不變式只有一句：**一個 generation 內每個來源在 UI 執行緒上恰好完成一次，
generation 完成時恰好刷新一次面板與快取**。但執行它的地方散在
**五個啟動點**（`main.cpp:3137` WM_TIMER、`:3036`／`:3044` watcher marker、`:3880` 啟動、
設定套用、Ctrl+R）與**四個完成排空點**（`:3097` `kRebuildDoneMessage`、`:3104` 同 handler 內的
sibling drain、`:3126` `kRebuildDeliveryFailedMessage`、`:3901` `wWinMain` 的
`MsgWaitForMultipleObjectsEx` 分支）。

NR-118 的修法是把 `ShouldStartRebuild()` 加到**純值 class**（`catalog_refresh.h:77`），
因為 host 這一側沒有可測的落點；但呼叫端仍必須自己記得在 `:3040` 與 `:3138` 把它和
`ScheduleDebouncedRebuild` 配對——配錯正是 NR-118 修掉的那一類 bug。**seam 開在了容易開的地方，
不是複雜度所在的地方**：決策可測，決策的「使用」不可測。

`src/app_host/full_rescan_throttle.h` 是同一個問題的小版本：兩行 predicate 有自己的 header、
CMake target、測試，但它和 `main.cpp:3020-3052` 維護的 per-source `g_last_full_rescan_ms` map
不可分割，而 NR-130 的真正風險（stamping 順序、與 `ShouldStartRebuild` 的互動）在那張 map 上。

## Decisions already made — do not reopen

1. **抽出一個物件，不是好幾個小類**。整個要點就是同一個物件持有那條不變式；拆成
   「取消器／交接器／完成器」等於把現況的分散原樣搬家。
2. `RebuildPipeline` 放 `src/app_host/`，**編成一個 library 讓測試可連結**（現況這些碼只編進
   `.exe`，這是它零測試的機械原因）。
3. **注入兩個 seam** 讓測試可替換：
   - 「post 到 UI」callback（現為 `PostMessageW`）
   - 「枚舉一個來源」callback（現為三個 `Enumerate*Catalog`）
   其餘一律私有：取消旗標、交接註冊表（NR-131 的 `HandoffRegistry<RebuildResult>`）、
   delivery-failure vector、bounded join。HWND、Shell、COM 一律留在 pipeline 外或
   由注入的 callback 持有（`AGENTS.md` 的 no-HWND/no-Shell 核心規則）。
4. `full_rescan_throttle.h` 的常數與 predicate **併入本模組**（per-source timestamp map 本來
   就屬於這裡），其測試改為驅動 pipeline。`kFullRescanNever` sentinel 語意不變。
5. **行為零變更**。NR-100／NR-115／NR-116／NR-118／NR-123／NR-130 的每一條決策原封搬移，
   包含：delivery failure 必須讓 generation 仍然完成、首輪 source failure 保留該來源快取行、
   watcher/timer 不得取代冷啟動完整 rebuild、shutdown bounded join（5 s，逾時放棄，
   **不用 `TerminateThread`**）、full-rescan 限流。搬移時**必須把原註解連同 NR 編號一起帶走**。
6. 不改 `CatalogRefreshCoordinator` 的介面或語意。它已經是對的。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`AGENTS.md`：

> 必須保持既有 build／CTest 可用；不得用關閉測試來取得綠燈。

NR-123 的既有決策（不得反悔）：shutdown 路徑 bounded wait，逾時放棄 join，
**不使用 `TerminateThread`**（持 COM 鎖的執行緒上強制終止可能 deadlock 全 process）。

## Files to read and trace first

- `src/app_host/main.cpp`：`:271-372`（全域宣告區）、`:1474-1893`（`StartRebuild`／
  `JoinRebuildThreads`／worker body）、`:3020-3147`（watcher marker、WM_TIMER、
  `kRebuildDoneMessage`、`kRebuildDeliveryFailedMessage` 四個 handler）、
  `:3884-3926`（`wWinMain` 的啟動 rebuild 與 `MsgWaitForMultipleObjectsEx` 分支）。
- `src/catalog/catalog_refresh.{h,cpp}` 全部，特別是 `ShouldStartRebuild`（`:77` 一帶）。
- `src/app_host/full_rescan_throttle.h`、`tests/unit/full_rescan_throttle_test.cpp`。
- `src/app_host/catalog_watcher.{h,cpp}`。
- `docs/work-items/NR-100/NR-115/NR-116/NR-118/NR-123/NR-130` 六份文件的 Decisions 與交接區。
- `tests/CMakeLists.txt`（NR-055 的 list-plus-loop；`:41-44` 的 CTest 編號警告）。

## Scope

1. 新增 `src/app_host/rebuild_pipeline.{h,cpp}` 並在 `src/CMakeLists.txt` 建成 library。
   介面約六個成員（實作時可依實情調整，但**不得為了容納呼叫端而放大**）：
   - `Request(sources, reason)` — 內含 `ShouldStartRebuild` 守門與 debounce 決策的配對
   - `OnResultMessage(wparam, lparam)`
   - `OnDeliveryFailureMessage(...)`
   - `OnDebounceTimer()`
   - `DrainPending()` — 涵蓋 `wWinMain` 的 `MsgWaitForMultipleObjectsEx` 分支
   - `Shutdown(timeout)` — NR-123 的 bounded join
2. 九個全域搬進 pipeline 成為私有狀態；`main.cpp` 只留一個 pipeline 實例。
   四個 message handler 縮成一行轉呼叫。
3. `full_rescan_throttle.h` 併入 pipeline（常數、predicate、per-source map），刪除該 header、
   其 CMake target 與獨立測試；測試改寫為驅動 pipeline 的限流案例。
   **CTest 編號**：`tests/CMakeLists.txt:41-44` 警告編號不得位移，移除項放在清單末端，
   或同步更新 `docs/testing.md` 的測試數。
4. **watcher 索引 ↔ 來源對照表**（2026-08-10 架構審查第二輪加入本 item，因為
   `g_watch_sources` 本來就在上述九個全域裡）：`WatchIndexToSource`（`main.cpp:1857-1862`）
   對超出範圍的索引**靜默回傳 `StartMenu`**，而索引與來源的對齊只靠 `StartWatchers`
   （`:1867-1909`）的加入順序與一行註解維持。搬進 pipeline 時改為一張明確的
   `{path, recursive, source}` 表，`SourceForIndex(i)` 對未知索引回傳明確的「無來源」
   而非預設 `StartMenu`（呼叫端忽略該事件），並補一個 root-list → source-map 的測試。
   若某個 root 加入失敗，對齊必須仍然正確。
5. 新增 `tests/unit/rebuild_pipeline_test.cpp`。**必測案例**（每一條都對應一個已修過的 bug）：
   - 來源 B 的 `PostMessageW` 失敗、來源 A 成功時，generation 仍然完成一次（NR-100／NR-115）
   - 同一 generation 的完成回呼恰好觸發一次面板／快取刷新（不多不少）
   - watcher／timer 的部分 rebuild 在冷啟動完整 rebuild 在途時不得取代它（NR-118）
   - 首輪某來源失敗時保留該來源的快取行（NR-116，若語意在 coordinator 內則驗證 pipeline
     確實把該路徑接對）
   - full-rescan 限流：在限流窗內的第二次事件不啟動 rebuild（NR-130）
   - `Shutdown` 在 worker 不回應時於逾時後返回，不呼叫 `TerminateThread`（NR-123）

## Non-goals

- 不改任何使用者可見行為、不改訊息常數、不改 debounce 時間（500 ms）或限流窗（1000 ms）。
- 不動 `CatalogRefreshCoordinator`、不動枚舉器、不動 `catalog_cache` 格式。
- **不順手拆 `main.cpp` 的其他部分**（`WindowProc`、`Render`、`wWinMain` 的建構順序都是別的 item）。
- 不引入執行緒池、不改 worker 執行緒模型、不加高頻 timer。
- 不重開「搜尋太慢」等 §已否決的方向（與本 item 無關）。

## Acceptance

1. 九個全域在 `main.cpp` 歸零（grep 驗證），`main.cpp` 行數明顯下降。
2. 四個 message handler 各為一行轉呼叫。
3. `rebuild_pipeline_test` 的六類案例全部存在且通過。
4. 行為零變更：冷啟動、Ctrl+R、設定套用、watcher 觸發、關閉四條路徑手動確認一致
   （lifecycle check 通過即算 Agent 側證據）。
5. Release build 零新增 warning；完整 CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_rebuild_threads|g_rebuild_cancel|g_rebuild_handoffs|g_delivery_failure_mutex|g_rebuild_delivery_failures|g_rebuild_failure_event|g_watch_sources|g_last_full_rescan_ms|g_catalog_cache_disable_writes" src/app_host/main.cpp
# expect: 零命中（全部搬入 rebuild_pipeline）。
rg -n "full_rescan_throttle" src tests
# expect: 零命中（已併入 pipeline）。
```

## Handoff

交接區需記錄：最終介面與每個成員涵蓋的原呼叫點對照表、九個全域的搬移落點、
六類測試的實際斷言、註解（含 NR 編號）搬移確認、CTest 編號處理方式、build／CTest 證據。

### 交接區（2026-08-10）

- **介面與 caller 對照**：`Request` 收斂 Ctrl+R、設定套用、啟動、AppsFolder on-demand、watcher
  marker 與 normal event；`OnResultMessage` 收斂 `kRebuildDoneMessage`；
  `OnDeliveryFailureMessage` 收斂 `kRebuildDeliveryFailedMessage`；`OnDebounceTimer` 收斂 WM_TIMER；
  `DrainPending` 收斂 failure-event message loop 分支；`Shutdown` 收斂 WM_DESTROY 與換代 join。
- **搬移落點**：`rebuild_pipeline.cpp` 私有持有 worker vector／cancel、`HandoffRegistry<RebuildResult>`、
  failure mutex/vector/event、watch-index source table、full-rescan timestamp map、cache-write guard；
  `main.cpp` 只持有一個 `g_rebuild_pipeline`，不再持有九個 rebuild 全域。
- **seam**：post callback、source enumeration callback、settings snapshot、completion/repaint/debounce
  callbacks 均由建構子注入；pipeline 核心不持有 HWND、Shell 或 COM pointer。
- **測試斷言**：`rebuild_pipeline_test` 覆蓋 delivery failure 仍完成 generation 且 completion 一次、
  首輪 AppsFolder failure 保留 cache row、完成一代後 1000ms full-rescan 限流、明確 watcher index
  mapping／未知 index 忽略，以及 shutdown bounded wait；detached worker 以完成事件同步後才釋放
  pipeline。`user_folder_catalog_test` 的 source-sanity 也改讀搬移後的 pipeline 實作。
- **註解與既有決策**：`rebuild_pipeline.cpp` 保留 NR-097 setup exception、NR-098 cooperative cancel、
  NR-100 delivery failure、NR-115 event wake-up、NR-123 bounded wait／不使用 `TerminateThread` 的註解；
  NR-116 的 source-retention 語意仍由 coordinator 原樣承接。
- **CTest 編號**：移除 NR-130 獨立 header-only test；新增 pipeline test 置於 `tests/CMakeLists.txt`
  既有註冊尾端，未移動原有 list-plus-loop 或 exception target 的位置。
- **驗證**：Release build 成功；focused `nimblerun_rebuild_pipeline_test` 與
  `nimblerun_user_folder_catalog_test` 通過；完整 CTest **28/28 通過**。移除
  `full_rescan_throttle_test` 後 suite count 已由 `docs/testing.md` 的 26 更新為 28，
  pipeline test 置於註冊尾端。
