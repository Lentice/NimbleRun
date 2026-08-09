# NR-095 — AppsFolder 從未成功列舉時，下一次顯示面板應立即重試

Phase 2 · Depends on: NR-006, NR-011, NR-063, NR-081

- Source: `docs/design-spec.md` §FR-008（AppsFolder on-demand 與失敗保留語意）／§NFR-003
- Origin: 2026-08-09 全 repo 稽核（`CatalogRefreshCoordinator` 的初始 staleness 狀態）
- Priority: HIGH（首次 AppsFolder 失敗可能讓 packaged apps 在前 10 分鐘內永遠不再出現）

## Why

`CatalogRefreshCoordinator::last_appsfolder_success_ms_` 以 `0` 代表「尚未有成功列舉」；
`ShouldRefreshAppsFolder` 卻把它當成「在 monotonic time 0 曾成功」。因此，若程式在系統
啟動後 10 分鐘內第一次掃描 AppsFolder 失敗，`ApplySourceFailure` 會正確保留舊結果，
但接下來面板在 10 分鐘內再次顯示時，`now_ms - 0 < kAppsFolderStaleMs`，不會觸發重試。
使用者只能等到系統 uptime 超過 10 分鐘或手動 Ctrl+R 才可能恢復 packaged apps。

這也使 NR-081 的「失敗後下一次 ShowPanel 再試」決策在低 uptime 啟動路徑失效；問題不在
generation 取代，而在「沒有成功」與「在 t=0 成功」共用一個值。

## Decisions already made — do not reopen

1. 修在純 `CatalogRefreshCoordinator` 狀態，不在 `ShowPanel` 加第二個例外條件，也不加
   timer 或背景輪詢。
2. 「從未成功」在下一次面板顯示時視為 due；一旦有成功列舉，才開始使用 10 分鐘時鐘。
3. 保留 NR-081 的 running-generation guard：rebuild 進行中仍不得啟動 on-demand
   AppsFolder generation。
4. **覆寫 NR-081 Decisions §3 的「不 baseline `last_appsfolder_success_ms_`」**：新證據
   顯示 baseline `0` 會把 no-success 與 success-at-zero 混淆。覆寫只限於新增明確的
   no-success 狀態；不把啟動時間當成成功時間，也不改失敗不記成功的語意。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> AppsFolder 不做背景輪詢；當面板被叫出且距上次成功列舉超過 10 分鐘時，在背景低優先序重新列舉。

> 單一來源失敗時保留該來源舊結果及其他來源的新結果。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/catalog_refresh.h` — `last_appsfolder_success_ms_` 與 coordinator API。
- `src/catalog/catalog_refresh.cpp` — `ShouldRefreshAppsFolder`、`RecordAppsFolderSuccess`、
  `IsRebuildInProgress`。
- `src/app_host/main.cpp` — `ShowPanel` 呼叫點與 `kRebuildDoneMessage` 成功／失敗分流。
- `tests/unit/catalog_refresh_test.cpp` — `TestAppsFolderStaleness`、
  `TestAppsFolderStalenessSkipsRunningRebuild`。
- `docs/work-items/NR-081-appsfolder-on-demand-supersedes-rebuild.md` — 被覆寫的既有決策。

## Scope

1. 用最小的純值狀態區分「尚未成功」與「已有成功 timestamp」；`ShouldRefreshAppsFolder`
   先保留 NR-081 的 running-generation guard，再依此狀態判定。
2. `RecordAppsFolderSuccess` 必須同時把狀態標為已成功並保存 `now_ms`；失敗路徑不得改動
   該狀態。
3. 在既有 coordinator test 新增：未成功時低 uptime due、失敗後仍 due、t=0 的成功在
   10 分鐘內不 due、超過 10 分鐘 due；既有 NR-081 running-generation case 原樣保留。

## Non-goals

- 不新增 timer、worker retry、sleep、背景輪詢或 UI 提示。
- 不修改 `ShowPanel`、generation merge、source failure 保留舊結果的語意。
- 不把 AppsFolder 成功時間改成 wall-clock，也不改 10 分鐘常數。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. coordinator test 證明「未成功」在 `now_ms=1000` 即 due；失敗不會把它變成 not due。
3. 成功時間為 `0` 時，`now_ms=1000` 不 due、`kAppsFolderStaleMs + 1` due。
4. rebuild 進行中即使 stale 也不 due；generation 完成後恢復上述判定。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_catalog_refresh_test --output-on-failure
```

