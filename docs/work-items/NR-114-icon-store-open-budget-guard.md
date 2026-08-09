# NR-114 — IconStore 開啟時拒絕超過 whole-pack budget 的實體檔案

Phase 3 · Icon cache integrity · Depends on: NR-075, NR-108

- Source: `docs/design-spec.md` §NFR-001、§FR-009、§10.2
- Origin: 2026-08-09 第十次全 repo audit；追蹤 `MapFile` → `DecodeHeader` → `ScanIndex` → `Open` 的 read path
- Priority: IMPORTANT（合法 CRC 的 oversized trailing bytes 可使 cache 在超過 whole-pack 目標時仍進入 Ready）

## Why

`src/icons/icon_store.cpp:53-83` 的 `MapFile()` 先依實體 `GetFileSizeEx` 建立完整 mapping；
`Open()` 隨後才呼叫 `DecodeHeader()`。`src/icons/icon_pack_format.cpp:105-144` 目前只檢查
header 的 `payload_end <= size`、`payload_end <= kPackByteBudget` 與下限，沒有檢查實體 `size`
本身不得超過 `kPackByteBudget`。因此可構造雙 header CRC 正確、`payload_end == kPayloadStart`、
但檔案尾端多出 bytes 的 `icons.cache`：header 會回傳 `PackStatus::Ok`，`Open()` 進入 `ScanIndex`
並標記 `StoreState::Ready`，即使 physical file size 已超過 32 MiB。

NR-075 已封住 `payload_end` 超界與巨大 payload read；NR-108 已封住 successful Flush 的 whole-pack
equation，但兩者都沒有封住「Open 前 physical file size 已超額、header payload_end 仍合法」這條輸入路徑。
Icon cache 可完全重建，故應安全降級，不應把 oversized mapping 當成可接受的 Ready cache。

## Decisions already made — do not reopen

1. `kPackByteBudget` 是唯一 32 MiB 常數；read path 與 write path 必須共用，不新增第二個 budget magic number。
2. 超過 physical budget 的 `icons.cache` 是可重建 cache corruption；可刪除／重建或停用，不得影響
   settings、pins、usage 等 user data，也不得讓 Open 先把超額檔案視為 Ready。
3. 保留 dual-header、CRC、fixed index、mmap、append 與 `.tmp` + replace 設計；只補 Open-time
   whole-pack guard，不重寫 pack schema。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001：

> `icons.cache` 檔案大小 | ≤ 32 MiB | > 48 MiB

`docs/design-spec.md` §FR-009：

> 該檔為**可完全重建的加速器**，任何毀損或版本不符都必須能在不損失使用者資料的前提下降級運作。

`docs/design-spec.md` §10.2：

> 可完全重建的快取（`catalog.cache`、`icons.cache`）允許以 append 方式就地追加，但 compaction
> 與整檔重寫仍須走 `.tmp` ＋ replace。

`AGENTS.md`：

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/icons/icon_pack_format.{h,cpp}` — `kPackByteBudget`、`kPayloadStart`、`DecodeHeader`、`PackStatus`。
- `src/icons/icon_store.{h,cpp}` — `MapFile`、`Open`、`ScanIndex`、`Compact`、`GrowView`、`file_size_`、
  `max_bytes_`，以及 over-budget write guard（NR-108）。
- `src/icons/icon_worker.cpp` — Open／flush／rebuild lifecycle；確認 Open 降級不讓 pending queue 無界。
- `tests/unit/icon_pack_format_test.cpp`、`tests/unit/icon_store_test.cpp`、CMake test registration。
- `docs/work-items/NR-050-icon-pack-hardening.md`、NR-075、NR-108 — 保留既有 corruption、memory guard、
  whole-pack write decisions。

## Scope

1. 在 mapping／header acceptance 的最窄共用邊界拒絕 `file_size > kPackByteBudget`；不要只看
   `payload_end`，也不要先把 oversized file 標為 Ready 再等 Flush 修理。
2. 定義並實作 cache-only 的最小降級：over-budget file 會被安全重建、停用或等價地回到 bounded
   empty state；`StoreStats`／diagnostic 可區分此類 input，但不可把路徑或 user data 寫入 log。
3. 保留 `file_size == kPackByteBudget` 的合法邊界，並確認 `payload_end == kPackByteBudget` 仍可讀；
   only trailing bytes over budget 的 header 也必須被拒絕。
4. 新增 focused tests：雙 header CRC 合法且 physical size 為 budget+1、payload_end 為 payload start；
   exact budget boundary；Open 結果、重建後檔案大小與 header validity。必要時補一個 `DecodeHeader`
   pure-format assertion，避免把 OS mapping 行為全塞進 integration test。

## Non-goals

- 不改 `kPackByteBudget` 數值、pack schema、index layout、CRC、LRU policy、icon variants 或 Shell provider。
- 不把 oversized cache 當作 user-data corruption，不 preserve `.corrupt` 副本來累積磁碟使用量。
- 不引入 SQLite、壓縮、第二個 cache 檔、service、network 或 administrator requirement。
- 不調整 NR-108 的 pending batch／eviction write equation；本 item 只補 Open-time physical-size acceptance。

## Acceptance

1. `IconStore::Open()` 永遠不會把 physical `icons.cache` size 大於 `kPackByteBudget` 的檔案以
   `StoreState::Ready` 接受；不會先建立超額 mapping 後才繼續正常使用。
2. `file_size == kPackByteBudget` 且 header／index 合法仍可 Ready；`payload_end` 仍受既有上下界保護。
3. Over-budget cache 降級後不崩潰、不寫壞 settings／pins／usage，重建後檔案回到 bounded empty／valid pack。
4. Focused icon format/store tests、Release build 與完整 CTest 通過，無新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icon_pack_format|icon_store|icon_worker" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kPackByteBudget|kPayloadStart|DecodeHeader|MapFile|file_size_|StoreState::Ready" src/icons tests/unit
git diff --name-only
# expect: icon format/store 與 focused tests；不改 catalog、UI 或 user-data stores。
```

