# NR-065 — File events arriving during an in-flight rebuild must not be dropped

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-008（500 ms debounce、事件驅動重建）／§NFR-002
- Origin: 2026-08-07 第三次全 repo 稽核（catalog_refresh coordinator 狀態機）

## Why

`ApplySourceResult`（`src/catalog/catalog_refresh.cpp:81-83`）**無條件清除
`pending_[source]`**，不管事件是在掃描開始之前還是之後抵達的：

```cpp
source_entries_[source] = std::move(entries);
pending_[source] = false;      // ← 無條件
received_[source] = true;
```

時間線（全部走 `main.cpp` 既有路徑）：

1. T0：檔案事件 → `NotifySourceEvent` 設 `pending=true`＋`last_event_ms_=T0`，
   `ScheduleDebouncedRebuild` 啟動 500 ms timer。
2. T0+500：timer 觸發 → `StartRebuild([source])` → `BeginGeneration` → worker 開始掃。
3. T1（掃描途中）：又一個事件 → `NotifySourceEvent` 把 `pending` 保持 true、
   `last_event_ms_=T1`、timer 重設到 T1+500。
4. T2（T1 < T2 < T1+500）：worker 完成 → `ApplySourceResult` **把 `pending` 清掉**。
5. T1+500：timer 觸發 → `DueSources` 看到 `pending=false` → **不重建**。

T1 的變更若發生在掃描器讀過該目錄之後，就永遠進不了 catalog——直到下一次外部
觸發（Ctrl+R、重開機、或剛好又有新事件）。`MarkSourceFullRescan`（buffer overflow）
不受影響：`main.cpp:2234-2239` 會立即 `StartRebuild`，且 `last_event_ms_=kNever`
永遠 due；受傷的是**一般 debounce 事件**——新安裝的 app 靜默不出現。

`ApplySourceFailure`（`catalog_refresh.cpp:95`）有同一個問題：失敗收尾也無條件清
`pending`。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **修在 coordinator 的狀態機，不修 timer**：`BeginGeneration` 時為每個 active
   source 記下「當下的事件時間戳快照」；`ApplySourceResult/Failure` 只在
   `last_event_ms_[source]` 仍等於快照時才清 `pending_[source]`。事件在掃描期間
   抵達（時間戳已變）→ `pending` 保留 → 既有的 500 ms timer（T1+500 那顆）接住，
   觸發第二次重建。這是既有 timer 機制的自然收尾，**不新增 timer、不新增訊息**。
2. **`received_` 與 merge 語意不變**：generation 完成與否只看 `received_`；掃描
   結果照常合併。保留 `pending` 只影響「是否還有下一輪」，不影響「這一輪的結果
   是否生效」。
3. **`kNever` 快照互動**：`MarkSourceFullRescan` 把 `last_event_ms_` 設為 `kNever`；
   快照比較是值比較，kNever ≠ 任何真實時間戳，所以掃描期間的 full-rescan marker
   照樣存活（與現況一致）。
4. **不做 worker 內合併去重**：第二次掃描會重列舉整個來源，有界、事件驅動、
   正確。合併「掃描開始後的事件」進掃描中的結果是把狀態機搞複雜的開始。
5. **測試直接覆蓋 coordinator**：`catalog_refresh_test` 已是純值驅動，加兩個
   focused case（見 Acceptance）。

## Binding constraints — quoted, do not go looking for them

design-spec §FR-008：

> - Catalog 更新採啟動一次背景建置、檔案變更事件加 500 ms debounce、設定變更即時重建及 `Ctrl+R` 手動完整重建。

design-spec §NFR-002：

