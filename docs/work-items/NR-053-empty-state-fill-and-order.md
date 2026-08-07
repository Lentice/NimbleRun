# NR-053 — The empty state must order by usage score and fill the visible grid

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §4.2（空狀態內容與排序）／§4.6（使用分數）
- Origin: 2026-08-06 repo audit, spec-conformance findings #3 與 #4

## Why

§4.2 對「還沒打字時面板顯示什麼」有三條規則，目前實作了第一條、
做錯第二條、完全沒做第三條：

| §4.2 規則 | 現況 |
| --- | --- |
| 1. 釘選項目優先顯示於最前 | ✅ `panel_model.cpp:47-56` |
| 2. 未釘選常用項目，**依使用分數排序** | ❌ 依 `last_launch_utc` 遞減排序 |
| 3. 若資料不足，**補入最近啟動或字母排序靠前的 App，直到視窗可見容量** | ❌ 完全未實作 |

**規則 2**：`UsageStore::Recent()`（`usage_store.cpp:248` 一帶）只按最後啟動
時間排序，而 §4.6 的使用分數（`UsageScore`）在空狀態完全沒被用到——它只在
搜尋結果的 tie-break 裡起作用（`StampRankingFields`，`main.cpp:876`）。
後果：一個一小時前開過一次的冷門 App，排在每天用十次的主力 App 前面。
使用分數這個機制在使用者最常看到的畫面上是關掉的。

**規則 3**：`RefreshRows()` 的空 query 分支只放釘選＋常用
（`panel_model.cpp:41-64`），`usage_store.h:62` 還明文寫著
「never pads with other apps」。後果：**全新安裝、還沒釘選也還沒啟動過任何
App 時，使用者按下 `Alt+Space` 看到的是一個空的 24 格格狀畫面加上
「No matching apps」。** 這是產品的第一印象。

## Decisions already made — do not reopen

決定於撰寫本 item 時（理由見 Non-goals 與 How this stays maintainable）：

1. **不改 `usage.tsv` 的 schema。** §4.6 的完整公式是
   `launch_count_30d + 3 × launch_count_7d + recency_bonus`，而 `usage.tsv`
   只存終身總次數與最後啟動時間，所以 `UsageScore` 目前算的是
   `min(total, 1e6) + recency_bonus`（`usage_store.h:78` 的既有 `ponytail:`
   註解已載明這個取捨）。**本 item 沿用現有的 `UsageScore`，不動持久化格式。**
   把 7/30 日 bucket 做出來需要改 schema、加遷移、改測試，是獨立的 item，
   而且與本 item 要修的「空狀態根本沒用到分數」正交。
2. **填充順序：釘選 → 依使用分數排序的有紀錄 App → 字母序的其餘 App。**
   §4.2 規則 3 寫「最近啟動**或**字母排序靠前」；前半段已被規則 2 涵蓋，
   所以填充材料就是「沒有使用紀錄的 App，依顯示名稱排序」。不要做隨機、
   不要做「推薦」。
3. **填到「視窗可見容量」為止，不是填滿整個 catalog。** §4.2 原文就是
   「直到視窗可見容量」。容量取一頁格狀的格數。
4. **排序在 model 層做，資料在 host 層備齊。** `PanelModel` 已經是純資料
   模型（無 HWND），本 item 維持這個分界：`main.cpp` 負責把「候選清單」
   交給 model，model 負責決定順序與截斷。

## Binding constraints — quoted, do not go looking for them

design-spec §4.2：

> 空狀態（尚未輸入）顯示：
> 1. 釘選項目優先顯示於最前。
> 2. 未釘選常用項目，依使用分數排序。
> 3. 若資料不足，補入最近啟動或字母排序靠前的 App，直到視窗可見容量。

design-spec §4.6：

> `usage_score = launch_count_30d + 3 × launch_count_7d + recency_bonus`

design-spec §4.9 / §4.3（可見容量的來源）：

> 格狀版面為 6 欄 × 4 列。

design-spec §FR-011（釘選項目的處理，本 item 不得破壞）：

