# NR-151 — NR-139 殘餘：偽造 inline delivery-failure 可使 generation 提前完成，把過時快照寫進 catalog.cache

Phase 3 · Security · Depends on: —（NR-139 的同一威脅模型第二半；兩者都動
`rebuild_pipeline.cpp` 的失敗傳遞，建議在 NR-139 後）

- Source: NR-139（同 family：「偽造訊息不可產生內容效果」）、`docs/design-spec.md`
  §10.2（catalog.cache 是被信任為不可信的磁碟狀態）、NR-113（cache 行不經來源驗證）
- Origin: 2026-08-10 第十四次稽核第 2 輪（正確性軸，MEDIUM；high confidence——
  完整追蹤了 QueueFailure→OnDeliveryFailureMessage→GenerationComplete→cache 寫入鏈）。
- Priority: **MEDIUM**——NR-139 修掉了 crash，但「偽造失敗標記 active source」的
  內容效果仍在。

## Why

NR-139 後，`ApplySourceResult`／`ApplySourceFailure` 的 `.find()` 守門只拒絕
**非 active** 的 source。偽造訊息若 `w_param` = 目前 generation、`l_param` =
某個 worker 仍在列舉中的 **active** source，會通過守門：

1. `received_[source] = true`（`catalog_refresh.cpp`）→ `GenerationComplete` 提早成立；
2. `RebuildMerged` 以該來源的**過時/cache 種子** entries 發布 snapshot；
3. `OnGenerationCompleteRefresh`（`main.cpp:1248-1260`）把過時 snapshot **寫進
   `catalog.cache`**（磁碟）；
4. 記憶體 snapshot 會在真結果到達時自癒（同 generation 重新 merge），但
   `completed_generation_` 守門（`rebuild_pipeline.cpp:167-173`）使
   `on_complete_`（含 cache 寫入與 reconcile）**不再觸發**——catalog.cache 的
   過時行會留到下次完整 rebuild；且 snapshot-assembler 的 reconcile（
   `snapshot_assembler.cpp:77-79`）在提前完成時以缺源快照為準跑過一次，
   `usage.tsv`／`favorites.txt` 的保留期 reconcile 以缺失項目為據。

同 user attacker 本就能直接改寫資料檔（NR-139 的 accepted-risk 論證），但
「偽造一則 message 即可讓**下一次完整 rebuild 前**的 cache 與 reconcile 內容錯誤」
超出 NR-139 交接區「自癒、無持久效果」的結論——效果是持久的（到下次 rebuild）。

## Decisions already made — do not reopen

1. **inline 路徑改走 token registry**（與 `OnResultMessage` 同型，NR-131）：
   `QueueFailure`（`rebuild_pipeline.cpp:147-165`）在 `failures_` 之外，
   `handoffs_.Register(std::unique_ptr<RebuildResult>{new RebuildResult{failed=true,
   generation, source}})` 並把 token 放進 `l_param` post。
   `OnDeliveryFailureMessage` 的 `w_param != 0` 分支改為 `handoffs_.Take(token)`
   ——與 `OnResultMessage` 相同形狀；取不到（偽造 token）→ `return 0`。
   `w_param == 0`（drain）分支與 `failure_event_` 機制完全不動。
2. **double-OOM 的既有 inline 形狀保留為最後手段**：`Register`／post 皆失敗時沿用
   現在直接 post `(generation, source)` 的 best-effort（process 已在 OOM 邊緣，
   此路徑本質上不可達；保留它以維持「generation 不永久卡住」的 NR-100 契約）。
3. `RebuildResult` 的 `failed` 欄位已存在（`:109-125` 的 worker 例外路徑已用它），
   不新增結構。
