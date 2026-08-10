# NR-139 — 偽造的 `kRebuildDeliveryFailedMessage`（WM_APP+10）可讓常駐 tray process 當場終止

Phase 3 · Robustness · Depends on: —（無依賴，最先做；這是本批次唯一的 CRITICAL 級 crash）

- Source: `AGENTS.md`（Keep changes scoped… Prefer the smallest working change）、
  `docs/design-spec.md` §11（診斷與可靠度）、NR-077 家族（同 integrity 偽造訊息不得 crash 常駐程式）
- Origin: 2026-08-10 第十四次全 repo 稽核（正確性軸，IMPORTANT）。主 Agent 已逐行追蹤訊息路徑驗證。
- Priority: **IMPORTANT**——偽造訊息即可 `std::terminate`，與 NR-077 修掉的 crash 向量同型
  （NR-077 只堵了 `kRebuildDoneMessage`／`kIconReadyMessage` 的 lParam 解參考，`WM_APP+10`
  是之後才加的可靠度路徑，NR-100）。

## Why

`OnDeliveryFailureMessage`（`src/app_host/rebuild_pipeline.cpp:214-226`）只驗證
`w_param != 0 && l_param <= UserFolder`，就把 `(generation, source)` 直接送進
`refresh_.ApplySourceFailure(generation, source)`。`ApplySourceFailure`
（`src/catalog/catalog_refresh.cpp:133-151`）在 generation 相符後執行
`generation_event_snapshot_.at(source)`（`:143`；`ApplySourceResult` 的 `:123` 同型）。

`generation_event_snapshot_` 只含有 `BeginGeneration`（`:84-101`）時 `active_sources_`
的成員。`.at()` 對「generation 相符但 source 不在 active 集合」的值擲
`std::out_of_range`。WindowProc → message loop 全程無 try/catch（grep 驗證：
`main.cpp` 零 try/catch），例外逃出 `DispatchMessageW` → `std::terminate`，常駐 tray
程序當場死亡。

**觸發情境**：任何同 user 程序（瀏覽器、shell 等）對主視窗
`PostMessageW(hwnd, WM_APP+10, gen, src)`。generation 自 1 起逐次遞增、可暴力探測；
當單來源 rebuild（AppsFolder on-demand 或 watcher 部分重建）進行中，傳入相符
generation + 非 active 的來源值（0/1/2 中任一個不在 active 集合者）即命中。
窗體 class 名與 message 常數全部公開（`main.cpp:65-93`），無秘密可言。

**正常路徑是否也踩得到？** 不會——worker 只為 active sources 啟動
（`Start` 的 `BeginGeneration(sources)` 即 active 集合），`ApplySourceResult` 只由
token registry 路徑到達（`OnResultMessage`，`rebuild_pipeline.cpp:193-212`），stale
generation 在 `ApplySource*` 的第一道 `generation != generation_` 就早退。
`.at()` 只會被偽造訊息命中——但「偽造訊息可殺死常駐程式」正是 NR-077 明文處理的
同級向量，本 item 只是補上漏網的第四條。

## Decisions already made — do not reopen

1. **根因修在共用函式**：兩個 `ApplySource*` 的 `.at()` 改 `.find()`，非成員
   `return false`（與 `generation != generation_` 同語意：不是這個 generation 的來源，
   就是無效）。這同時保護未來的所有呼叫端。`.at()` 在此無任何合法失敗理由。
2. `OnDeliveryFailureMessage` 的成員守門（「source ∈ 目前 active generation」）**列為選修**：
   有 `.find()` 守門後 crash 已封閉，成員守門只是讓「偽造失敗只對 active source 有效」的
   剩餘向量更窄（見 Non-goals）。
