# NR-035 — File-backed icon store: mmap read, append write, eviction, compaction

- Status: `done`
- Phase: 3
- Depends on: NR-033
- Source: `docs/design-spec.md` §FR-009、§10.1、§10.2、§10.4、§NFR-001

## Goal

把 NR-033 的純值格式接上真實檔案：`%LOCALAPPDATA%\NimbleRun\icons.cache` 的開檔、mmap 讀取、append 寫入、預算淘汰、compaction，以及毀損時的復原動作。

本 item **不碰 PNG 編解碼、不碰 worker、不碰 renderer**：payload 對本模組而言是不透明位元組。因此可以用合成位元組完整測試，不需 WIC。

## 必讀

`AGENTS.md`（含 Work item authoring rules）、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§10.1／§10.2／§10.4／§NFR-001、`docs/work-items.md`、`docs/work-items/NR-030-icon-cache-spec-amendment.md`、`docs/work-items/NR-033-icon-pack-format.md`（佈局是權威定義）、`src/storage/atomic_text_file.h`（既有 tmp＋flush＋atomic replace 與毀損處理慣例）、`src/catalog/catalog_cache.cpp`（既有可重建快取的處理方式）、`src/pins/pin_store.cpp`（既有 30 天過期與「空 catalog 不刪資料」的保守原則）、本文件。

依賴檢查：若 NR-033 未 `done`，**回報阻塞**。

## 硬約束

- 全部檔案操作在**背景 thread** 被呼叫（NR-036 的 worker）。本模組不得假設自己在 UI thread，也不得呼叫任何 UI／視窗 API。
- 不得使用 SQLite 或任何 DB（§10.2）。
- 讀取一律走 mmap（`CreateFileW` ＋ `CreateFileMappingW` ＋ `MapViewOfFile`），**不得**把整檔讀進 heap：payload 不常駐是 §NFR-001 待機工作集的依據。
- 就地 append 僅適用於本快取檔；compaction 與整檔重寫必須走 `.tmp` ＋ `MoveFileEx(MOVEFILE_REPLACE_EXISTING)`（§10.2，NR-030 已界定此例外範圍）。
- 任何毀損、權限、磁碟滿、檔案被鎖的情形都必須**降級為「沒有快取」而繼續運作**，不得 throw 到呼叫端、不得彈對話框、不得阻擋圖示取得。
- 不得原地覆寫使用者資料；本模組只碰 `icons.cache` 與其 `.tmp`，不得刪除或改名同目錄下任何其他檔案。
- 不新增設定項、不新增 UI 字串、不新增網路／遙測／服務／driver／管理員權限。

## 預算與淘汰規則（權威定義）

- 位元組預算 **32 MiB**（`kMaxPackBytes`），阻擋門檻由 §NFR-001 記錄；筆數上限為 NR-033 的 `kIndexCapacity`（512）。
- 淘汰依 `last_used_utc` 由舊到新（LRU）。
- **釘選項目豁免 LRU 淘汰**，但不豁免筆數與位元組硬上限：若全部 512 槽都是釘選項目且仍需空間，則淘汰其中 `last_used_utc` 最舊者。
- 淘汰只在 worker 的 flush／compaction 時發生，**面板顯示中不得淘汰**。
- compaction 只在死區位元組 > 檔案總 payload 的 50% 時執行。

## 失效規則（權威定義）

- 來源為真實檔案：`source_stamp = MakeSourceStamp(last_write_time, size)`。開檔時取得目前值，與 entry 內的值不等即視為過期（miss）。
- 來源為 AppsFolder／AUMID（`launch_identity` 含 `shell:AppsFolder\`，或 `source_path` 無法 stat）：`source_stamp = 0`，改用 **30 天 TTL**（沿用 §FR-011 pin 保留天數的同一常數來源，不新增魔術數字），以 `fetched_utc` 起算。
- 過期的 entry 視為 miss 並標記為可回收槽；不在讀取路徑上做寫入以外的動作。

## Scope

### 新檔 `src/icons/icon_store.{h,cpp}`（加入 `nimblerun_icons` 庫）

```cpp
// Injectable seam so tests can point at a temp directory instead of
// %LOCALAPPDATA%. Empty path => the store is disabled (all Lookup miss, all
// Put no-op), which is also the state after an unrecoverable failure.
struct IconStorePaths {
    std::filesystem::path pack;  // ...\icons.cache
};

