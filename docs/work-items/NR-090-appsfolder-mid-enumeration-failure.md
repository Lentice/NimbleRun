# NR-090 — AppsFolder 中途列舉失敗不得提交部分結果

Phase 2 · Depends on: NR-006, NR-011, NR-063

- Source: `docs/design-spec.md` §FR-008、§NFR-003、§12.1／§12.2
- Origin: 2026-08-08 全 repo 稽核（AppsFolder Shell enumerator 的成功／結束碼分流）
- Priority: HIGH（來源快照與 pins／usage 可能被部分結果逐步清掉）

## Why

`src/catalog/appsfolder_catalog.cpp` 的 `EnumerateAppsFolderCatalog()` 以
`IEnumShellItems::Next(1, &child, nullptr)` 逐項列舉，但目前把所有
`next != S_OK` 都當成正常結束：

```cpp
const HRESULT next = enumerator->Next(1, &child, nullptr);
if (next != S_OK) {
    break;
}
```

`S_FALSE` 的確代表正常走到結尾；但列舉中途的失敗 HRESULT 也會走同一個
`break`，`AppsFolderEnumerateResult::source_ok` 仍維持 `true`。因此已經收集到的
前綴 entries 會被 worker 當成完整成功結果提交：

1. `main.cpp` 的 rebuild worker 只依 `source_ok` 設定 `RebuildResult::failed`。
2. `CatalogRefreshCoordinator::ApplySourceResult()` 以部分 entries 取代舊的
   AppsFolder source。
3. `kRebuildDoneMessage` handler 記錄 `RecordAppsFolderSuccess()`，下一次面板
   顯示的 10 分鐘重試也被延後。
4. `RefreshPanelSnapshot()` 對部分 snapshot 做 usage／pin 對帳；消失的項目可能
   從 `usage.tsv` 移除，pin 也可能在 30 天保留期後被丟棄。

這是 NR-063 已修正的「來源級失敗回報」契約仍有一個中途漏網，不是重開 NR-063
的 COM 初始化／Known Folder／`BindToHandler` 早退路徑。`source_ok` 已經是正確的
邊界；本 item 只補齊 Shell enumerator 的 clean-end 判定。

## Decisions already made — do not reopen

1. `next == S_FALSE` 才是正常結束；`next != S_OK && next != S_FALSE` 一律是
   source-level failure，回傳 `source_ok = false`，保留舊 AppsFolder snapshot。
2. `next == S_OK` 但 `child == nullptr` 也視為來源失敗，先安全結束本次列舉，避免
   對空 COM 指標解參考；若有非空 `child`，先釋放它。
3. 子項目的 display/parsing name 取得失敗仍是既有的 per-item skip，繼續列舉並增加
   `failed_items`；不要把正常的單項瑕疵升級成整個來源失敗。
4. worker、coordinator、`RecordAppsFolderSuccess()` 的既有成功／失敗分派不重寫；
   修正後自然由 `source_ok` 進入 NR-063 已存在的 failure path。
5. 不新增 retry、timer、Shell COM mock 或 UI 通知。這是 OS enumerator failure
   path，沿用 NR-063「以正路徑測試＋sanity self-check，不發明失敗注入 seam」的
   最小方案。

## Binding constraints — quoted

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-003：

> Catalog 重建可取消或具世代編號；舊工作完成後不得覆蓋較新的結果。

`docs/development.md`：

> Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/appsfolder_catalog.h` — `AppsFolderEnumerateResult::source_ok` and
  `failed_items` contract established by NR-063.
- `src/catalog/appsfolder_catalog.cpp` — `EnumerateAppsFolderCatalog()` loop around
  `IEnumShellItems::Next`; `S_FALSE`／failure／null child are the only new decision
  points.
- `src/app_host/main.cpp` — rebuild worker's AppsFolder case and
  `kRebuildDoneMessage`; verify `source_ok` already routes failure to
  `ApplySourceFailure` and does not record success.
- `src/catalog/catalog_refresh.{h,cpp}` — existing source-failure preservation; do not
  change coordinator semantics.
- `tests/unit/appsfolder_catalog_test.cpp` — live AppsFolder smoke test and stable-id
  invariants.
- `docs/work-items/NR-063-source-failure-reaches-refresh.md` — prior source-level failure
  decision; this item extends its boundary without changing its decisions.

## Scope

1. In `EnumerateAppsFolderCatalog()`, distinguish clean end from failure:
   - `S_FALSE` ends the loop successfully.
   - Any other non-`S_OK` result sets `result.source_ok = false` and stops.
   - `S_OK` with a null `child` sets `source_ok = false` and stops without dereferencing
     the null pointer.
   - Release a non-null child on the failure branch before leaving the loop.
2. Keep all existing child extraction, program-like filtering, `failed_items`, identity,
   and launch-identity behavior unchanged.
3. Keep `main.cpp` and `catalog_refresh` unchanged unless tracing proves the existing
   `source_ok -> failed -> ApplySourceFailure` path is not used; no duplicate guard in
   the caller.
4. Add or update the smallest focused runnable check in
   `tests/unit/appsfolder_catalog_test.cpp`: the normal live enumeration must still
   report `source_ok == true` and preserve the existing entry invariants. Add a source
   code sanity self-check for the explicit `S_FALSE` versus failure branch; do not build
   a fake `IEnumShellItems` implementation solely to force an OS failure HRESULT.

## Non-goals

- Do not change Start Menu or user-folder `FindNextFileW` handling; that is a separate
  audit item because those sources have different multi-root / missing-root semantics.
- Do not change `CatalogRefreshCoordinator`, generation ordering, or snapshot merge rules.
- Do not add worker retries, polling, timers, cancellation, or a Shell COM abstraction.
- Do not show a balloon, dialog, or new UI text for a preserved source failure.
- Do not change stable IDs, AppsFolder filtering, launch identity, or cache schema.

## Acceptance criteria

1. A clean AppsFolder enumeration ending in `S_FALSE` returns `source_ok == true`.
2. A non-`S_OK`/non-`S_FALSE` `Next()` result returns `source_ok == false` and does not
   submit the collected prefix as a successful source result.
3. A null child is never dereferenced and is classified as source failure.
4. The existing NR-063 failure path remains the only consumer: failed AppsFolder results
   keep the prior AppsFolder entries and do not call `RecordAppsFolderSuccess()`.
5. Release build and the full CTest suite pass; no new warning is introduced.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# Clean end is explicit; the old `next != S_OK { break; }` conflation is gone.
Select-String -Path src/catalog/appsfolder_catalog.cpp -Pattern 'next == S_FALSE|source_ok = false'
# expect: an explicit S_FALSE branch and a failure branch that writes source_ok=false

# The existing failure handoff is still wired once at the host boundary.
Select-String -Path src/app_host/main.cpp -Pattern 'result->failed = !res.source_ok|ApplySourceFailure|RecordAppsFolderSuccess'
# expect: source_ok feeds result->failed; ApplySourceFailure remains reachable;
# RecordAppsFolderSuccess is only in the non-failed AppsFolder success arm.

git diff --check
git diff --name-only
# expect: src/catalog/appsfolder_catalog.cpp, tests/unit/appsfolder_catalog_test.cpp
# (plus this item and docs/work-items.md); no coordinator or new abstraction files.
```

## Handoff

已在 `src/catalog/appsfolder_catalog.cpp` 的 `IEnumShellItems::Next()` 邊界完成分流：
`S_FALSE` 是 clean end；其他非 `S_OK` HRESULT 與 `S_OK`／null child 都釋放可用 child、
標記 `source_ok = false` 並停止，避免部分 entries 走成功提交。既有 child extraction、
filter、`failed_items` 與 identity 行為未改。

`tests/unit/appsfolder_catalog_test.cpp` 的 live smoke check 明確驗證 clean enumeration
仍回報 `source_ok == true`，並保留原有 entry invariants 與 determinism checks。Sanity
self-check 確認 source code 有明確 `next == S_FALSE`／failure 分支，且
`main.cpp` 仍由 `source_ok` 導向 `ApplySourceFailure`，`RecordAppsFolderSuccess` 只在
成功臂。

驗證：`cmake -S ... -DCMAKE_BUILD_TYPE=Release`、`cmake --build build` 成功；提升權限
執行 `ctest --test-dir build --output-on-failure`，24/24 通過。`main.cpp`、
`catalog_refresh` 零改動；無偏差、無未完成事項。sandbox 初次 CTest 的失敗是 `%TEMP%`
fixture 寫入權限限制，非程式失敗。
