# NR-061 — 空白狀態只顯示釘選與常用，不再字母填充

- Phase: 3
- 覆寫：**NR-053**（`docs/work-items/NR-053-empty-state-fill-and-order.md`）交付的
  「填充至一頁可見格數」與 `docs/design-spec.md` §4.2 規則 3。NR-053 的另一半
  ——非釘選區依 `usage_score` 排序——**保留不動**。

## 為什麼要覆寫 NR-053

NR-053 的前提是「全新安裝時空白格狀是產品的第一印象」，所以用 catalog 依名稱
補滿第一頁。實機結果是：一台只釘了 3 個、常用 2 個的機器，面板被
`3D Vision 相...`、`AccessPort`、`AlertMail`、`AlertMail4`、`AlertMail48`
這類使用者從未開過、也不打算開的項目塞滿 40 格。使用者無法分辨哪些格子有意義，
且對這些格子按右鍵選「Remove from recent」毫無反應——因為它們根本沒有 usage
紀錄。填充列讓每一格的語意從「我釘的或我用過的」退化成「字母排在前面的」。

使用者決策（2026-08-07，已確認）：空白狀態只顯示釘選與常用；沒有常用就保持
空白並給一句提示。這是 spec 層級的覆寫，本 item 一併修改 `docs/design-spec.md`。

## 使用者已確認的決策（不要重新設計）

1. **完全移除填充，不是「填少一點」也不是「加開關」。** 不要引入設定項。
2. **全空時顯示一句提示**，內容為「沒有常用項目」之意，不是空白畫面。
3. **常用項目對應的 App 已不存在時直接不顯示**（現況已如此），並且
   **連 usage 紀錄一起清掉**，否則該 App 重裝後會帶著舊分數突然跳回常用區。
4. 釘選項目找不到對應 App 時的處理**不屬於本 item**，見 NR-062。本 item 維持
   現況（靜默不顯示）。

## 硬約束（引用自專案規則，不要再去翻）

- `AGENTS.md`：偏好最小可行改動；先重用既有程式碼再新增 helper。
- `AGENTS.md`：UI 文字一律英文；超過一個畫面用到的字串要集中。
- `AGENTS.md`：新的非平凡邏輯要留一個可執行的檢查。
- `AGENTS.md`：搜尋、排名、持久化格式等核心邏輯不得依賴 HWND 或 Shell COM。
  `PanelModel` 是純值型別，本 item 不得在其中引入任何 Win32 相依。
- `AGENTS.md`：不得把 schema 遷移或破壞性資料清理夾帶進無關改動。本 item 的
  usage 清理**不是**無關改動（它是決策 3 的一部分），但只能刪掉「catalog 已
  建好且該 stable_id 不在 catalog 中」的紀錄，見下方守門條件。
- `docs/development.md` 的建置與測試指令照舊。

## 要讀與追的檔案

- `src/app_host/panel_model.cpp:91-179` — `RefreshRows()`。空查詢分支是主場。
- `src/app_host/panel_model.h:79-95` — `Rows()`、`RecentStartIndex()`、
  本次新增的 `RecentEndIndex()`（見下方「已存在的前置修正」）。
- `src/app_host/panel_model.cpp:139-169` — 要刪掉的填充區塊，含其
  `std::unordered_set` / `std::partial_sort` 與 `ponytail:` 註解。
- `src/app_host/panel_model.h:123` 與 `.cpp` 的 `EmptyStatePrewarmIds` —— NR-037
  的圖示預熱靠它取一頁 id。填充消失後它自然只回傳釘選＋常用的 id，**這是正確
  的**（預熱本來就不該為使用者不會看到的項目付出解碼成本），不要為了維持數量
  而回補 catalog 項目。
- `src/app_host/main.cpp:956-960` — `DrawEmptyStateHint()`，兩個呼叫點在
  `:1477`（grid）與 `:1588`（list）。
- `src/app_host/main.cpp:110-111` — `list_strings::kBuildingCatalog` /
  `kNoMatchingApps`。
- `src/app_host/main.cpp:1087-1110` — `RefreshPanelSnapshot()`：常用項目已經是
  「在 snapshot 中找得到才推進 `recent_entries`」，所以決策 3 的顯示面已經成立；
  本 item 只補 usage 紀錄的清除。
- `src/usage/usage_store.h:44-63` — `Clear()`、`Forget()`、`Recent()`。
- `src/pins/pin_store.h:86` — `Reconcile(catalog, now)` 是「拿 catalog 對帳並丟掉
  過期紀錄」的既有樣板，usage 的清理照它的形狀寫，不要另創風格。
- `docs/design-spec.md:137-156` — §4.2 空白查詢狀態。

### 已存在的前置修正（不要重做）