4. 不新增測試目標：`rebuild_pipeline_test` 加斷言。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp`：`:147-165`（`QueueFailure`）、`:193-226`
  （`OnResultMessage` 的 token 形狀 vs `OnDeliveryFailureMessage`）、`:104-145`
  （`Start` 的 worker 例外路徑）。
- `src/win/handoff_registry.h`（`Register`／`Take` 的既有 API）。
- `tests/unit/rebuild_pipeline_test.cpp`（失敗傳遞的既有測試形狀）。

## Scope

1. `QueueFailure`：優先註冊 failed `RebuildResult` 並 post token（帶 `failed=true`
   的 `RebuildResult` 建構用既有欄位）；失敗才退回 inline。
2. `OnDeliveryFailureMessage`：`w_param != 0` 分支走 `Take`；`failed=true` 的結果
   交給 `ApplySourceFailure`（與 `OnResultMessage` 的 `result->failed` 分支同處理）。
3. `tests/unit/rebuild_pipeline_test.cpp` 新增：偽造 inline 訊息（任意 w_param/l_param）
   → 不產生任何 content 效果（generation 不提前完成、無 reconcile）；token 路徑的
   正常失敗仍完成 generation（既有測試保持綠燈）。

## Non-goals

- 不改 `ApplySource*` 的 `.find()` 守門（NR-139 的根因修補保留）。
- 不改 `failure_event_`／`MsgWaitForMultipleObjectsEx` 喚醒機制。
- 不為 watcher/其他訊息加 sender 驗證（same-user threat model 結論不變）。

## Acceptance

1. `OnDeliveryFailureMessage` 的 `w_param != 0` 分支沒有直接消費 `l_param` 的內容值。
2. 新測試通過；既有 `rebuild_pipeline_test` 全綠。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R rebuild_pipeline --output-on-failure
```

## Handoff

- 2026-08-10 完成（NR-151, commit `NR-151: authenticate delivery-failure messages with handoff tokens`）。
- 變更：`src/app_host/rebuild_pipeline.cpp` — `QueueFailure`（:159-199）的 recorded
  分支改為優先 `handoffs_.Register` 一個 `failed=true` 的 `RebuildResult`
  （`new RebuildResult` + 既有欄位，與 worker 路徑 :121-127 同型）並 post
  `kRebuildDeliveryFailedMessage` `(generation, token)`（與 `kRebuildDoneMessage`
  同形）；`Register`／post 任一失敗即 `Erase` 並退回既有 `(0,0)` + `SetEvent`
  fallback。not-recorded（double-OOM）分支原樣保留（decision 2 的 best-effort）。
  `OnDeliveryFailureMessage`（:226-246）的 `w_param != 0` 分支改為
  `handoffs_.Take(l_param)`——與 `OnResultMessage` 同形；取不到或 `!result->failed`
  → `return 0`（偽造 token 無內容效果）；取得則 `ApplySourceFailure(generation,
  source)` + `CompleteIfReady` + `DrainFailures` + repaint。`w_param == 0`（drain）
  分支與 `failure_event_` 機制未動。
- 測試：`tests/unit/rebuild_pipeline_test.cpp` 新增 `TestForgedDeliveryFailureIgnored`
  ——worker 以 gate/release event 卡在列舉中，期間以 `OnDeliveryFailureMessage(1, 0/1/2)`
  發偽造 inline 訊息，斷言 `completed == 0`、`IsRebuildInProgress()`、snapshot 仍為
  快照種子（無提前 merge）；release 後真實結果仍恰好完成一次 generation。已實證
  判別力：stash 掉 `rebuild_pipeline.cpp` 只留新測試 → 測試失敗；套回修復 → 綠燈。
- 驗證：Release（llvm-mingw/Ninja）build 成功；新 warnings 0（唯一 warning 仍是
  `main.cpp:1395 unused variable 'target_size'`，NR-150 交接區已記錄與本 item 無關）；
  CTest 31/31 全綠（數量不變）。
- 交接：失敗的雙重保送（`failures_` 記錄 + token 註冊）維持不變——token 訊息直接
  套用失敗並 `DrainFailures`（雙套用為冪等：`ApplySourceFailure` 對同一 source 重跑
  僅重算相同 snapshot），drain 路徑仍是訊息遺失時的兜底。殘留風險（decision 1 明言
  接受）：偽造值若恰好等於真實 token（heap 位址）仍有效，與 `OnResultMessage` 現況
  相同；double-OOM 的 inline 形狀現為 no-op，NR-100「generation 不永久卡住」在該
  不可達路徑上由 `failure_event_` 兜底（recorded 時）維持。後續勿再把
  `OnDeliveryFailureMessage` 的 `w_param != 0` 分支改回直接消費 `l_param` 內容值。