> - 釘選項目恆顯示於最前，且不因使用分數變動而改變順序。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- Keep App Catalog data as ordinary copyable values.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/panel_model.cpp:41-74` — `RefreshRows()`。空 query 分支
  （:42-64）是本 item 的主場：釘選迴圈、`recent_start_` 的設定、
  常用迴圈。**`recent_start_` 是釘選／非釘選的唯一邊界**，NR-046 的拖曳重排
  與 NR-041 的釘選標記都依賴它——填充進來的項目必須落在 `recent_start_`
  **之後**，否則拖曳重排會把它們當成可重排的釘選。
- `src/app_host/panel_model.h` — `SetRecent`、`SetPins`、`SetCatalog`、
  `Columns()`、`ViewportRows()`、`recent_start_` / `RecentStartIndex()`。
  §2 需要一個新的輸入或一個新的容量參數，先看清楚既有的 setter 形狀再決定。
- `src/app_host/main.cpp:896-920` — `RefreshPanelSnapshot()`。它把
  `g_usage->Recent(g_settings.recent_count)` 解析成 `AppEntry` 後
  `SetRecent`。**這是本 item 提供填充材料的地方。**
  注意最後那段註解：「SetRecent is last, so its RefreshRows is the one that
  decides the visible rows」——順序有意義，不要打亂。
- `src/app_host/main.cpp:876-894` — `StampRankingFields()`。它已經替
  **整個 snapshot** 的每一筆算好 `entry.usage_score` 與 `entry.is_pinned`。
  **這是本 item 的關鍵發現：分數已經在每個 `AppEntry` 上了**，
  空狀態排序不需要再查 `UsageStore`，只要用 `entry.usage_score`。
- `src/usage/usage_store.h:60-67` — `Recent(cap)` 的契約（「newest last-launch
  first」「never pads with other apps」）與 `Records()`。
  §1 決定是改 `Recent` 還是在 model 層排序——**先讀 `Recent` 的其他呼叫端**
  （`Select-String -Path src -Pattern 'Recent\('`）再決定，改一個共用函式的
  排序會影響所有呼叫端。
- `src/usage/usage_store.cpp:248` 一帶 — `Recent()` 的排序比較子。
- `src/usage/usage_store.cpp:265` 一帶 — `UsageScore()` 的實作與其
  `ponytail:` 註解。**只讀不改。**
- `src/ui/panel_layout.h` — 格狀幾何常數（欄數、列數）。§2 的可見容量要從
  這裡取，不要寫死 24。
- `src/icons/icon_cache.h:58` — `kIconCacheWorkingSetItems = 24`，
  註解寫「one full page of cells」。**這是同一個數字的既有出處**，
  §2 若能直接用它就用它，不要製造第三個 24。
- `tests/unit/panel_model_test.cpp` — 既有 `Expect()` 慣例與空狀態測試。
- `tests/unit/recent_usage_test.cpp` — `UsageStore` 與 `UsageScore` 的既有測試。

## Scope

### 1. 未釘選區依使用分數排序（§4.2 規則 2）

`StampRankingFields()` 已經把 `usage_score` 寫進 snapshot 的每一筆
`AppEntry`，而 `RefreshPanelSnapshot()` 組出的 `recent_entries` 正是從
snapshot 拷貝來的——**分數已經在手上了**。

在 `PanelModel::RefreshRows()` 的常用迴圈之後，對非釘選區排序：

```cpp
        // NR-053: design-spec §4.2 rule 2 orders the non-pinned region by usage
        // score, not by recency. UsageStore::Recent() only sorts by last launch,
        // so a rarely used app opened an hour ago used to outrank a daily
        // driver. entry.usage_score is already stamped on every snapshot entry
        // by StampRankingFields (§4.6), so no extra lookup is needed here.
        // Ties fall back to the same deterministic order the rest of the app
        // uses: higher score, then shorter name, then case-insensitive name.
        std::stable_sort(rows_.begin() + recent_start_, rows_.end(),
                         [](const AppEntry& a, const AppEntry& b) { ... });
