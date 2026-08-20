# NR-192 — Catalog rebuild workers run at background priority

Phase 3 · Rebuild pipeline · Depends on: NR-132（done）

- Source: `docs/design-spec.md` §9.2、§FR-008
- Origin: 2026-08-20 使用者回報冷開機 Alt+3 卡在「還在準備中」；`grill-with-docs`／domain-modeling 已確認範圍與機制
- Priority: **MEDIUM**——關閉一個既有、從未實作的 spec 缺口，而非新增行為

## Goal

`RebuildPipeline` 每個來源的列舉 worker thread 進入 `THREAD_MODE_BACKGROUND_BEGIN`，讓 catalog rebuild（尤其是冷開機時的大型 UserFolder 掃描）不與開機期其他程式競爭磁碟／CPU／記憶體優先權。三個來源（StartMenu、AppsFolder、UserFolder）統一套用，不特別處理其中一個。

## 已確認的產品決策

1. **範圍**：套用在 `RebuildPipeline::Start()` 建立的每一條 worker thread（`rebuild_pipeline.cpp:146` 的 `thread_factory_` 呼叫），涵蓋開機、Ctrl+R、tray、設定套用後的所有 rebuild，不特別分流 UserFolder。
2. **機制**：`SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)`，在 worker thread **內部**、`enumerate_source_(...)` 呼叫之前設定；不呼叫 `THREAD_MODE_BACKGROUND_END`（worker 是一次性、跑完即結束，優先權隨 thread 結束回收，不需手動關閉）。選 `THREAD_MODE_BACKGROUND_BEGIN` 而非單純 `THREAD_PRIORITY_LOWEST`：瓶頸是磁碟 I/O，這個模式同時降 CPU、I/O 與記憶體優先權，比純 CPU 優先權更對症。
3. **既有 `kJoinTimeoutMs`（5000ms）不變**：background 優先權可能讓 worker 在系統忙碌時跑得比現在久，增加命中既有 detach-fallback（NR-123／NR-182）的機率，但這條 fallback 本來就是為「worker 可能跑比預期久」設計的既有安全機制，不需要新機制或延長逾時。
4. **失敗容忍**：`SetThreadPriority` 呼叫失敗（回傳 0）**不是**列舉失敗，只記一次診斷或忽略，不得影響 `enumerate_source_` 是否執行或 `RebuildResult::failed`。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.2：

> Icon worker：單一低優先序 worker，依可見項目載入；queue 有上限並可取消過期請求。

`docs/design-spec.md` §FR-008（已有明文要求、從未實作的缺口——本 item 的直接依據）：

> AppsFolder 不做背景輪詢；當面板被叫出且距上次成功列舉超過 10 分鐘時，在背景低優先序重新列舉。

`docs/design-spec.md` §19（重要實作原則摘要，第 4 點）：

> 待機完全事件驅動，不使用高頻 timer 或背景掃描。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`。
- `docs/design-spec.md` §9.2、§9.4、§FR-008。
- `docs/work-items.md` 的使用方式、Agent 交付規則、Item 總覽與「已否決的方向 — 不要重開」。
- `docs/work-items/NR-132-rebuild-pipeline-module.md`、`NR-182-rebuild-start-bounded-join.md`、`NR-123-rebuild-join-bounded-wait.md`；完成 item 文件只讀取，不回頭修改歷史紀錄。
- `src/app_host/rebuild_pipeline.h/.cpp`：`ThreadFactory` 型別、建構子注入點、`Start()` 內 `thread_factory_(...)` 呼叫（`rebuild_pipeline.cpp:118-190`）、`kJoinTimeoutMs`、`Shutdown()`。
- `tests/unit/rebuild_pipeline_test.cpp`：既有 `ThreadFactory` 注入測試方式（決定新測試怎麼驗證優先權被設定，而不需要真的量測系統排程）。

## Scope

1. 在 `RebuildPipeline::Start()` 的 worker lambda（`rebuild_pipeline.cpp:147` 附近，`enumerate_source_` 呼叫之前）加入一次 `SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)`；回傳值只做 best-effort 記錄，不影響列舉流程或 `RebuildResult`。
2. 不修改 `ThreadFactory` 的簽名或注入點；優先權設定是 worker lambda 內部的一行,不是新的 seam。
3. `docs/design-spec.md` §9.2 補一句，把「Scan worker 低優先序」寫成文字，對齊 §FR-008 現有對 AppsFolder 的既有要求（本 item 是把該要求擴大套用到全部三個來源並首次實作）。

## Non-goals

- 不改變 `kJoinTimeoutMs`（5000ms）或 shutdown／detach 邏輯。
- 不新增第二個 ThreadFactory 或優先權可設定的設定項；`THREAD_MODE_BACKGROUND_BEGIN` 是唯一機制，不做成使用者可調參數。
- 不修改 Icon worker（已經是低優先序，§9.2 既有規則,不在本 item 範圍)。
- 不處理 NR-193／NR-194／NR-195（max depth、拿掉開檔 probe、拆 generation）；本 item 與那三項技術上互不依賴，可獨立完成。
- 不新增 timer、輪詢或背景排程機制。

## Acceptance

1. `RebuildPipeline::Start()` 建立的每條 worker thread 在呼叫 `enumerate_source_` 前已設定 `THREAD_MODE_BACKGROUND_BEGIN`；三個來源（StartMenu、AppsFolder、UserFolder）與四個既有呼叫點（開機、Ctrl+R、tray、設定套用)全部套用,不特別分流。
2. `SetThreadPriority` 失敗不影響列舉結果、不觸發 `RebuildResult::failed`、不新增使用者可見錯誤。
3. 既有 `kJoinTimeoutMs`／`Shutdown()`／NR-123／NR-182 的行為完全不變。
4. `docs/design-spec.md` §9.2 反映 Scan worker 的低優先序行為。
5. Release build 無新增 warning；完整 CTest 通過，含既有 `nimblerun_rebuild_pipeline_test`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "rebuild_pipeline" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "THREAD_MODE_BACKGROUND_BEGIN|SetThreadPriority" src/app_host/rebuild_pipeline.cpp
rg -n "低優先序|THREAD_MODE_BACKGROUND" docs/design-spec.md
```

