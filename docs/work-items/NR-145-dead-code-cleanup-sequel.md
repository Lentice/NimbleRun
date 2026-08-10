# NR-145 — 死碼與 test-only API 清理 sequel：SlotRect 死參數、QueueDepth／Clear／EraseIf／kJoinTimeoutMs

Phase 3 · Code structure · Depends on: —（NR-128 的續集；全部為刪除，零行為變更）

- Source: `AGENTS.md`（Deletion over addition… Prefer the smallest working change）、
  NR-128（死碼與 test-only API 移除——漏了五件）
- Origin: 2026-08-10 第十四次全 repo 稽核（ponytail 軸，LOW）。主 Agent 已 grep 全部呼叫端驗證。
- Priority: **LOW**——無 bug；`SlotRect` 的死參數是 3am 陷阱（誤以為它參與幾何計算），
  `EraseIf` 是零呼叫者的純死碼。

## Why

| 項目 | 位置 | 證據 |
|---|---|---|
| `SlotRect` 的 `client_height_dip` 死參數 | `src/ui/panel_layout.h:127-129`（自承「it is not read」）、`panel_layout.cpp:69-70`（`(void)client_height_dip;`） | 4 個 prod 呼叫端（`main.cpp:758, 1407, 1549, 2145`）＋測試都要先算 client height 再傳進不讀它的參數 |
| `IconWorker::QueueDepth()` | `src/icons/icon_worker.h:125`、`icon_worker.cpp:172` | 只有 `icon_worker_test.cpp` 呼叫 |
| `IconCache::Clear()` | `src/icons/icon_cache.h:93`、`icon_cache.cpp:57` | 只有測試呼叫 |
| `HandoffRegistry::EraseIf` | `src/win/handoff_registry.h:56` | 全 repo 零呼叫（含測試） |
| `RebuildPipeline::kJoinTimeoutMs` | `src/app_host/rebuild_pipeline.h:85`（`= 5000`） | 宣告處即唯一出現處；呼叫端 `main.cpp:2799` 硬寫 `Shutdown(5000)` |

`SlotRect` 參數是「為對稱而對稱」（header 註解說它 mirror 反函數的 footer bound）——
保留理由不存在於負載上，誤以為它參與幾何正是 3am 陷阱（NR-133 的單一幾何來源
應該沒有這個殘渣）。

## Decisions already made — do not reopen

1. **`SlotRect` 簽名刪掉 `client_height_dip`**：5 個呼叫端（4 prod + 測試）一併刪參數。
   反函數 `SlotAtPointDip` 的簽名**不動**（它真的讀 footer bound）。
2. **`QueueDepth`／`Clear` 直接刪除**，測試改用「每案例建新實例」重置狀態
   （`icon_worker_test`／`icon_cache_test` 若有跨案例依賴共享實例才需要改結構；
   不得把測試幫手塞回 prod class）。
3. **`EraseIf` 直接刪除**（零呼叫者，無測試依賴）。
4. **`kJoinTimeoutMs`**：呼叫端改為 `Shutdown(RebuildPipeline::kJoinTimeoutMs)`（常數已是
   public static；讓 call site 引用它，刪掉「宣告與使用分家」狀態）。
