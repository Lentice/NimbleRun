# NR-183 — Explicit Refresh 繞過重掃節流：Ctrl+R 可被無限重複驅動全來源掃描

Phase 3 · Host lifecycle · Depends on: NR-182（同檔不同函式；先 NR-182 再 NR-183 避免改同一檔的接續衝突）

- Source: `docs/design-spec.md` §NFR-004（現文 `design-spec.md:807`：「只對可被無限重複驅動的重掃描路徑限流（NR-130）」）；`docs/work-items/NR-130-same-user-dos-surface.md`
- Origin: 2026-08-12 第十七次全 repo 稽核（codex 報告 H3）
- Priority: **HIGH**（同 user 程序可偽造 `WM_APP` 訊息驅動無限完整掃描；與 NR-182 疊加成 UI stall）

## Why

`RebuildPipeline::Request()`（`src/app_host/rebuild_pipeline.cpp:62-94`）對 `RebuildReason::Explicit` 直接 `Start(std::move(sources))`，**不經** `AcceptRebuildStart`（1 s per-source 節流，`rebuild_pipeline.h:96`）也不更新 `last_rebuild_start_ms_`。Ctrl+R 與 tray／空白處的「Refresh Apps」都投遞 `kRefreshMessage`；同 user process 也可 `PostMessageW` 偽造該 `WM_APP` 訊息（§NFR-004 承認的模型）。

影響：快速重複 Refresh 可反覆啟動三來源完整掃描；每次 `Start()` 又先 `Shutdown()`（NR-182 修完後為 bounded，但仍是一次完整換代），與 NR-182 疊加成可重複觸發的 UI stall。這與 NR-130 已實作的 FullRescan／watcher 路徑節流形成對比：**同一節流規則只蓋了一半**。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-004（現文 `design-spec.md:807`）：

> 同 user process 可偽造 `WM_APP` 命令訊息，屬 Windows 固有模型（`PostMessage` 無 sender 認證）；偽造退出訊息與偽造 `WM_CLOSE` 同級，NimbleRun 不試圖消除，只對可被無限重複驅動的重掃描路徑限流（NR-130）。

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp` — `Request()`（:62-94）的 Explicit 分支；`AcceptRebuildStart`／`last_rebuild_start_ms_`／`kRebuildStartMinIntervalMs` 的既有 FullRescan 用法（:71-82）。
- `src/app_host/rebuild_pipeline.h` — `RebuildReason` 列舉、`AcceptRebuildStart`、`last_rebuild_start_ms_`。
- `src/app_host/main.cpp` — `kRefreshMessage` 的投遞點（Ctrl+R `:2541` 一帶、tray Refresh `:2595` 一帶、空白處右鍵）。

## Scope

1. `Request()` 的 `RebuildReason::Explicit` 改走既有 per-source 節流：檢查 `AcceptRebuildStart(last, now)`，通過才 `Start()` 並更新 `last_rebuild_start_ms_`；被節流時**不丟棄** intent——沿用 `refresh_.NotifySourceEvent(source, now)`＋既有 `ShouldStartRebuild`／debounce 合併機制（與 FullRescan 分支同形，讓密集 Refresh 合併成一次受節流的 rebuild，而不是多排一次）。
2. 注意 `kRefreshMessage` 是一次性 full-rescan 語意：被節流合併後，pending 的完整 rebuild 必須仍會發生（由 `ShouldStartRebuild` 判定），不得變成「按了沒反應」。
3. 檢查並沿用 NR-130 的實作先例與交接區決策，避免與之牴觸（NR-130 是 FullRescan marker；本 item 補 Explicit 這半邊）。
4. 測試：若 `Request()` 的節流判定可抽純函式（輸入 last/now/reason → 是否 Start），沿用既有 `nimblerun_rebuild_pipeline_test` 的形狀加 focused 案例；否則以 sanity grep＋既有測試覆蓋。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-183 列。

## Non-goals

- 不新增另一套 timer／queue 抽象（既有 500 ms debounce 與 `ShouldStartRebuild` 已夠）。
- 不驗證 `WM_APP` 的 sender（§NFR-004 明確不試圖消除偽造面）。
- 不改 Ctrl+R 的產品語意（按一下必須完整重建）。
- 不動 watcher Change／FullRescan 已 done 的節流（NR-130、NR-147、NR-153）。

## Acceptance

- 快速連續 Refresh（≤1 s 間隔）合併成一次受節流的完整 rebuild，不產生無界重掃；單次 Refresh 行為與現況等價。
- 被節流時 pending 的完整 rebuild 仍會發生（不丟 intent）。
- 節流判定與 FullRescan 分支共用同一常數與同一語意（grep 可見 `kRebuildStartMinIntervalMs` 在兩處都被讀）。
- Release build 無 error／新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "RebuildReason::Explicit|AcceptRebuildStart|kRebuildStartMinIntervalMs" src/app_host/rebuild_pipeline.cpp
```

驗證：build 無 error／新增 warning；CTest 全 Passed；Explicit 分支不再無條件 `Start()`，且被節流時 intent 不丟。

## 交接區

（實作者填寫：Explicit 分支的最終形狀、被節流時 intent 如何保留（NotifySourceEvent 路徑）、單次 Refresh 行為不變的確認、測試案例、build／CTest 證據）
