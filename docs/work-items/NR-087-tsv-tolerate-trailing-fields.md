# NR-087 — usage.tsv／catalog.cache 容許新增未預期的尾端欄位，而非整檔隔離

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.4（快取／使用者資料的 schema 升版與損毀處理）
- Origin: 2026-08-08 第六次全 repo 稽核（schema 升版與設定檔解析穩健性）

## Why

`UsageStore::Load`（`src/usage/usage_store.cpp:49`）與 `LoadCatalogCache`
（`src/catalog/catalog_cache.cpp:128`）對每一列都要求**欄位數完全相等**
（`fields.size() != 3` / `!= kFieldCount`），不相等就整檔判為 `Corrupt`
（usage.tsv：`PreserveCorrupt` ＋ `records_.clear()`）或整檔捨棄重建
（catalog.cache：本來就是可重建的快取，這條路徑本身低風險）。

這與同一個 repo 內既有的、經過驗證的做法不一致：`SettingsStore::Load`
（`src/settings/settings_store.cpp:246`）是 key=value 格式，明文「Unknown keys
are ignored so a same-schema file stays readable」；`PinStore::Load`
（`src/pins/pin_store.cpp:52-54`）對 schema=1（2 欄）／schema=2（3 欄）都接受，
是「多欄位」情境下最接近的既有先例，但也只到「已知的兩個欄位數」為止，
沒有「未知的第 4 欄」的容忍分支。

具體風險場景：未來版本要在 `usage.tsv` 加一個新欄位（例如一個啟動情境旗標）時，
如果沒有同步把 `kSchemaVersion` 從 1 升到 2（一個很容易漏掉的人為步驟——目前
只有 pins 走過這個流程一次，usage／catalog 從未升過版，没有肌肉記憶），新版寫出
4 欄的一列會被舊版或漏改的程式碼當成「欄位數不對」→ `PreserveCorrupt`＋
`records_.clear()` →**使用者的整份 usage 歷史被隔離成 `.corrupt`，從即時路徑消失**
（`usage.tsv` 是使用者資料，不是快取，design-spec §10.4 的「非 Loaded 則空」契約
在這裡代價是常用排序與最近清單全部歸零，不是「重建即可」的小事）。

這不是「重開已否決方向」：NR-057 交接區明確否決的是「預先蓋一套通用 migration
框架」（`docs/work-items/NR-057-versioned-store-reader.md`「要升版時再開 item」），
本 item 不是框架，是把**現有三個 TSV reader 的欄位數比較從『相等』放寬成『至少』**
——與 `SettingsStore` 已經在用、已經驗證過的「忽略未知欄位」精神一致，換到
TSV 的位置式解析上就是「欄位數 ≥ 必要數，多出的欄位單純忽略」，行數與改動幅度
是防禦性解析的最小修正，不是新機制。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **只改三個 reader 的欄位數比較符號**：`usage_store.cpp:49`、
   `catalog_cache.cpp:128`、`pin_store.cpp:54` 的 `!=` 系列比較，改成「欄位數
   小於必要最小值才視為錯誤，大於等於則多出的欄位忽略」。不新增欄位、不新增
   schema 版本、不寫任何「讀取未知欄位」的邏輯——未知欄位就是單純不讀。
2. **不做通用「migration 框架」**：不加欄位名稱／型別的通用描述表、不加
   per-field 的可選/必要標記系統。這維持 NR-057 交接區的既有決策；本 item
   只是把現有位置式解析從「精確匹配」改成「至少匹配」，解析邏輯本身仍是
   手寫的欄位存取，與現狀相同。
3. **不動 `NewerSchema`／`OlderSchema`／`Malformed` 的既有分流**：本 item 處理
   的是「schema 版本沒變、但欄位數對不上」這個目前沒有任何分支覆蓋的中間地帶，
   不是版本比較邏輯本身。三者的既有行為（`ReadVersionedLines` 回傳值分流）原樣
   保留。
4. **`pin_store.cpp` 的既有兩分支（2 欄／3 欄）改寫成「≥2 欄」**，第 3 欄
   （`display_name`）存在就讀、不存在就空，第 4 欄以後一律忽略。不影響 NR-062
   的既有測試（2 欄與 3 欄兩種輸入的行為不變，只是新增「≥4 欄」時不再判
   corrupt）。
5. **不補「未來欄位」的假設欄位**：三個 store 的資料結構（`UsageRecord`、
   `AppEntry`／catalog 行、`PinRecord`）都不新增欄位，本 item 純粹是解析容錯，
   不是為不存在的欄位預先鋪路。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.4（既有條款，本 item 不新增）：