3. **不新增測試會紅的既有斷言**：既有 `catalog_refresh_test`／`rebuild_pipeline_test`
   即回歸網；本 item 只加防護與對應的新斷言。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/catalog_refresh.cpp`：`:84-101`（`BeginGeneration` 建 snapshot 集合）、
  `:103-131`（`ApplySourceResult`）、`:133-151`（`ApplySourceFailure`）。
- `src/app_host/rebuild_pipeline.cpp`：`:193-226`（`OnResultMessage` vs `OnDeliveryFailureMessage` 的驗證強度對照）。
- `src/catalog/catalog_refresh.h`（`generation_event_snapshot_` 型別）。
- `tests/unit/catalog_refresh_test.cpp`、`tests/unit/rebuild_pipeline_test.cpp`（既有 generation 語意測試的形狀）。

## Scope

1. `ApplySourceResult` 與 `ApplySourceFailure` 的
   `generation_event_snapshot_.at(source)` 改為 `.find()` + 非成員 `return false`。
   註解說明這是偽造訊息防護（正常路徑保證成員）。
2. `tests/unit/catalog_refresh_test.cpp` 新增**必測案例**：
   - `BeginGeneration({StartMenu})` 後，`ApplySourceFailure(gen, UserFolder)` 回
     `false` 且**不拋例外**；
   - 同 setup 下 `ApplySourceResult(gen, UserFolder, entries, diag)` 回 `false` 不拋；
   - generation 相符且 source 為 active 成員時行為與先前完全一致（成功路徑斷言不變）。
3. 選修：`OnDeliveryFailureMessage` 在送 `ApplySourceFailure` 前檢查
   `refresh_.IsActiveSource(generation, source)`（若加，需在
   `CatalogRefreshCoordinator` 加一個 const accessor，`active_sources_` 已是成員）。

## Non-goals

- **不把失敗訊息改走 token registry**（`RebuildResult{failed=true}` 通道）：偽造
  「active source 失敗」的剩餘窗口（`ApplySourceFailure` 成功 → 該來源缺項 → generation
  提前完成 → 真結果到達後同 generation 重算自癒）是 same-user、窄窗口、自我修復，
  且 same-user attacker 本就能直接改寫 `usage.tsv`／`favorites.txt`，無新增傷害。
  在交接區記錄即可，不另開 item。
- 不改 `QueueFailure`／`failure_event_` 通道、不改 `OnResultMessage`、不改 message 常數。
- 不順手改其他 `.at()`。

## Acceptance

1. grep 確認 `generation_event_snapshot_` 沒有 `.at(` 殘留。
2. 新測試存在並通過：stale/非成員 source 的 `ApplySource*` 回 `false` 不拋。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_refresh|rebuild_pipeline" --output-on-failure
```

```powershell
rg -n "generation_event_snapshot_.*\.at\(" src
# expect: 零命中
```

## Handoff

已完成（commit `NR-139: forged delivery-failure message cannot crash the process`）。

**變更內容**：`src/catalog/catalog_refresh.cpp` 的 `ApplySourceResult`（`:103-139`）與
`ApplySourceFailure`（`:141-165`）在 generation 相符檢查後各加一道 NR-139 守門：
`generation_event_snapshot_.find(source)` 非成員即 `return false`（不觸碰任何狀態，
與 generation mismatch 早退同語意）。兩函式原 `current == generation_event_snapshot_.at(source)`
改用守門迭代器（`snapshot->second`），`.at()` 完全移除，正常路徑（active 成員）行為
逐字不變。守門放在 `ApplySourceResult` 的 diagnostics fold／`source_entries_` 寫入之前，
確保「回 false」=「什麼都沒發生」。

**選修項未做**：`OnDeliveryFailureMessage` 的 `IsActiveSource` 成員守門未加——`.find()`
守門已讓非 active source 的呼叫成為無害 no-op（回 false、零副作用），window proc 再加一層
判斷是純冗餘；item 明言「only if clean」，此處 clean 判定為不需要，故維持最小 diff。

**測試**：`tests/unit/catalog_refresh_test.cpp` 新增兩個必測案例，註冊於既有 list-plus-loop
（置於 `TestFailureWakeupDrainCompletesGeneration` 之後）：
- `TestForgedFailureNonActiveSourceRejected`：`BeginGeneration({StartMenu})` 後
  `ApplySourceFailure(gen, UserFolder)` 回 `false` 不拋（若拋出，測試 process 即終止，
  等同 fail）；
- `TestForgedResultNonActiveSourceRejected`：同 setup 下 `ApplySourceResult(gen, UserFolder,
  entries)` 回 `false` 不拋，且 `Snapshot()` 維持空（無狀態污染）。
成功路徑既有斷言（NR-065／NR-100／NR-106／NR-115 等）全部未動。

**驗證**：Release (llvm-mingw) configure 成功；`cmake --build build` 零新增 warning；
完整 `ctest --test-dir build --output-on-failure` 31/31 通過（數量不變，故
`docs/testing.md` 未動）；focused `ctest -R "catalog_refresh|rebuild_pipeline"` 2/2 通過；
`rg -n "generation_event_snapshot_.*\\.at\\(" src` 零命中。單獨執行新測試：
`build/tests/nimblerun_catalog_refresh_test.exe` 印出
「NR-011/NR-022 catalog refresh check PASSED」。

**交接區（Non-goals 指定記錄，不另開 item）**：偽造「active source 失敗」的剩餘窗口
（`ApplySourceFailure` 對 active source 回 true → 該來源缺項 → generation 提前完成 →
真結果到達後同 generation 重算自癒）是 same-user、窄窗口、自我修復；same-user attacker
本就能直接改寫 `usage.tsv`／`favorites.txt`，無新增傷害。