## Handoff

實作者需記錄 exact-boundary 與 trailing-bytes fixture、Open state transition、重建後 physical size、
header／CRC 結果、memory mapping 是否在拒絕前建立、build／CTest 與未涵蓋的 OS file-lock／mapping failure path。

實作（2026-08-09，single clean worker）：

- **guard 形狀**：`IconStore::MapFile`（`src/icons/icon_store.cpp:67-75`）在 `GetFileSizeEx` 成功、`CreateFileMappingW`
  之前拒絕 `physical size > kPackByteBudget`（`Unmap()` 後 `SetLastError(ERROR_FILE_TOO_LARGE)` 回 false）——
  不建立超額 mapping。`Open()` 的 MapFile-failed 分支先擷取 `GetLastError()`，`ERROR_FILE_TOO_LARGE` 分支設
  `stats_.recreated = true`＋log `over-budget-recreated`，落入既有 `CreateEmptyPack() && MapFile()` 重建路徑；
  其餘錯誤維持 `open-failed`→Disabled、`ERROR_FILE_NOT_FOUND`→created。未新增 `StoreState`／`PackStatus` 值。
- **pure-format 守衛**：`DecodeHeader`（`icon_pack_format.cpp:110-113`）在 `size < kPayloadStart → Absent` 之後加
  `size > kPackByteBudget → BothHeadersBad`，讓格式層自衛。**覆寫 NR-075 dual-header「sane sibling wins」的
  實體大小假設**：physical size 超過 budget 時整檔拒絕；in-budget 檔案內單槽 over-budget 仍由 sane slot 取勝
  （該規則保留）。`kPackByteBudget` 為唯一 32 MiB 常數，未新增第二個 magic number；`max_bytes_` 不用作 guard。
- **fixture**：
  - `icon_pack_format_test`：`TestPackByteBudget` case 3（physical budget+1、slot B sane）期望由 Ok 翻轉為
    `BothHeadersBad`（附 NR-114 override 註解）；新增 trailing-bytes case（physical budget+1、雙槽
    `payload_end == kPayloadStart`、CRC 正確 → `BothHeadersBad`）；exact boundary（size==budget、payload_end==budget → Ok）保留。
  - `icon_store_test`：新增 `TestOverBudgetPhysicalFile`——雙槽 `payload_end == kPayloadStart` 且 CRC 正確的
    `budget+1` 實體檔 → `Open()==Ready`＋`recreated==true`＋entries==0＋檔重建為恰 `kPayloadStart` bytes。
- **Open state transition**：over-budget 實體檔 → MapFile 拒絕（未建 mapping）→ recreated 分支 → bounded empty pack
  → Ready。`ScanIndex` 保留 `stats_.recreated`。
- **既有測試回歸**：`TestOverBudgetPack`（NR-075）現在先被 MapFile 實體大小規則攔下（payload_end 不再觸及），
  結果相同（Ready＋recreated＋重建為 kPayloadStart）；`TestMaliciousPayloadEnd` 檔案小，仍由 DecodeHeader
  slot 檢查驅動重建，兩者皆綠。
- **build／CTest**：Release x64（LLVM-MinGW＋Ninja）無新增 warning；focused 3/3 綠（icon_pack_format、
  icon_store、icon_worker）；完整 CTest 25/25 綠（含 lifecycle_check）。
- **未涵蓋**：OS file-lock／mapping failure 仍走既有 `open-failed`→Disabled 路徑（未改）；`Compact`／`GrowView`
  產生的檔案永遠 ≤ budget，guard 不會誤傷合法 pack。commit：`<controller fills after commit>`
