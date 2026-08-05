# NR-008 — Shell launch adapter

- Status: `done`
- Phase: 1
- Depends on: NR-005、NR-006、NR-007
- Source: `docs/design-spec.md` §FR-010、§NFR-004、AC-004、AC-006

## Goal

用 Windows Shell API 啟動 Catalog 中已解析的 Win32、`.cmd`、`.bat`、捷徑與封裝 App。

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

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-010、§NFR-004、AC-004、AC-006、`docs/work-items.md`、本文件，並 trace `src/catalog/app_entry.h`、`src/catalog/start_menu_catalog.cpp`、`src/catalog/appsfolder_catalog.cpp`、`src/catalog/user_folder_catalog.cpp`（launch_identity 語意）、`src/app_host/main.cpp`（STA COM init）。實作 Shell launch adapter：以 `ShellExecuteExW` 或 Shell item verb 啟動；只接受 Catalog entry 的 launch identity，拒絕任意輸入；回傳成功／失敗與 error code；不取得 process handle，除非明確需要，取得必須關閉；啟動失敗不 crash、可保留面板。不提供 command line／URI action／admin launch／plugin。新增 focused test 驗證 API return、error path 與 handle cleanup，可啟動的測試 helper 執行後即終止。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。新增 `src/launch/shell_launch.{h,cpp}`（`nimblerun_launch` 靜態庫），`LaunchEntry(const AppEntry&, HWND owner = nullptr)` 對 catalog 的 launch_identity 做單次 `ShellExecuteExW`；空 identity 直接拒絕（回傳 `ERROR_INVALID_PARAMETER`），不使用 `SEE_MASK_NOCLOSEPROCESS`（無 process handle），假定 caller 已以 `COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE` 初始化 STA COM（§FR-010）。新增 `tests/unit/shell_launch_test.cpp`，CTest 名稱 `nimblerun_shell_launch_test`；測試驗證空 identity 被拒絕，以及 temp dir 內自終結 `.cmd` fixture（寫 marker 後 `exit`）可被 Shell 啟動並留下 marker。驗證：`ctest -R shell_launch` 通過，全套件 9/9 通過。
