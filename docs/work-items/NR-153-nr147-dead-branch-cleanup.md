# NR-153 — NR-147 留下的死分支與未用常數：Change 分支 if/else 兩臂相同、kRebuildStartMinIntervalMs 零引用

Phase 2 · Code structure · Depends on: —（NR-147 自己的修補引入的殘渣）

- Source: `AGENTS.md`（Deletion over addition… Prefer the smallest working change）
- Origin: 2026-08-10 第十四次稽核第 2 輪（ponytail 軸，MEDIUM；high confidence——
  兩臂逐字比對）。主 Agent 已讀 `Request` 與 `OnDebounceTimer` 驗證。
- Priority: **MEDIUM**——round-1 修補自己引入的死碼；3am 讀者會在錯的地方找限流邏輯。

## Why

`RebuildPipeline::Request` 的 Change 分支（`src/app_host/rebuild_pipeline.cpp:77-91`，
NR-147 改完後）：

```cpp
if (AcceptRebuildStart(last, now)) {
    refresh_.NotifySourceEvent(source, now);
} else {
    // NR-147: throttled Change event -- never dropped...
    refresh_.NotifySourceEvent(source, now);   // 兩臂逐字相同
}
```

`AcceptRebuildStart` 的結果完全不影響後續（兩臂都只 `NotifySourceEvent`）——真正的
限流守門在 `OnDebounceTimer`（`:240-267`，NR-147 交接區也自承）。`last` 的查找
（`:79-82`）與整段 if/else 是死碼，else 臂的 NR-147 註解描述的限流行為根本不在
這個分支執行。另有三個未用常數/字面值：

- `kRebuildStartMinIntervalMs`（`rebuild_pipeline.h:94`）宣告後**零引用**；
- `AcceptRebuildStart`（`:17-19`）硬寫 `1000` 與 `-1` 字面值（該檔已有
  `kFullRescanNever` 常量在別處用）。

## Decisions already made — do not reopen

1. Change 分支刪掉 if/else 與 `last` 查找，只留一行 `NotifySourceEvent(source, now)`
   ＋ NR-147 註解（「限流在 OnDebounceTimer 實行，此處事件絕不丟」）。
2. `AcceptRebuildStart` 改用 `kRebuildStartMinIntervalMs` 與
   `kNoRebuildStart`（`kFullRescanNever` 的同形 sentinel，若 rename 影響 FullRescan
   語意則只加不改既有常量）。
3. **零行為變更**：`OnDebounceTimer` 的實際守門不動；既有 throttle 測試即回歸網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp`：`:17-19`（`AcceptRebuildStart`）、`:56-91`
  （`Request`）、`:240-267`（`OnDebounceTimer` 的真守門）。
- `src/app_host/rebuild_pipeline.h`：`:94`（`kRebuildStartMinIntervalMs`）、既有
  sentinel 常數。
- `tests/unit/rebuild_pipeline_test.cpp`（throttle 測試，NR-147 產出）。

## Scope

1. 刪死分支與 `last` 查找。
2. 常數化：`AcceptRebuildStart` 引用 `kRebuildStartMinIntervalMs`（與
   `kNoRebuildStart`）。
3. 驗證：既有 throttle 測試（burst→一次、脈衝→限流、閘門後啟動）全綠——
   行為零變更。

## Non-goals

- 不動 `OnDebounceTimer` 的守門實作。
- 不重排 `Request` 的其他分支。

## Acceptance

1. `rebuild_pipeline.cpp` 的 Change 分支無 if/else（grep 驗證）。
2. `kRebuildStartMinIntervalMs` 有引用（rg 命中 > 1）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R rebuild_pipeline --output-on-failure
```

```powershell
rg -n "kRebuildStartMinIntervalMs" src/app_host
# expect: header 宣告 + AcceptRebuildStart 引用，至少兩處
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