```

比較子**照抄 `search_engine.cpp` 的 tie-break 順序**（使用分數高者優先 →
名稱較短者優先 → 不區分大小寫的名稱排序），去掉「已釘選優先」那一層
（這個區段全部未釘選）。**不要自創第二套 tie-break**；讀
`search_engine.cpp:121-178` 的 comparator 抄下來。若形狀允許共用，
就把 comparator 抽成 `search_engine.h` 的一個具名函式並兩處共用；
**若共用需要新增連結關係或改動 `SearchApps`，就不要共用**，複製 4 行並在
註解裡指名「與 `search_engine.cpp` 的 tie-break 同步」。

`std::stable_sort` 而非 `sort`：分數相同時保留 `Recent()` 給的時間順序，
是免費的合理次序。

**`recent_start_` 之前（釘選區）不得被排序**——§FR-011 明文釘選順序不因
使用分數變動。用 `rows_.begin() + recent_start_` 起始，並在測試中守住。

**不改 `UsageStore::Recent()`**：它的「newest first」契約有其他呼叫端
（至少是 recent 清單本身的 cap 語意），改它會擴散。先 grep 確認；
若確認 `Recent()` **只有** `RefreshPanelSnapshot` 一個呼叫端，
在交接區記錄這個事實，但本 item 仍在 model 層排序（決策 4：排序是
呈現規則，屬於 model）。

### 2. 資料不足時填滿可見容量（§4.2 規則 3）

**候選材料**：整個 catalog snapshot，`PanelModel` 已經透過 `SetCatalog`
持有它（`catalog_` 指標）。不需要新的輸入。

在常用迴圈與 §1 的排序**之後**，補上填充：

```cpp
        // NR-053: design-spec §4.2 rule 3. Without this a fresh install with no
        // pins and no launch history shows an empty grid and "No matching
        // apps" -- the product's first impression. Fill up to one visible page
        // with catalog entries that are neither pinned nor already listed,
        // ordered by display name (the "字母排序靠前" half of rule 3; the
        // "最近啟動" half is already covered by the recent region above).
        // Filler goes after recent_start_, so NR-046's pinned-drag range and
        // NR-041's pinned marker are unaffected.
