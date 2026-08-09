# NR-118 — Watcher/debounce 的部分 rebuild 不得在冷啟動時取代進行中的完整 rebuild

Phase 3 · Catalog refresh supersede · Depends on: NR-065, NR-081, NR-116

- Source: `docs/design-spec.md` §FR-008、§NFR-003、§10.2
- Origin: 2026-08-09 第十二次 fresh post-implementation audit；沿
  `kWatchChangedMessage` → `WM_TIMER` → `StartRebuild` → `BeginGeneration` 追蹤
- Priority: IMPORTANT（watcher 事件在冷啟動首輪完整 rebuild 期間觸發時，會被部分 rebuild 取代；
  從未被列舉的來源只剩 unverified cache 行或直接消失＋usage 資料損失，且無後續完整 rebuild 保證）

## Why

`StartRebuild`（`src/app_host/main.cpp:1600-1611`）一律先 `JoinRebuildThreads()`（取消並 join 在途
generation）再 `BeginGeneration(sources)`——每個新的 `StartRebuild` 都會**取代**目前 running 的
generation。NR-081 已對 ShowPanel on-demand 路徑加了 `ShouldRefreshAppsFolder` 守門（`main.cpp:2404-2406`），
但 watcher 觸發的兩個入口沒有：

- `WM_TIMER`（`:3034-3041`，500 ms debounce）對 `DueSources(now)` 直接 `StartRebuild(window, due)`；
- `kWatchChangedMessage` 的 full-rescan 分支（`:2950-2955`）對 `DueSources(now)` 直接 `StartRebuild`。

watcher 在首次完整 rebuild 之前就啟動（`StartWatchers()` `:3694` → `StartRebuild(window, all)` `:3775`）。
若首輪完整 rebuild（StartMenu＋AppsFolder＋UserFolder）進行期間，任何監看 root 有檔案事件
（Start Menu 被 installer/同步工具觸碰、自訂資料夾被寫入、AV 觸碰 .lnk 等），debounce 到期時
`WM_TIMER` 會以 `{該來源}` 取代在途的完整 rebuild：

- **有 valid cache**（NR-116 已 seed `source_entries_`）：被取代的 StartMenu／AppsFolder 保留 seed 的
  unverified cache 行——snapshot 不縮水，但那些 App **顯示卻不能啟動**（NR-022「invalid entry」對話框），
  且**沒有機制再排一次完整 rebuild**（Ctrl+R 手動、launch-failure 自動 refresh 只在點擊失敗時觸發、
  ShowPanel on-demand 只重列 AppsFolder、watcher 只重掃有事件的來源）。
- **無 cache**（全新安裝）：被取代來源的 `source_entries_` 為空 → `RebuildMerged` 丟棄其行 →
  `RefreshPanelSnapshot` 對縮水 snapshot 跑 usage Reconcile 並 Save → **該來源 usage.tsv 紀錄被刪除**
  ＋`SaveCatalogCache` 寫入縮水版。

這違反 §FR-008「背景完整建立一次最新 Catalog」與「單一來源失敗時保留該來源舊結果」：完整 rebuild
被部分 cycle 取代後，其他來源從未被目前執行期間成功列舉。NR-065 的 pending 機制只保證「事件不遺失」
（會觸發一次後續 rebuild），但它本身**允許取代**；NR-081 只守 ShowPanel、NR-116 只修 failure 保留，
兩者都未覆蓋 watcher/timer 入口。

## Decisions already made — do not reopen

1. 保留 NR-065 的 pending／generation-event-snapshot 語意：在途 scan 期間到達的事件不得遺失，且
   在目前 generation 完成後應觸發一次「更新的 rebuild」。本 item 只改變「何時開始 rebuild」，不改
   coordinator 的 supersede 機制本身。
2. 沿用 NR-081 的守門形狀：守門放 coordinator（純值、可測），host 呼叫它；不在每個 caller 複製
   `IsRebuildInProgress()` 判斷。
