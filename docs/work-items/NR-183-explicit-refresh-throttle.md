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

### Explicit 分支的最終形狀（`rebuild_pipeline.cpp:68-92`）

```cpp
if (reason == RebuildReason::Explicit) {
    bool can_start = true;
    for (const CatalogSource source : sources) {
        const auto it = last_rebuild_start_ms_.find(source);
        const std::int64_t last = it == last_rebuild_start_ms_.end()
                                      ? kNoRebuildStart
                                      : it->second;
        can_start &= AcceptRebuildStart(last, now);
    }
    if (can_start) {
        for (const CatalogSource source : sources) {
            last_rebuild_start_ms_[source] = now;
        }
        Start(std::move(sources));
        return;
    }
    for (const CatalogSource source : sources) {
        refresh_.NotifySourceEvent(source, now);
    }
}
```

- 三來源全部通過 `AcceptRebuildStart`（1 s per-source gate，NR-147 語意不變）才直接
  `Start(std::move(sources))` 並對全部來源蓋章；任一來源被節流就整筆合併。
- 節流判定與 FullRescan／OnDebounceTimer 共用同一個 `AcceptRebuildStart`（`kRebuildStartMinIntervalMs`
  的 shared single read，grep 可見 Explicit :81 與 FullRescan :99 都呼叫）。
- `can_start` 為真時的行為與改動前逐字等價（Start 全部來源＋return），單次 Ctrl+R 的
  產品語意未動；唯一的附帶效果是記錄 `last_rebuild_start_ms_`（改動前 Explicit 不更新它）。

### 被節流時 intent 如何保留（NotifySourceEvent 路徑）

- 任一來源被節流 → 對**全部**來源 `NotifySourceEvent(source, now)`，再落入既有共享尾部
  （`ShouldStartRebuild(now)` → `Start(DueSources(now))`，否則 `schedule_debounce_()`）。
- 因此 pending 的全來源完整 rebuild 一定發生：事件先過 500 ms debounce，再由
  `OnDebounceTimer` 逐來源過 gate（NR-147）──gate 未開就 re-arm，開了就
  `Start(DueSources)` 啟動全來源重建。「按了沒反應」不會發生，只是合併進下一次受節流
  的 rebuild。密集 Ctrl+R（≤1 s）在乾淨狀態下實測軌跡：第一下立即 Start；後續全部合併，
  ~1 s 後經 debounce＋gate 啟動一次完整 rebuild（測試見下）。
- 本改動不新增 timer／queue；不改 coordinator 的 `ShouldStartRebuild`／`DueSources` 語意
  （NR-118 守門保留）；`kRefreshMessage` 的 sender 不驗證（§NFR-004 不試圖消除偽造面）。
- 既有五個 `StartRebuild` call site（Ctrl+R、tray Refresh、空白處右鍵、launch-failure
  refresh、啟動重建）全部經 `Request(Explicit)`，一起被節流──這是 DoS 面所需的覆蓋，
  而啟動／設定套用後緊接的 refresh 若在 1 s 內也只會合併、不會遺失。

### 單次 Refresh 行為不變的確認

- 首按（或距上次任一來源 rebuild ≥1 s）：三來源 gate 全開 → 立即 `Start` 全部來源，
  與改動前一致。測試 `TestExplicitRefreshThrottled` 第一步即驗證「單次 Explicit →
  三來源各自列舉一次」。

### 測試案例（`tests/unit/rebuild_pipeline_test.cpp`）

- `TestExplicitRefreshThrottled`（新增，沿用既有 shape）：單次 Explicit 全來源立即重建
  （enumerations == 3）；緊接第二次 Explicit 在 50 ms 內被節流（仍為 3、debounce 被 arm）；
  排空第一代後經兩次 600 ms debounce tick，gate 開啟後 merged 重建發生（enumerations == 6、
  3 筆新結果）──證明密集 refresh 合併成一次受節流 rebuild 且 intent 不丟。
- `TestStartBoundedSupersedeAndFreshCancelFlag`（NR-182 測試改動）：第二次 Explicit 前加
  `Sleep(1100)` 等 gate 過期──NR-183 節流使原本背靠背的兩次 Explicit 會被合併，
  要維持 NR-182 的 supersede 場景（stuck worker 換代、bounded join、fresh cancel flag）
  必須先等 gate 開。`TestShutdownBounded` 等其餘案例未動。

### build／CTest 證據

- Release（LLVM-MinGW＋Ninja）：configure 無誤；build 無 error／新增 warning（
  `rebuild_pipeline.cpp`、`rebuild_pipeline_test.cpp` 重新編譯通過）。
- 完整 CTest 32/32 Passed（含 `nimblerun_rebuild_pipeline_test` 與
  `nimblerun_lifecycle_check`）。一次全量 run 中 lifecycle 曾逾時（panel-show poll 5 s），
  單獨重跑即過（10.6 s、18.2 s）──判定為該次全量 run 的機器負載所致，非本改動造成
  （panel-show 路徑不經 `Request`）。NR-182 測試在加 `Sleep(1100)` 後全綠。
- Agent checks 的 `rg` grep 命中：`RebuildReason::Explicit` :68、`AcceptRebuildStart` :19/81/99/315。
- `git status` 於提交後僅 `docs/work-items.md` 與 `docs/work-items/NR-183-*.md` 有修改。