```

實作要點，逐條照做：

- **容量**取一頁格狀的格數。優先重用既有的
  `kIconCacheWorkingSetItems`（`icon_cache.h:58`，註解已寫明它就是
  「one full page of cells」）或 `panel_layout.h` 的欄×列常數。
  **不要引入第三個 24。** 在交接區寫明你用了哪一個。
- **只在 rows 不足容量時才填**，且填到剛好達到容量就停。
- **排除已在清單中的**：釘選的（`pins_`）與已列出的（比對 `stable_id`）。
  用一個 `std::unordered_set<std::wstring_view>` 收集已列出的 id，
  不要對每個候選跑一次線性掃描（catalog 上限 5,000 筆 × 24 次是可接受，
  但 set 更短也更清楚）。
- **依 `display_name` 排序**，不區分大小寫，與 §1 的名稱比較用同一套規則。
  只需要前 N 筆，所以用 `std::partial_sort` 或先收集候選再
  `std::nth_element`——**但除非量測顯示需要，直接 `sort` 候選再取前 N 也
  可以接受**（5,000 筆一次排序，只在空狀態且 rows 不足時發生）。
  選最短的那個寫法，在交接區寫明選了哪個與為什麼。
- **填充項目與常用項目在視覺上不做區分。** 未在規格中，不要加分隔線、
  不要加標題、不要加淡化。

`recent_start_` **不變**：填充項目也在非釘選區。

### 3. 效能

空狀態的 `RefreshRows()` 目前是 O(pins × catalog)；本 item 加上
「一次 24 筆的分數排序」與「最壞情況一次 5,000 筆的名稱排序」。

- 分數排序：非釘選區最多 `recent_count`（上限 40）筆。不可測量。
- 填充排序：只在 rows < 容量時執行，也就是**釘選＋常用少於 24 筆時**。
  一旦使用者累積了足夠的常用項目，這條路就不再走。5,000 筆的
  `std::sort` 依 NR-047 的同規模量測（603 µs 的全掃描）推估在毫秒等級，
  而它發生在面板顯示時，不是每次按鍵。
- **量它。** 在 §4 的測試裡加一條 5,000 筆 catalog、零釘選零常用的
  `RefreshRows()` timing，比照 `search_engine_test.cpp` 的既有 timing block
  格式（`std::wprintf` 一行 + `Expect` 上限）。上限用 **50 ms**，與 §4.3 的
  同步計算預算一致。把數字填進 `docs/performance-baseline.md`
  的相關列（若沒有相關列就不要新增列，NR-056 會統一處理表格）。

**不要**為此加快取、dirty flag 或背景執行緒。

### 4. 測試

**`tests/unit/panel_model_test.cpp`**，全部用既有 `Expect()`：

規則 2：

- 三筆常用，`usage_score` 分別為 5 / 100 / 20，餵進 model 的順序刻意
  是最後啟動時間序（即 5、100、20）。斷言 rows 的非釘選區順序為
  100、20、5。
- 分數相同時，名稱較短者在前；名稱長度也相同時，不區分大小寫的名稱序。
- **釘選區不被排序**：兩個釘選項目分數為 1 與 999，斷言 rows[0]/rows[1]
  仍是 `SetPins` 給的順序。
- `RecentStartIndex()`（或 `recent_start_` 的存取器）在排序後仍指向同一個
  邊界。

規則 3：

- 零釘選、零常用、catalog 有 100 筆：斷言 rows 的筆數等於一頁容量，
  且內容是 catalog 中名稱排序最前的那些。
- 零釘選、零常用、catalog 只有 3 筆：斷言 rows 恰為 3 筆（不足容量時
  不製造空項目、不當機）。
- catalog 為空（`SetCatalog(nullptr)` 與空 vector 兩種）：rows 為空，
  不當機。
- **填充不重複**：一個 App 同時是釘選且名稱排序靠前 → 只出現一次；
  一個 App 同時在常用清單且名稱排序靠前 → 只出現一次。
- 釘選＋常用已達或超過容量時：**完全不填充**，rows 內容與本 item 前相同。
- 打字後（`SetQuery(L"a")`）：rows 是搜尋結果，**沒有任何填充項目**
  （§4.2 只管空狀態）。

效能：§3 的 5,000 筆 timing block。

**`tests/unit/recent_usage_test.cpp`**：斷言 `UsageStore::Recent()` 的行為
**未改變**（仍是 newest-first、仍不填充）——本 item 沒動它，這是防止未來
有人「順手」改它的守門員。

### 5. 更新 spec

`docs/design-spec.md` §4.2 的三條規則之後補上（繁體中文，維持周邊風格）：

> 規則 2 的使用分數以 §4.6 的 `usage_score` 為準；分數相同時依序比較名稱
> 較短者優先、不區分大小寫的名稱排序，與 §4.5 的同分比較規則一致。
> 規則 3 的填充材料為「既未釘選也不在常用清單中」的 Catalog 項目，依顯示
> 名稱排序，補至一頁格狀的可見格數為止；填充項目在視覺上與常用項目無異，
> 且不影響釘選區的範圍。

若 §4.6 一節載明了「使用分數用於何處」，補一句它同時決定空狀態非釘選區的
順序。不要動其他條文，**特別不要動 §4.6 的公式**（Decisions §1）。

## How this stays maintainable

**分數只算一次，貼在資料上。** `StampRankingFields()` 已經把 `usage_score`
寫進每一筆 `AppEntry`，搜尋的 tie-break 用它，本 item 讓空狀態也用它。
於是「使用分數怎麼算」只有 `UsageScore()` 一個答案，而「誰用得到它」不需要
各自去查 `UsageStore`。**未來要改分數公式（例如補上 §4.6 缺的 7 日 bucket）
只需要改 `UsageScore()`，兩個消費端自動跟上。**

**一個排序規則，一份 tie-break。** §1 明文要求與 `search_engine.cpp` 的
comparator 對齊。兩處若漂移，使用者會看到「搜尋 `a` 的順序」與「空狀態的
順序」對同一批 App 給出不同答案，而沒有任何錯誤訊息。

**`recent_start_` 是唯一的區段邊界。** NR-046 的拖曳、NR-041 的標記、
本 item 的排序與填充全部以它為界。新增任何空狀態區段時，先問它落在
邊界的哪一側。

**容量常數只有一個出處。** §2 明文禁止製造第三個 24。

## Non-goals

- **`usage.tsv` 加 7 日／30 日 bucket 以完整實作 §4.6 的公式。**
  Decisions §1；需要 schema 變更與遷移，是獨立 item。
  `usage_store.h:78` 的既有 `ponytail:` 註解仍是那件事的紀錄。
- **改 `UsageStore::Recent()` 的排序或讓它填充。** 它的契約是對的
  （「最近啟動」就該按時間排），空狀態的呈現順序是 model 的事。
- **視覺上區分填充項目**（分隔線、標題、淡化、"Suggested" 標籤）。未在規格中。
- **依安裝時間、檔案大小、圖示可用性或任何「推薦」啟發式排序填充。**
  §4.2 說字母序，就是字母序。
- **改搜尋結果的排序或 tie-break。** §4.5 已實作且有測試。
- **改釘選的順序規則。** §FR-011 明文禁止。
- **快取空狀態的 rows 或加 dirty flag。** §3 已有量測預算。
- **設定頁的「常用項目筆數」以外的新設定。** 不加「填充開關」。

## Interaction with other open items

- **NR-052** 改 `RefreshRows()` 的 `if (query_.empty())` **條件式**；本 item
  改該分支的**內容**。同函式相鄰行。**建議先落地 NR-052**（較小），
  本 item 接著改分支內部，衝突面最小。若順序相反，落地本 item 時要注意
  不要把條件式改回 `query_.empty()`。
- **NR-054／NR-055／NR-056** 無檔案交集。
- **未來的「設定頁使用者資料管理」item**（清理逾期 pin）會碰
  `PinStore`，不碰本 item 的路徑。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §4 的全部新案例存在且通過。
3. §3 的 5,000 筆空狀態 timing 出現在測試輸出，且在 50 ms 之內。

Manual（Release build，逐條打勾）：

1. **全新狀態**：備份並清空 `%LOCALAPPDATA%\NimbleRun`（或改用一個乾淨的
   使用者帳號），啟動、`Alt+Space`：**格狀畫面被填滿**，顯示的是名稱排序
   靠前的已安裝 App，**沒有** 「No matching apps」。
2. 啟動其中幾個 App 數次，重開面板：這些 App 出現在非釘選區前段，
   且**啟動次數多的排在只啟動一次的前面**（即使後者較晚啟動）。
   這一條是規則 2 的核心驗收，請具體記錄你用了哪幾個 App、各啟動幾次、
   實際看到的順序。
3. 釘選 3 個 App：它們固定在最前、順序不隨使用分數改變；拖曳重排
   （NR-046）仍只在這 3 格之間作用，不會把填充項目捲進來。
4. 累積超過一頁的常用項目後重開面板：**不再出現填充項目**，
   內容與本 item 前一致。
5. 打字進入搜尋、再 `Esc` 回到空狀態：畫面正確回到填滿的格狀。
6. 200% DPI 下重複 #1：填充筆數隨可見容量正確調整（若容量常數與 DPI
   無關，確認畫面仍無空洞）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "panel_model|recent_usage" --output-on-failure
```

