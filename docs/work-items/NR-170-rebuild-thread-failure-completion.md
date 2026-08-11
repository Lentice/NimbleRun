# NR-170 — RebuildPipeline 的 thread 建立失敗 catch 漏掉 CompleteIfReady，generation 永不完成

Phase 3 · Rebuild pipeline · Depends on: —

- Source: `docs/design-spec.md` §FR-008（每來源恰好完成一次）、NR-100／NR-106
  先例（rebuild completion 不可卡住的契約）
- Origin: 2026-08-11 第十六次稽核第 3 輪（codex backend，IMPORTANT）。主 Agent
  已重讀 `rebuild_pipeline.cpp:90-148` 驗證。
- Priority: **IMPORTANT**——單來源 rebuild（AppsFolder on-demand、watcher
  Change）建立 `std::thread` 失敗時，generation 已被 coordinator 標記、但
  `on_complete_` 永不執行：snapshot refresh、診斷、cache commit、launch-failure
  refresh gate 全數跳過，直到日後另一個 rebuild 才可能間接恢復。

## Why

`RebuildPipeline::Start`（`src/app_host/rebuild_pipeline.cpp:90-148`）有兩層
try/catch：

- **前置 setup failure 路徑（`:99-106`）**：`Settings` 拷貝／reserve 失敗時，
  `for` 迴圈逐來源 `ApplySourceFailure` 後呼叫
  `CompleteIfReady(generation)`——「generation 不卡住」契約完整。
- **每個 worker 的 `workers_.emplace_back`（`:143-146`）**：`std::thread`
  建立失敗的 catch 只做 `on_exception_()`＋`ApplySourceFailure(generation,
  source)`，**沒有 `CompleteIfReady(generation)`**。

後果：單來源 rebuild（AppsFolder on-demand `:1898`、watcher 單來源 `Request`）
在 thread 建立失敗時，該來源已 ApplySourceFailure（coordinator 的 pending 集合
清空、generation 已「完成」），但沒有任何東西再呼叫 `CompleteIfReady`——
沒有 worker 會 post 結果、沒有 token、`DrainFailures` 只在其他來源的訊息
抵達時才跑。`on_complete_`（UI 執行緒的 snapshot refresh／cache commit／
launch-failure gate 重置）永遠不執行。多來源 rebuild 若最後幾個 worker 建立
失敗，也依賴較早 worker 的訊息碰巧稍後到達才完成。

這是 NR-106「failure 必須完成 generation」保護的不變式在 `RebuildPipeline`
抽出後留下的缺口——`:99-106` 有、`:143-146` 漏。

## Decisions already made — do not reopen

1. **catch 內於 `ApplySourceFailure` 後補 `CompleteIfReady(generation)`**——
   與前置 setup failure 路徑同形（`:104` 先例），一行變更。
2. **新增可注入 thread factory 的測試 seam**：`Start` 的 worker 建立經由可替換
   的函式（如 `std::function<std::thread(Fn&&)>` 建構子參數，預設
   `[](Fn&& f) { return std::thread(std::move(f)); }`），測試注入拋例外的
   factory，斷言單來源 rebuild 在 thread 建立失敗時 completion 恰好執行一次。
   （沒有 seam 就無法在 CI 觸發 `std::thread` 失敗——OOM 不可注入。）
3. `ApplySourceFailure` 與 `CompleteIfReady` 的順序沿用 `:101-104` 先例
   （先 failure 再 complete）。
4. 不改 coordinator、不改 `QueueFailure`、不改 `DrainFailures`。

## Binding constraints — quoted, do not go looking for them

`docs/work-items/NR-106`（rebuild setup/handoff failure 必須完成 generation）：

> Rebuild setup／handoff failure 必須完成 generation。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp:90-148`（兩層 catch 的對照）、`:196-202`
  （`CompleteIfReady` 的「恰好一次」守門）。
- `src/app_host/rebuild_pipeline.h`（建構子參數、`workers_` 成員）。
- `tests/unit/rebuild_pipeline_test.cpp`（既有 fixture 與 completion 測試形狀；
  `PostToUi`／`EnumerateSource` seam 的先例）。

## Scope

1. `:143-146` 的 catch 補 `CompleteIfReady(generation)`。
2. `RebuildPipeline` 建構子新增 thread factory seam（預設行為零變更）。
3. 測試：注入拋 `std::bad_alloc` 的 thread factory，單來源 Start 後斷言
   `on_complete` 恰好呼叫一次、來源 failure 已套用。
4. 回歸：既有 `rebuild_pipeline_test` 全綠。

## Non-goals

- 不重試 thread 建立（OOM 重試無意義，failure 完成即契約）。
- 不改 coordinator 的 generation 語意、不改 `QueueFailure` 的 token 路徑。
- 不為 seam 增加第二個抽象層（一個 `std::function` 參數即足）。

## Acceptance

1. thread factory 拋例外時：單來源 rebuild 的 `on_complete` 恰好執行一次
   （測試斷言）。
2. 預設 factory 路徑與現況逐位元等價（既有測試全綠證明）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "rebuild" --output-on-failure
```

```powershell
rg -n "CompleteIfReady" src/app_host/rebuild_pipeline.cpp
# expect: :104（setup failure）與 thread-catch 各一次
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。
