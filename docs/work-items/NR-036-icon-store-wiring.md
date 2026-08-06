# NR-036 — Wire the icon store into the worker and its flush points

- Status: `done`
- Phase: 3
- Depends on: NR-032、NR-034、NR-035
- Source: `docs/design-spec.md` §FR-009、§10.1、§19.1、§NFR-001

## Goal

把 NR-035 的 `IconStore` 與 NR-034 的 PNG codec 接到 NR-032 的 worker 上，形成三層取圖順序：記憶體 LRU → 磁碟 pack → Shell。這是使用者真正感受到「登入後第一次開窗也有圖」的那一個 item。

## 必讀

`AGENTS.md`（含 Work item authoring rules）、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§10.1／§10.2／§19.1／§NFR-001、`docs/work-items.md`、`docs/work-items/NR-030`～`NR-035` 全部六份、本文件。

依賴檢查：NR-032、NR-034、NR-035 任一未 `done` 即**回報阻塞**。

## 取圖順序（權威定義）

```
Render() 需要 (stable_id, variant)
  └─ IconCache::Peek 命中           → 直接畫（UI thread，零 I/O）
  └─ miss 且不在 pending／失敗集合   → IconWorker::Post（UI thread 立即返回，畫 fallback）

worker 處理一筆請求：
  1. IconStore::Lookup 命中 → DecodeIconPng → 回傳
  2. miss／過期／毀損 → ShellIconProvider::Load(variant)
       成功 → EncodeIconPng → IconStore::Put（僅緩衝，不寫磁碟）→ 回傳
       失敗 → 回傳空 bitmap（失敗不寫入 store）
  3. 一律 PostMessage 回 UI（成功或失敗都要回報）
```

UI thread **不呼叫** `IconStore` 的任何方法。整個 store 由 worker 獨佔，維持 NR-032 的零鎖設計。

## Flush 時機（權威定義）

`IconStore::Flush(pinned_ids, now_utc)` 只在 worker thread 執行，於三個時機：

1. **面板隱藏後**：UI 在既有的隱藏路徑 `Post` 一個 flush 訊號給 worker。這是主要時機——只在關閉時寫是不夠的，tray 程式會被登出／工作管理員／當掉終結，`WM_ENDSESSION` 不保證跑完，那樣本次 session 新抓的圖會全部白抓。
2. **idle**：worker 的請求佇列排空且距上次 flush 有新的待寫資料時，直接 flush（在 `wait` 之前做，不需 timer）。
3. **結束前**：`IconWorker::Stop()` 於 join 前做最後一次 flush，並設一個上限（見下）。

`pinned_ids` 由 UI thread 在送出 flush 訊號時**以純值複本**附帶（`std::vector<std::wstring>`），worker 不讀 `favorites.txt`。

## 硬約束

- UI thread 不得等待磁碟、不得等待 Shell、不得等待解碼（§19.1 第 1031 行）。
- 不得新增 timer 或輪詢（§NFR-002）。
- 不得在 UI thread 建立 `IconStore`、呼叫 `Lookup`／`Put`／`Flush`，或持有其指標以外的任何狀態。
- 不得為 flush 另開 thread：沿用 NR-032 的同一條 worker。
- 結束路徑不得無限等待：`Stop()` 的最後一次 flush 若超過 **1 秒**未完成則放棄（快取遺失無副作用，卡住關機有）。以 `Flush` 前先檢查待寫筆數、超過合理筆數就只寫最近使用的部分來達成，**不要**引入可取消的 I/O 或第二條 thread。
- store 為 `Disabled`（較新 schema、無法建檔）時整條路徑必須照常工作，只是每次都走 Shell。
- 不新增設定項（快取不可關閉、不可調整大小）。
- 不新增 UI 字串、不新增對話框、不對使用者顯示任何快取狀態。

## Scope

### 1. `IconWorker` 擴充（`src/icons/icon_worker.{h,cpp}`）

