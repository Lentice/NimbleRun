# NR-073 — `kRebuildDoneMessage` must refresh the panel and write `catalog.cache` only when the generation actually completes

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-008（重建期間沿用舊 Catalog、成功後整批替換）／§NFR-001（資源預算）
- Origin: 2026-08-08 第四次全 repo 稽核（main.cpp 全檔）

## Why

`kRebuildDoneMessage` 處理器（`src/app_host/main.cpp:2311-2347`）對**每一份**來源結果
都無條件執行：

```cpp
if (result_applied && g_refresh->GenerationComplete(result->generation)) {
    g_launch_failure_refresh.OnRefreshComplete();
}
RefreshPanelSnapshot();
nimblerun::SaveCatalogCache(nimblerun::DefaultSettingsDir(),
                            g_refresh->Snapshot());
InvalidateRect(window, nullptr, FALSE);
```

而 `CatalogRefreshCoordinator` 的契約（`src/catalog/catalog_refresh.h:52-54`）是
`ApplySourceResult` **只在該 generation 的所有來源都回報後**才重算 merged snapshot。
一次全來源 rebuild 有三個來源（main.cpp:2385-2389），所以前兩份結果並未改變任何
東西，卻各自觸發：

1. `RefreshPanelSnapshot()` → `RefreshPins()`＋`SetCatalog`→`PanelModel::RefreshRows`
   （`panel_model.cpp`）把 `selected_`／`first_visible_` 重置為 0——使用者在 rebuild
   進行中瀏覽舊 snapshot 時，選取與捲動位置被無聲偷走 2～3 次（`Ctrl+R`、設定套用、
   啟動失敗 refresh 都是面板可見情境）。
2. 冗餘的 UI 執行緒磁碟寫入：`RefreshPins` 的 `g_pins->Save()`（favorites.txt）、
   usage `Reconcile`＋`Save`、以及整份多 MB 的 `catalog.cache` 二進位重寫——每輪
   重建最多 3 次，且 `result_applied == false`（stale generation）時也照寫。

違反 §FR-008「重建期間沿用舊 Catalog；成功後才整批替換」的精神與 NFR-001 的預算
假設。這是稽核新增發現，未在先前任何 item 涵蓋。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **把 `RefreshPanelSnapshot()`＋`SaveCatalogCache()` 收進 `GenerationComplete`
   區塊**：只有整代完成、snapshot 真正被 swap 的那份結果才需要重新指向 model 並
   落盤。`InvalidateRect` 留在外面每份結果照畫（成本極低，且每份結果都該重繪）。
2. **`OnRefreshComplete()` 的位置不動**（已在 `GenerationComplete` 內，NR-022 語意
   正確）。
3. **不加「snapshot 是否真的變了」的深比較**：`GenerationComplete` 時重算一次是
   正確且必要的（AppsFolder 10 分鐘到期路徑只有單一來源，第一次結果就 complete）。
4. **測試策略**：`catalog_refresh_test` 已覆蓋 coordinator 的 merge/failure 語意
   （`TestFailureKeepsOldSnapshot` 等），此改動是 host 端呼叫時機，無測試 seam；
   由 sanity grep（`RefreshPanelSnapshot` 只在 `GenerationComplete` 區塊內）與手動
   驗收覆蓋（NR-060 先例）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.

design-spec §FR-008：

