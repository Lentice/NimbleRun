# NR-160 — NR-151 的 OOM 最後手段已失效：!recorded 分支的 inline post 被接收端當成 token 靜默丟棄

Phase 3 · Robustness · Depends on: —（NR-151 自己留下來的最後手段，現在是死路）

- Source: `AGENTS.md`（Deletion over addition… Prefer the smallest working change）、
  NR-151（決策 2：「double-OOM 的 inline 形狀保留為最後手段」——該決策的假設
  已被實作本身推翻）
- Origin: 2026-08-10 第十四次稽核第 3 輪（驗證軸，LOW；high confidence——agent
  逐行追蹤接收端）。主 Agent 已核對 `QueueFailure` 與 `OnDeliveryFailureMessage` 驗證。
- Priority: **LOW**——僅在 `failures_` 配置失敗的記憶體壓力下可達；不 crash、不損
  資料、下次任何 rebuild 觸發即自癒；但「保留為最後手段」的承諾是假的，dead
  inline post 會誤導 3am 讀者。

## Why

NR-151（commit 5f760f6）後，`OnDeliveryFailureMessage` 的 `w_param != 0` 分支一律
`handoffs_.Take(l_param)`（把 l_param 當 token）。但 `QueueFailure` 的
`!recorded` 分支（`rebuild_pipeline.cpp:187-191`，`failures_.emplace_back` 拋
bad_alloc 的 OOM 路徑）仍以 `(generation, source-value)` 的 inline 格式 post——
接收端現在把 `(LPARAM)0..3` 當 token，`Take` 必回 nullptr → **訊息被靜默丟棄**：
failure 永不 apply、generation 卡在 in-progress（pending 事件累積），直到下一次
`Request` → `Shutdown` 取消。NR-151 決策 2 承諾「double-OOM 時保留既有 inline
為最後手段」，但該手段在實作落地後已失效。

## Decisions already made — do not reopen

1. **`!recorded` 分支改走與 recorded 分支相同的 token 路徑**：
   `new RebuildResult{failed=true,...}` 外包 try/catch（bad_alloc → 放棄），
   `handoffs_.Register` 回 0 → 接受 stuck（下一個 explicit rebuild 接手，
   `Shutdown` 會取消舊 generation——既有機制）。
2. **刪除 inline post**：接收端已不理解該格式，留著是會誤導的死路。
3. `(0,0)` + `failure_event_` 路徑只留給「已記錄但無法送達」的情況（recorded 分支
   的 post 失敗 → SetEvent），不變。
4. 行為影響面：僅 OOM 雙重失敗場景；正常路徑與 NR-151 測試全部不變。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp`：`:169-193`（`QueueFailure` 的 recorded／
  `!recorded` 分支）、`:251-269`（`OnDeliveryFailureMessage` 的 token 消費端）。
- `src/win/handoff_registry.h`（`Register` 的例外安全：內部捕獲回 0）。
- `tests/unit/rebuild_pipeline_test.cpp`（NR-151 的偽造訊息測試，回歸網）。

## Scope

1. `!recorded` 分支：`new RebuildResult`（try/catch）+ `Register` + post token；
   任一失敗即放棄（stuck 由下次 rebuild 自癒）。刪除 inline post 分支。
2. 驗證：NR-151 測試（偽造訊息無效果、token 正常路徑完成 generation）全綠；
   Release build 零新增 warning；CTest 全綠（數量不變）。

## Non-goals

- 不動 recorded 分支與 `(0,0)`/`failure_event_` 機制。
- 不為 OOM 路徑寫模擬測試（不可測、無回報價值）；以「接收端不再理解任何
  非 token 內容」為驗證點。

## Acceptance

1. `QueueFailure` 內不再有 `(generation, source)` 直接當 lParam 的 post。
2. NR-151 測試全綠；Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R rebuild_pipeline --output-on-failure
```

```powershell
rg -n -B 2 -A 8 "else if \(!post_to_ui_" src/app_host/rebuild_pipeline.cpp
# expect: 不再有把 source 值當 lParam 的 post（或該分支已改走 token）
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
