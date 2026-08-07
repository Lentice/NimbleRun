# NR-071 — 常用區依最後啟動時間排序，最新的在最前面

- Phase: 3
- 覆寫：`docs/design-spec.md` §4.2 規則 2「未釘選常用項目，**依使用分數排序**」
  與其下的同分比較段（`:142`、`:144`）。本 item 改為依 `last_launch_utc`
  遞減排序，最近啟動的排最前。
- 同時覆寫：`docs/work-items.md` §已否決的方向 中 NR-061 那一列所寫的
  「**NR-053 依 `usage_score` 排序的那一半保留**」。該句在 2026-08-07
  NR-061 完成時為真，本 item 之後不再為真；覆寫依據見下方〈為什麼〉。
- **不覆寫** §4.5／§4.6：搜尋結果的次要排序仍用 `usage_score`，分數模型一字不動。

## 為什麼

今天常用區疊了**兩層方向相反的排序**：

1. `UsageStore::Recent(cap)`（`src/usage/usage_store.cpp:160-176`）依
   `last_launch_utc` 遞減挑出前 N 筆 —— 已經是最新在前。
2. `PanelModel::RefreshRows()`（`src/app_host/panel_model.cpp:135-136`）緊接著
   用 `OrderByScoreThenName` 把這批**依 `usage_score` 重排**。

結果是使用者剛開過的 App 未必出現在最前面：一個十分鐘前才開的工具，會被一個
上週天天用、這週沒碰的 App 壓在後面。使用者的心智模型是「我剛用過的東西應該在
手邊」，第 2 層把第 1 層挑好的順序打亂了。

NR-053 當初導入第 2 層的理由寫在 `panel_model.cpp:26-28` 的註解裡：「否則一小時
前開過的冷門 App 會壓過天天用的主力」。那句話描述的現象是真的，但**判斷相反**：
使用者要的就是那個一小時前開過的東西排前面。真正需要固定位置的主力 App，答案是
**釘選**——釘選區永不重排，這就是它存在的意義。用分數把常用區排成「準釘選區」，
等於做了第二套半吊子的釘選，兩邊語意重疊。

使用者決策（2026-08-07，已確認，逐條）：

1. **純時間序**：每次啟動都把該 App 移到常用區最前面，含已經在畫面上的 App。
   不做「只有新進入清單的才插到最前」——那需要在 `usage.tsv` 多存一個「首次
   進入順序」欄位並升 schema，成本遠大於本 item，而且對使用者不可解釋
   （「為什麼這個跳前面那個不跳」）。格子位置不穩定的代價由釘選區吸收。
2. **搜尋不動**：§4.5 的次要排序仍是 `usage_score`。搜尋已先用 `MatchRank`
   分組，分數只在同層級內當 tie-break，那裡沒有位置記憶問題，且「打 `chr`
   要的是天天用的 Chrome，不是十分鐘前誤開一次的 Chromium」。
3. **不順手修「常用區會少於 `recent_count` 格」**：見〈非目標〉。

## 硬約束（引用自專案規則，不要再去翻）

- `AGENTS.md`：優先採取最小可行改動；先重用既有程式碼，再考慮新增 helper 或抽象。
  **本 item 的正確形狀是刪除，不是新增。**
- `AGENTS.md`：搜尋、排名、評分、持久化格式等核心邏輯盡量與 HWND 及 Shell COM 解耦。
- `AGENTS.md`：新的非平凡邏輯要留一個 focused 可執行檢查。
- `AGENTS.md`：不得把 schema 遷移或破壞性資料清理夾帶進無關改動。
  **本 item 不動任何持久化格式。**
- `AGENTS.md`：行為改變時同步更新相關文件。
- `docs/design-spec.md` §4.2：同一 App 不可重複出現；釘選與常用共用同一種格子
  外觀，不加分組標題或分隔線，順序本身即為區隔。這三條**不變**。
- `docs/design-spec.md` §FR-011：釘選區依使用者排序，永不被自動排序覆蓋。**不變。**

## 要讀與追的檔案

