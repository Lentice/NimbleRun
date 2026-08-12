# NR-182 — RebuildPipeline::Start() 無界 join 舊 worker：換代時 UI 凍結（§9.4）

Phase 3 · Host lifecycle · Depends on: NR-123（已 done，本 item 是它的 in-session 對應；獨立於 NR-181）

- Source: `docs/design-spec.md` §9.4（現文 `design-spec.md:731`）；`docs/work-items/NR-123-rebuild-join-bounded-wait.md`（決策與安全論證範本）
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 報告 I-3；codex 報告 H1）
- Priority: **HIGH**（面板凍結；換代路徑每個 Ctrl+R／設定套用都會走）

## Why

`RebuildPipeline::Start()`（`src/app_host/rebuild_pipeline.cpp:96-97`）的第一行是 `Shutdown()`——無參數版預設 `INFINITE`。`Start()` 全部從 UI thread 進來：Ctrl+R 與 tray Refresh（`kRefreshMessage`）、設定套用（`kSettingsMessage`）、ShowPanel 的 AppsFolder on-demand、冷啟動首輪 rebuild、launch 失敗 refresh。

`cancel_` 會被設起、三個列舉器都會檢查它，所以多數情況下 join 很快；但 cancel 旗標不能中斷**已經進去**的 Shell 呼叫（`IShellLinkW::Resolve` 掛在斷線網路磁碟、AppsFolder `BindToObject` 遇壞掉的 shell extension，可卡數秒到無限）。期間 UI thread 停在 window proc：面板不重繪、不吃鍵盤、hotkey 訊息堆積。這與 §FR-009「UI thread 不得等待 Shell」與 §9.4「等待有界」直接衝突。

NR-123 已替**關機**路徑做了有界等待（`kJoinTimeoutMs = 5000`），但 in-session 換代路徑被漏掉，且 header（`rebuild_pipeline.h:80-85` 一帶）把「Start() 用 INFINITE、永不 detach」寫成刻意設計。

**附帶 bug（Claude 報告）**：`Shutdown()` 的 detach 分支（NR-123 新增）在逾時後**不回存 `cancel_`**，它停留在 `true`。目前 detach 只在 process 退出時走到，沒人注意；但若本 item 讓 `Start()` 也走 bounded wait，detach 分支將在普通換代路徑被觸發——下一次 rebuild 的新 worker 一開工就看到 `cancel_ == true` 自我取消，所有來源立即失敗。因此本 item **必須**把 `cancel_` 換成 per-generation 旗標，兩件事一次解決。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4（現文 `design-spec.md:731`）：

> 關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑。等待有界，超時即繼續退出。

`docs/design-spec.md` §FR-008（現文 `design-spec.md:411`）：

> `Ctrl+R` 強制完整重建所有來源；成功啟動 App 不觸發刷新。

## Files to read and trace first

- `src/app_host/rebuild_pipeline.{h,cpp}` — `Start()`（:96）、`Shutdown()`、`cancel_` 的宣告與所有讀取點、`kJoinTimeoutMs`、`BeginGeneration`／generation 檢查（`IsActiveGenerationSource`）。
- `src/app_host/main.cpp` — `StartRebuild`（:1380）與全部呼叫點、WM_DESTROY 的 bounded 關機路徑（NR-123 形狀）。
- `docs/work-items/NR-123-rebuild-join-bounded-wait.md` — bounded-wait 形狀、安全論證、detach 分支的實作細節。

## Scope

1. `Start()` 改用 bounded shutdown：逾時值沿用既有 `kJoinTimeoutMs`（或同值新常數）；逾時後沿用 NR-123 的 detach 語意（先 `detach()` 再 `clear()`，不可直接 clear joinable thread）。
2. **`cancel_` 改為 per-generation 旗標**：`std::shared_ptr<std::atomic<bool>>`（或等價），`Start()` 建立新 generation 時換新旗標，舊 worker 繼續看舊旗標；detach 的舊 worker 執行完畢後 post 舊 generation 的結果會被既有 generation 檢查擋掉。**這是本 item 不可分割的一部分**（否則 detach 分支毒害下一輪 rebuild）。
3. 更新 `rebuild_pipeline.h` 中宣稱「Start() 用 INFINITE、永不 detach」的註解，改寫為 bounded-wait 語意（codex 報告指出該註解是現況的唯一契約紀錄）。
4. 測試：沿用 NR-123 的 sanity grep＋既有 `nimblerun_lifecycle_check`（換代 bounded wait 依賴真實 OS thread handle，無法抽純函式；hang 注入不可自動化）。若 per-generation 旗標可抽純函式（例如「給定新舊旗標，新 worker 讀到 false、舊 worker 讀到 true」）則加一個 focused 測試。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-182 列。

## Non-goals

- 不中斷或取消已卡住的 Shell 呼叫本體；不用 `TerminateThread`。
- 不修改 WM_DESTROY 的既有有界關機路徑（NR-123 已 done）。
- 不改 debounce／`AcceptRebuildStart` 節流（那是 NR-183）。
- 不重開 NR-123 的決策（逾時值、detach 形狀、安全論證沿用）。

## Acceptance

- `Start()` 對舊 worker 的等待有界（逾時即 detach 繼續）；正常暖機換代行為與現況等價。
- 新 generation 的 worker 永遠讀到 `cancel_ == false`；被 detach 的舊 worker 讀到自己的舊旗標，其遲到結果被 generation 檢查丟棄。
- header 註解與實作一致；`TerminateThread` 零命中。
- Release build 無 error／新增 warning；CTest 全綠（含 `nimblerun_lifecycle_check`）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "cancel_|kJoinTimeoutMs|Shutdown\(|TerminateThread" src/app_host/rebuild_pipeline.*
```

驗證：build 無 error／新增 warning；CTest 全 Passed；`cancel_` 為 per-generation 旗標且新 generation 重新建旗標；`TerminateThread` 零命中；bounded wait 只在 `Start()`／`Shutdown()` 內。

## 交接區

（實作者填寫：bounded-wait 形狀與逾時值、per-generation 旗標的型別與生命週期、detach 後新 worker 不受舊旗標影響的證據、header 註解 diff、build／CTest 證據）
