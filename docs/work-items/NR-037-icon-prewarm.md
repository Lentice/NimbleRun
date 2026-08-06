# NR-037 — Prewarm the first empty-state page after the panel hides

- Status: `done`
- Phase: 3
- Depends on: NR-036
- Source: `docs/design-spec.md` §FR-009、§4.2、§4.3、§NFR-002

## Goal

面板隱藏後，在背景把「下一次一定會顯示的那一頁」——空白查詢的第一頁（釘選項目 ＋ 常用項目，一頁 24 格）——的圖示載進記憶體 LRU。這樣第二次之後的每次 `Alt+Space`，第一幀就已經有圖，不必等磁碟或 Shell。

NR-036 已讓「登入後第一次」也有圖（從磁碟），本 item 補的是**記憶體層**：連磁碟讀取與 PNG 解碼都提前到使用者看不到的時候做。

## 必讀

`AGENTS.md`（含 Work item authoring rules）、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009（特別是第 391 行「Catalog 不預解碼所有圖示」）、§4.2／§4.3、§NFR-002、§NFR-001、`docs/work-items.md`、`docs/work-items/NR-029-empty-state-grid.md`、`docs/work-items/NR-031`／`NR-032`／`NR-036`、本文件。

依賴檢查：若 NR-036 未 `done`，**回報阻塞**。

## 與 §FR-009「Catalog 不預解碼所有圖示」的界線（重要）

預熱的是**有界的一頁**：`min(24, 空白查詢的項目數)`，即 §4.3 的一頁格數。不是整份 Catalog、不是 `recent_count` 的全部、不是全部釘選項目（釘選數無上限，§FR-011 沒有上限規定）。

這條界線必須寫進 `main.cpp` 預熱函式的註解，理由是它是 Spec 相容性的唯一依據；把上限改大就會違反 §FR-009 第 391 行。

## 硬約束

- 預熱只在 worker thread 做（沿用 NR-032 的同一條），**不新增 thread**。
- 預熱請求以 `visible = false` 送出，必須排在可見請求之後（NR-032 的 `push_front`／`push_back` 分流已提供，不要新增優先度層級）。
- 不新增 timer、不輪詢（§NFR-002）。預熱由「面板隱藏」這個既有事件觸發，不由時間觸發。
- 預熱不得造成任何可見變化：面板隱藏中抵達的結果只進 LRU，**不得** `InvalidateRect`（NR-032 已如此，本 item 不得回頭改）。
- 預熱不得延後 flush：`PostFlush`（NR-036 的時機 1）與預熱請求都在隱藏時送出，順序為**先 flush 再預熱**，避免新抓的圖要等到下一輪隱藏才落地。
- 預熱失敗的鍵沿用既有失敗集合，不得因預熱而讓使用者可見的重試機會消失（`ShowPanel` 仍會清空失敗集合）。
- 不改 LRU 容量公式；容量已由 NR-031 推導為 `釘選數 + recent_count + 24`，足以同時容納預熱集與一次搜尋結果。
- 不新增設定項、不新增 UI 字串。

## Scope

### 1. 預熱集的純值計算（`src/app_host/panel_model.{h,cpp}`）

空白查詢的列表已存在於 `PanelModel`（NR-018 的 pinned → recent 合併、NR-029 的 grid 版面）。新增一個 const 純值查詢：

```cpp
// Stable IDs of the first page of the empty-query view, capped at max_items.
// Empty when the model currently has a non-empty query: prewarming is only
// meaningful for the state the next panel show will actually start in.
std::vector<std::wstring> EmptyStatePrewarmIds(std::size_t max_items) const;
```

- 實作重用既有的空白查詢列組建路徑（`RefreshRows` 已產生的 `rows_`），**不要**複製第二份 pinned＋recent 合併邏輯。
- 若目前查詢非空，回傳空（呼叫端在隱藏時已呼叫 `Reset()`／清空查詢的既有路徑，故實務上為空查詢；此判斷是防呆）。
- 不改變任何狀態（`const`）。

### 2. 觸發點（`src/app_host/main.cpp`）

在 NR-036 已建立的**那一個**隱藏面板匯流函式內，`PostFlush` 之後：

