# NR-033 — Icon pack format codec and corruption classification

- Status: `done`
- Phase: 3
- Depends on: NR-030
- Source: `docs/design-spec.md` §FR-009、§10.2、§10.4、§9 職責表

## Goal

定義 `icons.cache` 的位元組佈局，並實作**純值**的編解碼與毀損分類。本 item **完全不碰檔案 I/O、不碰 WIC、不碰 Windows API**：所有函式對 `const std::uint8_t*` ／ `std::vector<std::uint8_t>` 操作，因此可以在不建視窗、不寫檔的情況下完整測試每一種毀損情形。

檔案端（mmap、append、淘汰、compaction）是 NR-035；PNG 編解碼是 NR-034。

## 必讀

`AGENTS.md`（含 Work item authoring rules）、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§10.2／§10.3／§10.4／§9 職責表、`docs/work-items.md`、`docs/work-items/NR-030-icon-cache-spec-amendment.md`、`docs/work-items/NR-031-icon-variant-key.md`、`src/storage/atomic_text_file.h`（既有持久化慣例）、`src/catalog/stable_id.h`（`HashStableId` 的輸出格式）、本文件。

依賴檢查：若 NR-030 未 `done`（§10.2 尚無 `icons.cache` 條目），**回報阻塞**。

## 硬約束

- **不得** `#include <windows.h>`、D2D、WIC 或任何 Shell 標頭。純 C++20 ＋ 標準庫。
- 不得引入第三方序列化或壓縮庫；CRC32 自行實作（約 15 行）。
- 不得依賴 `struct` 記憶體佈局：**逐位元組明確讀寫 little-endian**，不用 `#pragma pack`、不用 `reinterpret_cast` 到結構體、不用 `memcpy` 整個結構。理由是這個檔案要跨編譯器與未來版本存活。
- 所有解析函式對**任意**位元組輸入都必須是安全的：不得越界讀取、不得依輸入配置巨大記憶體、不得 throw 到呼叫端以外。
- 索引容量固定，使索引區為固定大小、append payload 不需搬動索引（這是「單檔仍可安全就地追加」的關鍵）。
- 不新增設定項。

## 佈局（權威定義；實作與測試都以本節為準）

全部欄位 little-endian。三個區塊，位移固定：

```
offset 0      : header slot A (32 bytes)
offset 32     : header slot B (32 bytes)
offset 64     : index block  (kIndexCapacity * 56 bytes)
offset 28736  : payload area (grows by append)
```

`kIndexCapacity = 512`，故索引區為 28672 bytes，`kPayloadStart = 64 + 28672 = 28736`。

### Header slot（32 bytes）

| 位移 | 大小 | 欄位 |
|---:|---:|---|
| 0 | 4 | magic `'N' 'R' 'I' 'C'` |
| 4 | 2 | `schema_version`（本版 = 1） |
| 6 | 2 | reserved（寫 0） |
| 8 | 4 | `generation`（單調遞增，較大者為較新） |
| 12 | 4 | `index_capacity`（本版 = 512） |
| 16 | 8 | `payload_end`（絕對檔案位移，最後一個 payload 位元組的下一格） |
| 24 | 4 | `crc32`（涵蓋位移 0..23） |
| 28 | 4 | reserved（寫 0） |

**雙份 header 的用途**：每次提交寫入「另一個」slot 並把 `generation` +1。任何一次撕裂寫入最多毀掉一個 slot，另一個仍可用。載入時取兩個 slot 中 CRC 有效且 `generation` 較大者。

### Index entry（56 bytes）

| 位移 | 大小 | 欄位 |
|---:|---:|---|
| 0 | 8 | `stable_id_hash`（由 stable ID 的 16 個十六進位字元解析成 u64） |
| 8 | 2 | `variant`（必須是 48／96／256 之一） |
| 10 | 2 | `flags`（bit0 = 使用中；0 表示空槽或已死） |
| 12 | 4 | `payload_len` |
| 16 | 8 | `payload_offset`（絕對檔案位移） |
| 24 | 4 | `payload_crc32`（涵蓋該筆 payload 全部位元組） |
| 28 | 8 | `source_stamp`（來源檔的 last-write-time 與 size 混成的 u64；**0 表示無檔案可 stat**，改用 TTL） |
| 36 | 8 | `fetched_utc`（epoch 秒；TTL 由此起算） |
| 44 | 8 | `last_used_utc`（epoch 秒；LRU 淘汰依據） |
| 52 | 4 | `entry_crc32`（涵蓋位移 0..51） |

**每筆 entry 自帶 CRC 的用途**：撕裂的 entry 只讓那一槽失效，其餘 511 槽不受影響。這是「單檔不比每顆一檔脆弱」的關鍵——毀損的復原粒度是一筆，不是整檔。

## Scope

### 1. 新檔 `src/icons/icon_pack_format.{h,cpp}`（加入 `nimblerun_icons` 庫）

常數：