enum class StoreState {
    Ready,        // 可讀可寫
    ReadOnly,     // 可讀不可寫（例如磁碟滿或寫入被拒）
    Disabled,     // 完全不用（NewerSchema、無法建檔、路徑為空）
};

struct StoreStats {   // 供診斷與測試斷言，不對使用者顯示
    std::size_t entries = 0;
    std::size_t dropped_entries = 0;   // 本次載入丟棄的毀損槽數
    std::uint64_t payload_bytes = 0;
    std::uint64_t dead_bytes = 0;
    bool recreated = false;            // 本次載入是否整檔重建
};

class IconStore {
public:
    explicit IconStore(IconStorePaths paths);
    ~IconStore();

    // Open (or create) the pack and classify its integrity. Safe to call once at
    // worker startup. Never throws.
    StoreState Open();
    StoreState State() const;
    StoreStats Stats() const;

    // Returns the stored payload bytes for (stable_id, variant) when present,
    // intact, and not stale. Copies out of the mapped view so the caller never
    // holds a pointer into the mapping. Empty vector = miss.
    // now_utc and source_stamp come from the caller (injectable clock / stat).
    std::vector<std::uint8_t> Lookup(const std::wstring& stable_id, int variant,
                                     std::uint64_t source_stamp, std::uint64_t now_utc);

    // Queue a payload for persistence. Buffers in memory; nothing touches disk
    // until Flush(). Overwrites any pending or stored entry for the same key.
    void Put(const std::wstring& stable_id, int variant,
             std::vector<std::uint8_t> payload,
             std::uint64_t source_stamp, std::uint64_t now_utc);

