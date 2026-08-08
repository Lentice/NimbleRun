# NR-083 — 建一次 stable_id 索引，取代熱鍵路徑上三次全 catalog 線性掃描

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §「效能目標」第 489 行（「暖狀態快捷鍵至可輸入 p95 ≤ 80 ms」）
- Origin: 2026-08-08 效能稽核（`RefreshPanelSnapshot` / `StampRankingFields` / `PanelModel::RefreshRows` 對照 catalog 上限與熱鍵路徑）

## Why

`ShowPanel` 到面板可輸入之間，同一份 catalog snapshot 被**未索引地全掃描三次**：

1. `StampRankingFields`（`src/app_host/main.cpp:1190-1208`）對 snapshot 每個
   entry 都跑一次 `std::find`（`pins` 向量）——本身是小集合，作者已在註解
   （`main.cpp:1188`）承認「pin 只有一小把，直接掃就好」，這部分不是問題。
2. `RefreshPanelSnapshot`（`main.cpp:1212-1241`）對每個 recent record，
   `for (... : g_refresh->Snapshot())` 全 catalog 線性找 `stable_id`
   （`:1230-1236`）——外層是 recent_count（`g_settings.recent_count`，通常
   個位數到十幾），內層是整個 catalog（設計上限 5,000，`docs/design-spec.md`
   catalog 上限章節），O(recent_count × catalog_size)。
3. `PanelModel::RefreshRows` 空 query 分支（`src/app_host/panel_model.cpp:56-77`）
   對每個 pin，`for (const AppEntry& entry : *catalog_)` 再全掃一次找
   `stable_id`（`:58-65`）——O(pin_count × catalog_size)，找不到時（missing pin，
   NR-062）是全掃無 `break` 提前命中。

三處都在 `ShowPanel` 這條「熱鍵到首幀」路徑上（`RefreshPanelSnapshot` 由
`ShowPanel` 呼叫；`RefreshRows` 由 `SetCatalog`/`SetRecent`/`SetPins` 觸發，這三個
又都在 `RefreshPanelSnapshot` 內被呼叫），對同一份 catalog 反覆做「拿 stable_id
換 AppEntry」這件事，卻沒有共用索引。design-spec 489 行把這條路徑的預算訂在
p95 ≤ 80 ms，而 `docs/performance-baseline.md` 第 12 行明載這個指標「尚未量測」。
在 catalog 上限（5,000）與典型 recent/pin 數量下，這三處線性掃描累加起來很可能
只是幾百微秒等級，不足以單獨壓爆 80 ms 預算——但這是**在該預算第一次被量測之前
就能看出來、且修法完全不影響行為**的複雜度問題：一次 `unordered_map<wstring,
size_t>` 索引可以同時餵給第 2、3 處（`StampRankingFields` 的 pin 掃描維持原樣，
理由同上）。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **只加一個索引：`stable_id -> catalog 內 index` 的 `unordered_map`**，建在
   `RefreshPanelSnapshot` 內、`g_refresh->Snapshot()` 確定不再變動之後，
   一次建好傳給需要它的兩處查詢（recent 解析、`PanelModel` 的 pin 解析）。
   不做「catalog 內建索引欄位」「snapshot 物件自帶 map」這類更大改動——索引只在
   這一次 refresh 期間有效，catalog 指標一換就重建，沒有失效管理的問題。
2. **`PanelModel::RefreshRows` 改吃索引，不吃 catalog 指標做內部線性掃描**：
   在 `PanelModel` 上加一個可選的索引參數／setter（型別為
   `const std::unordered_map<std::wstring_view, size_t>*` 或等價物，實作者可依
   簽名慣例調整），由 `RefreshPanelSnapshot` 在呼叫 `SetCatalog` 之前／同時給。
   不引入回呼或觀察者模式——`PanelModel` 目前是被動資料容器，維持這個形狀。
3. **`StampRankingFields` 的 pin 掃描不動**：pin 集合本身就小，作者原註解
   （`main.cpp:1188`）已經是正確判斷，不在本 item 範圍內用索引取代，避免無謂改動
   一個沒有問題的地方。
4. **不加 debounce、快取跨 frame 的索引、或提前量測 harness**：本 item 只解決
   「同一份 snapshot 內重複線性掃描」這個複雜度問題，不涉及量測本身
   （量測 harness 屬另一個未開的 item，見 `docs/performance-baseline.md` 第 12
   行「需要一支量測『熱鍵到首幀』的計時器，尚未實作」）。
