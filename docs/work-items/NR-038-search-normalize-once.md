# NR-038 — Normalize catalog names once, rank on indices

- Status: `done`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §FR-002／§FR-003／§NFR-001、`docs/performance-baseline.md`

## Goal

移除每次按鍵重複做的名稱正規化。`AppEntry::normalized_name` 目前**從來沒有被任何 catalog 來源填寫過**，導致 `SearchApps()` 對 catalog 每一筆都即時呼叫 `LCMapStringEx` 兩次並配置多個 `std::wstring`——而且每次按鍵都重算一模一樣的結果。本 item 把這份工作搬到「每次 catalog snapshot 一次」，並讓排序改在 8 bytes 的索引上進行。

改動後搜尋結果的順序與內容**必須完全不變**；這是純效能 item。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-002／§FR-003／§NFR-001、`docs/work-items.md`、`docs/work-items/NR-007-identity-dedup.md`、`docs/work-items/NR-011-catalog-refresh.md`、本文件。

## 現況事實（已查證，不需重新推導）

- `src/catalog/app_entry.h:18` 宣告 `std::wstring normalized_name;`。
- 全 repo 只有 `src/catalog/catalog_cache.cpp`（第 3 欄，讀 `:168`／寫 `:73`）與 `src/search/search_engine.cpp` 碰這個欄位。**三個 catalog 來源** `start_menu_catalog.cpp`、`appsfolder_catalog.cpp`、`user_folder_catalog.cpp` 都不設它。
- 因此 `search_engine.cpp:112` 的 `NormalizedName()` 實務上永遠 fallback 到 `display_name`，`:136` 的 `Normalize()` 對每筆重算。
- merged snapshot 的唯一擁有者是 `CatalogRefreshCoordinator`，寫入點只有兩個：`RebuildMerged()`（`catalog_refresh.cpp:141`）與 `SetSnapshot()`（`:113`，冷啟動由 `main.cpp:2016` 從 cache 載入時呼叫）。
- 查詢入口是 `main.cpp:1721` 的 `EN_UPDATE` → `PanelModel::SetQuery` → `PanelModel::RefreshRows`（`panel_model.cpp:65`）→ `SearchApps`，同步、無 debounce。

## 與既有 item 的關係（重要）

- **本 item 覆寫先前對 `normalized_name` 的隱含假設**：該欄位過去無人填寫，等同 dead field。自本 item 起，凡是經由 `CatalogRefreshCoordinator` 發布的 snapshot，`normalized_name` 一律為已正規化的非空值（`display_name` 本身為空者除外）。
- **不回頭修改** NR-007／NR-011 文件；覆寫指示只寫在本文件。
- `stable_id`、`display_name`、`launch_identity`、`source_path` 一字不改，既有 pin 與使用紀錄零遷移。
- `catalog_cache` 的檔案格式與欄位數不變、**不升版**；舊 cache 檔第 3 欄為空，載入後由本 item 的填寫點自動補上。

## 硬約束

- 核心邏輯不得依賴 HWND 或 Shell COM：正規化與比對必須是純值運算，可在不建視窗下測試。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限。
- 不新增設定項。
- App UI 文字一律英文（本 item 不新增 UI 字串）。
- 最小可行改動：不新增檔案、不新增模組、不為一個函式建 class。
- 搜尋結果的內容與順序不得改變（見 Acceptance 第一條）。

## Scope

### 1. 公開 `NormalizeName`（`src/search/search_engine.h` / `.cpp`）

把 `search_engine.cpp` 匿名 namespace 內的 `Normalize()` 提升為公開函式：

```cpp
// Collapses runs of whitespace to a single space, trims the ends, and maps to
// invariant lowercase. Catalog names are normalized once per snapshot with this
// and stored in AppEntry::normalized_name; the query is normalized with the same
// function on every keystroke, so the two can never drift apart.
std::wstring NormalizeName(std::wstring_view value);
```

- 實作**原封不動**沿用現有的 `CollapseWhitespace` + `LCMAP_LOWERCASE` 邏輯，只是改名並移出匿名 namespace。`CollapseWhitespace` 維持私有。
- `SearchApps` 內部對 query 的呼叫改用它。
- 不新開 `name_normalize.{h,cpp}`；正規化規則與比對規則放同一個檔案，就不可能漂移。`search_engine` 是純值模組（不含 HWND／COM），catalog 反向依賴它符合 AGENTS.md 的核心邏輯獨立性要求。

