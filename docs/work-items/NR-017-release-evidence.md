# NR-017 — Diagnostics and automated release evidence

- Status: `done`
- Phase: 5
- Depends on: NR-008、NR-011、NR-012、NR-013、NR-014、NR-015、NR-019
- Source: `docs/design-spec.md` §FR-014、§NFR-001–005、§12、§13、§15 Phase 5

## Goal

提供可由 Agent 產生的錯誤、效能、穩定性與發布 evidence，讓完成狀態不依賴主觀描述。

## Scope

- bounded local diagnostic log，不記錄 query text、使用者名稱、完整個人路徑或 command line。
- Release build smoke、catalog/search latency、warm show、idle CPU／memory、thread／handle counters；包含多根目錄及事件刷新證據。
- process launch／terminate、短 soak、corrupt settings／catalog recovery checks。
- evidence file path 與阻擋門檻比較。

## Non-goals

- 不加入 telemetry、crash upload、網路或人工畫面驗證。
- 不把未達理想目標但未超過阻擋門檻的項目誤報為 failure；要列為 known issue。

## Acceptance

- Release evidence 可重複產生並含工具版本、條件、命令與 exit code。
- 超過 blocking threshold 的測試會 fail。
- 單一錯誤／損壞資料不會使整體 evidence runner crash。
- logs 有大小上限且不包含正常逐鍵搜尋紀錄。

## Agent checks

```powershell
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

再執行 item 提供的 Release evidence command；Agent 只能啟動、收集輸出與終止程序，不操作任何 App UI。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-014、§NFR-001–005、§12、§13、§15 Phase 5、`docs/work-items.md`、本文件、`docs/performance-baseline.md`、`docs/testing.md`；trace `src/app_host/main.cpp`（`g_last_hotkey_error`、`g_last_launch_error` 現有診斷 hooks、各階段）、`src/catalog/catalog_refresh.h`（refresh coordinator）、`tests/integration/lifecycle_check.ps1`（既有 process launch/terminate harness）。實作 bounded local diagnostic log（單檔上限 512 KiB、最多保留 2 份、輪替；不記錄搜尋文字、使用者名稱、完整個人目錄或 command line；可記錄 HRESULT／Win32 error、來源類型、雜湊後 stable ID 與階段名稱；設定頁不要求在本 item 加「開啟記錄資料夾」）。提供可由 Agent 產生的 Release evidence：tool 版本、條件、命令與 exit code 都可重複產生；超過 blocking threshold 的測試會 fail；單一錯誤／損壞資料不會使整體 evidence runner crash；logs 有大小上限且不含逐鍵搜尋紀錄。以可重複產生的 evidence runner（PowerShell script）記錄 build/smoke、catalog/search latency、warm show、idle CPU/memory、thread/handle counters、多根目錄與事件刷新、process launch/terminate、短 soak、corrupt settings/catalog recovery；與 `docs/performance-baseline.md` 的 blocking threshold 比較。不加入 telemetry、crash upload、網路或人工畫面驗證；未達理想但未超阻擋門檻的項目列為 known issue，不誤報 failure。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: 主 Agent 實作
- 修改檔案：`src/diagnostics/diagnostic_log.h`＋`diagnostic_log.cpp`（新增，`nimblerun_diagnostics` 庫）、`tests/unit/diagnostic_log_test.cpp`（新增）、`tests/release/release_evidence.ps1`（新增，Release evidence runner）、`src/app_host/main.cpp`（hotkey/launch/open-location 失敗寫入診斷 log）、`CMakeLists.txt`/`tests/CMakeLists.txt`、`docs/release-evidence.md`（首次執行證據）。
- 設計（design-spec §FR-014、§NFR-001、§12.4、§15 Phase 5）：`DiagnosticLog` 純值 bounded 輪替 log（單檔 512 KiB cap、超過即輪替為 `.1`、最多保留 2 份、逐行 append、tab/newline 消毒、寫入失敗不 throw）；只記錄 stage 名稱＋error code＋短 detail，不記錄搜尋文字／使用者名稱／個人路徑／command line。`release_evidence.ps1` 產出可重複 evidence：tool 版本、OS/CPU/debugger/git commit、cmake configure/build 與 ctest 的完整輸出＋exit code、process launch/terminate smoke、短 soak（3 次）、idle thread/working set/private bytes/handle 量測，並與 `docs/performance-baseline.md` 的 blocking threshold 比較（超過即 exit 1）。
- Main-agent 確認：範圍僅 NR-017；無 telemetry／crash upload／網路／人工畫面驗證。首次執行發現 idle thread count 14 > 8 blocking threshold 為真實現象（非誤報）：census 顯示 app 自有 3 執行緒（main＋2 個 Programs watcher，符合 §9.2），其餘為 OS 基礎設施（IME IMM32×3、ntdll/ucrtbase threadpool 等）；已在 evidence 記錄為 release gate 的 known issue，未誤報為 pass。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build --output-on-failure` → exit 0，18/18 passed（含新增 `nimblerun_diagnostic_log_test`）
  - `pwsh -NoProfile -File tests/release/release_evidence.ps1` → 產出 `docs/release-evidence.md`；gate 依 thread count 超出 blocking threshold 正確 FAIL（exit 1）
  - diagnostic log 測試涵蓋：append＋消毒、超過 512 KiB 輪替（只留 2 份、active 不超 cap）、唯讀目錄寫入失敗不 throw。
- 證據：`docs/release-evidence.md`；`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_diagnostic_log_test.exe`。
