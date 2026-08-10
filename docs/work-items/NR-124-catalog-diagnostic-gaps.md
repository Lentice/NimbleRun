# NR-124 — §11 診斷缺口：損壞捷徑／缺失資料夾靜默略過，dedup 歧義計數被丟棄

Phase 3 · Catalog diagnostics · Depends on: NR-054, NR-063, NR-090, NR-091, NR-092（皆 done）

- Source: `docs/design-spec.md` §11（錯誤表）、§FR-007（記錄診斷資訊）、§FR-014（診斷格式）
- Origin: 2026-08-10 第十三次全 repo 稽核（spec 符合度軸）；主 Agent 已 grep 驗證枚舉器路徑
  無任何 `DiagnosticLog` 呼叫
- Priority: **MEDIUM**（spec 明文「記錄錯誤，繼續掃描」從未實作；故障排除時完全沒有線索）

## Why

spec §11 錯誤表明列三類「記錄」行為，實作全部靜默：

1. **單一捷徑損壞**（spec：「記錄錯誤，繼續掃描」）——`start_menu_catalog.cpp:138-139` 對損壞的
   `.lnk` 直接 `return;`，無任何計數或 log。
2. **自訂資料夾不存在／無權限**（spec：「保留設定、略過該來源並記錄一次」）——
   `user_folder_catalog.cpp:111-113` 靜默略過，`source_ok` 仍回 true（NR-063 的「資料夾不存在→空」
   語意正確，但「記錄一次」沒做）。
3. **去重歧義**（`UnjudgeableNameCollision` 的診斷目的）——`DeduplicateCatalog` 精心計算的
   `ambiguous_kept`／`removed_duplicates`（`dedup.cpp:98-111`）在 `catalog_refresh.cpp:232`
   `SetSnapshot(DeduplicateCatalog(merged).entries)` 直接丟棄，全 repo 除測試外零消費者。

grep 確認：三個枚舉器與 rebuild worker 路徑沒有任何 `DiagnosticLog` 呼叫。結果：使用者回報
「某個 App 不見了」時，日誌一片空白；`dedup` 的歧義計數是現成但被丟棄的診斷資產。

## Decisions already made — do not reopen

1. **log 只在 UI 執行緒的 generation-complete handler 寫**，不把 `DiagnosticLog*` 傳進枚舉器：
   計數由枚舉器帶回（`RebuildResult` 加計數欄位），`kRebuildDoneMessage` 完成處理時寫 sanitized
   一行式。理由：`DiagnosticLog::Write` 雖已執行緒安全（NR-054），但把日誌出口集中在 UI 執行緒
   維持「背景 worker 不碰宿主全域」的既有契約（NR-049）。
2. 沿用 FR-014 格式：階段名＋來源型別＋計數，**不帶路徑**（Sanitize 已有）；每 generation 至多
   每來源一行。
3. 不改枚舉器的 `source_ok` 語意（NR-063/NR-090/091/092 已釘：失敗保留舊結果、資料夾不存在→空）。
   本 item 只補「記錄」，不改變任何 catalog 行為。
4. 三類計數各自獨立；`removed_duplicates` 屬正常去重（非錯誤），只在計數非零時寫一行
   （不寫「0」的噪音行）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11（錯誤表）：

> 單一捷徑損壞 → 記錄錯誤，繼續掃描。

> 自訂資料夾不存在／無權限 → 保留設定、略過該來源並記錄一次。

`docs/design-spec.md` §FR-014：

> 診斷記錄不得包含完整路徑或其他使用者識別資訊；每行以階段名與事件名開頭。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/start_menu_catalog.cpp` — 損壞捷徑 `return;`（`:138-139`）與列舉迴圈結構。
- `src/catalog/user_folder_catalog.cpp` — 略過分支（`:111-113`）。
- `src/catalog/catalog_refresh.{h,cpp}` — `RebuildResult` 欄位、`SetSnapshot`（`:232`）、
  `ApplySourceResult`。
- `src/app_host/main.cpp` — `kRebuildDoneMessage` 完成處理（`g_diag` 寫入點）。
- `src/diagnostics/diagnostic_log.{h,cpp}` — Write 簽名與 Sanitize。
- `docs/work-items/NR-063-source-failure-reaches-refresh.md` — source_ok 語意的先例。

## Scope

1. `RebuildResult`（或 `DedupResult` 消費點）新增計數：損壞捷徑數（Start Menu）、略過資料夾數
   （UserFolder）、`ambiguous_kept`／`removed_duplicates`（由 `DedupResult` 帶出而非丟棄）。
2. 枚舉器在既有略過點累計計數（一行 `++`，不改控制流）；coordinator 把 `DedupResult` 計數
   併入 generation 結果。
3. `kRebuildDoneMessage` 完成處理新增至多三行 sanitized 診斷（計數非零才寫），格式照 FR-014。
4. 新增 focused 測試：`catalog_refresh_test` 或 `diagnostic_log_test` 驗證「非零計數 → 產生
   對應診斷行、零計數 → 無行」；損壞捷徑計數在 `start_menu_catalog_test` 的既有 fixture
   上可觀察（如既有 corrupt-link fixture 數 1）。

## Non-goals

- 不改變任何 catalog 行為／`source_ok` 語意／失敗保留舊結果的路徑。
- 不做使用者可見通知（本 item 只有日誌；NR-058 的 balloon 語意不擴充）。
- 不新增診斷檔、不 bump schema、不把 log 寫進 store 檔。

## Acceptance

1. 有損壞捷徑／略過資料夾／名稱歧義的 catalog 完成一次 generation 後，日誌出現對應
   sanitized 行（無路徑、格式符合 FR-014）。
2. 乾淨 catalog：完成 generation 後無新增診斷行（零噪音）。
3. 既有 catalog 行為與測試全部不變；Release build 無新增 warning；完整 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|start_menu_catalog|diagnostic_log|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "DiagnosticLog|g_diag" src/catalog src/app_host/main.cpp
git diff --name-only
# expect: 枚舉器零直接 diag 呼叫（計數帶回）；寫入只發生在完成 handler。
```