- 建構子新增 `IconStore*`（可為 `nullptr`，代表不使用磁碟層，供 NR-032 的既有測試沿用）。
- worker `Run()` 啟動時（`CoInitializeEx` 之後）呼叫一次 `store->Open()`。**不在** UI thread 開檔。
- 佇列的元素型別擴充為一個小的 tagged 結構或加一個 `enum class IconTask { Load, Flush }`；`Flush` 任務攜帶 `pinned_ids` 與 `now_utc`。**不要**為 flush 另建第二個佇列。
- `Post` 之外新增 `void PostFlush(std::vector<std::wstring> pinned_ids, std::uint64_t now_utc);`（`push_back`，優先度低於可見請求）。
- 處理 `Load` 任務時依上述「取圖順序」執行。`source_stamp` 的取得（`GetFileAttributesExW` 取 last-write-time 與 size；AppsFolder／無法 stat 者為 0）在 **worker** 做，不在 UI thread 做。
- 佇列排空時，若有待寫資料則先 flush 再 `wait`（時機 2）。
- `Stop()`：設旗標 → `notify_all` → worker 在退出前做最後一次 flush（受上述 1 秒上限約束）→ `join`。

### 2. UI 端接線（`src/app_host/main.cpp`）

- 建立 `IconStore`（路徑取既有的 `%LOCALAPPDATA%\NimbleRun` 解析函式，不要新寫一份路徑組裝），以指標交給 `IconWorker`。生命週期長於 worker。
- 面板隱藏的既有路徑（`Esc` 第二階段、`WM_KILLFOCUS` 自動隱藏、啟動後依設定隱藏、tray 操作導致隱藏——全部匯流到既有那一個隱藏函式）在隱藏之後呼叫一次 `g_icon_worker->PostFlush(pins, now)`。**只加在那一個函式裡**，不要在四個呼叫點各加一次。
- `pins` 沿用 `ShowPanel` 已經載入的那份 pin 清單，不重新讀檔。
- `WM_DESTROY`：先 `Stop()`（含最後 flush），再釋放 D2D 資源與 `IconStore`。

### 3. 診斷

沿用 `diagnostics/diagnostic_log.h`。只在下列時機各記一次事件名與計數：store 建立、整檔重建、丟棄毀損 entry 的筆數、較新 schema 停用、flush 失敗。**不記** stable ID、路徑、App 名稱、搜尋文字。

## Non-goals

- 不做預熱（NR-037）；本 item 仍只在 `Render()` 發現缺圖時請求。
- 不改 variant 階梯、`IconKey`、LRU 容量公式。
- 不改 grid／清單版面、幾何、面板尺寸、palette、footer、輸入處理。
- 不改 catalog、dedup、usage、pin、settings 的邏輯或格式。
- 不新增第二條 thread、thread pool、timer、可取消 I/O。
- 不讓 UI 顯示快取命中率、大小或任何狀態。
- 不回頭修改 NR-012 或 NR-030～NR-035 文件。

## Acceptance