```powershell
# 排序只作用在非釘選區：
Select-String -Path src/app_host/panel_model.cpp -Pattern 'stable_sort|partial_sort|std::sort'
# expect: 起點是 rows_.begin() + recent_start_（釘選區從不被排序）

# 分數來自已貼在 entry 上的欄位，不是重新查 UsageStore：
Select-String -Path src/app_host/panel_model.cpp -Pattern 'UsageStore|UsageScore'
# expect: no match

# UsageStore 未被改動：
git diff src/usage/
# expect: empty

# 沒有第三個 24：
Get-ChildItem -Recurse -Include *.cpp,*.h -Path src | Select-String -Pattern '= 24|24;'
# expect: 只有 icon_cache.h 的 kIconCacheWorkingSetItems 與 panel_layout 的既有幾何

# 搜尋路徑未被改動：
git diff src/search/
# expect: empty（若 §1 決定共用 comparator 並因此改了 search_engine.h，
#         交接區必須說明，且改動只能是「抽出具名函式」，不得改變行為）

# 改動範圍：
git diff --name-only
# expect: panel_model.cpp/.h、（可能）main.cpp、panel_model_test.cpp、
#         recent_usage_test.cpp、design-spec.md、（可能）performance-baseline.md
```

## 交接區

（實作者填寫：修改的位置、容量常數用了哪一個、tie-break 是共用還是複製
（及理由）、填充排序用了哪個演算法、`Recent()` 的呼叫端調查結果、
5,000 筆空狀態 timing 的實際數字、建置與 CTest 結果、6 條手動驗收結果
（#2 要寫出具體的 App 與次數）、sanity greps、偏差、未完成事項。）