- `src/app_host/panel_model.cpp:14-19`（`DisplayNameKey`）、`:21-42`
  （NR-053 註解 ＋ `OrderByScoreThenName`）、`:78-148`（`RefreshRows()`，
  特別是 `:118` 的 `recent_start_`、`:119-126` 的常用列迴圈、`:128-136` 的排序）。
- `src/usage/usage_store.cpp:160-176`（`Recent()`：`last_launch_utc` 遞減，
  同值時 `stable_id` 遞增，然後截斷到 `cap`）。**這個契約不改。**
- `src/app_host/main.cpp:1127-1156`（`RefreshPanelSnapshot()`）。**已確認**
  `:1145-1152` 是照 `recent_records` 的順序逐筆推進 `recent_entries`，所以
  `Recent()` 的 newest-first 順序一路保存到 `SetRecent()`；刪掉排序後順序天然
  正確，**不需要在 `AppEntry` 上新增 `last_launch_utc` 欄位**。
- `src/search/search_engine.cpp:170-181`（`SearchApps` 的比較子尾段）。
  **本 item 完全不改這個檔案**，只是要知道 `OrderByScoreThenName` 是它的複本，
  刪掉複本不影響本體。
- `src/usage/usage_store.cpp:109-120`（`Forget()`）與 `src/app_host/main.cpp`
  的 `kRemoveFromRecent` 分派處（`:2664` 一帶）。**只讀不改**，見範圍 §4。
- `docs/design-spec.md:137-157`（§4.2）、`:206-221`（§4.6，只讀不改）。
- `docs/work-items.md:160`（已否決方向表中 NR-061 那一列）。

### 依賴

NR-061（空白狀態只顯示釘選與常用，無字母填充）必須已完成——已 `done`。
本 item 沒有其他依賴。

## 範圍

### 1. 刪除 `panel_model.cpp` 的分數排序

刪除 `:128-136` 的整段（NR-053 註解 ＋ `std::stable_sort` 呼叫）。
在原位留下一行 NR-071 註解說明**為什麼這裡刻意沒有排序**，否則下一個讀到
「常用區未排序」的人會以為是漏了：

```cpp
        // NR-071: the recent region is deliberately NOT sorted here.
        // UsageStore::Recent() already returns records newest-first (last
        // launch descending, stable id ascending on ties) and the loop above
        // preserves that order, so the most recently launched app is the first
        // non-pinned cell. NR-053 used to re-sort this range by usage_score;
        // that made a daily driver outrank an app opened ten minutes ago,
        // which is the opposite of what the recent region means. Apps that
        // need a fixed position are pinned -- design-spec §4.2 rule 2.
```

同時刪除 `:21-42` 的 `OrderByScoreThenName` 與其上方的 NR-053 註解區塊
（`:21-28`），以及 `:14-19` 的 `DisplayNameKey`——**先確認 `DisplayNameKey`
在本檔案內沒有第二個呼叫點**（用編譯器：刪掉之後若有其他使用者會編譯失敗）。
若 `<algorithm>` 的 include 在本檔內只為 `std::stable_sort` 而存在，也一併刪；
`:120` 的 `std::find_if` 同樣來自 `<algorithm>`，所以**大機率要保留 include**，
編譯器說了算，不要憑猜。

這是本 item 唯一的行為改動，淨效果是刪除約 30 行。**不要新增 comparator、
不要新增 `OrderByRecency`、不要在 `AppEntry` 加欄位**——順序已經是對的，
要做的事只有「不要把它弄亂」。

### 2. 釘選區完全不受影響

`recent_start_`（`:118`）語意不變，釘選區本來就不在被刪掉的排序範圍內
（`stable_sort` 的起點是 `rows_.begin() + recent_start_`）。刪除後釘選區依然
照 pin 順序、依然永不重排。缺失佔位格（NR-062）也不受影響。

### 3. 搜尋路徑不變

`StampRankingFields()`（`main.cpp:1105-1123`）**繼續保留**：`usage_score` 仍是
搜尋結果的次要排序來源（§4.5），只是不再被常用區使用。
`src/search/`、`src/usage/` 兩個目錄的 `git diff` 在本 item 完成後必須是**空的**。