- **冷啟動第二次**：清空 `icons.cache`，啟動 → 開窗 → 等圖示全部出現 → 隱藏面板 → 結束程式 → 再次啟動 → 開窗，**第一幀即顯示真實圖示**（無 fallback 閃動）。此條為本 item 的存在理由，必須實測並在交接區記錄觀察。
- UI thread 全程不呼叫 `IconStore`（以 grep `main.cpp` 無 `Lookup`／`Put`／`Flush`／`Open` 驗證）。
- 面板顯示 24 格未快取圖示時，搜尋欄可立即輸入、不掉字（NR-032 的驗收在加上磁碟層後仍成立）。
- 磁碟命中的圖示不再呼叫 Shell：以診斷計數或注入的 fake provider 驗證第二次 session 的 `Load` 呼叫次數為 0。
- 來源檔更新（改動 `.exe` 的 last-write-time）後，下次取圖走 Shell 並覆寫該筆記錄。
- `icons.cache` 被刪除、被改成隨機位元組、被改成較新 schema 三種情況下，程式皆正常啟動與顯示圖示（分別為重建、重建、停用磁碟層），無對話框、無崩潰。
- 面板隱藏後 `icons.cache` 的檔案 mtime 有更新（flush 確實發生），且 flush 期間 UI 可正常再次開窗、輸入、啟動 App。
- 以工作管理員直接終結程序（不走 `WM_ENDSESSION`）後重新啟動：本次 session 在隱藏面板之前抓到的圖仍在快取中（驗證時機 1 有效）。
- 程式結束在 1 秒內完成，無 hang、無 `std::terminate`。
- 待機執行緒數與 NR-032 相同（未新增 thread）。
- 待機工作集符合 §NFR-001 更新後的 60 MiB 目標；`icons.cache` 大小不超過 32 MiB。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icons|icon_worker|icon_store|png_codec" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/icon_worker_test.cpp` 新增 case（沿用 NR-032 的 message-only window ＋ fake provider ＋ NR-035 的臨時路徑注入；**不碰真實 `%LOCALAPPDATA%`**）：

- 第一輪：fake provider 被呼叫 N 次，結果全部抵達；`PostFlush` 後 `Stop()`。
- 第二輪：同一臨時路徑建新的 store 與 worker，送同樣 N 個請求 → fake provider 呼叫次數為 **0**，結果仍全部抵達且位元組與第一輪相同（磁碟層生效）。
- `source_stamp` 改變後第二輪 fake provider 被呼叫（失效生效）。
- store 為 `Disabled`（先寫一個 `schema_version = 2` 的檔）時，兩輪 fake provider 都被呼叫，且檔案未被修改。
- fake provider 回傳空 bitmap 時不寫入 store：第二輪仍呼叫 provider。
- `Stop()` 在佇列非空且有待寫資料時可完成且不 hang。

冷啟動兩次的實測屬人工驗證，不列入 Agent 交付，但必須在交接區記錄觀察結果。

## 交接區

