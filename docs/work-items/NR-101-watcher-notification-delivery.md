# NR-101 — Directory watcher 的 PostMessage 失敗不得遺失 catalog 變更

Phase 2 · Depends on: NR-011, NR-063, NR-065, NR-074

- Source: `docs/design-spec.md` §FR-008／§9.1（watcher responsibility）／§NFR-003
- Origin: 2026-08-09 全 repo 稽核（`CatalogWatcher::WatchLoop` notification return-value trace）
- Priority: MEDIUM（queue 壓力或 window teardown race 時，檔案變更可能沒有任何 rebuild）

## Why

`CatalogWatcher::WatchLoop` 對 normal change、buffer overflow 與 error full-rescan 都呼叫
`PostMessageW`，但忽略回傳值。若 message queue 滿、window 已失效或 post 正好遇到 teardown：

- normal change 沒有進入 coordinator debounce；
- full-rescan marker 沒有進入 `MarkSourceFullRescan`；
- error episode 仍把 `reported` 設成 true，後續錯誤也不會再補送。

結果是 source 可以長期停留在舊 snapshot，直到下一個成功送達的 OS event、手動 refresh
或重啟。這不是 NR-074 所修的「每秒報錯」問題，而是一次通知遺失後沒有 recovery contract。

## Decisions already made — do not reopen

1. 仍維持 NR-074 的「每個 error episode 最多一個 full-rescan」與 event-driven idle path。
2. watcher 只負責有界 coalesced notification state；不直接擁有 coordinator、catalog 或 UI
   snapshot。
3. post failure 必須保留一個可恢復的 dirty/full-rescan intent，在安全且不 busy-loop 的
   時機交給現有 host message path。
4. window 確認已失效時要能安靜停止，不在 teardown 後無限重送。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> Start Menu 與使用者資料夾有變更時，以 watcher 觸發 debounce；事件遺失或 buffer overflow 時做完整重掃。

`docs/design-spec.md` §9.1：

> `catalog_watcher`：目錄變更通知與 debounce；不得固定輪詢。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/catalog_watcher.{h,cpp}` — `WatchLoop`、`SetRoots`、`Stop`。
- `src/app_host/main.cpp` — `kWatchChangedMessage`、`ScheduleDebouncedRebuild`、
  `MarkSourceFullRescan`。
- `src/catalog/catalog_refresh.{h,cpp}` — pending／debounce semantics。
- `tests/unit/catalog_refresh_test.cpp` — event coalescing／full rescan tests。
- `docs/work-items/NR-074-watcher-error-once-per-episode.md` — existing error episode decision。

## Scope

1. 檢查並處理三種 watcher notification 的 `PostMessageW` failure；normal 與 full-rescan
   的意圖都不可無聲消失。
2. 讓 recovery state 有界且可 coalesce；保留 NR-074 的 backoff，不以短週期 retry 取代
   正確的 event integration。
3. 新增 focused test／self-check 覆蓋 post success、post failure 後 dirty intent 保留、
   success 後 episode reset，以及 Stop/invalid window 不再送訊息。

## Non-goals

- 不改 coordinator 的 500 ms debounce、generation merge 或 source failure policy。
- 不引入 overlapped I/O、常駐 polling thread 或第三方 event library。
- 不把 watcher 改成直接掃描目錄；掃描仍由 host rebuild worker 負責。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. injected／self-check post failure 不會丟失 normal dirty 或 full-rescan intent。
3. 既有 NR-065／NR-074 事件 coalescing 與 error episode tests 原樣通過。
4. shutdown 後沒有對已銷毀 HWND 的重送或 busy loop。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_catalog_refresh_test --output-on-failure
```

```powershell
Select-String -Path src/app_host/catalog_watcher.cpp -Pattern 'PostMessageW|reported|ERROR_OPERATION_ABORTED|bytes_returned'
git diff --name-only
# expect: 變更集中於 watcher、必要的 host/test seam 與本 item。
```

## 交接區

實作（2026-08-09）：

