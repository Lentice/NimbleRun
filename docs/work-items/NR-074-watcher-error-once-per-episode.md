# NR-074 — A persistently failing directory watch must not drive a 1 Hz full-rebuild loop

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-008（目錄監看與重掃）／§NFR-002（待機事件驅動、禁止輪詢）
- Origin: 2026-08-08 第四次全 repo 稽核（main.cpp 與 catalog_watcher）

## Why

`WatchLoop`（`src/app_host/catalog_watcher.cpp:15-59`）在 `ReadDirectoryChangesW`
回 `FALSE`（且非 `ERROR_OPERATION_ABORTED`／停止旗標）時：

```cpp
if (ok == FALSE) {
    const DWORD error = GetLastError();
    if (error == ERROR_OPERATION_ABORTED || watch->stop.load()) {
        return;  // CancelIoEx from Stop(): normal shutdown
    }
    // ERROR_INVALID_PARAMETER or a transient failure: report a full rescan and
    // back off instead of busy-looping.
    if (watch->window && IsWindow(watch->window)) {
        PostMessageW(watch->window, watch->message,
                     static_cast<WPARAM>(watch->index), 1);
    }
    Sleep(1000);
    continue;
}
```

對**持續性**錯誤（設定的使用者資料夾所在的磁碟被拔除／分享被撤銷，或
`ERROR_ACCESS_DENIED`），每次失敗都 `PostMessageW(... lParam=1)`（full-rescan
marker）再睡 1 秒。UI 端（`src/app_host/main.cpp:2299-2304`）對 full-rescan marker
**繞過 debounce**：

```cpp
g_refresh->MarkSourceFullRescan(source);
const std::vector<nimblerun::CatalogSource> due = g_refresh->DueSources(MonotonicMs());
if (!due.empty()) {
    StartRebuild(window, due);
}
```

於是每 1 秒開一輪新的 rebuild generation：`StartRebuild` 先 `JoinRebuildThreads()`
（`main.cpp:1238`，在 UI 執行緒上擋住上一輪掃描），掃描結果因來源持續失敗而逐輪
作廢。合成效果是**恆定 ~1 Hz 的重建忙碌迴圈**——正是 §FR-008／NFR-002 明令禁止的
「輪詢／高頻工作」，且每輪都順帶觸發 NR-073 描述的面板刷新與 `catalog.cache` 重寫。
使用者情境：把一個自訂資料夾設在外接碟上、拔掉碟 → NimbleRun 從此每秒砸一次磁碟並
重置面板選取，直到重新插上或重開程式。稽核新增發現，未在先前 item 涵蓋。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **每個錯誤連續期間只報一次 full-rescan**：`WatchLoop` 加一個 `bool reported`
   旗標——`ok == TRUE` 時重置為 false；`ok == FALSE`（非停止）時**只在 `!reported`
   時** `PostMessageW`，之後設 `reported = true`。Sleep 1 s 的退避保留（錯誤期間不
   燒 CPU），但不再每 1 秒重新觸發 rebuild。磁碟恢復後 `ReadDirectoryChangesW` 再次
   成功 → 旗標重置 → 下一次真實事件（含 buffer overflow 的 full-rescan）照常送出。
2. **不在 UI 端擋 marker**：UI 端（`kWatchChangedMessage` 的 full_rescan 分支）不改，
   因為它無從得知錯誤是否持續。問題根源在 watcher 的送訊頻率，修在送訊端。
3. **不停止該 watch、不取消 `SetRoots`**：root 恢復後 watch 要能自動續監，
   `CreateFileW` 的 handle 仍有效；只在錯誤期間抑制重複通知。
4. **不加測試 seam**：`ReadDirectoryChangesW` 失敗是 OS 路徑，不可注入（NR-050／
   NR-068 先例）。由 sanity grep＋手動驗收覆蓋。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

design-spec §FR-008：

- 監看採 `ReadDirectoryChangesW` 非同步；overflow／`ERROR_NOTIFY_ENUM_DIR` 標記完整
  重掃；收到密集事件時 debounce 500 ms。