`PanelModel::RecentEndIndex()` 與 `recent_end_` 已於 2026-08-07 加入
（`panel_model.h`、`panel_model.cpp`），右鍵選單的 `in_recent` 判斷
（`main.cpp:2528` 附近）已改用 `recent_start <= cell < RecentEndIndex()`。
本 item 移除填充之後，`recent_end_` 會等於 `rows_.size()`，但**不要因此刪掉它**
——NR-062 的缺失釘選佔位列同樣要靠它把「Remove from recent」擋在外面。

## 範圍

### 1. `PanelModel::RefreshRows()` 刪掉填充區塊

刪除 `src/app_host/panel_model.cpp:139-169` 整段 `if (catalog_ != nullptr &&
rows_.size() < kIconCacheWorkingSetItems) { ... }`。連帶：

- 若 `<unordered_set>` 在移除後於本檔已無其他使用者，一併移除該 include。
- `OrderByName()`（`:48-55`）在移除後若只剩填充區塊一個呼叫點，一併刪除；
  若 `DisplayNameKey()` 仍被 `OrderByScoreThenName()` 使用則保留。
  **實作時用編譯器確認，不要憑印象刪。**
- `recent_end_` 的賦值位置不動。
- `std::stable_sort(rows_.begin() + recent_start_, rows_.end(), ...)` 不動。

刪除後空查詢的 `rows_` = 釘選（依 pin 順序）＋ 常用（依 `usage_score` 排序），
就這兩段。

### 2. 全空時的提示字串

`src/app_host/main.cpp` 的 `list_strings` 增加：

```cpp
constexpr wchar_t kNoRecentApps[] = L"No pinned or recent apps yet";
```

`DrawEmptyStateHint()` 目前是兩態（catalog 未備妥 → `kBuildingCatalog`，
否則 → `kNoMatchingApps`）。改成三態，**由呼叫點傳入空查詢與否**，因為
`kNoMatchingApps`（找不到符合的 App）只對搜尋結果成立，對空查詢是錯的字：

```cpp
void DrawEmptyStateHint(float row_height, bool searching) {
    const wchar_t* hint = !g_model->CatalogAvailable()
        ? list_strings::kBuildingCatalog
        : (searching ? list_strings::kNoMatchingApps
                     : list_strings::kNoRecentApps);
    ...
}
```

呼叫點：grid 分支（`:1477`）傳 `false`，list 分支（`:1588`）傳 `true`。
函式其餘的繪製程式碼一行不改。

### 3. 清掉 catalog 中已不存在的 usage 紀錄

在 `UsageStore` 新增，緊接 `Forget()` 之後：

```cpp
    // NR-061: drops every record whose stable_id is absent from `catalog`, so
    // an uninstalled app cannot reappear in the recent region with its old
    // score after a reinstall. Mirrors PinStore::Reconcile's contract: the
    // caller must pass a real (non-empty) catalog snapshot -- reconciling
    // against an empty one during startup would wipe every record. Returns
    // false when nothing was dropped, in which case the caller must not Save().
    bool Reconcile(const std::vector<AppEntry>& catalog);
```

實作照 `PinStore::Reconcile` 的形狀：`catalog` 為空時立刻回 `false`；把
catalog 的 `stable_id` 收進 `std::unordered_set<std::wstring_view>`，用
`std::erase_if`（或 `remove_if` + `erase`）刪掉不在其中的紀錄，回傳是否有變動。
**不做時間保留期**——釘選有 30 天保留期是因為釘選是使用者明確的選擇；使用統計
不是，重新啟動一次就回來了（NR-040 決策 4 已確立同一理由）。

呼叫點：`RefreshPanelSnapshot()`（`main.cpp:1087`）內，**在 `StampRankingFields()`
之前**，且只在 `g_refresh->Snapshot()` 非空時：

```cpp
    if (!g_refresh->Snapshot().empty() && g_usage->Reconcile(g_refresh->Snapshot())) {
        g_usage->Save();
    }
```

`Save()` 失敗只是不持久化，不改變記憶體狀態，不需要對話框（與既有寫入路徑一致）。

### 4. 修改 `docs/design-spec.md` §4.2

`:143` 的規則 3 整條刪除，只留規則 1、2。
`:145` 的段落刪掉「規則 3 的填充材料為……不影響釘選區的範圍。」整句，
保留前半關於規則 2 使用分數與同分比較的敘述。
在「規則：」清單（`:149` 起）新增一條：

```
- 釘選與常用皆為空時，格狀區顯示一行提示（英文 UI 文字），不以任何其他 App 填充；
  空白狀態的內容一律只來自釘選清單與使用紀錄。
```

並在同節補一句常用清理規則：

```
- 常用項目對應的 App 已不在 Catalog 中時不顯示，且其使用紀錄在下一次 Catalog
  對帳時清除；釘選項目的缺席處理見 §FR-011。
```