### 2. 單一填寫點（`src/catalog/catalog_refresh.cpp`）

- `SetSnapshot(std::vector<AppEntry> merged)` 在 `merged_ = std::move(merged);` **之前**，對每一筆做：

```cpp
// NR-038: the sole place a published snapshot gets its normalized names, so
// SearchApps never re-normalizes per keystroke. Only fill when empty: the disk
// cache already carries the value (catalog_cache field 3), and a test may
// supply its own.
for (AppEntry& entry : merged) {
    if (entry.normalized_name.empty()) {
        entry.normalized_name = NormalizeName(entry.display_name);
    }
}
```

- `RebuildMerged()` 的最後一行由 `merged_ = DeduplicateCatalog(merged).entries;` 改為 `SetSnapshot(DeduplicateCatalog(merged).entries);`，使全專案只剩**一個**填寫點。
- `catalog_refresh.cpp` 加 `#include "search/search_engine.h"`。若 CMake 中 catalog 與 search 是分開的 target，補上必要的連結；不為此重組 target 結構。
- **不動**三個 source builder，也不動 `dedup.cpp`。

### 3. 索引排序（`src/search/search_engine.cpp`）

- `NormalizedName(entry)` 的 fallback 保留（`normalized_name` 為空時退回 `display_name`），但迴圈內**不得**再呼叫 `NormalizeName`——直接把 `NormalizedName(entry)` 的結果餵給 `Rank()`。
- `RankedEntry` 改為輕量索引：

```cpp
struct Ranked {
    MatchRank rank;
    std::uint32_t index;
};
```

- 比較子改為透過 `catalog[left.index]` 取值，鍵序**維持原樣**：rank → `is_pinned`（true 先）→ `usage_score`（大者先）→ `display_name.size()`（小者先）→ 正規化名稱字典序 → `stable_id`。
- 排序後才依序把命中項 `push_back(catalog[item.index])`，每個命中恰好複製一次。
- `SearchApps` 的簽章與回傳型別不變，`panel_model.cpp:65` 零改動。
- 在函式開頭留一行 `ponytail:` 註解，說明目前是每次按鍵全表掃描，並指出升級路徑與觸發條件：

```cpp
// ponytail: full O(catalog) scan per keystroke. Measured sub-millisecond for a
// 5k catalog once names are pre-normalized (see search_engine_test). If a real
// catalog ever makes this visible, the next step is incremental narrowing (a
// longer query's match set is a subset of the shorter one's, for every tier),
// not a debounce -- a debounce only makes the first keystroke slower.
```

### 4. 不做的接線

`EN_UPDATE` 維持同步、無 debounce、無 timer。不引入查詢快取、不引入增量收窄狀態、不截斷結果筆數。

## Non-goals

- 不加 debounce 或任何 timer。
- 不做增量收窄／上次候選集快取。
- 不改 rank 階梯（Exact／NamePrefix／WordPrefix／Substring／Subsequence）或任何排序鍵的順序。
- 不改 `SearchApps` 的簽章、不改回傳 `std::vector<AppEntry>`。
- 不改 `catalog_cache` 的欄位數、分隔符、跳脫或版本。
- 不改 `PanelModel` 的空查詢分支（pins + recent）。
- 不改 `stable_id`、dedup、usage、pin 的邏輯或格式。
- 不動 icon lane（NR-032～NR-037）的任何檔案。

## Acceptance