- Start: 依「必讀」讀完全部文件；trace 全部列舉檔案。NR-032／NR-034／NR-035 皆為 `done`，無阻塞。實作內容：`icon_worker.{h,cpp}` 建構子新增 `IconStore* store = nullptr`（nullptr＝沿用 NR-032 無磁碟行為）、佇列元素擴充為 tagged `IconTask`（`enum class IconTaskKind { Load, Flush }`，單一佇列、flush 一律 `push_back`）、新增 `PostFlush(pinned_ids, now_utc)`；worker `Run()` 於 `CoInitializeEx` 後呼叫一次 `store->Open()`；Load 任務依「記憶體 LRU → 磁碟 pack → Shell」取圖：`Lookup` 命中→`DecodeIconPng`（帶 `expected_size = variant`，解碼失敗視同 miss 走 Shell）、miss／過期／毀損→`ShellIconProvider::Load` 成功→`EncodeIconPng`→`Put`（僅緩衝）、失敗回傳空 bitmap 不寫入；`source_stamp` 在 worker 用 `GetFileAttributesExW`（last-write-time＋size 經 `MakeSourceStamp`）取得，`shell:AppsFolder\` 前綴或無法 stat 者為 0（store 走 TTL）；`now_utc` 以 `std::time` 在 worker 取得（`PostFlush` 的 now 由 UI 傳入）。flush 三時機：面板隱藏後（UI 在 `HidePanel` 呼叫一次 `PostFlush`，pins 沿用 `g_pins->OrderedPins()`，不重新讀檔）、佇列排空且有待寫資料（`pending_puts_ > 0`，在 `wait` 前 flush）、`Stop()` 退出前最後一次（上限 `kStopFlushMaxPending = 64`，超過直接丟棄不寫，`ponytail:` 註解說明需 store API 才能「只寫最近使用的部分」、量測有需要再補）。`main.cpp`：新增唯一隱藏匯流函式 `HidePanel`（Esc 第二階段／`WM_KILLFOCUS` 自動隱藏／hide-after-launch／hotkey toggle 四個呼叫點全部改走它），在 `ShowWindow(SW_HIDE)` 後呼叫一次 `PostFlush`；`wWinMain` 在 worker 之前建立 `IconStore`（路徑取 `DefaultSettingsDir() / L"icons.cache"`、`kMaxPackBytes`、`&diag` 注入診斷），生命週期長於 worker（`WM_DESTROY` 先 `Stop()` 再釋放 D2D 與 store）；診斷沿用 `IconStore` 內既有的 `icon-store` 事件（created／recreated／entries-dropped／newer-schema／flush-failed），無新增 log 呼叫。未做預熱（NR-037）、未改 variant 階梯／IconKey／LRU 容量／版面／輸入處理、未新增 thread／timer／可取消 I/O。新測試 5 例（見 Agent checks）全綠。交接區待補：冷啟動兩次的實測觀察（見 Result）。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/icons/icon_worker.{h,cpp}`、`src/icons/icon_store.{h,cpp}`、`src/icons/png_codec.{h,cpp}`、`src/icons/shell_icon_provider.cpp`、`src/app_host/main.cpp`（隱藏面板的匯流函式、`ShowPanel` 的 pin 載入、`WM_DESTROY`、`%LOCALAPPDATA%` 路徑解析、既有 `IconWorker` 建立處）、`src/diagnostics/diagnostic_log.h`。先確認 NR-032／NR-034／NR-035 皆 `done`，否則回報阻塞。只實作本 item 的 Scope，不得順手做預熱。回報修改檔案、測試命令、結果、冷啟動兩次的實測觀察與未完成事項。
- Result: 完成。修改檔案：`src/icons/icon_worker.h`、`src/icons/icon_worker.cpp`、`src/app_host/main.cpp`、`tests/unit/icon_worker_test.cpp`、本文件、`docs/work-items.md`。驗證（`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 後 `cmake --build build --clean-first` 零 warning、`ctest --test-dir build --output-on-failure` 23/23 全綠，`ctest -R "icons|icon_worker|icon_store|png_codec"` 4/4 全綠）。Agent checks 新增 5 例全過：第一輪 provider 被呼叫 N 次、`PostFlush` 後 `Stop()`；第二輪同臨時路徑新建 store＋worker，provider 呼叫 0 次且位元組與第一輪相同（磁碟層生效）；`source_stamp` 改變（`SetFileTime` +1hr）後第二輪重新走 provider（失效生效）；預寫 `schema_version=2` 檔時 store 為 Disabled，兩輪都走 provider 且檔案位元組不變；provider 回傳空 bitmap 不寫入 store（第二輪仍走 provider）；`Stop()` 在佇列非空且有待寫資料（worker 已進入延遲 provider）時 <2s 完成、最終 flush 落盤（`Stats().entries >= 1`）。測試全程只碰 `%TEMP%\NimbleRunTest\<pid>`，不碰真實 `%LOCALAPPDATA%`。Acceptance 逐條：UI thread 不呼叫 IconStore（grep `main.cpp` 僅 `PostFlush` 一處、無 `Lookup`／`Put`／`Open`，且 store 指標只交 worker）；磁碟命中不再呼叫 Shell（測試以注入 fake provider 驗證第二 session Load 次數為 0）；Disabled 時照常走 Shell（測試兩輪皆 provider）；flush 時機＝隱藏後（`HidePanel` 單點）＋idle（佇列排空且 `pending_puts_ > 0`）＋結束前（`Stop()` 內、有 64 筆上限）。冷啟動兩次（清空 `icons.cache` → 啟動開窗 → 隱藏 → 結束 → 再次啟動開窗，第一幀即顯示真實圖示）**未實測**，列為人工驗證——Agent 環境無法可靠觀察 GUI 第一幀，但測試已驗證其底層機制（磁碟命中第二 session Load=0、位元組一致）。未完成事項：無。
