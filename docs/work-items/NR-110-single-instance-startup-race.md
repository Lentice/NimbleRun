# NR-110 — Single-instance wake-up 不得有 HWND 建立前競態

Phase 1 · Process lifecycle

- Source: `docs/design-spec.md` §FR-001、§9.3
- Origin: 2026-08-09 全 repo 稽核；追蹤 `wWinMain` mutex ownership 到 `CreateWindowExW` 的啟動時序
- Priority: MEDIUM（並行啟動的第二次執行可能無聲退出，第一個 instance 不會顯示面板）

## Why

`src/app_host/main.cpp::wWinMain` 目前先 `CreateMutexW(..., TRUE, kInstanceMutex)`，
接著在 `ERROR_ALREADY_EXISTS` 分支用 `FindWindowW(kWindowClass, nullptr)` 找既有 HWND
並 post `g_show_panel_message`。但第一個 instance 取得 mutex 後，仍要先完成
`CoInitializeEx`、`RegisterMainWindow`、`CreateWindowExW`；在這段窗口尚不存在的時間，
第二個 instance 會：

1. 看到 mutex 已存在；
2. `FindWindowW` 得到 null；
3. 關閉自己的 mutex handle 並 exit 0；
4. 第一個 instance 之後建立 hidden window，但已沒有 pending show request。

這違反 NR-002 已完成項目所要求的「第二次執行能讓第一個 instance 收到 show request」。
本 item 是新的啟動時序 evidence，不改 session-scoped mutex 或既有 registered message。

## Decisions already made — do not reopen

1. 維持目前使用者／session 範圍 mutex；不改成 global cross-session mutex。
2. 維持已註冊 window message 或同等的 named startup rendezvous；不啟動第二個 resident process。
3. 交握必須有界且可在第一個 window 建立後消費；不可無限等待或新增背景 polling。
4. 正常已建立 HWND 的 second-launch 路徑與 tray lifecycle 不變。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-001：

> 第二次執行時應通知既有實例顯示面板，然後立即退出。

> 使用目前使用者範圍的命名 Mutex 搭配命名 Event 或已註冊 Window Message。

`AGENTS.md`：

> Do not push branches, publish releases, or modify production systems without explicit approval.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

- `src/app_host/main.cpp` — `wWinMain`、`CreateMutexW`、`FindWindowW`、
  `RegisterWindowMessageW`、`RegisterMainWindow`、`CreateWindowExW`、show-message handler。
- `tests/integration/lifecycle_check.ps1` — current second-launch test and where it waits for HWND。
- `tests/CMakeLists.txt` — lifecycle CTest registration。
- `docs/work-items/NR-002-single-instance-tray.md` — existing mutex/message/tray decisions。

## Scope

1. Close the interval between mutex ownership and HWND creation so a second launch during that
   interval either wakes the first later or waits for a bounded startup rendezvous before exit。
2. Add a deterministic lifecycle test seam or launch gate that starts the second process before
   the first `CreateWindowExW` completes, then asserts one process and one delivered show request。
3. Review failure cleanup for mutex/event/message handles on normal startup failure and shutdown。

## Non-goals

- 不改 tray commands、hotkey registration、catalog startup 或 panel layout。
- 不引入 service、named pipe、network、global hook 或常駐 helper process。
- 不讓第二個 process 長時間常駐；它仍應在 request handoff 後退出。

## Acceptance

1. A second launch at any point after the first process owns the mutex results in the first
   instance showing the panel once the HWND is available, including the pre-HWND window。
2. The second process exits cleanly, no duplicate resident instance appears, and normal launch,
   tray Exit and existing lifecycle checks remain green。
3. No unbounded wait, busy loop or cross-session mutex is introduced。
4. Release build has no new warnings and the race test is deterministic rather than relying on
   a timing-only sleep.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R lifecycle --output-on-failure
```

```powershell
Select-String -Path src/app_host/main.cpp,tests/integration/lifecycle_check.ps1 -Pattern 'CreateMutexW|FindWindowW|RegisterWindowMessageW|CreateWindowExW|PostMessageW'
git diff --name-only
# expect: lifecycle handoff 與 focused test；不改 catalog/icon modules。
```

## Handoff

實作者需記錄 startup rendezvous 時序、second-launch ownership、race test 結果、normal
lifecycle 結果、build／CTest 與任何 OS version 差異。