- **順序不變**：對同一組 catalog 與同一個 query，改動前後 `SearchApps` 回傳的 `stable_id` 序列完全相同。既有 `tests/unit/search_engine_test.cpp` 全部 case 未經修改即通過（其 fixture 的 `normalized_name` 皆為空字串，正好驗證 fallback 路徑）。
- **預填被採用**：某筆 `display_name = L"Zebra"`、`normalized_name = L"notepad"` 時，query `L"note"` 命中該筆，query `L"zeb"` 不命中。
- **填寫點生效**：`CatalogRefreshCoordinator` 以 `normalized_name` 為空的 entry 走 `ApplySourceResult` → 完成 generation 後，`Snapshot()` 中每筆的 `normalized_name` 等於 `NormalizeName(display_name)`。同樣的斷言對直接呼叫 `SetSnapshot()` 也成立。
- **既有值被尊重**：`SetSnapshot` 對 `normalized_name` 已非空的 entry 不覆寫。
- **`NormalizeName` 行為**：`L"  Paint   3D  "` → `L"paint 3d"`；`L"   "` → 空字串；`L"ABC"` → `L"abc"`。
- **最壞路徑效能**：5,000 筆已預填的合成 catalog、query `L"e"`（單字元、近乎全命中），Release 版單次 `SearchApps` < 50 ms，並印出實測毫秒數。
- repo 內搜尋不到 `SearchApps` 迴圈中對 `NormalizeName`／`Normalize` 的呼叫。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "search" --output-on-failure
ctest --test-dir build -R "catalog" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/search_engine_test.cpp` 新增（純值，不需視窗）：

- `NormalizeName` 的三組值（上述 Acceptance 第五條）。
- 預填 `normalized_name` 被採用、`display_name` 被忽略的那組（第二條）。
- 5,000 筆計時 case：以 `L"App " + std::to_wstring(i)` 之類產生 `display_name`、`normalized_name` 預填，用 `std::chrono::steady_clock` 量 `SearchApps(catalog, L"e")`，`std::wprintf` 印出實測 ms，斷言 `< 50`。門檻刻意寬鬆兩個數量級：它要抓的是「有人把 per-entry 正規化加回去」造成的十倍級退化，不是幾個百分點的抖動，所以在 Debug 版與有負載的機器上也不該閃紅燈。

`tests/unit/catalog_refresh_test.cpp` 新增：

- `ApplySourceResult` 走完一個 generation 後，`Snapshot()` 每筆 `normalized_name` 已填且等於 `NormalizeName(display_name)`。
- 直接 `SetSnapshot()` 亦然。
- 預填非空者不被覆寫。

注意 `catalog_refresh_test.cpp:35` 既有 helper 會設 `entry.normalized_name = id;`，新 case 需自行建立不預填的 entry。

## 交接區

- Start: 依「必讀」讀完所有文件；trace `src/search/search_engine.{h,cpp}`、`src/catalog/app_entry.h`、`src/catalog/catalog_refresh.{h,cpp}`、`src/catalog/catalog_cache.cpp`、`src/app_host/panel_model.cpp`、`tests/unit/search_engine_test.cpp`、`tests/unit/catalog_refresh_test.cpp`。實作 Scope 1～3，明確不要動 Scope 4 所述的輸入路徑。回報修改檔案、測試命令、5k 計時的實測毫秒數、結果與未完成事項。
- Result: 完成。Scope 1：`Normalize()` 提升為公開 `NormalizeName`（`search_engine.h:14`），實作原封不動（CollapseWhitespace 維持私有），`SearchApps` 的 query 改用之。Scope 2：`SetSnapshot` 在 `merged_ = std::move` 前為空 `normalized_name` 填 `NormalizeName(display_name)`（`catalog_refresh.cpp:114`），`RebuildMerged` 末行改 `SetSnapshot(DeduplicateCatalog(merged).entries)`，加 include，CMake 補 `nimblerun_catalog PUBLIC nimblerun_search`（不重組 target）。Scope 3：`RankedEntry` 改 `struct Ranked { MatchRank rank; std::uint32_t index; }`，比較子經 `catalog[left.index]` 取值、鍵序原樣（rank → is_pinned → usage_score → display_name.size() → 正規化名稱字典序 → stable_id），排序後才複製 `catalog[item.index]`；迴圈內無 `NormalizeName`/`Normalize` 呼叫（repo 已 grep 驗證）；函式開頭含文件指定 ponytail 註解。Scope 4 未動（EN_UPDATE 同步、無 debounce/timer/cache/收窄/截斷）。測試：`search_engine_test` 新增 NormalizeName 三組值、預填被採用（Zebra/notepad）、5,000 筆計時（實測 633 µs = 0 ms，matched 5000）；`catalog_refresh_test` 新增 generation 填寫、直接 SetSnapshot 填寫、預填不被覆寫三 case。既有 `panel_model_test`／`ui_palette_layout_test` 的 fixture 原本預填 raw display_name（非正規化），違反「預填值逐字採用」的新契約而在 Release 下實測失敗，其 `Entry` helper 改為預填 `NormalizeName(name)`（對舊 code 的 `Normalize` 是冪等，行為與順序不變），其餘 case 未動。`ctest -R search` 1/1、`ctest -R catalog` 4/4、全套件 23/23 通過，build 無新增 warning。`icon_store_test` 在並行 ctest 下曾 flaky（LRU eviction，與本 item 無關，`nimblerun_icons` 不依賴 catalog/search），單獨重跑通過。未完成：無（未 commit／未 push）。