Focused runnable coverage必須包含：驗證 worker lambda 在呼叫 `enumerate_source_` 前呼叫了優先權設定（可用既有 `ThreadFactory` 注入點攔截驗證，或以 grep-based self-check 確認呼叫順序），以及 `SetThreadPriority` 失敗時列舉仍正常完成的一個 case。

## Handoff requirements

交接時記錄：

- `SetThreadPriority` 呼叫的確切位置（檔案／行號）與四個呼叫點（開機、Ctrl+R、tray、設定套用）皆套用的驗證方式。
- `kJoinTimeoutMs`／`Shutdown()`／NR-123／NR-182 行為未變的證據（grep 或既有測試通過紀錄）。
- `docs/design-spec.md` §9.2 的修改內容與行號。
- Agent checks 的完整命令與結果。

## 交接區（2026-08-20，實作完成）

### 實作與 caller 覆蓋

- `src/app_host/rebuild_pipeline.cpp:156-157` 的 worker lambda 在
  `enumerate_source_`（`:159`）前呼叫
  `SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)`；回傳值以
  `(void)` 明確忽略。這是 best-effort scheduling hint，失敗不呼叫 `on_exception_`、不改
  `result->failed`，列舉仍會繼續。
- 沒有修改 `ThreadFactory` 簽名、`Start()`／`Shutdown()` 的 join、detach、generation 或
  cancel 旗標。三個來源都共用同一個 worker lambda：`main.cpp:3274-3288` 的
  StartMenu、AppsFolder、UserFolder enumeration callback 會經同一條 pipeline。
- 四個既有使用者入口均間接走到該 lambda：冷啟動 `main.cpp:3437`、Ctrl+R／tray 的
  `kRefreshMessage` `main.cpp:2591`、設定套用 `main.cpp:2645`；Ctrl+R 與 tray 共用同一
  個 Refresh handler。額外的 launch-failure refresh `main.cpp:1090` 與 AppsFolder
  on-demand `main.cpp:2052` 也走同一 `StartRebuild`，沒有旁路。

### focused coverage 與不變行為

- `tests/unit/rebuild_pipeline_test.cpp:50-92` 新增
  `TestBackgroundPriorityAttemptIsNonFatal`。既有 `ThreadFactory` 注入的 worker 先嘗試
  進入 background mode，再執行 pipeline 的第二次 best-effort priority attempt；測試以
  真實 worker 結果斷言 enumeration 仍被呼叫、result 仍 post、generation 仍完成。這個
  測試不新增 priority seam；呼叫順序由 source grep 確認，API 回傳值由 production code
  忽略，因此 kernel-side priority failure 不會變成 catalog failure。
- `src/app_host/rebuild_pipeline.h:48-88` 與 `.cpp:119-123,348-371` 的
  `kJoinTimeoutMs=5000`、`Shutdown()`、NR-123 detach fallback、NR-182 per-generation
  cancel flag 均未改動；sanity grep 仍只看到既有 bounded-wait 路徑，`TerminateThread`
  沒有新增命中。
- `docs/design-spec.md:704-705` 的 §9.2 現在寫明 Scan worker 在來源列舉前進入低優先序
  background mode，降低 CPU、I/O 與記憶體優先權；§FR-008 未改動。

### Agent checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`
  → configure 成功。
- 第一次 `cmake --build build` 已完成所有本項編譯與測試 target，但最後重連
  `NimbleRun.exe` 因既有 PID 20556 鎖定檔案而回傳 Permission denied；確認該 PID 的完整路徑
  是 `E:\GitHub\NimbleRun\build\NimbleRun.exe` 後停止該程序，重跑相同命令 → 成功，最後輸出
  `[1/1] Linking CXX executable NimbleRun.exe`。
- `ctest --test-dir build -R "rebuild_pipeline" --output-on-failure`
  → **1/1 Passed**（`nimblerun_rebuild_pipeline_test`，11.84 s）。
- `ctest --test-dir build --output-on-failure`
  → **32/33 Passed**；唯一失敗為已知、與本 item 無關的
  `nimblerun_startup_option_test`（`FAILED: enable writes the entry`，registry-write
  permission failure）。`nimblerun_rebuild_pipeline_test`（11.64 s）與
  `nimblerun_lifecycle_check`（5.04 s）均 Passed；沒有修補或放寬該既有測試。
- `rg -n "THREAD_MODE_BACKGROUND_BEGIN|SetThreadPriority" src/app_host/rebuild_pipeline.cpp`
  → `156-157`；`rg -n "低優先序|THREAD_MODE_BACKGROUND" docs/design-spec.md` →
  `413`（既有 FR-008）、`704-706`（本項 §9.2／既有 Icon worker）。`git diff --check`
  → 通過。

### 未完成

- 無。此 item 沒有新增需要桌面人工確認的 UI 行為；完整測試的既有 registry-write failure
  已如上記錄，未由本項造成。