```cpp
inline constexpr std::uint8_t kPackMagic[4] = {'N', 'R', 'I', 'C'};
inline constexpr std::uint16_t kPackSchemaVersion = 1;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kHeaderSlotCount = 2;
inline constexpr std::size_t kIndexEntrySize = 56;
inline constexpr std::size_t kIndexCapacity = 512;
inline constexpr std::size_t kIndexOffset = kHeaderSize * kHeaderSlotCount;   // 64
inline constexpr std::size_t kPayloadStart = kIndexOffset + kIndexCapacity * kIndexEntrySize;  // 28736
```

CRC32（IEEE 802.3，reflected，poly `0xEDB88320`）：

```cpp
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);
```

以函式內 `static` 的惰性建表或直接位元運算實作皆可；**不要**引入外部實作。

純值結構（普通可複製，無指標）：

```cpp
struct PackHeader {
    std::uint16_t schema_version = kPackSchemaVersion;
    std::uint32_t generation = 0;
    std::uint32_t index_capacity = static_cast<std::uint32_t>(kIndexCapacity);
    std::uint64_t payload_end = kPayloadStart;
};

struct PackEntry {
    std::uint64_t stable_id_hash = 0;
    std::uint16_t variant = 0;
    bool in_use = false;
    std::uint32_t payload_len = 0;
    std::uint64_t payload_offset = 0;
    std::uint32_t payload_crc32 = 0;
    std::uint64_t source_stamp = 0;
    std::uint64_t fetched_utc = 0;
    std::uint64_t last_used_utc = 0;
};
```

編碼（寫入固定大小緩衝，`out` 必須至少 `kHeaderSize` ／ `kIndexEntrySize` 位元組；函式負責填 CRC 與 reserved 0）：

```cpp
void EncodeHeader(const PackHeader& header, std::uint8_t* out);
void EncodeEntry(const PackEntry& entry, std::uint8_t* out);
```

解碼與毀損分類：

```cpp
enum class PackStatus {
    Ok,
    Absent,          // 檔案不存在或短於 kPayloadStart
    BadMagic,        // 兩個 header slot 的 magic 都不對 → 不是我們的檔案
    NewerSchema,     // 可讀但 schema_version > kPackSchemaVersion
    BothHeadersBad,  // magic 對但兩個 slot 的 CRC 都不符
};

enum class EntryStatus {
    Ok,
    Free,            // flags bit0 = 0，正常空槽（不是毀損）
    CrcMismatch,     // entry_crc32 不符（撕裂寫入）
    OutOfBounds,     // payload_offset < kPayloadStart，或 offset + len > payload_end
    BadVariant,      // variant 不在 kIconVariants 內
};

// data/size 為整個檔案（或其 mmap view）。回傳選中的 header。
PackStatus DecodeHeader(const std::uint8_t* data, std::size_t size, PackHeader& out);

// slot < kIndexCapacity。header 用於邊界檢查（payload_end）。
EntryStatus DecodeEntry(const std::uint8_t* data, std::size_t size,
                        const PackHeader& header, std::size_t slot, PackEntry& out);

// payload 內容完整性；呼叫端在真正要用該筆之前呼叫。
bool VerifyPayload(const std::uint8_t* data, std::size_t size, const PackEntry& entry);
```

`DecodeHeader` 的選取規則：兩個 slot 各自檢查 magic 與 CRC；皆無 magic → `BadMagic`；有 magic 但皆 CRC 不符 → `BothHeadersBad`；取 CRC 有效且 `generation` 較大者；若其 `schema_version > kPackSchemaVersion` → `NewerSchema`（`out` 仍填入以便診斷）；`index_capacity != kIndexCapacity` 視為 `BothHeadersBad`（本版不做容量遷移）。

輔助（供 NR-035 使用，仍為純值）：

```cpp
// stable ID 是 16 個十六進位字元（src/catalog/stable_id.h）。解析失敗回傳 false，
// 呼叫端則跳過快取（不得以 0 當成有效 hash）。
bool ParseStableIdHash(const std::wstring& stable_id, std::uint64_t& out);

// 混成來源檔的 last-write-time 與 size。任一不可得時呼叫端傳 0 表示改用 TTL。
std::uint64_t MakeSourceStamp(std::uint64_t last_write_time, std::uint64_t size);
```

### 2. 一個空檔的初始位元組（同檔）

```cpp
// 產生一個合法的空 pack：兩個 header slot（generation 1 與 0）、512 個空槽、
// payload_end = kPayloadStart。回傳長度恰為 kPayloadStart。
std::vector<std::uint8_t> MakeEmptyPack();
```

NR-035 用它建立新檔，測試也用它當基底再刻意破壞。

## Non-goals

- 不做檔案開啟、mmap、寫入、刪除、compaction（NR-035）。
- 不做 PNG 編解碼（NR-034）。
- 不做淘汰決策（NR-035 依 `last_used_utc` 與預算決定，本 item 只提供欄位）。
- 不做 schema v2 或任何遷移路徑。
- 不做壓縮（payload 已是 PNG）、不做加密。
- 不改 `IconKey`、`IconCache`、`IconWorker`、renderer 或任何既有模組。
- 不改 `stable_id` 產生方式。

## Acceptance