    // Commit buffered puts: evict as needed (pinned_ids exempt), append payloads,
    // write index entries, then commit the alternate header. Compacts when dead
    // bytes exceed 50%. Returns false on any write failure (state may drop to
    // ReadOnly); the in-memory LRU above is unaffected either way.
    bool Flush(const std::vector<std::wstring>& pinned_ids, std::uint64_t now_utc);

private:
    ...
};
```

### 1. `Open()` 的毀損處理（對應 NR-033 的 `PackStatus`）

| 分類 | 動作 | 診斷 |
|---|---|---|
| `Absent` | 以 `MakeEmptyPack()` 建新檔（先寫 `.tmp` 再 replace），`Ready` | 記一次 `icon_pack_created` |
| `Ok` | 掃描 512 槽，收集 `EntryStatus::Ok` 者；`CrcMismatch`／`OutOfBounds`／`BadVariant` 的槽計入 `dropped_entries` 並視為可回收空槽 | `dropped_entries > 0` 時記一次 `icon_pack_entries_dropped` 含筆數 |
| `BadMagic`／`BothHeadersBad` | **刪除**檔案並重建空檔，`recreated = true`，`Ready` | 記一次 `icon_pack_recreated` |
| `NewerSchema` | **不動原檔**、不刪除、不寫入，`Disabled` | 記一次 `icon_pack_newer_schema` |

- 毀損不保留 `.corrupt` 副本（與 `settings.ini`／`usage.tsv`／`favorites.txt` 的慣例**刻意不同**）：這是可完全重建的快取，保留副本只會在使用者磁碟上累積無用位元組。此差異必須寫在 `icon_store.h` 的註解裡，避免日後被當成漏實作而「修正」。
- 診斷一律走既有 `diagnostics/diagnostic_log.h`，只記事件名與計數，**不記路徑、不記 App 名稱、不記搜尋文字**。
- 任何 `CreateFileW`／mapping 失敗（檔案被其他行程鎖住、權限不足）→ `Disabled`，記一次診斷，不重試、不等待。

### 2. 寫入順序（防撕裂，權威定義）

`Flush()` 對每一筆待寫 payload：

1. 在 `payload_end` 寫入 payload 位元組。
2. `FlushViewOfFile` 該範圍。
3. 寫入該筆的 index entry（含 `entry_crc32`）到一個空槽或可回收槽。
4. `FlushViewOfFile` 該 entry。

全部 payload 與 entry 寫完後，最後一步：

5. 把 header 寫入**另一個** slot（`generation + 1`、新的 `payload_end`），`FlushViewOfFile` 該 slot。

任何一步之後斷電：舊 header 仍有效，未被任何有效 entry 指向的新 payload 成為死區，於下次 compaction 清除。**不會**讀到半截資料。這條順序必須以註解寫在 `Flush()` 內。

檔案成長：mapping 需要先 `SetFilePointerEx` ＋ `SetEndOfFile` 擴大檔案再重建 mapping。為避免每筆都重建，`Flush()` 一次算出本輪所需總長並**一次**擴大（以 64 KiB 為擴充粒度向上取整）。

### 3. compaction

- 觸發：`dead_bytes > payload_bytes / 2`（且 `payload_bytes > 0`）。
- 做法：把所有有效 entry 的 payload 依序寫入一個新的 `.tmp`（重排 offset、`generation` 續增）、flush、`MoveFileEx` 取代原檔、重建 mapping。
- 失敗：刪除 `.tmp`、保留原檔、狀態維持 `Ready`（下次再試）。原檔在整個過程中不被修改。

### 4. 淘汰

- `Flush()` 開始時計算本輪所需空間；若超出 `kMaxPackBytes` 或無可用槽，依上述「預算與淘汰規則」選出犧牲者，把其 entry 的 `in_use` 清為 0（該槽變可回收、其 payload 變死區）。
- 釘選判斷以傳入的 `pinned_ids` 為準，本模組**不讀** `favorites.txt`（保持單一資料來源，避免第二份 pin 解析）。

## Non-goals

- 不做 PNG 編解碼（NR-034）；payload 是不透明位元組。
- 不接線到 worker、renderer 或 `IconCache`（NR-036）。
- 不做預熱（NR-037）。
- 不讀 `favorites.txt`／`usage.tsv`／`settings.ini`。
- 不做 schema v2、不做容量（512）遷移。
- 不做背景 timer 或定期 compaction 排程；compaction 只在 `Flush()` 內按條件發生。
- 不做加密、不做壓縮（payload 已壓縮）。
- 不改 `IconKey`、`IconCache`、`ShellIconProvider`、`IconWorker`、任何 UI 檔案。

## Acceptance

（測試以 `IconStorePaths` 指向 `%TEMP%\NimbleRunTest\<pid>` 下的臨時檔，**絕不**碰真實 `%LOCALAPPDATA%\NimbleRun`。payload 用合成位元組。時間與 `source_stamp` 全部由參數注入，測試不依賴真實時鐘。）

- 全新目錄 `Open()` → `Ready`、`recreated == false`、`entries == 0`，檔案長度為 `kPayloadStart`。
- `Put` ＋ `Flush` ＋ 新建一個 `IconStore` 再 `Open` → `Lookup` 取回**完全相同**的位元組。
- 同一 `stable_id` 的 48 與 96 為兩筆獨立記錄，互不覆蓋。
- `Lookup` 在 `source_stamp` 與寫入時不同時回傳空（過期）。
- `source_stamp == 0` 的記錄：`now_utc` 距 `fetched_utc` 29 天內命中、31 天後回傳空。
- `Lookup` 回傳的是**複本**：呼叫端在其後觸發 compaction／重建 mapping，先前取得的 vector 仍有效（不含指向 mapping 的指標）。
- **撕裂復原**：手動把檔案截短到「payload 已寫、header 未提交」的狀態 → `Open()` 為 `Ok`、該筆 miss、其餘筆命中、`dead_bytes > 0`。
- **單筆毀損不影響鄰筆**：寫入 5 筆後翻轉第 3 筆 entry 的一個位元 → `Open()` 為 `Ok`、`dropped_entries == 1`、其餘 4 筆全部 `Lookup` 命中。
- **payload 毀損**：翻轉某筆 payload 的一個位元 → 該筆 `Lookup` 回傳空（`VerifyPayload` 失敗），其餘命中，且該槽在下次 `Flush` 後被回收。
- **整檔毀損**：把兩個 header slot 的 magic 都改掉 → `Open()` 為 `Ready`、`recreated == true`、`entries == 0`，且同目錄下不存在 `.corrupt` 檔。
- **較新 schema**：把 `schema_version` 改為 2 → `Open()` 為 `Disabled`，`Lookup` 全部回傳空，`Put`／`Flush` 為 no-op，且**檔案位元組在 `Open`／`Flush` 前後完全未變**（以檔案雜湊比對）。
- **淘汰**：以小的 `kMaxPackBytes` 覆寫值（建構子或測試專用 setter 注入，不改預設常數）寫入超量記錄 → 最舊 `last_used_utc` 者被淘汰、最新者存活、`entries` 不超過上限。
- **釘選豁免**：讓最舊的記錄同時出現在 `pinned_ids` → 它存活，被淘汰的是次舊的未釘選記錄。
- **筆數上限**：寫入 512 筆後再寫第 513 筆 → 成功，且總筆數仍為 512。
- **compaction**：反覆覆寫同一批 key 直到死區過半 → `Flush` 後檔案總長變小、全部有效記錄仍可 `Lookup`、`dead_bytes` 大幅下降；過程中無 `.tmp` 殘留。
- **compaction 失敗保留原檔**：以唯讀目錄或預先佔用 `.tmp` 檔名模擬失敗 → 原檔位元組未變、全部記錄仍可 `Lookup`。
- 檔案被以 `FILE_SHARE_NONE` 開啟時 `Open()` → `Disabled`，不崩潰、不重試、不阻塞。
- 任意隨機位元組檔案（至少 50 組，含長度短於 `kPayloadStart` 者）餵給 `Open()` 皆不崩潰、不越界、不配置巨量記憶體。
- `Flush()` 期間不呼叫任何 UI／視窗 API（以 grep `icon_store.cpp` 無 `HWND`／`InvalidateRect`／`MessageBox` 驗證）。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icon_store" --output-on-failure
ctest --test-dir build --output-on-failure
```

