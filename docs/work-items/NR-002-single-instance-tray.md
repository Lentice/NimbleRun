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

## 交接區

- Start: 2026-08-04
- Subagent scope: 以使用者範圍 single-instance 訊號實作單實例；tray lifecycle 不持有 Shell COM pointer；提供 Open、Refresh、Settings、About、Exit 命令入口並 dispatch 到主視窗訊息；不處理完整 Settings page、catalog refresh、hotkey registration 或 icon 載入。
- Result: done
- Agent: general subagent
- 修改檔案：`src/app_host/main.cpp`（single-instance wake message、tray lifecycle、tray command dispatch）、`tests/integration/lifecycle_check.ps1`（新增，focused lifecycle check）、`tests/CMakeLists.txt`（註冊 `nimblerun_lifecycle_check` 至 CTest）。
- 設計：session-scoped named mutex `Local\NimbleRun.SingleInstance`；wake 用 `RegisterWindowMessageW(L"NimbleRun.ShowPanel")`，第二個程序 FindWindow＋PostMessage 後以 exit 0 退出。tray 用 `Shell_NotifyIconW`，左鍵 show panel、右鍵 `TrackPopupMenu`（Open/Refresh Apps/Settings/About/Exit），各命令 dispatch 到主視窗 `WM_APP+n` 訊息；Refresh/Settings/About 目前為 no-op placeholder（NR-011/NR-013 認領），Exit 走 `DestroyWindow→WM_DESTROY`（移除 tray、unregister hotkey、PostQuitMessage）。hide／close panel 僅 SW_HIDE，不結束程序。
- 行為變更（spec 一致）：`RegisterHotKey` 失敗改為 non-fatal（`main.cpp`），tray 常駐程序繼續存活，符合 §9.3 step 6；衝突提醒 UX 屬 NR-003。
- Agent checks（2026-08-04）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build --output-on-failure` → exit 0，2/2 passed（`nimblerun_search_test`、`nimblerun_lifecycle_check`）
  - lifecycle check：第一程序隱藏啟動、唯一 window 屬正確 PID；第二程序 exit 0 且第一程序存活並顯示；tray Exit message 讓第一程序乾淨退出；結束後無殘留 NimbleRun process。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\NimbleRun.exe`。
