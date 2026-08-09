# NR-116 — Cold-start cache ＋ 首輪 source failure 必須保留該來源的快取行（及其 usage 紀錄）

Phase 3 · Catalog snapshot retention · Depends on: NR-011, NR-063, NR-113

- Source: `docs/design-spec.md` §FR-008、§10.2、§NFR-003
- Origin: 2026-08-09 第十次全 repo audit 後的 fresh read-only audit；沿
  `SetSnapshot` → 首輪 rebuild → `ApplySourceFailure` → `RebuildMerged` → `RefreshPanelSnapshot` 的 Reconcile 追蹤
- Priority: IMPORTANT（valid cache 冷啟動＋首輪單一來源失敗時，該來源的快取行被整批丟棄，
  usage.tsv 對應紀錄被 Reconcile 刪除——使用者資料損失＋違反 §FR-008）

## Why

`CatalogRefreshCoordinator::SetSnapshot`（`src/catalog/catalog_refresh.cpp:160-177`）只把
`LoadCatalogCache` 的快取寫進 `merged_`，**從不 seed `source_entries_`**；`RebuildMerged`
（`:195-204`）卻只從 `source_entries_` 重建 merged snapshot。冷啟動流程：
`LoadCatalogCache` → `SetSnapshot(cached)` → 背景 `StartRebuild({StartMenu, AppsFolder, UserFolder})`
（`src/app_host/main.cpp:3613`、`:3754-3761`）。

若**首輪** rebuild 中某來源失敗（例如 AppsFolder COM 列舉失敗，正是 NR-090/NR-095 處理的類別），
`ApplySourceFailure`（`catalog_refresh.cpp:128-146`）「保留該來源舊 entries」——但 `source_entries_`
在冷啟動下對該來源是空的，無舊 entries 可保留。三個來源都回報後 `RebuildMerged` 只含成功來源的
fresh entries，**cache 中該失敗來源的所有行被整批丟棄**：

- `OnGenerationCompleteRefresh` → `RefreshPanelSnapshot`（`main.cpp:1450-1466`）對「非空但已縮水」的
  snapshot 跑 `g_usage->Reconcile`（`main.cpp:1422-1424`；`usage_store.cpp:153-170` 只擋空 catalog，
  不擋縮水）並 `Save()` → **該失敗來源 App 的 usage.tsv 紀錄被永久刪除**（§10.2 usage.tsv 是使用者資料）。
- `SaveCatalogCache`（`main.cpp:1462-1465`）把縮水版寫回 cache。

這違反 §FR-008「單一來源失敗時保留該來源舊結果及其他來源的新結果」：冷啟動下「該來源舊結果」就是
cache 中該來源的行。NR-081 只修了 on-demand AppsFolder 取代進行中 rebuild 的守門，NR-063 建立了
failure path 但依賴 `source_entries_` 已有舊值；兩者都未覆蓋「cache 只進 `merged_`」的冷啟動缺口。
NR-113 交接區曾假設「首輪失敗時 cache row 的未驗證 identity 會留在 snapshot 直到後續成功重新驗證」——
本 item 的追蹤證明那是錯的（行會被丟棄），此發現是**覆寫該交接區假設的新證據**，但不否定 NR-113 的
launch guard（未驗證行仍不可啟動）。

## Decisions already made — do not reopen

1. 冷啟動下失敗來源的「舊結果」= cache 中歸屬於該來源的行；首輪失敗不得把這些行從 snapshot 丟棄。
2. cache 行保留為 NR-113 的 unverified（`launch_verified == false`）：可顯示、不可啟動；該來源後續
   成功 enumeration 時以 fresh verified entries 整批取代。
3. 同一 stable_id 同時存在「未驗證 cache 行」與「fresh verified 行」時，**verified 行必須勝出**（它是
   目前來源的真實結果，且可啟動）；不能讓未驗證行靠既有 source-priority 把 verified 行 dedup 掉。
4. 只在**啟動 cache load** 時 seed `source_entries_`；`RebuildMerged` 內部的 `SetSnapshot` 不得再 seed
   （會以 dedup 勝者覆寫各來源的 losers，破壞下一次失敗保留）。
5. 不新增 timer、執行緒、輪詢；不動 NR-113 launch guard、NR-077 token registry、cache schema。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 單一來源失敗時保留該來源舊結果及其他來源的新結果。

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。

`docs/design-spec.md` §10.2：

> `usage.tsv`：版本化 UTF-8 TSV；欄位為 stable ID、總啟動數、7／30 日 buckets 或必要時間資料、最後啟動 UTC。

`docs/design-spec.md` §NFR-003：