5. 行為零變更：CTest 數量不變（不增減測試目標）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/ui/panel_layout.{h,cpp}`：`:69-70`、`:127-129`（`SlotRect`）；grep 5 個呼叫端。
- `src/icons/icon_worker.{h,cpp}`：`:125`、`:172`（`QueueDepth`）；grep 測試呼叫端。
- `src/icons/icon_cache.{h,cpp}`：`:93`、`:57`（`Clear`）；grep 測試呼叫端。
- `src/win/handoff_registry.h`：`:56`（`EraseIf`）。
- `src/app_host/rebuild_pipeline.h:85`、`src/app_host/main.cpp:2799`。
- `tests/unit/icon_worker_test.cpp`、`tests/unit/icon_cache_test.cpp`、`tests/unit/panel_layout` 相關測試。

## Scope

1. 刪除上表五項（`kJoinTimeoutMs` 為呼叫端改用常數）。
2. 受影響測試改用新實例或刪掉該斷言（斷言內容若本來就是「queue boundedness」的探針，
   需確認 queue bound 行為仍有其他斷言覆蓋——`icon_worker_test` 的 bound 測試若只靠
   `QueueDepth` 斷言，該如何維持覆蓋由實作 agent 判斷並在交接區記錄）。
3. 驗證：`git diff` 只含上述刪除；CTest 全綠。

## Non-goals

- 不重構 `SlotAtPointDip` 或任何幾何計算。
- 不清理 `main.cpp` 其他死碼（如有，列回候選）。
- 不新增測試目標。

## Acceptance

1. grep 驗證：`QueueDepth`、`IconCache::Clear`、`EraseIf`、`kJoinTimeoutMs` 在 src/ 零命中
   （`kJoinTimeoutMs` 除 call site 引用外零重複）。
2. `SlotRect` 簽名無 `client_height_dip`。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "QueueDepth|\.Clear\(\)|EraseIf|kJoinTimeoutMs" src
# expect: 零命中（.Clear() 需排除其他物件的合法 Clear，逐條人工確認）
rg -n "client_height_dip" src
# expect: 零命中
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff（2026-08-10，NR-145 done）

- **SlotRect**：`panel_layout.h:129` 簽名改 `SlotRect(int slot, int columns)`，
  header 註解補 NR-145 說明；`panel_layout.cpp:69` 刪 `(void)client_height_dip;`。
  5 個呼叫端全改：`main.cpp:758, 1407, 1549, 2148`＋
  `tests/unit/ui_palette_layout_test.cpp:586`。`OpenKeyboardItemMenu`（`main.cpp:2147`）
  的 `client_height_dip` local 因此變 unused，一併刪除（否則新增 warning）。
  `SlotAtPointDip`／`FooterTopDip` 未動。
- **QueueDepth**：`icon_worker.h:125` 宣告＋`icon_worker.cpp:172-175` 定義刪除；
  `mutex_` 的 `mutable` 與其註解一併還原（唯一 const 使用者就是 QueueDepth）。
  測試改用行為斷言（見下），未把測試幫手塞回 prod class。
- **IconCache::Clear**：`icon_cache.h:93`／`icon_cache.cpp:57-60` 刪除。
  實證：`icon_cache_test.cpp` 從未呼叫它（每案例都是新實例），連測試都不需要改。
- **EraseIf**：`handoff_registry.h:55-65` 刪除，零呼叫者（含測試）；`Erase`／`Clear`
  保留（`rebuild_pipeline.cpp:264`、`main.cpp:2817` 是 `HandoffRegistry::Clear`，非本
  item 範圍）。
- **kJoinTimeoutMs**：偏差——item 正文寫「常數已是 public static」是錯的，實際在
  `rebuild_pipeline.h` 的 **private** section（:85）。移到 public（:51，含 NR-145
  註解），`main.cpp:2802` 改
  `Shutdown(nimblerun::RebuildPipeline::kJoinTimeoutMs)`。
- **QueueDepth 探針的行為替代**（item 交給實作 agent 決定，此為記錄）：
  - `TestQueueCapDropsPrewarmWhenFull`／`TestVisibleEvictsPrewarmWhenFull`：
    QueueDepth 三連斷言改為「結果集合精確等於期望 key set」＋
    `!AnyResultIn(150ms)` 無額外結果。cap-63 bug 會讓 pump 收不滿 65 個結果
    （timeout 失敗）、cap-65 bug 會多出第 66 個結果（AnyResultIn 失敗）；
    drop/evict 的 key 永不報到（key set 失敗）。原測試兩處
    `PumpResults(kMaxQueuedTasks + 1)` 計數斷言保留。
  - `TestFlushCoalesces`：改用 `ThrowingStore`（測試檔內既有 helper，`flush_calls`
    計數）——三次 `PostFlush` 後 worker 只會呼叫一次 `IconStore::Flush`
    （poll 到 `flush_calls >= 1` 後斷言 `== 1`；`Stop` 的最終 flush 因
    `pending_puts_` 已被 flush 歸零而不會多算）。未改 `ThrowingStore`。
  - `TestCancelPrewarmDropsQueuedPrewarm`：深度斷言改為結果集合
    `{a|48, vis|48}`＋無額外結果；被取消的 b、c 永不報到。
- **驗證**：Release Ninja llvm-mingw 建置零新 warning；`ctest` 31/31 全綠
  （測試總數維持 31，未增刪測試目標）；聚焦
  `ctest -R "icon_worker|icons_cache|dpi_theme"` 3/3。
- **偏差**：item Agent check 的 `rg "client_height_dip" src → 零命中` 無法達成——
  該名字仍合法存在於 `FooterTopDip`／`ViewportRowsForHeightDip`／`SlotAtPointDip`
  簽名與其呼叫端（`main.cpp:647, 686, 739`），而 item 明令不得動
  `SlotAtPointDip`。驗證改為：`SlotRect` 簽名與所有呼叫端零殘留
  （`rg "SlotRect\(" src` 只剩 2 參數呼叫）、`QueueDepth|EraseIf` 零命中、
  `kJoinTimeoutMs` 僅宣告＋call site 兩處、`.Clear()` 僅 2 個合法
  `HandoffRegistry::Clear`。
