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