## Handoff

實作者需記錄計數欄位形狀、三條診斷行文字、零噪音行為、測試案例與 build／CTest 證據。

### 交接區（2026-08-10，實作完成）

計數欄位形狀（目前工作樹為準，dedup.cpp 已是 NR-121 的 name-keyed 分桶、計數語意不變）：

- `catalog_refresh.h` 新增純值 `struct GenerationDiagnostics`：`corrupt_links`（StartMenu 損壞 `.lnk`）、
  `skipped_directories`（UserFolder 無法開啟的資料夾）、`ambiguous_kept`／`removed_duplicates`（由
  `DedupResult` 帶出，不再在 `RebuildMerged` 丟棄）。coordinator 私有成員
  `generation_diagnostics_`，`BeginGeneration` 歸零、`ApplySourceResult` 把枚舉器計數累加進來
  （`ApplySourceFailure` 不累加，所以失敗／取消的 walk 不會把部分計數當完整結果報）、`RebuildMerged`
  覆寫 dedup 兩個計數；公開存取器 `LastGenerationDiagnostics()`。`ApplySourceResult` 增加第四參數
  `const GenerationDiagnostics& diagnostics = {}`（既有測試呼叫端零改動）。
- 枚舉器結果結構各加一欄：`StartMenuEnumerateResult::corrupt_links`、`UserFolderEnumerateResult::
  skipped_directories`；`EnumerateProgramsDirectory` 增加可選 out-param `std::size_t* corrupt_links`。
  枚舉器只在既有略過點 `++`（start_menu_catalog.cpp 的 `!link.loadable` return、user_folder_catalog.cpp
  的 `FindFirstFileW == INVALID_HANDLE_VALUE`），不改任何控制流與 `source_ok` 語意（NR-063/090/091/092 不變）。
- `main.cpp` 的 `RebuildResult` 增加 `nimblerun::GenerationDiagnostics diagnostics`；worker 把枚舉器計數
  拷入（StartMenu→corrupt_links、UserFolder→skipped_directories），`kRebuildDoneMessage` handler 把
  `result->diagnostics` 傳給 `ApplySourceResult`。**寫入只在 `OnGenerationCompleteRefresh`（UI 執行緒、
  所有完成路徑的單一 choke point）**：`if (g_diag && g_refresh)` 對 `RebuildDiagnosticLines(
  LastGenerationDiagnostics())` 逐行 `Write(L"rebuild", line)`。枚舉器零直接 DiagnosticLog 呼叫（sanity
  grep 驗證，見下）。

三條診斷行文字（FR-014 格式：stage `rebuild`＋detail＝來源型別＋計數，不帶路徑）：

```
rebuild\tstartmenu corrupt-links N
rebuild\tuserfolder skipped-directories N
rebuild\tdedup ambiguous N removed M
```

- 每類計數獨立、計數非零才寫該行；dedup 兩計數共享一行（「至多三行」），只要任一非零就寫整行。
- `removed_duplicates` 屬正常去重，計數非零照寫（不含「0」的噪音行）。

零噪音行為：`GenerationDiagnostics{}`（全零）時 `RebuildDiagnosticLines` 回傳空 vector，
`OnGenerationCompleteRefresh` 不寫任何行。

測試案例：

- `catalog_refresh_test.cpp` 新增 `TestRebuildDiagnosticLines`（全零→空；各自非零→單行；四計數全非零
  →恰三行）、`TestGenerationDiagnosticsAggregation`（per-source 計數隨 ApplySourceResult 累加、失敗
  generation 不貢獻計數）、`TestGenerationDiagnosticsIncludesDedupCounts`（同 stable_id→removed=1；
  同名異 id 跨來源→ambiguous=2，兩個 peer 都被保留）。
- `start_menu_catalog_test.cpp` 既有 fixture 增加斷言：`EnumerateProgramsDirectory(..., &corrupt_links)`
  對 1 個 Broken.lnk → `corrupt_links == 1`（既有 corrupt-link fixture，未新增 fixture）。

build／CTest 證據（自己的 build-wi-nr124 目錄，Release，LLVM-MinGW）：

```
cmake -S . -B build-wi-nr124 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release   # OK
cmake --build build-wi-nr124        # 94/94 無警告
ctest --test-dir build-wi-nr124 -R "catalog_refresh|start_menu_catalog|diagnostic_log|lifecycle" --output-on-failure
  # 4/4 通過（start_menu 32.97s、catalog_refresh 0.83s、diagnostic_log 7.49s、lifecycle 4.20s）
ctest --test-dir build-wi-nr124 --output-on-failure   # 26/26 通過
```

sanity grep：`rg -n "DiagnosticLog|g_diag" src/catalog src/app_host/main.cpp` —— `src/catalog/` 零命中；
`main.cpp` 的寫入點新增處在 `OnGenerationCompleteRefresh`（`g_diag->Write(L"rebuild", line)`），餘為既有
呼叫。未執行任何 git 命令。
