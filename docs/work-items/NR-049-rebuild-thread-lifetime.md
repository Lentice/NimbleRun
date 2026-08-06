# NR-049 — Catalog rebuild threads must not read `g_settings` or outlive the window

Phase 3 · Status `ready` · Depends on: —

- Source: `docs/design-spec.md` §FR-008（背景重建）／§NFR-002（閒置資源）／§11（失效與復原）
- Origin: 2026-08-06 repo audit, findings #2 與 #3（最高嚴重度：use-after-free）

## Why

`StartRebuild`（`src/app_host/main.cpp:924`）替每個 catalog source 開一條
**detached** `std::thread`，而 lambda 內直接讀檔案範圍的 `g_settings`：

```cpp
result->entries = g_settings.include_windows_apps ? ... : ...;
result->entries = nimblerun::EnumerateUserFolderCatalog(g_settings);
```

`g_settings` 是 UI 執行緒擁有的可變全域，含 `std::vector<CatalogRoot>` 與多個
`std::wstring`。兩個具體的當機情境：

1. **重建中改設定 → use-after-free。** watcher 事件觸發 UserFolder 重建，
   detached lambda 正在 `EnumerateUserFolderCatalog(g_settings)` 裡走一棵大目錄樹、
   持有 `root.path.c_str()`；使用者此時在設定對話框按 OK，UI 執行緒執行
   `g_settings = reloaded;`，舊的 `catalog_roots` buffer 與每個 root 字串被釋放。
   掃描執行緒接著讀已釋放的 heap → 當機，或帶著垃圾路徑遞迴下去。
   `include_windows_apps` 與 `catalog_extensions` 走同一條無同步的路。
2. **重建中結束程式 → 靜態解構競賽。** 執行緒 detach 後永不 join。使用者按
   `Ctrl+R` 後立刻從匣選單選 Exit，`DestroyWindow` → `PostQuitMessage` →
   訊息迴圈結束 → `wWinMain` 返回，CRT 開始解構命名空間範圍的 `g_settings`，
   而 worker 還在讀它；worker 隨後 `PostMessageW` 到已死的 HWND，失敗並洩漏
   heap 上的 `RebuildResult`。`WM_DESTROY` 完全沒有等待這些執行緒。

兩者根因相同：**跨執行緒共享可變的 UI 狀態，且執行緒生命週期無人負責。**

## Decisions already made — do not reopen

決定於撰寫本 item 時（理由見 Non-goals 與 How this stays maintainable）：

1. **以「值拷貝」解決資料競賽，不是加鎖。** 重建執行緒需要的是**啟動當下**
   的設定快照，不是最新設定；加 mutex 會讓 UI 執行緒在使用者按 OK 時被一次
   完整目錄掃描擋住，而拷貝一個 `Settings` 只是幾個字串與一個小 vector。
2. **以「可 join 的執行緒容器」解決生命週期，不是 detach 加旗標。** 用一個
   檔案範圍的 `std::vector<std::thread>` 並在 `WM_DESTROY` 逐一 `join()`。
   不加取消旗標、不加 `std::atomic<bool> g_shutting_down`：掃描本身是有界的
   （catalog 依 §FR-003 上限 5,000 筆），等它結束比正確實作合作式取消便宜得多。
3. **不引入執行緒池、不引入 `std::jthread` 之外的抽象。** 若 LLVM-MinGW 的
   libstdc++ 支援 `std::jthread`，仍**使用 `std::thread` + 明確 join**：
   `jthread` 的自動 join 發生在解構點，而我們要的 join 點是 `WM_DESTROY`，
   兩者不同。
4. **不動 `CatalogRefreshCoordinator` 的 generation 機制。** 過期世代的丟棄
   邏輯（`BeginGeneration`／`ApplySourceResult`）已正確，本 item 不碰。

## Binding constraints — quoted, do not go looking for them

design-spec §FR-008：

> - 檔案系統事件以 500 ms debounce 合併後才觸發重建。
> - 重建在背景執行緒進行，不阻塞 UI。
> - 以 generation 標記區分世代，過期世代的結果直接丟棄。

design-spec §NFR-002（閒置時的資源）：

> 閒置時不得有忙碌迴圈或高頻計時器。

design-spec §11：

> - 任一子系統失效時，其餘功能必須續行。

`AGENTS.md`：

- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep App Catalog data as ordinary copyable values.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:924-957` — `StartRebuild`。本 item 的主要改動點。
  注意 `sources` 已經是**傳值**參數，`generation` 與 `source` 也已按值捕獲；
  **只有 `g_settings` 是按參考洩進 lambda 的**（透過全域，不是捕獲清單）。
- `src/app_host/main.cpp` 的 `g_settings` 宣告與**每一處寫入**。用
  `Select-String -Path src/app_host/main.cpp -Pattern 'g_settings'` 列出全部，
  逐一判斷哪些在 UI 執行緒、哪些在 worker。寫入點至少包含設定對話框套用後的
  `g_settings = ...`（審計指出約在 :2082）與啟動時的載入。
- `src/app_host/main.cpp` 的 `WM_DESTROY` handler — 既有的關閉順序（停 icon
  worker → 排空 `kIconReadyMessage` → 銷毀 store）。**join 點要放進這個既有
  順序裡，位置見 §2**，不要另起一個關閉路徑。
- `src/catalog/user_folder_catalog.h` / `.cpp` — `EnumerateUserFolderCatalog`
  的簽章：確認它取 `const Settings&`。它可以維持不變，本 item 傳入的是
  執行緒自己擁有的那份拷貝的參考。
- `src/settings/settings_store.h` — `Settings` 的定義。確認它是可拷貝的
  ordinary value（`AGENTS.md` 要求如此），拷貝成本只有字串與 vector。
- `src/catalog/catalog_refresh.h` — `BeginGeneration` / `ApplySourceResult`。
  **不改**，但要理解 join 之後遲到的結果如何被丟棄。
- `src/app_host/catalog_watcher.h` — watcher 執行緒的停止方式，是本 repo 既有
  的「有生命週期負責人」範例，可作為 §2 的形狀參考。

## Scope

### 1. 執行緒只讀自己擁有的設定拷貝

在 `StartRebuild` 內、開執行緒**之前**，取一份快照，並按值捕獲：

```cpp
    // NR-049: the rebuild threads must never touch g_settings. It is UI-thread
    // state, and the settings dialog can reassign it (freeing catalog_roots and
    // every root string) while a scan is halfway through a directory tree.
    // A Settings is an ordinary copyable value (AGENTS.md), so each thread gets
    // the snapshot that was current when the rebuild started -- which is also
    // the semantically correct input: a rebuild reflects the settings that
    // triggered it, not settings applied after it began.
    const nimblerun::Settings settings_snapshot = g_settings;
    for (const nimblerun::CatalogSource source : sources) {
        std::thread worker([window, generation, source, settings_snapshot]() {
            ...
            result->entries = settings_snapshot.include_windows_apps ? ... : ...;
            ...
            result->entries = nimblerun::EnumerateUserFolderCatalog(settings_snapshot);
```

按值捕獲會替**每一條**執行緒各拷貝一份，這是要的：三條執行緒各自擁有，
沒有共享，沒有生命週期問題。不要為了省一次拷貝而改用
`std::shared_ptr<const Settings>`——那是為了省幾百 bytes 而加一層間接。

`StartRebuild` 內**不得再出現 `g_settings`**；Agent checks 用 grep 守住。

### 2. 執行緒可被 join，並在 `WM_DESTROY` join

新增檔案範圍容器，緊鄰其他 rebuild 相關 global：

```cpp
// NR-049: rebuild threads are owned, not detached. Joined in WM_DESTROY so a
// scan can never outlive the window it posts to, nor the globals it reads.
// Only ever touched on the UI thread (StartRebuild and WM_DESTROY), so it needs
// no lock of its own.
std::vector<std::thread> g_rebuild_threads;
```

`StartRebuild`：

- 開執行緒前，先清掉已完成的：

  ```cpp
      // NR-049: reap finished threads so the vector cannot grow without bound
      // across a long session of watcher-driven rebuilds. A thread that has
      // already posted its result is joinable and returns from join() at once.
      std::erase_if(g_rebuild_threads, [](std::thread& t) {
          if (t.joinable() && ???) ...
      });
  ```

  `std::thread` **沒有** "has finished" 查詢，所以上面那個形狀行不通。改用
  最簡單且正確的做法：**每次 `StartRebuild` 開始時，join 掉上一輪的全部執行緒
  再開新的一輪。**

  ```cpp
      // NR-049: join the previous cycle before starting a new one. Rebuilds are
      // debounced 500 ms apart (§FR-008) and a cycle's threads have almost
      // always finished by the time the next one starts, so this is normally a
      // no-op; when it is not, blocking the UI thread briefly is the correct
      // trade against an unbounded thread vector. If a rebuild ever becomes
      // slow enough that this is visible, the fix is to make the scan
      // cancellable, not to go back to detach().
      for (std::thread& worker : g_rebuild_threads) {
          if (worker.joinable()) {
              worker.join();
          }
      }
      g_rebuild_threads.clear();
  ```

  **實作者必須驗證這一點的實際感受**：手動驗收 #3 就是為此而設。若在真實
  機器上 `Ctrl+R` 連按會卡頓，記錄在交接區並停手回報，不要自行加取消機制。

- `worker.detach();` 改為 `g_rebuild_threads.push_back(std::move(worker));`。

`WM_DESTROY`：在既有的「停 icon worker」步驟**之後**、銷毀 `g_refresh` 與
`g_settings` 相關資源**之前**，加入同一段 join 迴圈（抽成一個檔案範圍的
`void JoinRebuildThreads()` 讓兩處共用，這是唯一值得的抽取）。

順序理由必須寫進註解：worker 讀 `g_settings` 與 `g_refresh`，所以 join 一定要
排在任何拆除它們的動作之前。

### 3. 排空遲到的 `kRebuildDoneMessage`

join 之後，仍可能有 worker 在 join 前一刻 `PostMessageW` 成功、訊息還躺在
佇列裡，其 `LPARAM` 是 `new RebuildResult`。既有的 icon worker 關閉路徑已經有
「排空 `kIconReadyMessage`」的先例——**照抄那個形狀**：

```cpp
    // NR-049: a thread can post its result microseconds before we join it, so
    // drain the queue and delete the payloads. Mirrors the existing
    // kIconReadyMessage drain directly above; without it every shutdown during
    // a rebuild leaks one RebuildResult per source.
    MSG message;
    while (PeekMessageW(&message, window, kRebuildDoneMessage, kRebuildDoneMessage,
                        PM_REMOVE)) {
        delete reinterpret_cast<RebuildResult*>(message.lParam);
    }
```

以工作樹裡 `kIconReadyMessage` 排空的實際寫法為準，不要用本文的版本覆蓋它的
慣例。

### 4. 測試

`src/app_host/main.cpp` 是 Win32 進入點，無法單元測試，所以**本 item 的自動化
檢查是 grep 形式的**（見 Agent checks），加上一個可執行的實證：

在 `tests/unit/catalog_refresh_test.cpp` 加一個 case，證明本 item 所依賴的
前提——`Settings` 的拷貝是獨立的：

```cpp
// NR-049: StartRebuild hands each rebuild thread a by-value Settings snapshot,
// so a later mutation of the original cannot reach into the running scan. This
// pins that Settings really is an ordinary copyable value with no shared
// buffers (AGENTS.md), which is the whole basis of the fix in main.cpp.
```

具體斷言：建一個帶至少一個 `catalog_roots` 項與非空 `catalog_extensions` 的
`Settings`，拷貝它，然後 `original.catalog_roots.clear()` 並改
`include_windows_apps`，斷言拷貝的內容完全不變（含 root 的 `path` 字串內容）。

這是一個便宜的守門員：若哪天有人把 `Settings` 改成持有指標或 `string_view`，
這個測試會紅，而 `main.cpp` 的修法會在無聲中失效。

## Performance

- 每次重建多三次 `Settings` 拷貝：三個小字串 vector，數百 bytes，發生在
  500 ms debounce 之後、一次完整目錄掃描之前。不可測量。
- `StartRebuild` 開頭的 join 在正常情況是 no-op（上一輪早已結束）。最壞情況
  是使用者在一次慢掃描期間再次觸發重建，此時 UI 執行緒等待該掃描結束。
  §Scope 2 已載明若此可見則另開 item 做可取消掃描。
- `WM_DESTROY` 的 join 讓關閉可能多等一次掃描。這是正確的取捨：目前的替代
  方案是「不等，然後在解構中的全域上讀寫」。

`docs/performance-baseline.md` 若有「catalog rebuild」相關列，順手填上你量到的
數字（NR-056 會全面回填，這裡只填你手上有的）。

## How this stays maintainable

**跨執行緒邊界只傳值。** 這是本 repo 已經在遵守的規則（`AGENTS.md`：App
Catalog data 是 ordinary copyable values；catalog 模組不持有 Shell COM 指標），
`StartRebuild` 只是漏了一個全域。修法讓規則在這裡也成立，而不是替它加一把鎖
變成例外。**新增任何背景工作時，捕獲清單裡不得出現全域名稱**——這是本 item
留下的唯一契約，Agent checks 用 grep 機械化守住。

**每條執行緒都有一個負責 join 的人。** `catalog_watcher` 已經是這樣，
`icon_worker` 已經是這樣，rebuild 執行緒是最後一個例外。統一之後，
「關閉時要等誰」在 `WM_DESTROY` 一處讀得完。

## Non-goals

- **可取消／可中止的目錄掃描。** 掃描有界（§FR-003 上限 5,000 筆），等它結束
  比正確實作合作式取消便宜。若手動驗收 #3 顯示會卡頓，那才是開那個 item 的
  依據。
- **執行緒池、`std::async`、任務佇列。** 三條短命執行緒不需要調度器。
- **替 `g_settings` 加 mutex，或把它包成執行緒安全型別。** 值拷貝之後就沒有
  共享可保護；加鎖只會製造 UI 執行緒被掃描擋住的新問題。
- **稽核其餘 ~60 個 `g_*` 全域的執行緒安全性。** 本 item 只處理 rebuild
  執行緒實際碰到的那一個。其餘全域若只在 UI 執行緒讀寫就沒有問題；若你在
  追蹤時發現第二個跨執行緒的全域，**記在交接區**，不要順手改。
- **改 generation 機制或 `ApplySourceResult`。**
- **改 icon worker 的關閉路徑。** 它已正確，只是被當作範本。

## Interaction with other open items

- **NR-054**（診斷記錄檔位置與序列化）也會碰跨執行緒議題，但碰的是
  `DiagnosticLog`，與本 item 的檔案／函式沒有交集，可任意順序。
- **本 item 與 NR-050（icons.cache 強化）都在修 crash 級缺陷但位於不同子系統**
  （app_host vs icons），互不衝突。
- 任何未來新增 catalog source 的 item 都必須遵守本 item 建立的「不捕獲全域」
  契約。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §Scope 4 的 `Settings` 拷貝獨立性測試存在且通過。
3. `StartRebuild` 的函式本體內不出現 `g_settings`。
4. `main.cpp` 內不出現 `detach()`（若 icon worker 或 watcher 另有正當的
   detach，記在交接區並在 grep 預期輸出中載明）。

Manual（Release build，逐條打勾）：

1. **重建中改設定不當機**：把一個很大的資料夾（數千個檔案）加為 user folder，
   按 `Ctrl+R` 觸發重建，**立刻**開啟設定對話框、移除該資料夾、按 OK。
   程式不得當機，且下一次面板開啟顯示的是移除後的結果。
2. **重建中結束程式不當機、不洩漏**：`Ctrl+R` 後立刻從匣選單 Exit。程式必須
   正常結束（可能稍慢），Task Manager 中不留下 `NimbleRun.exe`。
3. **連續重建不卡頓**：連按 `Ctrl+R` 五次，觀察面板是否有可見停頓。若有，
   記錄實際感受與時間，**這是 §Scope 2 明列的回報項**。
4. 一般使用（開面板、搜尋、啟動）行為與本 item 前一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R catalog_refresh --output-on-failure
```

```powershell
# 重建 lambda 不再碰全域設定：先看 StartRebuild 的範圍，再確認其中沒有 g_settings
Select-String -Path src/app_host/main.cpp -Pattern 'g_settings' -Context 0,0
# expect: 只在 UI 執行緒的載入／套用／讀取處，且沒有一處落在 StartRebuild 內

# 執行緒不再 detach：
Select-String -Path src/app_host/main.cpp -Pattern 'detach\(\)'
# expect: no match（若有例外，交接區必須解釋）

# join 點存在且成對（StartRebuild 開頭 + WM_DESTROY）：
Select-String -Path src/app_host/main.cpp -Pattern 'JoinRebuildThreads|g_rebuild_threads'
# expect: 宣告 1、push_back 1、JoinRebuildThreads 定義 1 + 呼叫 2

# 遲到訊息有排空：
Select-String -Path src/app_host/main.cpp -Pattern 'kRebuildDoneMessage'
# expect: 定義、PostMessageW、WindowProc case、以及新的 PeekMessageW 排空

# 沒有引入鎖或旗標：
Select-String -Path src/app_host/main.cpp -Pattern 'std::mutex|std::atomic|shutting_down'
# expect: 僅既有的 icon worker 相關項目（若有），無新增
```

## 交接區

（實作者填寫：修改的位置、建置與 CTest 結果、4 條手動驗收的實際結果
（特別是 #3 的卡頓觀察）、追蹤時發現的其他跨執行緒全域、sanity greps、
偏差、未完成事項。）