### 4. 「Remove from recent」不需要改（但要加守門員測試）

`UsageStore::Forget()`（`usage_store.cpp:109-120`）刪的是整筆 `UsageRecord`，
`total_launches` 與 `last_launch_utc` 一起消失，所以右鍵「Remove from recent」
**現在就已經把分數清乾淨**，連帶從搜尋 tie-break 中消失。這是正確行為，
本 item 不改任何一行，但要新增一個守門員測試把它釘住（見〈Agent 檢查〉），
防止日後有人把 `Forget` 改成「只清時間戳、保留次數」。

另外兩條「離開常用區」的路徑**維持現狀、不清分數**，這是刻意的：

- 被 `recent_count` 上限擠出去：紀錄還在，搜尋仍算它的分數。
- `Reconcile()` 因 App 從 Catalog 消失而刪除：§4.2 明文規定的對帳。

### 5. 修改 `docs/design-spec.md` §4.2

`:142` 的規則 2 改為：

```
2. 未釘選常用項目，依最後一次啟動時間排序，最近啟動者在最前。
```

`:144` 整段（「規則 2 的使用分數以 §4.6 的 `usage_score` 為準；分數相同時…」）
**整條取代**為：

```
規則 2 的時間以 §4.6 的啟動統計所記錄的最後一次成功啟動時間為準；時間相同時
依 stable ID 遞增排序，確保重新載入後順序決定性一致。此處**不**使用
`usage_score`——分數只用於 §4.5 的搜尋結果次要排序。需要固定位置的 App 應由
使用者釘選（規則 1），釘選區永不被自動排序覆蓋。
```

§4.6 一字不動（分數模型與搜尋的關係沒變）。

### 6. 修改 `docs/work-items.md`

- Item 總覽表最後新增一列：
  `| NR-071 | 常用區依最後啟動時間排序，最新在最前 | 3 | ready | NR-061 | [NR-071](work-items/NR-071-recent-ordered-by-recency.md) |`
  （狀態由實作者依實際進度更新）
- §已否決的方向 中 NR-061 那一列（`:160`）的「否決理由」欄末尾，在
  「**NR-053 依 `usage_score` 排序的那一半保留。**」之後**接一句**，不要刪除原句
  （原句是當時決策的真實紀錄）：
  「（2026-08-07 由 NR-071 覆寫：常用區改依最後啟動時間排序，`usage_score`
  僅留給 §4.5 搜尋。）」
- **不要修改 `docs/work-items/NR-053-*.md` 與 `NR-061-*.md` 兩份已完成文件**
  （`AGENTS.md`：完成的 item 文件是歷史紀錄，不得編輯）。

## 非目標

- **不修「常用區實際格數少於 `recent_count`」**。`Recent(cap)` 是先取 N 筆、
  才由 `panel_model.cpp:119-126` 濾掉已釘選項，所以釘了 3 個常用 App 就只剩
  17 格。這是「取幾筆」的問題不是「怎麼排」的問題，牽動 `Recent()` 的契約與
  `recent_usage_test.cpp` 的 20-cap 測試。要做請另開 item。
- 不改搜尋結果的排序（§4.5）。
- 不改 `usage_score` 公式或 `recency_bonus` 級距（§4.6）。
- 不改 `usage.tsv` 的格式、schema 版本或任何持久化行為。
- 不在 `AppEntry` 新增 `last_launch_utc` 或任何欄位。
- 不加分組標題、分隔線或「Recent」標籤（§4.2 明文禁止）。
- 不做「最近啟動的閃一下／高亮」之類的視覺提示。
- 不改 `recent_count` 的預設值或 8..40 的範圍。

## 驗收條件

1. 空白查詢下，最後一次啟動的非釘選 App 是常用區的**第一格**
   （即 `Rows()[RecentStartIndex()]`），不論它的 `usage_score` 多低。
2. 啟動一個原本排在常用區中段的 App → 它移到常用區第一格，其前方項目各往後
   一格，釘選區完全不動。