3. 進行中 generation 期間 watcher 工作**不得取代它**：改以既有 500 ms debounce timer 延後服務
   （rebuild 完成後的下一個 timer tick 開始 pending rebuild）。延後是 event-driven 且有界
   （rebuild 本身有界），不是 polling、不是固定 sub-60s idle timer（§FR-008 已授權 500 ms debounce）。
4. 完整 rebuild 的 caller（Ctrl+R、launch-failure、settings apply、首輪啟動）維持可取代——它們提供
   全來源結果，取代是安全的，不在本 item 範圍。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 啟動時先載入有效的 Catalog cache，立即提供舊結果；再背景完整建立一次最新 Catalog。

> 收到密集事件時 debounce 500 ms，合併成一次受影響來源的重掃。

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-002：

> 主執行緒阻塞於 `GetMessage`／`MsgWaitForMultipleObjectsEx`，沒有工作時不使用 busy loop。

> 禁止固定小於 60 秒的 timer。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `kWatchChangedMessage`（`:2944-2961`，full-rescan 分支 `:2950-2955`）、
  `WM_TIMER`（`:3034-3044`）、`StartRebuild`（`:1600-1611`，取代語意）、`ScheduleDebouncedRebuild`
  （`:1772-1777`）、ShowPanel on-demand（`:2400-2407`，NR-081 守門先例）、watcher 啟動（`:3694`）與
  首輪 rebuild（`:3775`）。
- `src/catalog/catalog_refresh.{h,cpp}` — `IsRebuildInProgress`（`:82-84`）、`HasDueRebuild`、
  `DueSources`、`BeginGeneration`（supersede）、NR-065 pending 語意。
- `tests/unit/catalog_refresh_test.cpp` — 新 case 的家；沿用 NR-065/100/106/115/116 的既有形狀。
- `docs/work-items/NR-065-inflight-events-not-dropped.md`、NR-081、NR-116 — 保留既有 pending／supersede
  guard／cold-start retention 決策；不要編輯已完成 item 文件。

## Scope

1. `CatalogRefreshCoordinator` 新增純值守門（例）：
   ```cpp
   // NR-118: true when a rebuild should start now -- at least one source is due
   // and no generation is running. Starting a partial rebuild while one runs
   // would supersede it (BeginGeneration drops the in-flight generation), which
   // at cold start leaves never-enumerated sources with only unverified cache
   // rows or nothing at all (design-spec §FR-008). Callers that always rebuild
   // every source (Ctrl+R, launch-failure, settings apply, startup) may bypass
   // this and still supersede.
   bool ShouldStartRebuild(std::int64_t now_ms) const;
   ```
   實作：`return !IsRebuildInProgress() && HasDueRebuild(now_ms);`
2. `WM_TIMER`（`:3036-3042`）：改用 `ShouldStartRebuild(now)`；`due` 非空但 `ShouldStartRebuild`
   false（即 rebuild 在途）時，重新 `SetTimer(window, kRebuildTimerId, 500, nullptr)` 延後——目前
   generation 完成後的下一個 tick 再服務 pending。`due` 為空時不 re-arm。
3. `kWatchChangedMessage` full-rescan 分支（`:2950-2955`）：改用 `ShouldStartRebuild(now)`；在途時以
   `ScheduleDebouncedRebuild(window)` 延後（full-rescan marker 依既有 `kNever` 語意恆為 due，timer tick
   會接住）。非在途時維持立即 `StartRebuild`。
4. 新增 focused coordinator test：`BeginGeneration` 進行中（有一來源 pending）時 `ShouldStartRebuild`
   false；該 generation 完成後同 pending → true；無 pending → false。並證明「不呼叫 BeginGeneration」
   的語意：在途 generation 完成後 snapshot 仍含全部來源的 fresh entries（不被部分 cycle 取代）。

## Non-goals

- 不改 `StartRebuild`／`BeginGeneration` 的 supersede 機制、不改 NR-065 pending 語意、不改 catalog
  cache schema、不改 watcher 本體。
- 不新增 polling、固定 idle timer、thread、retry loop 或第二套 rebuild worker。
- 不重開 NR-081（on-demand guard）或 NR-116（failure retention）的已完成決策；只補 watcher/timer 入口。