design-spec §NFR-002：

- 監控目錄採 OS completion／event 通知；背景執行緒完成工作後應回收；不得建立常駐
  thread pool 只為未來可能的工作。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/catalog_watcher.cpp:15-59` — `WatchLoop`。主場（`:31-45` 錯誤路徑）。
- `src/app_host/main.cpp:2293-2310` — `kWatchChangedMessage`（`full_rescan` 分支）。
  **只讀不改**，確認語意。
- `src/app_host/catalog_watcher.cpp:100-113` — `Stop`（確認 `CancelIoEx` 正常關閉路徑
  不受旗標影響）。

## Scope

在 `WatchLoop` 的 `for (;;)` 迴圈內加檔案範圍（匿名 namespace）或函式局部旗標：

```cpp
bool reported = false;   // NR-074: one full-rescan notice per failure episode
for (;;) {
    ...
    const BOOL ok = ReadDirectoryChangesW(...);
    if (watch->stop.load()) {
        return;
    }
    if (ok == FALSE) {
        const DWORD error = GetLastError();
        if (error == ERROR_OPERATION_ABORTED || watch->stop.load()) {
            return;
        }
        // NR-074: a persistent error (root removed, access denied) must not
        // post a full-rescan marker every second -- that drives a 1 Hz rebuild
        // loop in the host. Report the first failure only; the backoff sleep
        // continues, and the next successful ReadDirectoryChangesW resets the
        // flag so a genuine later event is reported again.
        if (!reported && watch->window && IsWindow(watch->window)) {
            PostMessageW(watch->window, watch->message,
                         static_cast<WPARAM>(watch->index), 1);
        }
        reported = true;
        Sleep(1000);
        continue;
    }
    reported = false;
    ...
}
```

不做其他改動。

### 測試

不加新測試執行檔（OS 失敗路徑不可注入）。由 sanity grep＋`ctest` 全綠＋手動驗收
覆蓋。

### 更新 spec？

不需。§FR-008／§NFR-002 描述的行為層級未動——本次讓實作符合「事件驅動、不輪詢」。

## How this stays maintainable

「一次錯誤 → 一次通知」是 watcher 送訊端的固有語意：任何 UI 端消費者（目前只有
rebuild 觸發）不再需要知道錯誤是瞬時還是持續。`reported` 是 `WatchLoop` 內的生命週期
與 watch 一致（`SetRoots` 重建 watch 時重新開始），無跨執行緒狀態。

## Non-goals

- **不改 UI 端 `kWatchChangedMessage` 的 full-rescan 分支。**
- **不停止／重啟失敗的 watch、不取消 root。**
- **不加退避計時器**（`Sleep(1000)` 保留，錯誤期間已無訊息，1 秒一次空迴圈不構成
  高頻工作）。
- **不為測試發明 seam。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項）。
2. sanity grep：`PostMessageW(watch->window, watch->message,` 的 full-rescan 呼叫
   （`lParam = 1`）被 `!reported` 條件包住；`reported` 在 `ok == TRUE` 路徑重置。

Manual：

3. 設定一個自訂資料夾指到可移除磁碟 → 拔除磁碟 → 觀察約 10 秒：rebuild 只在第一次
   失敗觸發一次，之後不再有新的 rebuild（日誌或 `catalog.cache` 最後寫入時間不再
   變化），無 ~1 Hz 掃描。
4. 重新插上磁碟、觸發任一檔案事件：來源恢復正常更新（旗標已重置，單一事件照常
   debounce 後重建）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# full-rescan 送訊被 !reported 條件包住、ok==TRUE 路徑重置：
Select-String -Path src/app_host/catalog_watcher.cpp -Pattern "reported"
# expect: 3 處左右（宣告、錯誤路徑設 true、成功路徑重置 false）

# 改動範圍：
git diff --name-only
# expect: 只有 src/app_host/catalog_watcher.cpp
```

## 交接區

（實作者填寫：旗標放置處、手動驗收 3/4 的實際觀察、建置與 CTest 結果、sanity greps、
偏差、未完成事項。）