5. **不改 `AppEntry`、`PinRecord`、`UsageRecord` 的資料結構**，索引只存
   `wstring_view -> index`，view 指向 snapshot vector 內的 `stable_id`，
   索引的生命週期不得超過建索引當下的 snapshot（snapshot 是
   `g_refresh->MutableSnapshot()`，rebuild 會整份替換，索引不得跨 rebuild 保留）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` 第 489 行：

> | 暖狀態快捷鍵至可輸入 | p95 ≤ 80 ms | p95 > 150 ms |

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.
- New non-trivial logic needs one focused runnable test or self-check.

`docs/performance-baseline.md` 第 12 行：

> | Warm hotkey to input-ready, p95 | ≤ 80 ms | > 150 ms | Not measured | 需要一支量測「熱鍵到首幀」的計時器，尚未實作 |

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:1190-1241` — `StampRankingFields`（不改）與
  `RefreshPanelSnapshot`（本 item 的主要修改點：recent 解析改吃索引）。
- `src/app_host/panel_model.h` / `panel_model.cpp:41-77` — `RefreshRows` 的
  pin 解析迴圈；需要新增一個吃索引的入口（setter 或 `SetCatalog` 多帶一個參數，
  實作者決定哪個改動面小）。
- `src/catalog/app_entry.h` — `AppEntry::stable_id` 的型別，確認索引用
  `std::wstring_view` 或 `std::wstring` 當 key 與現有欄位型別相容。
- `src/app_host/main.cpp:1944` 一帶（`ShowPanel`）— 確認呼叫順序：
  `RefreshPanelSnapshot` 何時被呼叫，索引要建在這條路徑的哪一步。
- `docs/work-items/NR-062-missing-pin-placeholder.md` — `PanelModel::RefreshRows`
  找不到 pin 對應 entry 時要落回 placeholder 的既有行為，索引化後必須保留。

## Scope

1. 在 `RefreshPanelSnapshot`（`main.cpp:1212-1241`）裡，`StampRankingFields()`
   之後、使用 snapshot 做 recent 解析之前，建一次
   `std::unordered_map<std::wstring_view, size_t>`（key 為 `stable_id`，value 為
   `g_refresh->Snapshot()` 內的 index），取代 `:1230-1236` 的巢狀迴圈。
2. 把同一個索引（或等價的唯讀參照）交給 `PanelModel`，取代
   `panel_model.cpp:58-65` 的巢狀迴圈；找不到時的 placeholder 行為
   （`:66-76`）不變。
3. `StampRankingFields` 的 pin 掃描原樣保留（Decisions §3）。

## Non-goals

- 不新增「熱鍵到首幀」的量測 harness（那是 baseline 文件已列出的另一件未開工作）。
- 不改 `StampRankingFields` 的 pin 查找。
- 不改 `SearchApps`／`DeduplicateCatalog`（`docs/performance-baseline.md` 已用量測
  否決重開這兩個議題，門檻是先量到單幀 > 8 ms）。
- 不改 `usage.tsv` 寫入時機或 `Settings::Load()` 的呼叫頻率（稽核中列為 LOW，
  影響推測為次要，不在本 item 範圍）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有測試數量不減少）。

Manual（Release build）：

1. 有釘選與最近使用項目的情況下按熱鍵開面板，畫面內容與修改前一致
   （釘選順序、最近使用順序、missing pin placeholder 都不變）。
2. 解除釘選、卸載一個曾被 pin 的 App 後重新整理 catalog，missing pin
   placeholder 仍正確顯示（NR-062 迴歸）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# RefreshPanelSnapshot 的 recent 解析不再對每筆 recent 全掃 catalog：
Select-String -Path src/app_host/main.cpp -Pattern 'unordered_map.*wstring_view'
# expect: RefreshPanelSnapshot 內新增一處

# StampRankingFields 的 pin 掃描保持原樣未變：
Select-String -Path src/app_host/main.cpp -Pattern 'std::find\(pins.begin\(\), pins.end\(\)'
# expect: 仍存在、未被索引取代

git diff --name-only
# expect: src/app_host/main.cpp、src/app_host/panel_model.h、
#         src/app_host/panel_model.cpp（及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

實作位於既有 `main.cpp`／`panel_model.{h,cpp}`：`RefreshPanelSnapshot` 建立
`std::unordered_map<std::wstring_view, std::size_t>`，以 `PanelModel::SetCatalogIndex`
傳入 pin 解析；recent 解析也改用同一索引，`StampRankingFields` 的 pin 線性掃描維持不變。
本次補上 `tests/unit/panel_model_test.cpp` 的 `TestCatalogIndexResolvesPinsLikeScan`，覆蓋
存在／缺失 pin、順序、placeholder 與清除索引後回退線性掃描。

Agent checks：Release build 成功、完整 CTest **24/24 通過**；`unordered_map` 與既有
`std::find(pins.begin(), pins.end())` sanity greps 均符合。手動驗收未執行（GUI 操作不屬
Agent 可執行範圍）；focused test 已證明索引路徑與原掃描路徑產生相同 rows。未完成事項：無。