新測試 `tests/unit/icon_store_test.cpp`（新 CTest 目標 `nimblerun_icon_store_test`）：涵蓋上述 Acceptance 每一條；測試結束刪除臨時目錄。

## 交接區

- Start: 2026-08-05。NR-033 已 done。兩次 subagent 委派皆無產出（與 NR-010/NR-011 相同先例），由主 Agent 直接實作。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/icons/icon_pack_format.h`（佈局常數與 `Decode*`／`Verify*`／`MakeEmptyPack`）、`src/storage/atomic_text_file.h`（tmp＋flush＋replace 與毀損處理慣例）、`src/catalog/catalog_cache.cpp`（可重建快取的既有處理）、`src/diagnostics/diagnostic_log.h`（事件記錄介面與不可記錄的內容）、`tests/unit/recent_usage_test.cpp` 或 `startup_option_test.cpp`（既有以臨時路徑注入的測試手法，本 item 應沿用同一風格）。先確認 NR-033 已 `done`，否則回報阻塞。只實作本 item 的 Scope，不得碰 WIC、worker 或 renderer。回報修改檔案、測試命令、結果與未完成事項。
- Result: 完成。新增 `src/icons/icon_store.{h,cpp}` 與 `tests/unit/icon_store_test.cpp`（CTest `nimblerun_icon_store_test`）；`nimblerun_icons` 連結 `nimblerun_diagnostics`（PUBLIC）。`ctest -R icon_store` 1/1、全套件 23/23 通過、clean build 無新增 warning、`rg HWND|InvalidateRect|MessageBox src/icons/icon_store.*` 無命中。實作時抓到三個真實 bug 並修復：(1) `FindFreeSlot` 未跳過本輪已分配的 `touched` 槽，多筆新鍵全撞同一槽互相覆蓋；(2) `Compact` 在 mapping 仍持有檔案時 `MoveFileEx` 取代會 ERROR_SHARING_VIOLATION，必須先 `Unmap` 再換檔、失敗時重新 `MapFile`＋`ScanIndex` 恢復；(3) `ScanIndex` 的 `stats_=StoreStats{}` 清掉 `recreated` 旗標。另將 dead_bytes 改以實際檔大小計算以涵蓋 header 以外的尾端垃圾（撕裂復原測試成立的前提）。測試每個 section 先重置為乾淨 empty pack（`MakeEmptyPack()`），避免共享檔被前一 section 的 schema-2 或舊 entry 污染；`FindEntry` helper 依 hash 定位 slot，不假設 pending 迭代順序。未完成：無（worker/renderer 接線屬 NR-036）。