## 非目標

- 不動搜尋狀態（§4.3）的任何行為。
- 不動釘選缺席的呈現（§FR-011 / NR-062）。
- 不動 `usage_score` 的計算或非釘選區的排序（NR-053 的這一半保留）。
- 不新增設定項、不新增「隱藏此 App」清單。
- 不改 `favorites.txt` 或 `usage.tsv` 的 schema 版本（本 item 只刪紀錄，不加欄位）。
- 不動面板尺寸：格數變少時下方留白即可，不做動態縮放。

## 驗收條件

1. 空查詢時 `Rows()` 只含釘選項目與常用項目，數量等於兩者之和（去除重複後）。
2. 無釘選、無常用時 `Rows()` 為空，面板顯示 `No pinned or recent apps yet`。
3. catalog 尚未備妥時仍顯示 `Building app catalog…`（未回歸）。
4. 搜尋無結果時仍顯示 `No matching apps`（未回歸）。
5. `RecentStartIndex()` 仍是釘選／非釘選的唯一邊界，NR-046 的拖曳重排未回歸。
6. 對常用格右鍵有「Remove from recent」且選了之後該格消失（NR-040 未回歸）。
7. 某 App 從 catalog 消失後，一次 `RefreshPanelSnapshot()` 之後其紀錄不再存在於
   `usage.tsv`。
8. catalog 為空（啟動中）時 `Reconcile` 不刪任何紀錄。
9. `docs/design-spec.md` §4.2 不再提到填充。

## Agent 檢查（可執行）

在 `tests/unit/panel_model_test.cpp` 新增並在 `main()` 呼叫：

- `TestEmptyStateHasNoFiller()`：catalog 4 筆、1 pin、1 recent →
  `Rows().size() == 2`，且兩筆 id 分別是該 pin 與該 recent。
- `TestEmptyStateAllEmpty()`：catalog 4 筆、無 pin、無 recent → `Rows().empty()`。
- 既有的 `TestPinnedRegionNotSortedByScore()`（`:788` 附近）斷言了
  `Rows().size() == 4` 與填充順序，**必須改寫**成只斷言釘選區順序與
  `RecentStartIndex() == 2`；不要為了讓舊斷言通過而保留填充。
- 既有 `TestRecentEndIndexExcludesFiller()` 同樣需改寫或刪除（填充已不存在）。

在 `tests/unit/recent_usage_test.cpp` 新增並呼叫：

- `TestReconcileDropsAbsent()`：3 筆紀錄、catalog 只含其中 2 個 →
  回 `true`，`Records().size() == 2`。
- `TestReconcileEmptyCatalogKeepsAll()`：catalog 為空 → 回 `false`，紀錄不變。
- `TestReconcileNoChange()`：catalog 涵蓋全部紀錄 → 回 `false`。

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

手動驗收（Release build，逐條在交接區記錄結果）：

1. 清空查詢 → 面板只剩自己釘的與確實開過的 App，不再有字母排序的陌生項目。
2. 把 `favorites.txt` 與 `usage.tsv` 移走後啟動 → 面板顯示
   `No pinned or recent apps yet`，不是空白也不是 `No matching apps`。
3. 搜尋一個不存在的字串 → 仍顯示 `No matching apps`。
4. 對常用格右鍵 → 有 `Remove from recent`，選了該格消失。
5. 釘選格拖曳重排仍正常（NR-046）。

## 交接區

實作日期：2026-08-07。

### 1. `PanelModel::RefreshRows()` 填充區塊刪除

刪除的是修改前 `panel_model.cpp` 的第 131-170 行（`// NR-053: design-spec
§4.2 rule 3...` 註解起，到 `if (catalog_ != nullptr && rows_.size() <
kIconCacheWorkingSetItems) { ... }` 區塊結束）。刪除後 `RefreshRows()` 的空
查詢分支只剩「釘選 → 常用 → `recent_end_` 賦值 → `stable_sort`」四步。

- `OrderByName()`：**連帶刪除**。編譯器確認它刪除填充區塊後已無其他呼叫點
  （`DisplayNameKey()` 仍被 `OrderByScoreThenName()` 使用，保留）。
- `<unordered_set>`：**連帶刪除**（filler 是檔案內唯一使用者）。
- `"icons/icon_cache.h"`：filler 刪除後檔案內已無 `IconCache`/`kIconCacheWorkingSetItems`
  等符號使用，一併移除該 include（item 未明列，但屬同一「編譯器確認未使用
  才刪」規則的自然延伸）。
- `recent_end_` 賦值位置、`std::stable_sort(rows_.begin() + recent_start_, ...)`
  維持原樣未動。

### 2. 空白提示三態