> - 閒置時零背景活動；事件驅動，無輪詢。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/catalog/catalog_refresh.h` — `CatalogRefreshCoordinator` 的欄位（`pending_`、
  `last_event_ms_`、`received_`、`active_sources_`、`generation_`）。新欄位加在這裡
  （`generation_event_snapshot_` 或等價）。
- `src/catalog/catalog_refresh.cpp:65-73`（`BeginGeneration`）、`:75-88`
  （`ApplySourceResult`）、`:90-101`（`ApplySourceFailure`）— 主場。
- `src/catalog/catalog_refresh.cpp:22-30`（`NotifySourceEvent`／`MarkSourceFullRescan`）
  — 事件時間戳的唯一寫入點，確認快照比較的語意。
- `src/app_host/main.cpp:2228-2245`（`kWatchChangedMessage`）與 `:2276-2287`
  （`WM_TIMER`）— **只讀不改**，確認 timer 在 `pending` 保留時會接住。
- `tests/unit/catalog_refresh_test.cpp:70-111` — `TestDebounceCoalescing`／
  `TestOverflowForcesFullRescan` 的既有寫法（fixture 直接呼叫 coordinator）。

## Scope

### 1. 快照事件時間戳

`BeginGeneration`：對每個 `active_sources_` 記下 `last_event_ms_[source]` 當下的
值（新欄位，例如 `std::map<CatalogSource, std::int64_t> generation_event_snapshot_`
或併入既有 map 結構，以實作最小為準）。

### 2. 條件式清除 pending

`ApplySourceResult` 與 `ApplySourceFailure` 兩者：

```cpp
if (last_event_ms_[source] == generation_event_snapshot_[source]) {
    pending_[source] = false;   // 掃描期間沒有新事件：這輪就是最新的
}
// 否則保留 pending：事件在掃描開始後抵達，既有 debounce timer 會接住
```

注意 `last_event_ms_` 對「從未有事件」的 source 是缺項——`BeginGeneration` 快照
時缺項就記缺項（例如 `kNever` 或 map 不建 entry），比較時對缺項做同值處理。
先讀 `DueSources`／`HasDueRebuild` 現行對缺項的處理，保持一致。

### 3. 測試

`catalog_refresh_test` 新增兩個 case（照既有 `TestDebounceCoalescing` 形狀）：

- **正常收尾不回歸**：`NotifySourceEvent(T0)` → `BeginGeneration({src})` →
  `ApplySourceResult(gen, src, entries)`（掃描期間無新事件）→ `pending` 為 false、
  `DueSources(T0+debounce)` 空。
- **掃描期間事件不被丟**：`NotifySourceEvent(T0)` → `BeginGeneration({src})` →
  `NotifySourceEvent(T1)`（掃描途中）→ `ApplySourceResult(gen, src, entries)` →
  `pending` 仍 true、`DueSources(T1+debounce)` 回傳該 source。
- `ApplySourceFailure` 同第二個 case 各跑一次（失敗收尾也不丟事件）。

### 4. 更新 spec？

§FR-008 描述的是行為層級（debounce、事件驅動），本 item 是讓描述成立。
不需改 spec；若 交接區 有值得寫回的字句（例如「掃描期間的事件會觸發下一輪」），
由實作者判斷，兩句以內。

## How this stays maintainable

**「這輪掃描是否已過時」是 coordinator 自己的狀態，不是 timer 的責任。** 快照比較
把「掃描開始後有新事件」變成一等公民，timer 永遠只要看 `pending`；未來任何
觸發方式（新事件、設定變更、Ctrl+R）都自動正確——不會再出現「某個觸發路徑忘了
清/留 pending」的分叉。

## Non-goals

- **不加 merge 邏輯**：不把「掃描期間的事件」併進正在進行的掃描。
- **不縮短 debounce、不加第二顆 timer。**
- **不改 `MarkSourceFullRescan` 的立即重建行為。**
- **不動 `main.cpp`**（`kWatchChangedMessage`／`WM_TIMER` 只讀）。
- **不新增日誌或通知。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋新增 case，仍在
   `nimblerun_catalog_refresh_test` 內，不新增測試執行檔）。
2. 上述四個新 case 通過（結果與 §Scope 3 一致）。

Manual：

3. 在 `%LOCALAPPDATA%\NimbleRun\logs` 觀察：安裝一個新 app 的同時觸發 `Ctrl+R`
   重建，重建結束後幾秒內（≤ 1 秒）應有第二輪 rebuild 日誌，且新 app 出現在
   面板（若無法手動製造該時序，在交接區說明並以 coordinator 測試代替）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 清除 pending 的兩處都有快照比較守衛：
Select-String -Path src/catalog/catalog_refresh.cpp -Pattern 'pending_\[source\] = false'
# expect: 2 處，且各在前幾行內有 snapshot 比較（或改用同值的判斷式）

# BeginGeneration 有快照：
Select-String -Path src/catalog/catalog_refresh.cpp -Pattern 'snapshot'
# expect: 至少 3 處——BeginGeneration 寫入、ApplySourceResult／ApplySourceFailure 比較

# 改動範圍：
git diff --name-only
# expect: src/catalog/catalog_refresh.{h,cpp}、tests/unit/catalog_refresh_test.cpp
```

## 交接區

（實作者填寫：修改的位置、快照欄位的實際形狀、缺項（從未有事件的 source）的
比較處理、建置與 CTest 結果、sanity greps、偏差、未完成事項。）