### 交接內容（2026-08-06）

**修改的位置**
- `src/app_host/panel_model.cpp`：檔首新增匿名 namespace 的三個 helper——`DisplayNameKey`（鏡像 `search_engine.cpp` 的 `NormalizedName` 回退規則）、`OrderByScoreThenName`（§1 的 comparator）、`OrderByName`（§2 填充的 comparator）。`RefreshRows()` 空 query 分支：常用迴圈後新增 `std::stable_sort(rows_.begin() + recent_start_, rows_.end(), OrderByScoreThenName)`（規則 2），其後新增填充區塊（規則 3，`catalog_ != nullptr && rows_.size() < kIconCacheWorkingSetItems` 才跑）。
- `tests/unit/panel_model_test.cpp`：新增規則 2 三案例＋規則 3 七案例＋5,000 筆 timing block，共 11 條；`wmain` 全部註冊；結尾 PASSED 標籤補 `NR-053`。
- `tests/unit/recent_usage_test.cpp`：新增 `TestRecentNeverPads` 守門員（newest-first 且 cap 20 下 2 筆不填充），`wmain` 註冊。
- `tests/unit/pin_store_test.cpp`：兩條既有 PanelModel 案例因規則 3 行為改變做必要調整（見「偏差」）。
- `docs/design-spec.md` §4.2：三條規則後補入 item 提供的原文段。§4.6 未載明分數用於何處，依 item 不補句。
- `docs/work-items.md`：NR-053 狀態改 `done`，計畫決策紀錄最上方新增 done 紀錄。

**容量常數**：用 `icons/icon_cache.h:58` 的 `kIconCacheWorkingSetItems`（= 24，註解即「one full page of cells」）。不用 panel_layout 的欄×列，因 `panel_layout.h` 只有 `kGridColumns`（6）、沒有列數常數，走欄×列等於自造第三個 24。未製造第三個 24（grep 驗證）。

**tie-break 共用還是複製**：**複製**（`OrderByScoreThenName`，`panel_model.cpp` 檔內），並以註解指名與 `search_engine.cpp:170-181` 同步。理由：item §1 明訂「若共用需要新增連結關係或改動 `SearchApps`，就不要共用」；抽出共用具名函式雖不需新增連結（`nimblerun_panel_model` 已 PUBLIC `nimblerun_search`），但會改動 `SearchApps` 的 comparator 主體讓它呼叫外部函式，屬「改動 SearchApps」，故照 item 的 fallback 路徑複製。複製內容去掉 pinned 層後為：分數高→名稱短→大小寫不敏感名稱（`DisplayNameKey`，即正規化名、空時回退 display name）→ stable id，與 search 的尾段逐鍵一致。

**填充排序演算法**：`std::partial_sort`（`candidates` 收集 catalog 內既非釘選也不在 `rows_` 的項目，只取前 `needed = min(容量 - rows 數, 候選數)`）。選它而非全量 `sort`：同為一行、是 stdlib 給「top-N of larger set」的專用演算法，且只在空狀態 rows < 24 時執行；`ponytail:` 註解已載明。候選為 `std::vector<const AppEntry*>`（避免拷貝 5,000 筆），comparator 以 lambda 解參考轉呼叫 `OrderByName`。