```powershell
Select-String -Path src/catalog/catalog_refresh.cpp -Pattern 'ShouldRefreshAppsFolder|RecordAppsFolderSuccess|IsRebuildInProgress'
Select-String -Path src/app_host/main.cpp -Pattern 'ShouldRefreshAppsFolder'
# host 呼叫點仍只有 ShowPanel 一處；新狀態只存在 coordinator。
git diff --name-only
# expect: 僅 catalog coordinator、其 unit test、本 item 與 tracker 文件。
```

## 交接區

- **實作表示**：新增 `bool appsfolder_has_success_ = false;` 成員（`catalog_refresh.h`），
  `last_appsfolder_success_ms_` 保持 `0`。未選擇 `kNever` sentinel，因為 `kNever` 只存在於
  `.cpp` 的 anonymous namespace，member 在 header，跨檔共用會比新增一個 bool 更動更多。
  `ShouldRefreshAppsFolder`：保留 `IsRebuildInProgress()` guard 在最前；接著
  `if (!appsfolder_has_success_) return true;`（從未成功 → due），最後才做
  `now_ms - last_appsfolder_success_ms_ >= kAppsFolderStaleMs`。`RecordAppsFolderSuccess`
  同時存 `now_ms` 並設 `appsfolder_has_success_ = true`；`ApplySourceFailure` 完全不碰
  成功狀態。`RecordAppsFolderSuccess(0)` 是真實的「t=0 成功」，既有
  `TestAppsFolderStaleness` 原樣通過。
- **新增測試**（`tests/unit/catalog_refresh_test.cpp`，均在 `wmain` 列表註冊）：
  - `TestAppsFolderNeverSucceededIsDueAtLowUptime` — scope (a)：未成功時 `now_ms=1000` 即 due。
  - `TestAppsFolderFailureKeepsDue` — scope (b)：AppsFolder-only generation 走
    `ApplySourceFailure` 後，`now_ms=1000` 仍 due。
  - scope (c) 由既有的 `TestAppsFolderStaleness` 原樣涵蓋（`RecordAppsFolderSuccess(0)` →
    `now_ms=1000` 不 due、`kAppsFolderStaleMs+1` due），未新增重複案例。
  - scope (d) 既有 `TestAppsFolderStalenessSkipsRunningRebuild` 未改動，通過。
- **建置／CTest**（Release x64，LLVM-MinGW+Ninja）：build 成功，**無新增 warning**；
  `ctest --test-dir build --output-on-failure` **24/24 通過**；
  `ctest --test-dir build -R nimblerun_catalog_refresh_test --output-on-failure` **1/1 通過**。
- **Sanity grep**：
  - `Select-String -Path src/app_host/main.cpp -Pattern 'ShouldRefreshAppsFolder'` →
    僅 `main.cpp:1994` 一處（ShowPanel）。`RecordAppsFolderSuccess` 僅 `main.cpp:2577`
    成功臂一處。
  - `git diff --name-only` → `src/catalog/catalog_refresh.cpp`、
    `src/catalog/catalog_refresh.h`、`tests/unit/catalog_refresh_test.cpp`，另含
    `docs/work-items.md`（**非本 item 所改**：controller 在本次執行前已加入 NR-095～
    NR-104 的 tracker 列與決策紀錄，屬 pre-existing working-tree 變更，未動它）。
- **偏差**：`main.cpp` 未修改；`docs/work-items.md` 未修改（其 dirty 來自 controller）。
  未加「手動重試」觀察——本修正是純 coordinator 狀態，agent 檢查即涵蓋判定邏輯。

