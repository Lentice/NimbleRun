# NR-162 — 全 visible 隊列 eviction：被踢的可見請求永不回報，pending_ 永久卡死

Phase 3 · Robustness · Depends on: —

- Source: `AGENTS.md`（No busy loops… keep the idle path event-driven）、
  NR-099（隊列上限與 visible 優先的既有決策——本 item 補上「eviction 必須
  可觀察」的缺口）、design-spec §FR-009（fallback 恢復）
- Origin: 2026-08-11 第十四次稽核第 5 輪（claude backend，IMPORTANT；
  high confidence——agent 追蹤 pending_ 的生命週期）。主 Agent 已核對
  `icon_worker.cpp:112-121`、`main.cpp:1059-1070`、
  `icon_request_session.cpp` 驗證。
- Priority: **IMPORTANT**——正常操作可達（非 OOM）：冷 cache 大結果集
  快速翻頁時 >64 個不同 visible key 排隊，`pop_back()` 踢掉最舊的可見任務
  （違反 NR-099 註解「back 永遠是 prewarm」的斷言），該 key 永遠卡在
  `pending_`：圖示永遠 fallback、`pending_` 單調成長直到重啟。

## Why

`IconWorker::Post`（`icon_worker.cpp:112-121`）：visible 任務 `push_front`，
`while (queue_.size() > kMaxQueuedTasks) queue_.pop_back();`。註解（NR-099）
斷言 back 永遠是 prewarm/flush——但當隊列全為 visible 任務（≥64 個不同
visible key 同時排隊）時，`pop_back()` 移除的是**最舊的可見任務**，且
`Post` 仍回 `true`。

消費端：`main.cpp:1065-1066` `RequestVisibleIcon`——`Post` 回 true 即
`BeginRequest(encoded)` 記入 `IconRequestSession::pending_`。`pending_` 只在
`OnResult`（需 IconResult 送達）與 `DrainDropped`（需 worker 回報 dropped
keys）清除；被踢的任務兩者皆不觸發 → key 永久卡在 `pending_` →
`ShouldRequest` 永回 false → 該列永遠畫字母 fallback，`pending_` 每個 key
的 `std::wstring` 累積到 session 結束。

Trigger：冷 `icons.cache`（首次執行或重建後）輸入寬查詢（如 `e`）→ 結果
數百列，連續按 PgDn。每頁最多 8 個 fresh visible 請求（`Render` →
`DrawIconOrFallback` → `RequestVisibleIcon`），自動重複超越 Shell fetch
速度（~5-30 ms/icon）→ 排入 >64 個不同 visible key。

測試缺口：`tests/unit/icon_worker_test.cpp:475-519` 只測混合隊列
（「evicted exactly one prewarm from the back」）；全 visible 情形未測；
`:687` 只測 worker 回報的 drop 路徑。

## Decisions already made — do not reopen

1. **eviction 可觀察化**：`pop_back()` 移除的是 `Load` 任務且
   `request.visible` 時，把其 `encoded_key` 推入 `g_icon_dropped_keys`
   （既有恢復通道）並照常 PostMessage 觸發 drain——UI 在下次
   `kIconReadyMessage`/ShowPanel 即清掉該 pending key，下輪 render 重新
   請求。這是「被踢的任務也走既有 dropped 管道」的最小變更。
2. **測試**：新增「post `kMaxQueuedTasks + 1` 個 visible 請求 → 最舊可見
   任務被回報為 dropped（dropped keys 含第一筆）」的單元測試。
3. 不動 `kMaxQueuedTasks = 64` 與優先序策略本身；不改 `IconRequestSession`
   的 API。
4. 若 `PostMessage` 失敗（目標視窗已死）：dropped key 無人 drain——接受
   （下一個 ShowPanel 的既有 drain 會清 `g_icon_dropped_keys` 全表；且視窗
   已死時無 UI 可修復）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

design-spec §FR-009：圖示載入失敗時顯示 fallback，且之後可恢復。

## Files to read and trace first

- `src/icons/icon_worker.cpp:102-135`（Post 的 eviction 段）、`:37-56`
  （`RememberDroppedRequest` 既有回報通道）。
- `src/app_host/main.cpp:1059-1070`（`RequestVisibleIcon` 的 BeginRequest）。
- `src/app_host/icon_request_session.cpp`（pending_ 的清除面）。
- `tests/unit/icon_worker_test.cpp:475-519, 687`（既有混合隊列與 drop 測試）。

## Scope

1. eviction 時對被踢的可見任務呼叫 `RememberDroppedRequest`（或同通道的
   inline 回報），讓 `pending_` 得以釋放。
2. 新增全 visible 隊列 eviction 的單元測試。
3. 回歸：icon_worker 測試全綠；Release build 零新增 warning；CTest 全綠
   （數量不變——只加測試不改 target 數）。

## Non-goals

- 不調整隊列上限、不改變 visible 優先的排程語意。
- 不做「請求撤回」或改 `IconRequestSession` API。

## Acceptance

1. `pop_back()` 移除非 prewarm 的可見任務時，其 key 進入 dropped 回報
   （`g_icon_dropped_keys` 或被 UI 消費的等價通道）。
2. 新測試：全部 visible 的隊列，第 65 個請求使最舊可見任務被回報為
   dropped；`IconRequestSession::pending_` 對該 key 可被 `DrainDropped`
   清除。
3. 既有測試全綠；CTest 全綠（數量不變）；Release build 零新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R icon_worker --output-on-failure
```

```powershell
rg -n -A 6 "queue_.pop_back\(\)" src/icons/icon_worker.cpp
# expect: eviction 段對被踢的可見任務有 dropped 回報
```