**`Recent()` 呼叫端調查**：grep `Recent\(` 於 `src/`——`usage_store.h:63`（宣告）、`usage_store.cpp:248`（定義）、`main.cpp:912`（`RefreshPanelSnapshot`，唯一生產呼叫端）、`main.cpp:925`（`SetRecent`，非 `Recent()` 呼叫）。確認 `UsageStore::Recent()` 只有 `RefreshPanelSnapshot` 一個呼叫端，但依決策 4（呈現順序屬 model）仍在 model 層排序，不改 `UsageStore::Recent()`；`recent_usage_test` 的 `TestRecentNeverPads` 守門員防止未來被「順手」改動。

**5,000 筆空狀態 timing 實測**：**67 µs（0 ms）**，rows 24（`NR-053: RefreshRows over 5000-entry empty state took 67 us (0 ms), rows 24`），遠低於 50 ms 上限。`docs/performance-baseline.md` 無空狀態 RefreshRows 相關列，依 item「若沒有相關列就不要新增列」，未動該檔。

**建置與 CTest 結果**：Release（LLVM-MinGW + Ninja）configure＋`--clean-first` 全量重建成功、**0 warning / 0 error**；`ctest` **23/23 全綠**（含 `nimblerun_lifecycle_check`）；`ctest -R "nimblerun_recent_usage_test|nimblerun_list_vertical_slice_test"` 2/2 通過（panel_model 的 CTest 註冊名確為 `nimblerun_list_vertical_slice_test`，執行檔 `nimblerun_panel_model_test`，另直接執行 exe 驗證 exit 0）。`nimblerun_pinning_test` 最初因規則 3 新行為紅燈，調整 fixture 後全綠。

**sanity greps**：全符合——`stable_sort|partial_sort` 命中 `std::stable_sort(rows_.begin() + recent_start_, ...)`（起點確在釘選區之後）；`UsageStore|UsageScore` 於 `panel_model.cpp` **零命中**（含 PowerShell 預設大小寫不敏感，欄位名 `usage_score` 帶底線不會誤配 `UsageScore`）；`git diff src/usage/` 空、`git diff src/search/` 空（comparator 採複製故未碰 search 路徑）；`= 24|24;` 只命中既有 `kIconCacheWorkingSetItems`、panel_layout 既有幾何註解與無關的 `1024`/`kSearchFontDip`/`kDay`，無第三個 24；`git diff --name-only` 為 design-spec.md、panel_model.cpp、panel_model_test.cpp、pin_store_test.cpp、recent_usage_test.cpp（＋未追蹤的 item 文件交接區與 work-items.md）。

**偏差**（兩處必要調整，皆為 item 自身驗收強制的行為改變所致，設計決策零偏差）：
1. `panel_model_test.cpp` 既有 `TestEmptyStateNoRecords` 原本用非空 catalog（1 筆）斷言「無紀錄→空狀態」，而規則 3 現在會把非空 catalog 補滿、該案例會紅；fixture 改為空 catalog 以維持「空狀態」原意（規則 3 的「空 catalog 兩形」已由新案例 `TestEmptyStateEmptyCatalogNoCrash` 覆蓋）。
2. `pin_store_test.cpp` 兩條既有案例被規則 3 填滿行為推翻（不在 item 的 `git diff --name-only` 預期清單，屬必要調整）：`TestPanelModelPinnedFirst` 的 `rows.size()==2` 因填充變 3——改斷言 rows[0]=p、rows[1]=r1、rows[2]=r2（填充）與 p 只出現一次，測試主旨（釘選在前、不重複）不變；`TestPanelModelHidesAbsentPin` 的 `Rows().empty()` 因填充顯示 catalog 項目——改斷言 rows=[a]（填充）且 ghost pin 永不渲染，主旨（absent pin 不顯示）不變。

**未完成事項**：6 條手動驗收（全新狀態填滿、啟動次數排序、釘選固定＋拖曳、超過一頁後不再填充、Esc 回到填滿格狀、200% DPI）屬人工視覺／操作驗證，依 `AGENTS.md` 交付規則不在 Agent 範圍；未 git commit／未 push。
