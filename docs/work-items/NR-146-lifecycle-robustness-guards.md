# NR-146 — ShowPanel 的 GetMonitorInfoW 未檢查＋shutdown 逾時 detach 後 pipeline 不得銷毀

Phase 3 · Robustness · Depends on: —（`main.cpp` 生命週期的兩個一線缺陷，同一檔）

- Source: `AGENTS.md`（Prefer the smallest working change…）、`docs/design-spec.md` §9.4
  （關閉不得無限等待）、NR-123（bounded join 的既有設計——本 item 補其物件生命週期漏洞）
- Origin: 2026-08-10 第十四次全 repo 稽核（正確性軸，兩項皆 LOW；主 Agent 已讀碼驗證）。
- Priority: **LOW**——兩者都是窄窗口的健壯性問題，不是常態 bug；修法各一行。

## Why

**（a）`GetMonitorInfoW` 回傳值未檢查**（`src/app_host/main.cpp:1792`）：

```cpp
MONITORINFO monitor_info{};
monitor_info.cbSize = sizeof(monitor_info);
const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
GetMonitorInfoW(monitor, &monitor_info);
const RECT work_area = monitor_info.rcWork;
```

`GetMonitorInfoW` 失敗時 `rcWork` 未定義（`{}` 初始化下是零）。失敗 → 面板依 0×0 work
area 定位與 clamp（`ClampWindowSize` 對垃圾尺寸仍會產出合法尺寸，不 crash，但面板
出現在錯誤位置）。實務上 `MonitorFromPoint` 幾乎不會回 NULL，故 LOW。

**（b）shutdown 逾時 detach 後 pipeline 被銷毀**（`main.cpp:2799` → `:3206`）：

`Shutdown(5000)` 逾時時把 worker 執行緒 detach（`src/app_host/rebuild_pipeline.cpp:259-263`），
但 `g_rebuild_pipeline.reset()`（`main.cpp:3206`）在 message loop 結束後銷毀 pipeline；
detached worker 稍後仍會呼叫 `handoffs_.Register`（`:127`）、`post_to_ui_`（`:133`）、
`enumerate_source_`（`:113`）——全是已銷毀 `this` 的成員（UAF）。NR-123 的
「process 結束 OS 回收執行緒」論證只在「執行緒比 process 晚醒」時不成立——正是
hung Shell 呼叫逾時 detach 的此刻。窗口窄（process 已在退出中）但真實存在。

## Decisions already made — do not reopen

1. （a）`GetMonitorInfoW` 失敗 → `return;`（不顯示面板）。`MonitorFromPoint` 失敗是
   系統級異常，錯位顯示比不顯示更糟；不做 fallback 幾何（那是另一份臆測）。
2. （b）`Shutdown(5000)` 逾時後**不再 `reset()` pipeline**：改為
   `g_rebuild_pipeline.release()`（deliberate leak，一行 + 註解，引用 NR-123 與本 item）。
   process 正在退出，OS 回收記憶體；detached worker 在其存活期間保持 `this` 有效。
   若實作時發現 `wWinMain` 的收尾順序已保證 pipeline 比 worker 晚活（例如 reset 前有
   其他等待），仍**必須**保留 release 語意——「detach 即放棄物件生命週期管理」是
   最簡且誠實的契約。
3. **不做**「Shutdown 回傳是否逾時」的 API 擴充（呼叫端只需知道「逾時就不該銷毀」，
   不需知道細節；`Shutdown` 內已有 `finished` 旗標，若改回傳 bool 也是選修，但
   一行 `release()` 已足夠，不增加介面面積）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4（節錄，以原文為準）：

> 關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑。等待有界，超時即繼續退出。

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`：`:1785-1809`（`ShowPanel` 定位）、`:2790-2805`（teardown 的
  `Shutdown(5000)`）、`:3200-3209`（`g_rebuild_pipeline.reset()`）。
- `src/app_host/rebuild_pipeline.cpp`：`:242-270`（`Shutdown` 的 detach 分支）。

## Scope

1. `ShowPanel`：`GetMonitorInfoW` 失敗 → `return;`。
2. teardown：逾時 detach 後 `g_rebuild_pipeline.release()`（若 `Shutdown` 未逾時則維持
   `reset()`——需要知道逾時與否，實作時以 `Shutdown` 的現有 `finished` 資訊或
   workers 狀態判斷；若必須知道，最小改動是讓 `Shutdown` 回傳 `bool finished`）。
3. 驗證：Release build + CTest 全綠；正常關閉路徑（無 hung）行為與先前完全一致。

## Non-goals

- 不重寫 teardown 順序、不引入 shutdown 狀態機。
- 不改 `RebuildPipeline::Shutdown` 的 detach 設計本身（NR-123 已決策；`TerminateThread`
  對 Shell/COM 鎖不安全）。
- 不為此加測試目標（兩處皆一行防護；既有 suite 即驗證網——AGENTS 的
  「New non-trivial logic needs one focused runnable test」不適用於兩行防護）。

## Acceptance

1. grep 驗證：`ShowPanel` 內 `GetMonitorInfoW` 後有失敗分支；teardown 路徑在逾時分支
   使用 `release()` 而非 `reset()`。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "GetMonitorInfoW" src/app_host/main.cpp
rg -n "g_rebuild_pipeline\.(reset|release)" src/app_host/main.cpp
# expect: GetMonitorInfoW 有失敗分支；逾時分支 release()（正常分支 reset()）
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff（2026-08-10）

- 實作內容（3 檔，28+ / 5- 行）：
  - `src/app_host/main.cpp:1795-1798`：`ShowPanel` 在 `GetMonitorInfoW` 後加
    `if (!GetMonitorInfoW(monitor, &monitor_info)) return;`（失敗不顯示面板）。
  - `src/app_host/rebuild_pipeline.h:78-84` 與 `rebuild_pipeline.cpp:242,270`：
    `Shutdown` 改回傳 `bool finished`（true = 正常 join；false = 逾時 detach）。
    `~RebuildPipeline` 與 `Start` 的 INFINITE 呼叫永不 detach，忽略回傳值，語意不變。
  - `src/app_host/main.cpp:315-318`：新增 `g_rebuild_shutdown_timed_out` 旗標；
    `:2807-2811` WM_DESTROY 分支記錄 `Shutdown` 回傳值；
    `:3219-3227` teardown 逾時分支 `g_rebuild_pipeline.release()`（deliberate leak，
    附 NR-123/NR-146 註解），正常分支維持 `reset()`。
- 決策覆核：item 決策 3 傾向「一行 release()、不做 API 擴充」，但 scope 第 2 項與
  acceptance 第 1 項要求「逾時分支 release、正常分支 reset」；主 Agent 指示採方案 (a)
  （`Shutdown` 回傳 `bool finished`，最小改動）。正常關閉路徑行為與先前完全一致。
- 驗證：Release Ninja llvm-mingw 建置零新增 warning（`main.cpp:1389` 的
  `target_size` unused warning 為既有，stash 對照確認）；CTest 31/31 全綠（數量不變）。
- 測試：未新增（兩處皆一行防護，item non-goals 明訂）。
