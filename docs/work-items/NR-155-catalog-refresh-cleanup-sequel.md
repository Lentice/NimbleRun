# NR-155 — catalog_refresh 清理 sequel：SourceEntries 死碼刪除＋防偽守門抽共用 helper

Phase 3 · Code structure · Depends on: —（同檔的兩件小清理；NR-142/139 的續集）

- Source: `AGENTS.md`（Deletion over addition…）、NR-128/145（死碼與重複清理運動）
- Origin: 2026-08-10 第十四次稽核第 2 輪（ponytail 軸，LOW；兩件皆 high confidence）。
- Priority: **LOW**——零呼叫者死碼＋3 行重複守門；與 NR-139/142 同檔，順手收口。

## Why

`src/catalog/catalog_refresh.cpp` 兩件：

1. **`SourceEntries`（`:163-168`＋`catalog_refresh.h:112`）是全死碼**：grep 全部
   src/ 與 tests/ 零呼叫者（NR-128/NR-145 的刪除運動漏掉它；第四次稽核就列過）。
2. **NR-139 的防偽守門是兩份拷貝**：`ApplySourceResult`（`:121-124`）與
   `ApplySourceFailure`（`:146-149`）各有一段相同的「`generation_event_snapshot_`
   find 未命中即 `return false`」。NR-142 剛在同檔收斂的正是同型重複；未來若守門
   要改（例如加 generation 檢查順序），兩處會漂移。

## Decisions already made — do not reopen

1. `SourceEntries` 連同 header 宣告刪除（零呼叫者、零測試依賴）。
2. 抽 3 行私有 helper（anonymous namespace 或 coordinator 私有成員，依既有風格）
   `IsActiveGenerationSource(generation, source)`：generation 相符＋snapshot 命中才
   true；兩個 Apply 共用，NR-139 註解搬入。
3. **零行為變更**：既有 `catalog_refresh_test`／`rebuild_pipeline_test` 即回歸網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/catalog/catalog_refresh.cpp`：`:103-151`（兩個 Apply）、`:158-168`
  （`SourceEntries`）、`:84-101`（`BeginGeneration`）。
- `src/catalog/catalog_refresh.h`：`:112` 一帶。
- `tests/unit/catalog_refresh_test.cpp`（回歸網）。

## Scope

1. 刪 `SourceEntries`（含 header 宣告）。
2. 抽 `IsActiveGenerationSource` helper，兩個 Apply 共用。
3. 驗證：`git diff` 只含上述；全部測試通過。

## Non-goals

- 不順手改 `RebuildMerged`／`SeedSourceEntriesFromSnapshot`（後者是另一件事，有呼叫者）。
- 不新增測試目標。

## Acceptance

1. grep `SourceEntries` 在 src/ 與 tests/ 零命中。
2. `generation_event_snapshot_` 的 find 守門只有一份。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_refresh|rebuild_pipeline" --output-on-failure
```

```powershell
rg -n "SourceEntries|IsActiveGenerationSource" src/catalog
# expect: SourceEntries 零命中；IsActiveGenerationSource 定義一處、呼叫兩處
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作（2026-08-10）：

- **SourceEntries 死碼刪除**：`catalog_refresh.cpp` 的
  `CatalogRefreshCoordinator::SourceEntries`（含 `static const kEmpty` 回退值）與
  `catalog_refresh.h:112` 宣告一併刪除。grep `SourceEntries` 於 src/ 與 tests/ 零命中
  （殘留的 `SeedSourceEntriesFromSnapshot` 是不同方法，有呼叫者，未動）。
- **防偽守門抽共用 helper**：新增私有 const 成員
  `bool IsActiveGenerationSource(std::uint64_t generation, CatalogSource source) const`，
  header 宣告於 `ClearPendingIfEventUnchanged` 旁（同既有 private helper 風格）。
  `ApplySourceResult` 與 `ApplySourceFailure` 的開頭守門改為
  `if (!IsActiveGenerationSource(generation, source)) return false;`；原 NR-139 註解
  （forged `kRebuildDeliveryFailedMessage`）與「newer rebuild started」註解搬入 helper
  body，兩份拷貝合併為一份。後續 `generation_event_snapshot_.at(source)` 因 find 守門
  已過而保證命中，與原本的 `snapshot->second` 行為一致。
- **驗證**：Release Ninja llvm-mingw configure＋build 通過；`catalog_refresh.cpp` 零
  warning（build 全程唯一 warning 是 `main.cpp:1395` 既有未使用 `target_size`，與本
  item 無關）；`ctest --test-dir build --output-on-failure`：31/31 全綠（數量不變）；
  `ctest --test-dir build -R "catalog_refresh|rebuild_pipeline" --output-on-failure`：
  2/2。
- **未完成風險**：無。純 refactor，零行為變更。