持久化檔案的 schema 升版與損毀處理契約——較新 schema 不覆寫原檔、使用者資料損毀
時回退安全預設值。本 item 處理的是「同一 schema 版本內欄位數波動」，不變更此契約。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.
- New non-trivial logic needs one focused runnable test or self-check.

`docs/work-items/NR-057-versioned-store-reader.md` 交接區：

> 預先蓋一套 migration 框架就是替不存在的需求寫程式……要升版時再開 item

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/usage/usage_store.cpp:43-71` — `UsageStore::Load` 的逐列解析，
  `fields.size() != 3`（`:49`）是本 item 的主要修改點。
- `src/catalog/catalog_cache.cpp:121-135` — `LoadCatalogCache` 的逐列解析，
  `fields.size() != kFieldCount`（`:128`）。
- `src/pins/pin_store.cpp:46-75` — `PinStore::Load`，NR-062 既有的
  `fields.size() != 2 && fields.size() != 3`（`:54`）分支，本 item 改成
  `< 2`。
- `src/settings/settings_store.cpp:232-247` — 既有的「未知 key 忽略」先例，
  本 item 的容錯精神來源，不是照抄程式碼（key=value 與位置式 TSV 結構不同）。
- `tests/unit/pin_store_test.cpp` 內 `TestOlderSchemaPinsLoad`
  （約 `:303-320`）— 既有的 2 欄／3 欄測試，確認本 item 的修改不破壞它，並在
  它旁邊加一個「4 欄輸入」的測試。
- `docs/work-items/NR-080-store-load-partial-state.md`、
  `NR-072-refreshpins-preserve-newer-schema.md` — 「非 Loaded 則空」契約的既有
  決策，本 item 不觸碰該契約，只是讓更少的合法輸入落入需要套用該契約的
  `Corrupt` 分支。

## Scope

1. `usage_store.cpp:49`：`fields.size() != 3` → `fields.size() < 3`。第 4 欄
   以後忽略（尚無語意，單純不讀）。
2. `catalog_cache.cpp:128`：`fields.size() != kFieldCount` →
   `fields.size() < kFieldCount`。
3. `pin_store.cpp:54`：`fields.size() != 2 && fields.size() != 3` →
   `fields.size() < 2`；`:66` 的 `fields.size() == 3` 改成
   `fields.size() >= 3`（第 3 欄仍是 `display_name`，第 4 欄以後忽略）。
4. 三處都不改寫入端（`SerializeEntry`／`Save` 等）：本 item 只放寬讀取端的
   容錯，寫入端欄位數不變。

## Non-goals

- 不新增任何 schema 版本或新欄位。
- 不做通用 migration／欄位描述框架（Decisions §2）。
- 不改 `ReadVersionedLines` 的 header／schema 版本比較邏輯。
- 不處理「欄位數變少」（欄位被移除）的情境：目前三個 store 都還沒發生過欄位
  移除，且移除欄位必然伴隨欄位語意改變，需要當下的欄位定義決定怎麼處理，
  超出「放寬欄位數下限」這個小修正的範圍，真的要移除欄位時再開 item。

## Acceptance

Automated：

1. `ctest --test-dir build --output-on-failure` 全綠，新增至少一項針對
   「欄位數多於目前定義」的輸入測試（usage 或 pins 擇一即可，兩者邏輯相同）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 三處欄位數比較都改成下限，不再要求完全相等：
Select-String -Path src/usage/usage_store.cpp -Pattern 'fields.size\(\) < 3'
Select-String -Path src/catalog/catalog_cache.cpp -Pattern 'fields.size\(\) < kFieldCount'
Select-String -Path src/pins/pin_store.cpp -Pattern 'fields.size\(\) < 2'
# expect: 三者皆命中一次

git diff --name-only
# expect: src/usage/usage_store.cpp、src/catalog/catalog_cache.cpp、
#         src/pins/pin_store.cpp、對應測試檔（及本 item 文件與
#         docs/work-items.md 追蹤表格）
```

## 交接區

實作已存在於既有本地 commit `09de537`：三個 reader 的欄位數檢查改為下限，並在
`tests/unit/pin_store_test.cpp` 加入 schema=2 四欄輸入的 `TestLoadTrailingFieldsIgnored`。
寫入端與 schema 分流未改。Release build 成功，完整 CTest **24/24 通過**，三處
sanity greps 各命中一次。手動驗收未執行；本次 opencode job 為 clean-worktree 驗證，
沒有新增 patch。未完成事項：無。
