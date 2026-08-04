# NR-017 — Diagnostics and automated release evidence

- Status: `planned`
- Phase: 5
- Depends on: NR-008、NR-011、NR-012、NR-013、NR-014、NR-015
- Source: `docs/design-spec.md` §FR-013、§NFR-001–005、§12、§13、§15 Phase 5

## Goal

提供可由 Agent 產生的錯誤、效能、穩定性與發布 evidence，讓完成狀態不依賴主觀描述。

## Scope

- bounded local diagnostic log，不記錄 query text、使用者名稱、完整個人路徑或 command line。
- Release build smoke、catalog/search latency、warm show、idle CPU／memory、thread／handle counters。
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