`list_strings::kNoRecentApps` 加在 `main.cpp:114`（緊接 `kNoMatchingApps` 之後）。
`DrawEmptyStateHint(float row_height, bool searching)` 定義於 `main.cpp:963`。
呼叫點：grid 分支 `main.cpp:1492` 傳 `/*searching=*/false`；list 分支
`main.cpp:1603` 傳 `/*searching=*/true`（實際行號因 NR-046 拖曳重排程式碼已
先加入而與 item 內原引用的 `:1477`/`:1588` 有些許偏移，內容與呼叫語意一致）。

### 3. `UsageStore::Reconcile`

宣告於 `usage_store.h:60-68`（緊接 `Forget()` 之後），實作於
`usage_store.cpp:141-157`，形狀照抄 `PinStore::Reconcile`：空 catalog 立即
回 `false`；用 `std::unordered_set<std::wstring_view>` 收集 catalog 的
`stable_id`；用 `std::erase_if` 刪除不在其中的紀錄；依刪除前後筆數決定回傳
值。`usage_store.h` 新增 `#include "catalog/app_entry.h"`（`AppEntry` 簽名
需要），`usage_store.cpp` 新增 `#include <unordered_set>`。

呼叫點：`main.cpp:1104-1107`，在 `RefreshPins()` 之後、`StampRankingFields()`
（`main.cpp:1109`）之前：

```cpp
if (!g_refresh->Snapshot().empty() && g_usage->Reconcile(g_refresh->Snapshot())) {
    g_usage->Save();
}
```

### 4. `docs/design-spec.md` §4.2

現行行號（修改後）：規則清單在 `:139-142`（只剩規則 1、2）；緊接其後
`:144` 段落已刪除填充材料那句、只留規則 2 的分數/同分比較敘述；
`:148-150` 是「規則：」清單，`:149` 為新增的「不填充」規則、`:150` 為新增的
常用清理規則。

### 額外發現：NR-053 遺留的填充相依測試

item 只點名 `TestPinnedRegionNotSortedByScore` 與
`TestRecentEndIndexExcludesFiller` 需要改寫，但實際刪除填充後還有一批測試
直接斷言填充行為，全部改動如下：

- `tests/unit/panel_model_test.cpp`：刪除 `TestEmptyStateFillsToOnePage`、
  `TestEmptyStateFillDoesNotManufactureRows`、
  `TestEmptyStateFillExcludesPinnedApp`、`TestEmptyStateFillExcludesRecentApp`、
  `TestEmptyStateNoFillAtCapacity`、`TestEmptyStateFillOnlyWhenQueryEmpty`、
  `TestEmptyStateRefreshRowsTiming`（皆整段測試填充行為，已無等價行為可測）。
  連帶移除檔案內因此變成未使用的 `#include <chrono>`、
  `#include "icons/icon_cache.h"` 與三個 `using std::chrono::...`
  （編譯器確認：移除後專案照常編譯）。保留 `TestEmptyStateEmptyCatalogNoCrash`
  （與填充無關）。改寫 `TestPinnedRegionNotSortedByScore` 與
  `TestRecentEndIndexExcludesFiller`（見下）。新增
  `TestEmptyStateHasNoFiller()`、`TestEmptyStateAllEmpty()`（item 指定的
  Agent 檢查），並在 `wmain()` 更新呼叫清單。
- `tests/unit/pin_store_test.cpp`：`TestPanelModelPinnedFirst` 與
  `TestPanelModelHidesAbsentPin` 原本斷言「填充補上第三格 / 補上唯一的
  catalog 項目」，改為斷言「無填充，rows 只有釘選＋常用（或全空）」。
- `tests/unit/recent_usage_test.cpp`：新增 `TestReconcileDropsAbsent`、
  `TestReconcileEmptyCatalogKeepsAll`、`TestReconcileNoChange`（item 指定），
  並新增 `#include "catalog/app_entry.h"` 與 `CatalogEntry()` 小 helper 供
  這三個測試建 catalog fixture。

### 建置與測試結果

```
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

建置成功，無警告。`ctest` 23 個測試全數通過（100%），包含
`nimblerun_recent_usage_test`、`nimblerun_list_vertical_slice_test`
（panel model）、`nimblerun_pinning_test`（含上述改寫的兩個測試）。

### 手動驗收（未執行，待人工在互動式 Alt+Space 環境驗證）

以下 5 條需要真人在 Release build 上互動操作，本次自動化實作**未執行**，
結果未知，留給人工驗收：

1. 清空查詢 → 面板只剩自己釘的與確實開過的 App。
2. 移走 `favorites.txt` 與 `usage.tsv` 後啟動 → 顯示
   `No pinned or recent apps yet`。
3. 搜尋不存在的字串 → 顯示 `No matching apps`。
4. 常用格右鍵 → 有 `Remove from recent`，選了該格消失。
5. 釘選格拖曳重排仍正常（NR-046）。
