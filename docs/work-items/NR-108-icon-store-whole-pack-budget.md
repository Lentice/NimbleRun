# NR-108 — IconStore write path 必須遵守 whole-pack byte budget

Phase 3 · Icon cache integrity

- Source: `docs/design-spec.md` §NFR-001、§FR-009、§10.2
- Origin: 2026-08-09 全 repo 稽核；追蹤 `IconStore::Put`／`Flush` 的批次大小、eviction 與 `GrowView`
- Priority: MEDIUM（cache 可重建，但超額寫入會違反 release gate，下一次開啟還可能把 cache 判壞重建）

## Why

`kPackByteBudget` 是整個 `icons.cache` pack 的 32 MiB budget，但目前 write path 有兩個
獨立缺口：

1. `IconStore::Flush`（`src/icons/icon_store.cpp` 約 `:452-466`）先把本批所有 pending
   writes 標記為 `touched`，eviction 永遠不會淘汰這些新寫入；若一個 payload 或一個
   pending batch 本身超過可用 budget，兩段 eviction loop 會 `break`，仍繼續寫入。
2. `max_bytes_` 直接等於 `kMaxPackBytes`，但 live payload 的比較沒有扣除固定的
   `kPayloadStart` index/header bytes；`new_payload_end = header_.payload_end + new_bytes`
   也沒有在 `GrowView` 前以 whole-pack budget hard guard。

這與 NR-075 item 中「Flush 的 eviction keeps `payload_end` within [budget]」的假設不符；
新證據來自實際 batch flow，故本 item 明確覆寫該假設，不重開 NR-050/NR-075 已完成的
corrupt-header read guard。超額 header 之後會被 `DecodeHeader` 拒收，造成 cache self-poison
與不必要重建；在本次執行中也可能先觸碰超過產品資源上限的 mapping。

## Decisions already made — do not reopen

1. 32 MiB 的單一常數來源仍是 `kPackByteBudget`；不得新增第二個 magic number。
2. hard limit 也適用於 pinned entry 與單一 pending batch；不能靠「通常 PNG 很小」成立。
3. 超過可用空間的 pending icon 可丟棄或淘汰，cache 是 rebuildable accelerator；不得讓
   `GrowView`、`memcpy` 或 committed header 超出 budget。
4. 不改 pack schema、header/index layout、CRC、mmap 讀取策略或 user-data atomic policy。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001：

> `icons.cache` 檔案大小 ≤ 32 MiB（> 48 MiB 為阻擋門檻）。

`docs/design-spec.md` §FR-009：

> 該檔為可完全重建的加速器，任何毀損或版本不符都必須能在不損失使用者資料的前提下降級運作。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/icons/icon_pack_format.{h,cpp}` — `kPackByteBudget`、`kPayloadStart`、`DecodeHeader`。
- `src/icons/icon_store.{h,cpp}` — `Put`、`Flush`、`LivePayloadBytes`、`EvictOne`、`GrowView`、
  `Compact`、`kMaxPackBytes`。
- `src/icons/icon_worker.cpp` — `pending_puts_`、queue bound、normal／shutdown flush timing。
- `tests/unit/icon_store_test.cpp`、`tests/unit/icon_pack_format_test.cpp` — existing budget,
  corruption and compaction assertions。
- `docs/work-items/NR-035-icon-store-file.md`、NR-050、NR-068、NR-075 — preserve existing
  cache degradation and ownership decisions。

## Scope

1. Enforce whole-pack budget before mapping growth and payload copy, accounting for the fixed
   pack prefix and every payload in the current batch.
2. Define the minimal deterministic behavior for a payload/batch that cannot fit (drop or
   evict according to existing LRU/pin rules) while keeping index/header/file coherent.
3. Add focused tests for one oversized payload, an oversized multi-put batch, pinned entries,
   compaction, and the physical file/header `payload_end` upper bound.

## Non-goals

- 不引入 SQLite、第二種 cache file、壓縮格式或新的 eviction policy beyond the necessary hard guard。
- 不調整 icon variant、Shell provider、worker queue cap 或 UI rendering。
- 不把已量測的 render/search micro-optimization 重新開成工作項目。

## Acceptance

1. No successful `Flush` can leave `payload_end` or the physical `icons.cache` size above
   `kPackByteBudget` (including the fixed prefix and a full pending batch).
2. Oversized/pinned input degrades by dropping or evicting cache entries without crashing,
   corrupting headers, or disabling unrelated user data.
3. Reopening the resulting pack returns a valid `Ready`/empty-cache state; `DecodeHeader` and
   existing corruption/compaction tests remain green.
4. Release build has no new warnings and the focused icon-store test fails if the guard is removed.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon_store|icon_pack_format|icon_worker" --output-on-failure
```

```powershell
Select-String -Path src/icons/icon_store.cpp,src/icons/icon_pack_format.h -Pattern 'kPackByteBudget|kPayloadStart|new_payload_end|GrowView|LivePayloadBytes|EvictOne'
git diff --name-only
# expect: icon store/format 與 focused tests；不改 app catalog 或 UI。
```

## Handoff

實作者需記錄 batch budget equation、oversized/pinned policy、file-size/header evidence、
compaction outcome、build／CTest 與未涵蓋的 OS write-failure path。

