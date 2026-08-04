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