3. 釘選區順序在任何啟動行為後都不變（§FR-011）。
4. 兩筆 `last_launch_utc` 相同的紀錄，其相對順序依 `stable_id` 遞增，且重啟
   應用程式後順序完全相同。
5. 已釘選的 App 不出現在常用區（§4.2「同一 App 不可重複出現」未回歸）。
6. 缺失釘選佔位格（NR-062）仍在釘選區原位，未被推進常用區。
7. 搜尋結果的排序完全未變：同一組查詢在改動前後結果順序逐項相同。
8. 右鍵「Remove from recent」後該 App 從常用區消失，且其分數不再影響搜尋排序。

## Agent 檢查（可執行）

`tests/unit/panel_model_test.cpp`——**改寫**兩個現有測試（它們斷言的是已被
本 item 否決的行為，留著就是繼續守護錯誤規格）：

- `TestRecentOrderedByUsageScore()`（`:766`）→ 改名
  `TestRecentOrderedByRecency()`。原本設 `usage_score = 5/100/20` 並斷言
  `100` 排最前；改為**保留那組刻意「錯誤」的分數**，並把 `SetRecent()` 傳入的
  順序設為 newest-first（第 0 筆最新），斷言 `Rows()` 的常用區順序**等於傳入
  順序**、與分數大小無關。這個測試的價值就在於分數與順序刻意相反。
- `TestRecentTieBreakByLengthThenName()`（`:787`）→ 改名
  `TestRecentIgnoresNameAndScoreTieBreaks()`。原本三筆同分斷言依名稱長度／
  字典序重排；改為斷言常用區**維持傳入順序**，名稱長短與字典序都不影響。

**新增**：

- `TestRecentPreservesInputOrder()`：`SetRecent()` 傳入 5 筆、`usage_score`
  刻意設成遞增（與傳入順序相反），斷言 `Rows()` 由 `RecentStartIndex()` 起的
  `stable_id` 序列與傳入序列逐項相同。
- `TestPinnedRegionStillNotSorted()`：沿用既有 `TestPinnedRegionNotSortedByScore()`
  （`:823`）的 fixture，改名並確認在沒有排序之後釘選區順序仍等於 pin 順序。
  （既有測試可能已通過，但它的名字暗示「有排序但排除釘選區」，語意已變。）

`tests/unit/recent_usage_test.cpp`——**新增**兩個守門員（不改既有案例）：

- `TestForgetClearsScoreCompletely()`：`RecordLaunch(id, t)` ×3 →
  `UsageScore` 對該 id 為正 → `Forget(id)` → `Records()` 不含該 id，
  且重新 `Save()`／`Load()` 後仍不含。釘住「移除即清分數」。
- 既有 `TestRecentOrdering`（`:94-98`）與 20-cap（`:105-112`）**不得修改**——
  `Recent()` 的契約是本 item 的前提，動它就是動錯地方。

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

三項 sanity grep（全部必須為預期值）：

```powershell
# OrderByScoreThenName 與 DisplayNameKey 應完全消失於 panel_model.cpp
Select-String -Path src/app_host/panel_model.cpp -Pattern 'OrderByScoreThenName|DisplayNameKey|stable_sort'
# 搜尋與使用統計兩個模組完全未動
git diff --stat src/search/ src/usage/
# usage_score 在 panel_model.cpp 應零命中（欄位、註解皆無）
Select-String -Path src/app_host/panel_model.cpp -Pattern 'usage_score'
```

手動驗收（Release build，8 條逐條在交接區記錄結果）。第 1、2 條需要真實啟動
記錄：先開一個平常不用的 App（例如小算盤）一次，`Alt+Space` 後它應該是常用區
第一格。

## 交接區

（實作者填寫：實際刪除行數、`<algorithm>` include 的去留與編譯器依據、
`DisplayNameKey` 是否還有其他呼叫點、`pin_store_test.cpp` 是否有測試因順序改變
而需要調整斷言、`ctest` 結果、手動驗收 8 條的結果、以及任何偏離本文件之處。）
