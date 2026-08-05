# NR-003 — Normal global hotkey and conflict handling

- Status: `planned`
- Phase: 1
- Depends on: NR-001、NR-002
- Source: `docs/design-spec.md` §4.1、§4.7、§FR-002、§11、§13 AC-001

## Goal

以 `RegisterHotKey` 提供一般全域快捷鍵；快捷鍵衝突或被 Windows 保留時拒絕，不安裝低階 keyboard hook，也不攔截原生輸入。

## Scope

- 預設 `Alt+Space` 與 `MOD_NOREPEAT`。
- 設定變更採先註冊新鍵、成功後才解除舊鍵。
- 首次失敗保留 tray 並發出一次非阻擋提醒；已有舊鍵時保留舊鍵。
- 記錄 Win32 error，不記錄一般鍵盤輸入。

## Non-goals

- 不覆寫 `Win+R` 或任何 OS shortcut。
- 不使用 `WH_KEYBOARD_LL`、keyboard logger、busy retry 或自動靜默 fallback。

## Acceptance

- 有效 hotkey 會送出 show／hide request。
- 衝突鍵被拒絕，原生按鍵行為不被攔截。
- 重設或換鍵失敗不會讓既有有效 hotkey 消失。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

focused check 必須以 Win32 registration result／error code 驗證成功、衝突、unregister 與 rollback；不要求 Agent 模擬或操作 UI。

## 交接區

- Start: 2026-08-04
- Subagent scope: 以 `RegisterHotKey` + `MOD_NOREPEAT` 提供預設 `Alt+Space` 全域快捷鍵；衝突或 Windows 保留時拒絕並保留 tray 與舊鍵；設定變更採先註冊新鍵、成功後解除舊鍵；不覆寫 OS shortcut、不裝 `WH_KEYBOARD_LL`。
- Result: done
- Agent: general subagent
- 修改檔案：`src/app_host/hotkey.h`＋`src/app_host/hotkey.cpp`（新增，可測試的 `nimblerun::GlobalHotkey`）、`src/app_host/main.cpp`（改用 `g_hotkey.Initialize`；衝突時保留 tray 並發一次性 tray balloon；`WM_HOTKEY` 改用 `ActiveId()`；`WM_DESTROY` 改 `Shutdown()`；記錄 `g_last_hotkey_error`）、`tests/unit/hotkey_registration_test.cpp`（新增）、`CMakeLists.txt`＋`tests/CMakeLists.txt`（新增 `nimblerun_hotkey` lib 與 `nimblerun_hotkey_test`）。
- 設計：`Initialize` 以 `MOD_ALT|MOD_NOREPEAT` + `VK_SPACE` 註冊；失敗回傳 `HotkeyResult{false, error}`（1409 或 OS 保留），無 LL hook／攔截／retry／靜默 fallback。`Swap` 先以未使用 id（`kGlobalHotkeyId`/`kProbeHotkeyId` 輪換）註冊新鍵，成功後才解除舊鍵；失敗時舊鍵原封不動，無需額外 rollback 分支。首次註冊失敗 → tray 先建立，再 `NIM_MODIFY+NIF_INFO` 一次性非阻擋 balloon（英文文案），tray Settings 為持久入口。NR-013 可呼叫 `g_hotkey.Swap(...)` 換鍵。
- Agent checks（2026-08-04）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build --output-on-failure` → exit 0，3/3 passed（`nimblerun_search_test`、`nimblerun_hotkey_test`、`nimblerun_lifecycle_check`）
  - hotkey test 以 Win32 registration result/error code 驗證：success、conflict（1409）、unregister、swap rollback 保留舊鍵、swap success 先註冊後釋放、default Alt+Space 雙合法結果。
  - live probe：啟動 NimbleRun 後外部再 `RegisterHotKey` 相同 Alt+Space → 被 1409 拒絕，證明應用實際持有預設鍵；tray Exit 後 exit 0。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_hotkey_test.exe`。
