# NR-011 — Catalog refresh and immutable snapshots

- Status: `done`
- Phase: 2
- Depends on: NR-005、NR-006、NR-007、NR-019
- Source: `docs/design-spec.md` §FR-008、§NFR-002、§NFR-003、AC-007、AC-012、AC-013

## Goal

讓 Start Menu 與使用者資料夾變更、設定變更及手動 refresh 能在不阻塞列表的情況下更新 Catalog，並以完整 snapshot 一次替換。

## Scope

- Start Menu directory notification 與 500 ms debounce。
- 已設定 user-folder directory notification 與 500 ms debounce；每個 root 依 recursive flag 設定 `bWatchSubtree`，不在面板每次顯示時完整重掃。
- notification 只要求檔名／目錄名／最後寫入等必要變更；副檔名在 worker 過濾，buffer overflow／`ERROR_NOTIFY_ENUM_DIR` 時退回該來源完整重掃。
- AppsFolder on-demand refresh，不做高頻 polling。
- 啟動先使用有效 cache，再背景完整建立一次最新 Catalog。
- `Ctrl+R` 強制完整重建；成功 launch 不觸發 refresh。
- generation／cancellation，舊工作不得覆蓋新結果。
- 重建期間保留舊 snapshot，成功後 atomic swap。

## Non-goals

- 不重做 catalog enumeration rules。
- 不建立常駐 thread pool 或固定小於 60 秒的 timer。

## Acceptance

- 密集檔案事件只觸發一次合併 refresh。
- refresh failure 保留可用舊 snapshot。
- 面板顯示期間不被 scan 阻塞，且單一來源失敗不清空其他來源。
- cache 有效且沒有來源變更時，反覆顯示面板或 launch 不會觸發無條件完整 scan。
- 舊 generation 完成後不能覆蓋較新的結果。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R catalog_refresh --output-on-failure
```

使用 fixture event stream 與 deterministic generation assertions；不要求人工修改真實 Start Menu。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-008、§NFR-002、§NFR-003、AC-007、AC-012、AC-013、`docs/work-items.md`、本文件；trace `src/catalog/`（start_menu/appsfolder/user_folder enumeration、`dedup.h`）、`src/settings/settings_store.h`、`src/app_host/main.cpp`（NR-010 啟動建 snapshot、tray `kRefreshMessage` 目前 no-op）。實作 Catalog refresh：Start Menu 與已設定 user-folder 的 `ReadDirectoryChangesW` 非同步監看（user-folder 依 recursive flag 設 `bWatchSubtree`）+ 500 ms debounce 合併重掃；notification 只要求必要變更、副檔名在 worker 過濾；buffer overflow／`ERROR_NOTIFY_ENUM_DIR` 退回該來源完整重掃；AppsFolder on-demand（面板叫出且距上次成功 >10 分鐘才背景重列舉，不做高頻 polling）；啟動先載入有效 cache 再背景完整重建；`Ctrl+R` 強制完整重建、成功 launch 不觸發 refresh；generation／cancellation，舊工作不得覆蓋新結果；重建期間保留舊 snapshot、成功後 atomic swap；refresh failure 保留可用舊 snapshot、面板顯示期間不被 scan 阻塞、單一來源失敗不清空其他來源。以 fixture event stream 與 deterministic generation assertions 驗證；可建立純 values refresh coordinator（debounce state、generation、dirty flags）供測試。不重做 enumeration rules、不建常駐 thread pool 或小於 60 秒 timer。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: 主 Agent 實作（兩次 subagent 皆未產出變更）
- 修改檔案：`src/catalog/catalog_refresh.h`＋`catalog_refresh.cpp`（新增，純值 refresh coordinator）、`src/catalog/catalog_cache.h`＋`catalog_cache.cpp`（新增，`catalog.cache` schema=1）、`src/app_host/catalog_watcher.h`＋`catalog_watcher.cpp`（新增，`ReadDirectoryChangesW` 每 root 一背景 thread）、`src/app_host/panel_model.h/.cpp`（catalog 改 pointer＋`SetCatalog`/`SetRecent`）、`src/app_host/main.cpp`（啟動改 cache＋背景 rebuild、watcher message、`WM_TIMER` debounce、Ctrl+R、tray Refresh、settings 套用後重建、AppsFolder 10 分鐘 on-demand）、`tests/unit/catalog_refresh_test.cpp`（新增）、`CMakeLists.txt`/`tests/CMakeLists.txt`。
- 設計（design-spec §FR-008）：`CatalogRefreshCoordinator` 純值管理 per-source dirty/debounce/generation/snapshot：`NotifySourceEvent`＋500ms debounce 合併、`MarkSourceFullRescan`（buffer overflow／`ERROR_NOTIFY_ENUM_DIR` 立即到期）、`BeginGeneration(sources)` 記錄 generation 與待收來源、`ApplySourceResult/Failure` 只在該 generation 所有來源都回報後才 atomic 重建 merged（不顯示半成品）；stale generation 被忽略；單一來源失敗保留舊 entries、其他來源新結果照常套用。`catalog.cache` 保存 merged entries（版本化、tmp＋atomic replace、corrupt 改名保留、較新 schema 原檔不動、載入後跑 dedup）。`CatalogWatcher` 每個 root 一背景 thread 以 `ReadDirectoryChangesW` 監看（user-folder 依 recursive flag 設 `bWatchSubtree`），overflow 以 full-rescan marker 回報；`CancelIoEx` 支援乾淨關閉。main.cpp：啟動先 `LoadCatalogCache` 立即顯示、再背景 full rebuild；watcher 事件走 debounce `SetTimer(500)`；`Ctrl+R`／tray Refresh 強制全來源重建；成功 launch 不觸發；AppsFolder 於面板顯示且距上次成功 >10 分鐘才背景重列舉；設定套用後重啟 watcher＋重建。
- Main-agent 確認：範圍僅 NR-011，未重做 enumeration rules、未建 thread pool 或 <60s timer；NR-002/003/005/006/007/008/009/010/012/013/015 全數回歸綠。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build -R catalog_refresh --output-on-failure` → exit 0，`nimblerun_catalog_refresh_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，15/15 passed（含 lifecycle_check）
  - coordinator 測試涵蓋：500ms debounce 合併、overflow 立即 full rescan、stale generation 不覆寫新結果、失敗保留舊 snapshot、單一來源失敗隔離、AppsFolder 10 分鐘規則、無部分 snapshot（generation 完整才 swap）、cache round-trip／corrupt→rebuild／newer schema 原檔不動。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_catalog_refresh_test.exe`。