- 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2311-2347` — `kRebuildDoneMessage` 處理器。主場。
- `src/catalog/catalog_refresh.h:40-70` — `GenerationComplete`／`ApplySourceResult`
  契約（merged 只在全代完成後更新）。**只讀不改**。
- `src/app_host/main.cpp:1179-1208` — `RefreshPanelSnapshot`（確認其內容與成本）。
- `src/app_host/main.cpp:2280-2310` — `StartRebuild` 的 due sources 與
  `kWatchChangedMessage` full-rescan 分支（呼叫端語意）。

## Scope

把 `RefreshPanelSnapshot()` 與 `SaveCatalogCache(...)` 兩行移進：

```cpp
if (result_applied && g_refresh->GenerationComplete(result->generation)) {
    // NR-022: the launch-failure refresh gate releases for the next failure.
    g_launch_failure_refresh.OnRefreshComplete();
    // NR-073: the merged snapshot only changes when the whole generation has
    // reported; refreshing the panel and writing catalog.cache for the first
    // 1..n-1 results would reset the selection and re-persist data the UI
    // thread already holds, every cycle (design-spec §FR-008).
    RefreshPanelSnapshot();
    nimblerun::SaveCatalogCache(nimblerun::DefaultSettingsDir(),
                                g_refresh->Snapshot());
}
InvalidateRect(window, nullptr, FALSE);
```

不新增 helper、不重排其他分支。

### 測試

`catalog_refresh_test` 既有案例應維持全綠（coordinator 語意未動）。本 item 不加新測試
執行檔；由 Agent checks 的 sanity grep＋`ctest` 全綠＋手動驗收覆蓋。

### 更新 spec？

不需。§FR-008 描述的行為層級未動——本次讓 host 的呼叫時機符合既有規格。

## How this stays maintainable

「snapshot 只在整代完成時變動」是 `CatalogRefreshCoordinator` 的核心不變式；host 的
刷新與落盤現在與這個不變式同步。日後新增來源只要 coordinator 契約不變，host 行為
自動正確。

## Non-goals

- **不改 coordinator（`catalog_refresh` 一字不動）**——merge／failure 語意已對。
- **不加「snapshot 深比較」或變更追蹤。**
- **不為此改動加測試 seam。**
- **不觸碰 `kWatchChangedMessage`／`WM_TIMER` 分支的 rebuild 觸發邏輯。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項）。
2. sanity grep：`RefreshPanelSnapshot(` 與 `SaveCatalogCache(` 在
   `kRebuildDoneMessage` 處理器內**只**出現於 `GenerationComplete` 區塊之中。

Manual：

3. 三來源 rebuild 進行中（例如 `Ctrl+R`）：面板可見時選取／捲動位置不被每份來源結果
   重置，只在一輪完成後更新一次。
4. rebuild 完成後 `catalog.cache` 每輪只重寫一次（以 `%LOCALAPPDATA%\NimbleRun\`
   檔案最後寫入時間驗證）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# RefreshPanelSnapshot 在 kRebuildDoneMessage 處理器內只在 GenerationComplete 區塊：
# （讀 main.cpp:2311-2347，確認兩次呼叫都在 if (result_applied && ...) 內）
Select-String -Path src/app_host/main.cpp -Pattern "RefreshPanelSnapshot\(\)"
# expect: 呼叫點中屬於 kRebuildDoneMessage 的那一個位於 GenerationComplete 區塊

# 改動範圍：
git diff --name-only
# expect: 只有 src/app_host/main.cpp
```

## 交接區

（實作者填寫：搬移後的位置與縮排、`ctest` 結果、sanity greps、手動驗收 3/4 的實際
觀察、偏差、未完成事項。）

實作（2026-08-08）：

- **搬移**：`RefreshPanelSnapshot()` 與 `SaveCatalogCache(...)` 兩行移進
  `if (result_applied && g_refresh->GenerationComplete(result->generation)) { ... }`
  區塊（`OnRefreshComplete()` 之後、`InvalidateRect` 留在外），NR-073 註解六行照
  item 正文，縮排與區塊一致。
- **`ctest`**：Release build 無新增警告；`ctest` 23/23 全綠（coordinator 一字未動）。
- **sanity greps**：`RefreshPanelSnapshot(` 的 6 個呼叫點中，屬於 `kRebuildDoneMessage`
  的唯一呼叫位於 `GenerationComplete` 區塊（另 5 處為 ShowPanel／設定套用／啟動等
  合法獨立呼叫端）；`SaveCatalogCache(` 全 repo 僅 1 處、位於該區塊；`git diff
  --name-only`＝只有 main.cpp。
- **手動驗收**：三來源 rebuild 進行中的選取／捲動重置與 `catalog.cache` 每輪重寫
  次數為視覺／檔案時間戳驗證，本工作區未實跑。
- **偏差**：無。未完成事項：無。