```cpp
// Prewarm exactly one empty-state page (design-spec §4.3, 24 cells). Bounded on
// purpose: §FR-009 forbids predecoding all catalog icons.
for (const std::wstring& id : g_model->EmptyStatePrewarmIds(nimblerun::kIconCacheWorkingSetItems)) {
    // grid variant for the current monitor DPI; skip keys already cached,
    // pending, or in the failure set
    g_icon_worker->Post({entry, key, /*visible=*/false});
}
```

- variant 取**grid** 狀態的需求（`kIconSizeDip` 於當前 DPI 下的實體像素經 `IconVariantForPixels`），因為下次開窗一定是空白查詢＝grid 狀態。清單狀態的 variant 在同一階梯內多半相同，不需另外預熱。
- 由 stable ID 取回 `AppEntry` 沿用既有 catalog snapshot 的查詢方式；查不到的 ID 直接跳過（釘選項目可能已不在 catalog，§FR-011 允許此狀態）。
- 已在 LRU（`Peek` 命中）、已在 pending、已在失敗集合的鍵一律跳過，避免每次隱藏都重送 24 筆。
- DPI 變更後（`WM_DPICHANGED`）不需特別處理：下次隱藏時自然以新 DPI 的 variant 預熱。

## Non-goals

- 不預熱第二頁、不預熱搜尋結果、不預熱整份 Catalog。
- 不預熱清單狀態專用的 variant。
- 不在啟動時預熱（啟動路徑要保護 §NFR-001 的「冷啟動至可接收快捷鍵 ≤ 500 ms」；預熱只在第一次隱藏之後才有意義）。
- 不新增 thread、timer、優先度佇列、取消機制。
- 不改 grid／清單版面、幾何、面板尺寸、palette、輸入處理。
- 不改 LRU 容量公式、variant 階梯、store 格式、flush 時機。
- 不改 catalog、dedup、usage、pin、settings 的邏輯或格式。
- 不回頭修改 NR-029／NR-031／NR-032／NR-036 文件。

## Acceptance