**post failure 注入現實**：不可注入。`PostMessageW` 對活著的 window 只會在 queue 滿
（10,000 則）或 teardown race 時失敗，沒有可決定性強制失敗的測試 seam；對已銷毀的
HWND 雖然會失敗，但那走的是 `PostNotification` 開頭的 `IsWindow` 守門（return quietly），
不經過 retry 路徑。post-failure retention 由 `PostNotification`（`catalog_watcher.cpp`
kNotifyChange／kNotifyFullRescan 常數、coalesce、成功清除、失敗保留＋有界 retry）的
code inspection 覆蓋，比照 NR-097／NR-098 的 OS-only 路徑處理方式。

**dirty/full-rescan retained state**：`Watch::pending_notify`（`std::atomic<int>`）：
0＝無、1＝normal change、2＝full-rescan marker，full rescan 主導（`level > pending` 才
提升，正常 change 永不降級 full rescan）。只有 watcher thread 碰它。`PostNotification`
先 coalesce、再依 merged level 送 `lParam = (pending == 2) ? 1 : 0`；成功清 0，失敗保留
並做 `kPostRetries`（2）次 × `kPostRetrySleepMs`（250 ms）的有界 retry（非 1 Hz 迴圈），
用罄仍保留。`WatchLoop` 頂端（`ReadDirectoryChangesW` 之前）看到 `pending_notify != 0`
就再送一次，所以在下一個 event／overflow／error cycle 恢復，event-driven、無 timer。

**episode reset**：NR-074 的 `reported` 邏輯原樣保留——錯誤路徑 `!reported` 才送
full-rescan、之後 `reported = true`、`Sleep(1000)` 退避；`ok == TRUE` 路徑重置 false。
舊程式碼 error path 裡 `watch->window && IsWindow(...)` 的守門改由 `PostNotification`
內部承接（`!reported` 之後 `reported = true` 無條件設定，與舊行為一致：window 無效時
仍記為已報過）。

**Stop／invalid-window**：`Stop()` 照舊 `CancelIoEx`＋join，join 後 thread 不再送訊。
`PostNotification` 開頭 `!watch.window || !IsWindow(watch.window)` 即 return quietly，
teardown 後不重送、不 crash；`DestroyWindow` 後再觸發事件只會走這個守門。

**新測試**：`tests/unit/catalog_watcher_test.cpp`：
- `TestWatchDeliversChange`——temp dir＋message-only window，`SetRoots` 後建立檔案，
  泵到 wParam=1、lParam=0 的通知（5 s timeout）；`Stop()`＋`DestroyWindow`＋
  `fs::remove_all`。
- `TestWatchStopsQuietly`——(1) `Stop()` 後建立檔案，短窗確認不再有通知；(2)
  `DestroyWindow` 後再建檔案，`Stop()` 不 hang、不 crash（IsWindow 守門）。
額外一個 `ArmWatch` helper：`SetRoots` 非同步啟動 thread，單一檔案可能早於
`ReadDirectoryChangesW` arm 而漏掉，故持續建檔直到收到第一個通知，確認 watch live 再
做斷言（並順帶驗證 re-arm）。

**CTest**：24 → 25（新 `nimblerun_catalog_watcher_test`）。`ctest --test-dir build
--output-on-failure` 25/25 全綠；`ctest -R catalog_watcher` 連續 6 次全過（約 1.25–
1.31 s / 次）。

**建置**：Release x64（LLVM-MinGW + Ninja）無新增 warning。

**偏差**：
1. **測試註冊方式**：loop list 格式每個 entry 只有一個 source 檔，而 `catalog_watcher.cpp`
   只編進 NimbleRun 執行檔、不屬任何 library（未改 top-level CMakeLists.txt），故仿照
   `nimblerun_panel_model_test` exception block，把 `../src/app_host/catalog_watcher.cpp`
   直接編入 test target，註冊在 loop 之後使既有 CTest 編號不位移（新測試編號 25）。
2. 測試用 `WriteWatchFile` 命名，避免與 Win32 `WriteFile` macro 衝突。
3. `kPostRetries`＝2、`kPostRetrySleepMs`＝250 為本 item 選定的有界 retry 值（ticket 的
   "a couple of retries at ~250 ms each"）。

