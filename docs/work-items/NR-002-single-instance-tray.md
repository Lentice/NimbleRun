# NR-002 — Single instance and tray lifecycle

- Status: `planned`
- Phase: 1
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-001、§4.10、§9.3–9.4

## Goal

讓同一使用者工作階段只有一個 NimbleRun，且第二次執行能通知既有程序顯示面板後退出；隱藏面板不等於結束程序。

## Scope

- 使用目前使用者範圍的 single-instance 訊號。
- 建立不持有 Shell COM pointer 的 tray lifecycle。
- 提供 Open、Refresh、Settings、About、Exit 的命令入口。

## Non-goals

- 不實作完整 Settings page。
- 不處理 catalog refresh、hotkey registration 或圖示載入。

## Acceptance

- 第二個程序不建立第二個常駐 instance。
- 第二次執行能讓第一個 instance 收到 show request。
- Exit 才結束程序；hide／close panel 後 process 仍可存活。
- tray 命令可被 dispatch 到正確的主視窗訊息。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

另加 focused lifecycle check：啟動第一個程序、確認唯一 process／window，再啟動第二個程序，確認第二個退出且第一個仍存活；測試結束必須終止測試程序。