- 隱藏面板後，24 格空白狀態圖示在背景載入完成；再次 `Alt+Space` 時**第一幀即為真實圖示**，無 fallback 閃動。
- 預熱期間面板隱藏，畫面無任何變化（無閃爍、無 `WM_PAINT` 風暴）；以診斷或斷點確認隱藏中未呼叫 `InvalidateRect`。
- 預熱請求排在可見請求之後：隱藏後立刻再次開窗並輸入搜尋字串，搜尋結果的圖示先於剩餘預熱項目抵達。
- 連續隱藏／顯示十次，`Post` 的總筆數不隨次數線性成長（已快取的鍵被跳過）。
- 預熱項目數上限為 24：釘選 40 個 App 時，隱藏後最多送出 24 筆。
- 釘選項目已不在 catalog 時被跳過，不產生失敗請求、不寫入失敗集合。
- 隱藏時先發生 flush、後發生預熱（以診斷順序或 fake 記錄驗證）。
- 待機執行緒數與 NR-036 相同；待機 CPU 在預熱完成後回到 0（worker 阻塞於 condition variable）。
- 待機工作集符合 §NFR-001 更新後的 60 MiB 目標（預熱後 LRU 常駐約 24 筆 decoded bitmap；96×96×4 B × 24 ≈ 0.9 MiB，最大 variant 256 的情形亦應量測記錄）。
- repo 內搜尋不到第二份 pinned＋recent 合併邏輯（`EmptyStatePrewarmIds` 重用既有 `rows_`）。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "list_vertical_slice|icon" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/panel_model_test.cpp` 新增 case（純值，不需視窗）：

- 空白查詢、3 個釘選 ＋ 5 個常用 → `EmptyStatePrewarmIds(24)` 回傳 8 個 ID，順序與 `rows_` 一致（釘選在前）。
- 項目數 40（釘選 40）→ `EmptyStatePrewarmIds(24)` 回傳恰 24 個。
- `max_items = 0` → 回傳空。
- 查詢非空時回傳空。
- 空 catalog／無釘選無常用 → 回傳空。
- `EmptyStatePrewarmIds` 為 `const`：呼叫前後 `SelectionIndex()`／`FirstVisibleRow()`／`RowCount()` 不變。
- 回傳的 ID 全部能在傳入的 catalog snapshot 中找到（不含已不存在的釘選項目所對應的 ID——若模型的 `rows_` 本就已濾掉 absent pin，則以斷言固定此性質）。

`tests/unit/icon_worker_test.cpp` 新增 case：

- 先送 3 個 `visible = false` 請求，再送 1 個 `visible = true` 請求 → fake provider 的呼叫順序中，`visible = true` 那筆排在第一個未處理的預熱請求之前。

第一幀無 fallback 閃動屬人工驗證，不列入 Agent 交付，但必須在交接區記錄觀察結果。

## 交接區

- Start: 2026-08-05。NR-036 已確認 `done`（`HidePanel` 於 main.cpp、`PostFlush` 於 icon_worker 皆存在）。Trace 完成 panel_model（`RefreshRows`／`rows_`／pinned＋recent 合併）、main.cpp（`HidePanel` 匯流函式、`IconKeyFor`、`g_pending_icon_keys`／`g_requested_icon_keys`、`g_refresh->Snapshot()` 查詢方式）、icon_worker（`Post` push_front/push_back 分流、`IconRequest.visible`）、icon_cache（`kIconCacheWorkingSetItems=24`、`Peek`）、panel_layout（`kIconSizeDip`）。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/panel_model.{h,cpp}`（`RefreshRows`、`rows_`、pinned＋recent 合併、`Columns()`）、`src/app_host/main.cpp`（隱藏面板的匯流函式、NR-036 的 `PostFlush` 呼叫點、`IconKeyFor`、pending 與失敗集合、catalog snapshot 的 stable ID 查詢方式）、`src/icons/icon_worker.{h,cpp}`、`src/icons/icon_cache.h`（`kIconCacheWorkingSetItems`）。先確認 NR-036 已 `done`，否則回報阻塞。只實作本 item 的 Scope，維持預熱上限為一頁。回報修改檔案、測試命令、結果、第二次開窗第一幀的實測觀察與未完成事項。
- Result: 已完成。修改檔案：`src/app_host/panel_model.{h,cpp}`（新增 `std::vector<std::wstring> EmptyStatePrewarmIds(std::size_t max_items) const`：查詢非空或 `max_items==0` 回傳空；否則取 `rows_` 前 `min(max_items, rows_.size())` 筆的 `stable_id`，重用 `RefreshRows` 的 pinned→recent 合併結果，不複製第二份合併邏輯；註解說明上限界線為 §4.3 一頁 24 格且 §FR-009 禁止預解碼全 Catalog）、`src/app_host/main.cpp`（在 `HidePanel` 內 `PostFlush` 之後呼叫新增的 `PrewarmEmptyStatePage`：variant 取 `kIconSizeDip` 於 `GetDpiForWindow` DPI 的實體像素經 `IconVariantForPixels`；由 `g_refresh->Snapshot()` 依 stable ID 解析 `AppEntry`，查不到跳過；`Peek` 命中／在 `g_pending_icon_keys`／在 `g_requested_icon_keys` 的鍵一律跳過；以 `visible=false` `Post`；註解標明「預熱上限＝一頁」為 §FR-009 相容性的唯一依據）、`tests/unit/panel_model_test.cpp`（新增 7 case：3 釘選＋5 常用→8 個且順序與 `rows_` 一致、釘選 40→恰 24、`max_items=0`→空、查詢非空→空、空 catalog→空、const 不改 SelectionIndex/FirstVisibleRow/rows_、absent pin 被濾除且回傳 ID 全部可在 catalog snapshot 找到）、`tests/unit/icon_worker_test.cpp`（新增 1 case：3 個 `visible=false`＋1 個 `visible=true`，後者跳至第一個未處理預熱請求之前，以結果到達順序驗證）。Agent checks：`ctest -R "list_vertical_slice|icon"` 5/5、全套件 23/23 通過；clean build 無新增 warning。未完成：無程式事項。「第二次開窗第一幀無 fallback」屬人工驗證，無法在 Agent 環境實測（未操作視窗），需實際執行 Release 版：首次 `Alt+Space` 顯示後隱藏，等約 1 秒（預熱完成），再次 `Alt+Space` 應無 fallback 閃動；連續隱藏／顯示十次時 `Post` 總筆數不隨次數線性成長（已快取鍵被跳過）；隱藏期間畫面無變化（無 `InvalidateRect`）亦屬人工驗證。
