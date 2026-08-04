# NR-008 — Shell launch adapter

- Status: `planned`
- Phase: 1
- Depends on: NR-005、NR-006、NR-007
- Source: `docs/design-spec.md` §FR-009、§NFR-004、AC-004、AC-006

## Goal

用 Windows Shell API 啟動 Catalog 中已解析的 Win32、捷徑與封裝 App。

## Scope

- `ShellExecuteExW` 或 Shell item verb。
- 只接受 Catalog entry 的 launch identity。
- 回傳成功／失敗與 HRESULT／Win32 error；成功交由 caller 更新 usage。
- 正確關閉必要的 process handle。

## Non-goals

- 不拼接搜尋輸入成 arbitrary command line。
- 不提供 command line、URI action、admin launch 或 plugin action。

## Acceptance

- valid Catalog value 可送出 Shell launch request。
- invalid／未解析 entry 被拒絕，不執行任意輸入。
- launch failure 不會讓 caller crash，且可保留面板顯示。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R shell_launch --output-on-failure
```

Agent 只需檢查 API return、error path 與 handle cleanup；可啟動的測試程序由 helper 執行後終止，不要求控制目標 App。
