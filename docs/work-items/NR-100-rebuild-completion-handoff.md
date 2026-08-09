# NR-100 — Rebuild result delivery 失敗時，不得讓 generation 永久卡住

Phase 3 · Depends on: NR-063, NR-073, NR-077

- Source: `docs/design-spec.md` §FR-008（完整 snapshot／source failure）／§NFR-003
- Origin: 2026-08-09 全 repo 稽核（`StartRebuild` 的 token registry→PostMessage→coordinator trace）
- Priority: HIGH（message queue 滿或 window 狀態變化時，generation 會永遠 in progress）

## Why

`StartRebuild` worker 已依 NR-063/NR-077 在 `PostMessageW(window, kRebuildDoneMessage, ...)`
失敗時移除 handoff registry entry，避免 leak；但這條 failure path 沒有通知 UI 或
`CatalogRefreshCoordinator` 該 source 已失敗。結果是該 generation 的 `received_` 永遠
少一項，`IsRebuildInProgress()` 一直為 true，整代 snapshot 不再完成，且 NR-081 的
AppsFolder on-demand guard 可能持續阻擋後續 refresh。

這是 NR-063 已修好的「不要洩漏 payload」之外的剩餘語意問題：payload cleanup 成功，
但 source completion 沒有發生。

## Decisions already made — do not reopen

1. 保留 NR-077 token registry 與「未知 lParam 不解參考」安全邊界。
2. `PostMessageW` 失敗時，每個 source 必須仍以成功或 `ApplySourceFailure` 恰好完成一次；
   不可直接呼叫 UI-owned coordinator 的未同步方法，也不可讓 worker 直接修改 snapshot。
3. 走最小的 event-driven failure handoff（或在 UI 已擁有的 generation state 上建立等價
   的失敗完成路徑）；不重試成為 1 Hz loop。
4. generation 完成後仍只在既有 `RefreshPanelSnapshot`／cache write choke point 刷新一次。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-003：

> Catalog 重建可取消或具世代編號；舊工作完成後不得覆蓋較新的結果。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `StartRebuild`、`g_rebuild_handoffs`、`kRebuildDoneMessage`、
  `JoinRebuildThreads`。
- `src/catalog/catalog_refresh.{h,cpp}` — `ApplySourceFailure`、generation completion。
- `tests/unit/catalog_refresh_test.cpp` — existing failure isolation and generation tests。
- `tests/integration/lifecycle_check.ps1` — window/message lifecycle。
- `docs/work-items/NR-063-source-failure-reaches-refresh.md`、NR-077 — existing cleanup and
  handoff decisions; do not duplicate them。

## Scope

1. 為 rebuild completion message 建立可測的 post-failure path；source completion 仍在 UI
   thread 的 coordinator boundary 發生，並保留舊 source entries。
2. 對成功 post、post failure、stale generation、window teardown 四條路徑做 ownership
   review：registry entry、queued message、result payload 各只清理一次。
3. 新增一個 focused self-check／test，模擬 delivery failure 後證明 generation 可完成，
   不會卡在 `IsRebuildInProgress()`。

## Non-goals

- 不改 NR-077 的 token validation，不把 raw pointer 放回 Windows message。
- 不新增無界 retry、busy wait、polling 或第二個 rebuild worker。
- 不改 source enumeration、dedup、staleness policy 或 cache format。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. delivery failure test 證明 source 走 `ApplySourceFailure`、保留舊資料、generation 最終
   完成；payload 不洩漏。