## Acceptance

1. 冷啟動首輪完整 rebuild 進行期間，watcher 事件不再以部分 cycle 取代它：完整 rebuild 完成、全來源
   fresh verified；pending 事件在完成後以一次更新的 rebuild 服務（NR-065 語意保留）。
2. 無 cache 冷啟動＋watcher 事件：snapshot 不縮水、usage 不因該來源被丟棄而刪除、cache 不被縮水版覆寫。
3. 非冷啟動（來源已有 verified entries）：進行中 generation 不被取代；事件不遺失，完成後有後續 rebuild。
4. Release build 無新增 warning；focused coordinator test、完整 CTest 與既有 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ShouldStartRebuild|IsRebuildInProgress|DueSources|ScheduleDebouncedRebuild|StartRebuild" src tests
git diff --name-only
# expect: coordinator 守門＋main.cpp 兩入口＋focused tests；不改 settings、pins、usage schema。
```

## Handoff

實作者需記錄守門形狀、WM_TIMER 延後與 re-arm 行為、full-rescan 延後行為、冷啟動 fixture、
pending 事件在完成後被服務的證據、non-cold-start 回歸、build／CTest 與未涵蓋的 OS-only path。

實作（2026-08-09，single clean worker）：

- **守門形狀**：`CatalogRefreshCoordinator` 新增公開 `bool ShouldStartRebuild(std::int64_t now_ms) const`
  （`catalog_refresh.h` 於 `IsRebuildInProgress` 旁、`catalog_refresh.cpp` one-line：
  `return !IsRebuildInProgress() && HasDueRebuild(now_ms);`）。純值、可測，沿用 NR-081 的 coordinator-guard 形狀。
- **WM_TIMER**（`main.cpp:3042-3057`）：`KillTimer` 後取 `now`，`ShouldStartRebuild(now)` true →
  `StartRebuild(DueSources(now))`；false 且 `DueSources(now)` 非空（rebuild 在途、有 pending）→
  `SetTimer(window, kRebuildTimerId, 500, nullptr)` re-arm（目前 generation 完成後下一個 tick 服務
  pending）；`due` 為空不 re-arm。re-arm 有界：只在 rebuild 在途且 pending 存在時重複，rebuild 完成即終止。
- **full-rescan 分支**（`main.cpp:2950-2964`）：`MarkSourceFullRescan` 後 `ShouldStartRebuild(now)` true →
  立即 `StartRebuild(due)`（原行為）；false 且 due 非空 → `ScheduleDebouncedRebuild(window)` 延後
  （`kNever` marker 恆為 due，下一個 tick 接住）。normal-event 分支未改。
- **冷啟動 fixture**：`catalog_refresh_test` 新增 `TestShouldStartRebuildDefersDuringInFlightGeneration`——
  無 pending→false；pending＋無 running→true；三來源 generation 在途且另一來源 due→false（不得取代）；
  generation 完成→true（pending 在完成後可被服務）；再開始→false；該 cycle 完成清除 pending→false。
- **pending 服務證據**：NR-065 的 `generation_event_snapshot_` 使在途事件在目前 generation 完成後仍 pending
  （`ApplySourceResult` 時間戳比對不變），re-arm 的 timer 在下一個 tick 以 `ShouldStartRebuild` true 啟動
  該來源的更新 rebuild；完整 rebuild 的 caller（Ctrl+R `:2940`、launch-failure `:1074`、settings `:3088`、
  首輪 `:3788`）未改、仍可取代。
- **build／CTest**：Release x64（LLVM-MinGW＋Ninja）無新增 warning；focused 2/2 綠（catalog_refresh、
  lifecycle）；完整 CTest 26/26 綠。
- **未涵蓋**：full-rescan 延後依賴「`kNever` 恆為 due」不變式（既有 `MarkSourceFullRescan`／`DueSources`
  釘住）；re-arm 的 500 ms timer 僅在 rebuild 在途時存在，非 idle 固定 timer。commit：`<controller fills after commit>`