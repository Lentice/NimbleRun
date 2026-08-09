# NR-105 — Directory watcher 保留通知不可因無後續事件而永久沉默

Phase 2 · Catalog refresh

- Source: `docs/design-spec.md` §FR-008、§9.1、§NFR-002
- Origin: 2026-08-09 全 repo 稽核；沿 `CatalogWatcher::PostNotification` 到 `WatchLoop` 的失敗後時序追蹤
- Priority: MEDIUM（message queue 短暫壅塞後，目錄可能無限期停留在舊 snapshot）

## Why

NR-101 已讓 `PostMessageW` 失敗時保留 `Watch::pending_notify`，也已加入兩次有界重試。
但目前 `src/app_host/catalog_watcher.cpp` 的 recovery 只在 `WatchLoop` 下一輪、也就是
下一個 filesystem event／overflow／error cycle 進入 `ReadDirectoryChangesW` 前才執行：

1. `PostNotification`（目前約 `:28-50`）送不出去後保留 pending intent。
2. `WatchLoop`（目前約 `:59-64`）嘗試一次 recovery，接著阻塞在
   `ReadDirectoryChangesW`（目前約 `:66-74`）。
3. 如果 message queue 隨後恢復，但該目錄沒有任何新的變更，watcher 沒有被喚醒，
   pending intent 便永遠不會再送到 host。

因此 NR-101 交接區所寫的「下一個 event／overflow／error cycle 恢復」不是完整的
liveness contract；這是 NR-101 的後續修正，不是重開其「coalesced state、停止時不重送」決策。

## Decisions already made — do not reopen

1. 保留 normal change 與 full-rescan 的 coalescing；full-rescan 不得被降級。
2. Recovery 必須仍是 event-driven；不得以 1 Hz timer、busy loop 或無界 retry 掩蓋問題。
3. Invalid/destroyed HWND 與 `Stop()` 必須安靜停止，不得在 teardown 後重送。
4. 使用最小的既有 Win32 message／wait integration；不引入第三方 event library、polling
   thread 或第二個 watcher。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> Start Menu 與使用者資料夾有變更時，以 watcher 觸發 debounce；事件遺失或 buffer overflow 時做完整重掃。

`docs/design-spec.md` §9.1：

> `catalog_watcher`：目錄變更通知與 debounce；不得固定輪詢。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/catalog_watcher.cpp` — `PostNotification`、`WatchLoop`、`Stop`、`pending_notify`。
- `src/app_host/catalog_watcher.h` — `CatalogWatcher::Watch` 的 ownership、stop 與通知欄位。
- `src/app_host/main.cpp` — `kWatchChangedMessage`、`ScheduleDebouncedRebuild`、
  `MarkSourceFullRescan`、`WM_DESTROY`。
- `src/catalog/catalog_refresh.{h,cpp}` — pending／500 ms debounce／full-rescan semantics。
- `tests/unit/catalog_watcher_test.cpp`、`tests/unit/catalog_refresh_test.cpp` — 現有
  watcher lifetime 與 coalescing checks。
- `docs/work-items/NR-101-watcher-notification-delivery.md`、NR-074 — 不得破壞既有
  retention、episode 與 shutdown decisions。

## Scope

1. 補上「post 失敗後沒有任何後續 filesystem event」的 recovery path；normal change 與
   full-rescan 都必須最終抵達既有 host message path，或在 HWND 已失效時明確停止。
2. 保持 pending state 有界、可 coalesce，並檢查成功 post／post failure／queue 恢復／
   `Stop()` 的 ownership 與 race。
3. 新增一個 deterministic focused test 或 self-check：注入一次 post failure，刻意不產生
   後續檔案事件，證明 retained intent 仍能送達，並覆蓋 shutdown 不 hang。

## Non-goals

- 不改 coordinator 的 500 ms debounce、generation merge 或 source failure policy。
- 不改 directory enumeration、cache format、AppsFolder staleness policy。
- 不用固定 timer 或常駐 polling 取代 OS directory notification。

## Acceptance

1. 在 post 暫時失敗、之後 queue 恢復且目錄沒有新事件的情境，normal change 與
   full-rescan intent 都能被 host 收到一次以上必要的通知；不會永久卡在 pending。
2. full-rescan 仍主導 normal change，既有 NR-074 的 error episode 不變。
3. HWND teardown／`Stop()` 後無對失效視窗的重送、thread 可 join，且既有 watcher／refresh
   tests 全部通過。
4. Release build 無新增 warning；focused test 能在不依賴人工操作下失敗於此 regression。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_watcher|catalog_refresh" --output-on-failure
```

```powershell
Select-String -Path src/app_host/catalog_watcher.cpp -Pattern 'pending_notify|PostMessageW|ReadDirectoryChangesW|Stop'
git diff --name-only
# expect: 變更集中於 watcher、必要的 host/test seam 與本 item。
```

## Handoff

實作（2026-08-09）：

- **wake/retry integration**：`ReadDirectoryChangesW` 的 directory handle 改用
  `FILE_FLAG_OVERLAPPED`，每個 watcher thread 建立一個 completion event。沒有
  `pending_notify` 時以 `WaitForSingleObject(INFINITE)` 等待 filesystem completion；
  retained intent 存在時才以 1 s、2 s、4 s…最多 30 s 的條件式退避 timeout 喚醒
  `PostNotification`。這不是 idle timer 或 polling；成功 post／invalid HWND 清除
  pending 後立即回到無限 OS wait。`Stop()` 仍以 `CancelIoEx` 喚醒 overlapped read，
  再 join thread。

- **post outcome table**：
  - normal change：`pending_notify = 1`；full-rescan：提升為 `2`，不會被 normal 降級。
  - `PostMessageW` 成功：送出既有 `wParam`／`lParam`，清除 pending。
  - 暫時失敗：保留 coalesced intent；條件式 recovery wait 會在沒有 filesystem event
    時重試，queue 恢復後仍走既有 host message path。
  - invalid／destroyed HWND：清除 pending、安靜停止送訊；不在 teardown 後重送。
  - `Stop()`／`ERROR_OPERATION_ABORTED`：離開 loop；completion handle 關閉後由既有
    owner join，沒有 detached thread。

- **focused test**：`TestPendingNotificationRecoversWithoutEvent` 填滿 message-only
  window 的 thread queue，觸發一次檔案變更使 `PostMessageW` 失敗，等待 bounded retry
  完成後清空 queue；不再產生 filesystem event，仍收到 normal notification。既有
  `TestWatchStopsQuietly` 同時覆蓋 `Stop()` 後不再通知與 invalid HWND teardown 不 hang。
  測試 fixture 改用 CTest working directory，避免此環境 `%TEMP%` ACL 拒絕建立目錄。

- **驗證**：Release configure／build 通過，無新增 warning；
  `ctest --test-dir build -R catalog_watcher --output-on-failure`：1/1；
  `ctest --test-dir build -R "catalog_watcher|catalog_refresh" --output-on-failure`：
  2/2（提升權限重跑以允許既有 `%TEMP%` fixture）；
  `ctest --test-dir build --output-on-failure`：25/25 全綠。

- **未完成風險**：沒有另外製造 64 KiB directory-notification overflow 的獨立測試；
  full-rescan 使用同一個 retained/coalescing/recovery 狀態機，既有 overflow／error
  episode 來源邏輯未改。未做人工長時間實機 queue 壓力測試。