3. 既有 stale-generation、single-source-failure、lifecycle tests 原樣通過。
4. `g_rebuild_handoffs` 在每個 post outcome 都有唯一 owner，WM_DESTROY drain 不 double free。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure
```

```powershell
Select-String -Path src/app_host/main.cpp -Pattern 'kRebuildDoneMessage|ApplySourceFailure|PostMessageW|g_rebuild_handoffs'
git diff --name-only
# expect: completion failure 只改 host／coordinator seam／focused tests。
```

## 交接區

（實作者填寫：delivery failure 注入點、UI completion path、ownership table、generation／
lifecycle 結果、建置／CTest、偏差與未完成事項。）

### Delivery failure 注入點

`src/app_host/main.cpp` 的 `StartRebuild` worker lambda，`PostMessageW(window,
kRebuildDoneMessage, ...)` 回傳失敗的 `!PostMessageW(...)` branch（原 NR-063 leak guard 內）：

1. 先依既有邏輯 erase `g_rebuild_handoffs[token]`（刪除 payload，不洩漏）。
2. 在 `g_delivery_failure_mutex` 下把 `(generation, source)` 記入新的檔案層級
   `g_rebuild_delivery_failures`（`std::vector<std::pair<std::uint64_t,
   nimblerun::CatalogSource>>`，定義在 `g_rebuild_handoffs` 旁）。
3. best-effort `PostMessageW(window, kRebuildDeliveryFailedMessage, 0, 0)`（純 wake-up，
   回傳值忽略；queue 滿時連這個也失敗也無妨，entries 仍在 vector 內等下次 drain）。

記錄的語意：「此 source 已完成但結果無法送達，UI 必須以 failure 完成它」。

### UI completion path

- 新常數 `kRebuildDeliveryFailedMessage = WM_APP + 10`。
- 新 helper `bool DrainRebuildDeliveryFailures()`：在 mutex 下 `swap` 清空
  `g_rebuild_delivery_failures`，對每個 `(generation, source)` 呼叫
  `g_refresh->ApplySourceFailure(generation, source)`（stale generation 已被
  coordinator 忽略）；至少套用一筆且之後 `!g_refresh->IsRebuildInProgress()` 時回傳 true。
- 新 helper `void OnGenerationCompleteRefresh()`：抽出原 `kRebuildDoneMessage` case 尾端的
  completion block，只做三件事——`g_launch_failure_refresh.OnRefreshComplete()`、
  `RefreshPanelSnapshot()`、`if (!g_catalog_cache_disable_writes) SaveCatalogCache(...)`；
  不含 `InvalidateRect`。
- `kRebuildDoneMessage` case：既有 completion block 改成呼叫 `OnGenerationCompleteRefresh()`，
  在 final `InvalidateRect` 前多加 `if (DrainRebuildDeliveryFailures()) {
  OnGenerationCompleteRefresh(); }`（覆蓋「另一個 sibling 的 post 失敗、這個成功」的常見情形）。
- 新 `case kRebuildDeliveryFailedMessage:`：`if (DrainRebuildDeliveryFailures()) {
  OnGenerationCompleteRefresh(); } InvalidateRect(window, nullptr, FALSE); return 0;`。

`InvalidateRect` 行為完全不變：每個 handled message 恰好一次。沒有新增 timer／retry／
polling／第二 worker；worker 不呼叫任何 UI-owned coordinator 方法。

### Ownership table（四種 outcome）

| Outcome | registry entry（NR-077） | queued message | result payload | generation completion |
| --- | --- | --- | --- | --- |
| 成功 post | `kRebuildDoneMessage` case 內 erase（唯一 owner） | 該 message 處理時移出 | 隨 `unique_ptr<RebuildResult>` 在 case 內釋放 | case 內 `ApplySourceResult`，整代完成時 `OnGenerationCompleteRefresh` |
| post 失敗 | worker erase（NR-063 leak guard，唯一 owner） | 無 | erase 即刪除，不洩漏 | worker 記入 `g_rebuild_delivery_failures`，UI drain 以 `ApplySourceFailure` 完成 |
| stale generation | 同上（token 屬性的 stale 由 coordinator 忽略） | — | 同上 | `ApplySourceFailure/Result` 對舊 generation 回 false，drain 的 stale entries 被 drop 無害 |
| window teardown（WM_DESTROY） | drain 後 `g_rebuild_handoffs.clear()`（既有 NR-077，未改） | `PeekMessageW` 排空（既有） | 隨 registry clear 釋放 | 關閉中，`JoinRebuildThreads` 後不再有新 post／新 delivery-failure entry |

`g_rebuild_delivery_failures` 由 worker 在 `g_delivery_failure_mutex` 下 append、UI 在
同一 mutex 下 swap-out；WM_DESTROY 不特別清（依 ticket 不改 WM_DESTROY drain/clear），
worker join 後不會再有新 entry，殘留資料隨 process 結束釋放。

### 新測試

`tests/unit/catalog_refresh_test.cpp` 新增 `TestDeliveryFailureCompletesGeneration`（已在
`wmain` 註冊）。模擬 UI drain 在純 coordinator 邊界的行為：`BeginGeneration({StartMenu,
AppsFolder, UserFolder})` → `ApplySourceResult(StartMenu)`（assert `IsRebuildInProgress()`）
→ `ApplySourceFailure(AppsFolder)`（assert 仍 in progress）→
`ApplySourceFailure(UserFolder)` → assert `GenerationComplete(gen)`、`!IsRebuildInProgress()`、
snapshot 只含已送達的 StartMenu entry（舊資料保留、無 partial wipe）。不需要模擬
`PostMessageW`。

### Generation / lifecycle 結果

- `nimblerun_catalog_refresh_test`（含新測試）：Passed。
- `nimblerun_lifecycle_check`（window/message lifecycle，WM_DESTROY drain 不 double free）：Passed。
- 既有 stale-generation、single-source-failure、AppsFolder on-demand、gate 測試原樣通過。

### 建置 / CTest

- Configure：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake"
  -DCMAKE_BUILD_TYPE=Release` — 成功。
- Build：`cmake --build build` — 成功，**無新增 warning**（只重建 `main.cpp.obj` 與
  `catalog_refresh_test.cpp.obj` 兩個 TU，皆無 warning）。
- `ctest --test-dir build --output-on-failure` — **24/24 全綠**（100% tests passed）。
- `ctest --test-dir build -R "catalog_refresh|lifecycle" --output-on-failure` — 2/2 全綠。

### 偏差

無。所有 Decisions/Scope 照做：token registry / NR-077 邊界未動、coordinator 檔案未動、
WM_DESTROY drain/clear 未動、NR-098 cancellation 未動、未加 timer／retry／polling／第二
worker。`git diff --name-only` 只列出 `src/app_host/main.cpp` 與
`tests/unit/catalog_refresh_test.cpp`。狀態未更動（`docs/work-items.md` 由 main controller
管）。未 commit。

