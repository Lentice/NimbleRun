# NR-196 — Name the legacy `recursive=true` depth instead of reusing a default

Phase 2 · Settings contract · Depends on: NR-193（done）

- Source: NR-193 交接檢查（2026-08-20）
- Origin: NR-193 完成後的獨立驗證發現一處隱式耦合，非功能缺陷
- Priority: **LOW**——純粹可讀性/可維護性收尾，行為不變且已被既有測試鎖住

## Goal

`SettingsStore::Load()` 遷移 schema=1 `catalog_root=path|true` 這一行時，目前寫成：

```cpp
CatalogRoot root;
root.path = path;
if (const auto parsed = ParseBool(raw_depth)) {
    root.max_depth = *parsed ? root.max_depth : kMinCatalogDepth;
}
```

`*parsed ? root.max_depth : kMinCatalogDepth` 這句能正確跑出 `true→20`，完全是因為它讀取的是剛預設建構的 `root.max_depth`（`CatalogRoot::max_depth = 20`）。這個對應關係只存在於「巧合」：`CatalogRoot` 的預設值恰好等於 NR-193 決定的「schema=1 true 應遷移成的深度」。兩者語意上是兩件事，未來若有人為了別的原因調整 `CatalogRoot::max_depth` 的預設值，這行遷移邏輯會在不知情的情況下被改變行為，而不會有任何編譯錯誤或明顯線索。

把這個值命名成一個獨立常數，讓遷移邏輯讀自己的常數，不再依賴 `CatalogRoot` 的預設成員值。

## 已確認的產品決策

1. 新增 `kDefaultCatalogDepth = 20`（`src/settings/settings_store.h`，與既有 `kMinCatalogDepth`/`kMaxCatalogDepth`同一位置），語意為「新增資料夾的預設深度，以及 schema=1 `recursive=true` 遷移的目標深度」。
2. `CatalogRoot::max_depth` 的成員預設值改用這個常數（`int max_depth = kDefaultCatalogDepth;`），而不是寫死的字面量 `20`。
3. `settings_store.cpp` 的遷移那行改成直接寫 `kDefaultCatalogDepth`，不再讀 `root.max_depth`：
   ```cpp
   root.max_depth = *parsed ? kDefaultCatalogDepth : kMinCatalogDepth;
   ```
4. 純重構、零行為改變：既有 `TestCatalogRootSchemaMigration`（`tests/unit/settings_store_test.cpp`，斷言 `schema=1 true→20`、`false→0`）必須原樣通過，不需要新增測試——這個既有測試就是本 item 的 regression guard。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Don't add features, refactor, or introduce abstractions beyond what the task requires.

## Files to read and trace first

- `src/settings/settings_store.h:19-24`（`CatalogRoot` 定義）、`:36-39`（`kMinCatalogDepth`/`kMaxCatalogDepth`）。
- `src/settings/settings_store.cpp:245-268`（`Load()` 的 `catalog_root=` 解析與 schema=1 遷移分支）。
- `tests/unit/settings_store_test.cpp` 的 `TestCatalogRootSchemaMigration`（既有測試，本 item 不新增測試，只確認它繼續通過）。
- 其餘讀取 `CatalogRoot::max_depth` 預設值的呼叫端（`settings_editor.cpp` 的 `AddRoot`）：確認換成常數後行為不變（`AddRoot` 若已明確傳入 `20`，不受影響；若依賴成員預設值，改成引用同一常數）。

## Scope

1. `settings_store.h`：新增 `inline constexpr int kDefaultCatalogDepth = 20;`，`CatalogRoot::max_depth` 改用它做預設值。
2. `settings_store.cpp`：遷移分支改讀 `kDefaultCatalogDepth`，不再讀 `root.max_depth`。
3. 若 `settings_editor.cpp::AddRoot` 或其他呼叫端有寫死的 `20` 字面量指涉同一語意（新增資料夾預設深度），一併換成 `kDefaultCatalogDepth`；沒有的話不必新增呼叫。

## Non-goals

- 不改變 `kMinCatalogDepth`(0)／`kMaxCatalogDepth`(50) 的邊界值或任何驗證邏輯。
- 不改變任何使用者可見行為、UI 或設定檔格式。
- 不新增測試（既有 `TestCatalogRootSchemaMigration` 已覆蓋）。
- 不處理本輪其他 item；範圍僅限這一個常數命名。

## Acceptance

1. `kDefaultCatalogDepth` 存在且等於 20，`CatalogRoot::max_depth` 的預設值與遷移分支的 `true` 分支都讀這個常數，不再有字面量 `20` 或隱式依賴 `root.max_depth` 的巧合寫法。
2. `tests/unit/settings_store_test.cpp` 的 `TestCatalogRootSchemaMigration`（以及其餘 catalog-root 相關案例）原樣通過。
3. Release build 無新增 warning；完整 CTest 通過。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R "settings" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kDefaultCatalogDepth" src/settings
```

## Handoff requirements

交接時記錄：

- `kDefaultCatalogDepth` 的定義位置與所有使用點。
- Agent checks 的完整命令與結果。

## 交接區

- `kDefaultCatalogDepth = 20` 定義於 `src/settings/settings_store.h:36-39`（與 `kMinCatalogDepth`/`kMaxCatalogDepth` 同一位置）；`CatalogRoot::max_depth` 的成員預設值（`settings_store.h:47`）改用它。
- `settings_store.cpp`（`Load()` 的 schema=1 遷移分支，約 `:259-261`）改讀 `kDefaultCatalogDepth`／`kMinCatalogDepth`，不再讀剛建構的 `root.max_depth`。
- 額外發現並修正同一巧合：`src/app_host/settings_dialog.cpp:604` 的 `AddRoot(path, 20)` 字面量換成 `AddRoot(path, kDefaultCatalogDepth)`——原本的 `20` 語意上就是同一個「新增資料夾預設深度」，只是没命名。
- 未新增測試：`tests/unit/settings_store_test.cpp` 既有的 `TestCatalogRootSchemaMigration`（斷言 schema=1 `true`→20、`false`→0）作為 regression guard，改動後原樣通過。
- Agent checks：
  - `cmake --build build`：成功，無新增 warning。
  - `ctest --test-dir build -R "settings" --output-on-failure`：2/2 passed。
  - `ctest --test-dir build --output-on-failure`：33/33 passed。
  - `rg -n "kDefaultCatalogDepth" src/settings src/app_host/settings_dialog.cpp`：命中 `settings_store.h` 定義處、`settings_store.cpp` 遷移分支、`settings_dialog.cpp` 的 `AddRoot` 呼叫。