> 任一使用者資料夾不存在、無權限或無法列舉時，不得清空其他 Catalog 來源。

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/catalog_refresh.{h,cpp}` — `SetSnapshot`（`:160-177`）、`source_entries_`（`:110`）、
  `RebuildMerged`（`:195-204`）、`ApplySourceFailure`（`:128-146`）。主場之一。
- `src/catalog/dedup.cpp` — `Beats`（`:58-63`）與 `DeduplicateCatalog`。主場之二。
- `src/app_host/main.cpp` — 啟動 cache load（`:3600-3613`）、`RefreshPanelSnapshot` 的 Reconcile
  （`:1422-1424`）、`OnGenerationCompleteRefresh`（`:1450-1466`）。
- `src/usage/usage_store.cpp:153-170` — `Reconcile`（**只讀不改**，確認「空 catalog 才不刪」語意）。
- `tests/unit/catalog_refresh_test.cpp`、`tests/unit/identity_dedup_test.cpp` — 新 case 的家；
  NR-113 的 `TestCacheLoadEntriesAreUnverified` 與 NR-063/100/106 的 failure tests 是既有形狀。
- `docs/work-items/NR-011-catalog-refresh.md`、NR-063、NR-113 — 保留既有 snapshot／failure／
  launch-provenance 決策；不要編輯已完成 item 文件。

## Scope

1. 新增 coordinator 方法（例）`void SeedSourceEntriesFromSnapshot();`：把目前 `merged_` 依
   `AppSource`→`CatalogSource` 對映（UserStartMenu／CommonStartMenu→StartMenu、AppsFolder→AppsFolder、
   UserFolder→UserFolder）分批寫入 `source_entries_`，**保留每個 entry 原樣（含 `launch_verified`）**。
   不觸碰 `pending_`／`last_event_ms_`／generation state。host 啟動在 `refresh.SetSnapshot(std::move(cached))`
   之後呼叫一次（`main.cpp:3613` 一帶）。`RebuildMerged` 內的 `SetSnapshot` 呼叫**不得**再觸發 seed。
2. `dedup.cpp` 的 `Beats` 加 verified 優先規則：`candidate.launch_verified != kept.launch_verified` 時，
   verified 者勝（放在 IsShortcut／source-priority 之前）。enumeration 產出的 entry 全為 verified
   （同質），此規則只影響混合來源的冷啟動／失敗保留情境，不改變既有排序。
3. 保留語意：失敗來源的快取行維持 unverified（NR-113 guard 擋啟動）；該來源下次成功時被整批取代；
   完整成功 rebuild 後 `source_entries_` 全是 fresh verified。
4. 新增 focused tests：
   - `catalog_refresh_test`：seed `SetSnapshot` 產生的 snapshot → `BeginGeneration` 三來源 → 一來源
     `ApplySourceFailure`、另兩來源 `ApplySourceResult` → 斷言最終 snapshot 含失敗來源的 seed 行
     （unverified）與成功來源的 fresh 行；generation 完成、`IsRebuildInProgress()` false。此斷言直接
     防止「縮水 snapshot → usage Reconcile 刪除」的資料損失路徑。
   - `identity_dedup_test`：同一 stable_id 一個 verified、一個 unverified → verified 勝出；兩者皆
     verified 時既有 source-priority 不變。
   - 既有 NR-113 `TestCacheLoadEntriesAreUnverified`、NR-063/100/106/115 failure tests 原樣通過。

## Non-goals

- 不改 `catalog.cache`／`icons.cache` schema、不重寫 enumeration、不改 NR-113 launch guard、
  不改 NR-077 token registry、不加 timer／執行緒／輪詢。
- 不把 cache 行變成 verified；不把「完整 rebuild 成功後仍保留 cache 行」當成目標（成功來源以 fresh 取代）。
- 不新增第二份 merged-vs-source 對帳邏輯；seed 只在啟動 cache load 發生一次。

## Acceptance

1. 冷啟動載入 valid cache 後，首輪 rebuild 中任一來源失敗：該來源的 cache 行仍顯示於 snapshot
   （unverified），成功來源的 fresh 行同時存在；`IsRebuildInProgress()` 最終 false。
2. 同一 stable_id 的 verified 行勝過 unverified 行；既有 source-priority／shortcut 規則在 verified
   同質時不變。
3. usage Reconcile 不再因單一來源失敗而刪除該來源的紀錄（snapshot 未縮水）；既有「空 catalog 不刪」
   行為不變。
4. Release build 無新增 warning；focused tests、完整 CTest 與既有 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|identity_dedup" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SeedSourceEntriesFromSnapshot|launch_verified|ApplySourceFailure|SetSnapshot" src tests
git diff --name-only
# expect: coordinator seed + dedup + focused tests；不改 settings、pins、usage schema。
```

## Handoff

實作者需記錄 seed 方法形狀、verified 優先規則在 `Beats` 的位置、冷啟動 fixture、失敗來源行的
unverified 保留、generation 完成證據、usage 資料損失路徑被關閉的方式、build／CTest 結果與
任何未涵蓋的跨來源 stable_id 收斂邊角。