- `kPayloadStart == 28736`；`kIndexOffset == 64`；`kHeaderSize * 2 + kIndexCapacity * kIndexEntrySize == kPayloadStart`（以 `static_assert` 固定）。
- `MakeEmptyPack()` 長度為 `kPayloadStart`，`DecodeHeader` 回傳 `Ok`、`payload_end == kPayloadStart`，且全部 512 槽 `DecodeEntry` 回傳 `Free`。
- Header round-trip：任意 `PackHeader` 經 `EncodeHeader`／`DecodeHeader` 後每個欄位相等。
- Entry round-trip：任意 `PackEntry` 經 `EncodeEntry`／`DecodeEntry` 後每個欄位相等，含 `in_use == false` 的情形。
- **毀損分類逐項可驗證**：
  - 檔案長度 0、1、`kPayloadStart - 1` → `Absent`。
  - 兩個 slot 的 magic 都改掉 → `BadMagic`。
  - magic 保留、兩個 slot 各翻一個位元 → `BothHeadersBad`。
  - slot A 有效（generation 5）、slot B 損壞 → 回傳 `Ok` 且取到 generation 5；反之亦然。
  - 兩者皆有效、generation 分別為 7 與 8 → 取到 8。
  - `schema_version = 2` → `NewerSchema`。
  - `index_capacity = 256` → `BothHeadersBad`。
  - entry 內任一位元翻轉 → `CrcMismatch`，且**同一檔內其他槽仍為 `Ok`**（這條是本 item 的核心價值，必須有測試）。
  - `payload_offset = kPayloadStart - 1`、或 `payload_offset + payload_len = payload_end + 1`、或 `payload_len` 為 `0xFFFFFFFF` → `OutOfBounds`，且不發生越界讀取或大量配置。
  - `variant = 64` → `BadVariant`。
  - payload 位元翻轉 → `VerifyPayload` 為 false，但 `DecodeEntry` 仍為 `Ok`（entry 與 payload 的完整性彼此獨立）。
- `Crc32` 對已知向量正確：空輸入為 0、`"123456789"`（9 bytes ASCII）為 `0xCBF43926`。
- `ParseStableIdHash`：16 個合法十六進位字元成功、大小寫皆可；長度 15／17、含非十六進位字元、空字串皆回傳 false。
- 以任意長度的隨機位元組（至少 200 組，長度 0～`kPayloadStart + 1024`）餵給 `DecodeHeader`／`DecodeEntry`／`VerifyPayload`，全部函式皆正常回傳、不崩潰、不越界（在 sanitizer 或 Debug 建置下觀察）。隨機來源用固定 seed 的 `std::mt19937`，確保可重現。
- 模組不包含 `windows.h`（以 grep 驗證）。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icon_pack_format" --output-on-failure
ctest --test-dir build --output-on-failure
```

新測試 `tests/unit/icon_pack_format_test.cpp`（新 CTest 目標 `nimblerun_icon_pack_format_test`），涵蓋上述 Acceptance 每一條。全部純值，不建視窗、不寫檔。

## 交接區

- Start: 2026-08-05。NR-030 已 `done`（§10.2 有 `icons.cache` 條目），無阻塞。trace 完成：`stable_id.h` 的 `HashStableId` 輸出 16 個小寫 hex 字元；`icon_cache.h` 的 `kIconVariants` = {48, 96, 256}（DecodeEntry 的 variant 檢查直接重用）；`atomic_text_file.h` 的版本化/毀損慣例（本 item 為純值 codec，不碰 I/O）；`tests/unit/icon_cache_test.cpp` 的 `Expect`＋`wmain` 測試組織與 `tests/CMakeLists.txt` 註冊方式。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/catalog/stable_id.h`（`HashStableId` 輸出寬度與大小寫）、`src/icons/icon_cache.h`（`kIconVariants`）、`src/storage/atomic_text_file.h`（既有版本化與毀損處理慣例）、`tests/unit/` 內任一現有測試的組織方式與 `CMakeLists.txt` 註冊方式。先確認 NR-030 已 `done`，否則回報阻塞。只實作 Scope 1～2，不得建立 `icon_store` 或碰 WIC。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。新增 `src/icons/icon_pack_format.{h,cpp}`（加入 `nimblerun_icons` 庫）與 `tests/unit/icon_pack_format_test.cpp`（新 CTest 目標 `nimblerun_icon_pack_format_test`）。逐位元組 little-endian 讀寫、無 `#pragma pack`／`reinterpret_cast` 到結構體、無整結構 `memcpy`；`static_assert` 固定 64／28736／等式；模組無 `windows.h`。`DecodeEntry` 的空槽判定依 in-use flag（先於 CRC），否則全零空槽會因 CRC 非零被誤判為毀損。MakeEmptyPack 回傳長度恰 28736（slot A generation 1、slot B 0、512 空槽、payload_end=28736）。測試 18 組（含固定 seed fuzz 400 組、單槽翻轉不影響鄰槽、payload 翻轉 VerifyPayload false 但 DecodeEntry Ok）。`ctest -R icon_pack_format` 1/1、全套件 20/20 通過（含既有 19 項＋新增）、clean build 無 warning。
