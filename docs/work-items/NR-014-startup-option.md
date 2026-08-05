# NR-014 — Startup option

- Status: `done`
- Phase: 4
- Depends on: NR-004、NR-013
- Source: `docs/design-spec.md` §FR-012、AC-001、AC-008

## Goal

讓使用者可選擇目前使用者範圍的 Windows startup 行為，預設關閉且不需管理員權限。

## Scope

- 以一種集中封裝的 Startup Known Folder shortcut 或 HKCU Run 實作。
- enable／disable、移動 EXE 後重新建立。
- 不把 startup option 與 global hotkey registration 混成一個 rollback。

## Non-goals

- 不寫 HKLM、不建立 service、driver 或 scheduled task。
- 不加入 silent auto-start default。

## Acceptance

- fresh settings 不建立 startup entry。
- enable 只影響目前使用者。
- disable／reset 可移除 NimbleRun 自己建立的 entry，不碰其他 entry。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R startup --output-on-failure
```

測試使用隔離的 HKCU／temporary startup fixture；每次測試結束清理自己的 entry，不操作其他使用者資料。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-012、AC-001、AC-008、`docs/work-items.md`、本文件；trace `src/settings/settings_store.h`（Settings.auto_start）、`src/app_host/settings_dialog.cpp`（NR-013 設定頁如何處理 hotkey rollback、AddRoot 等）、`docs/work-items/NR-013-settings-ui.md`。實作 Startup option：以一種集中封裝的 Startup Known Folder shortcut 或 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 實作（擇一並集中封裝）；enable／disable、移動 EXE 後重新建立；不把 startup option 與 global hotkey registration 混成一個 rollback；不寫 HKLM、不建 service/driver/scheduled task、不加 silent auto-start default。Acceptance：fresh settings 不建立 entry；enable 只影響目前使用者；disable／reset 只移除自己建立的 entry、不碰其他 entry。以隔離的 HKCU／temporary startup fixture 測試，每次結束清理自己的 entry。接上 NR-013 設定頁（auto-start 開關）。回報修改檔案、測試命令、結果與未完成事項。
- Result: done（2026-08-05）。選擇 HKCU Run value（非 Startup folder shortcut）集中封裝於 `src/settings/startup_option.{h,cpp}`：`StartupOptionRegistry{base=HKCU, subkey}` injectable seam（測試指向 `HKCU\Software\NimbleRunTest\<pid>`，絕不碰真實 Run key，結束清理）；全模組只透過 `base` 存取、無 HKLM 路徑。`GetStartupStatus`（Disabled/Enabled/EnabledMoved/UnknownError）、`SetStartupEnabled(bool)`（REG_SZ "NimbleRun" value，enable 指向 `GetModuleFileNameW` 目前路徑並可重寫修復移動 EXE；disable 只 `RegDeleteValueW` 刪該 value、不刪 key、其他 entry 保留、absent 為 no-op）。`SettingsEditor::SetAutoStart(bool)` 依既有 setter 模式；設定頁 Launcher group 新增 "Launch at startup" checkbox（`IDC_AUTO_START`）。Apply 與 hotkey rollback 完全獨立：IDOK 先寫 Run entry，persist 成功才保留，Apply 失敗以純 registry 呼叫回滾；auto_start=true 時每次 OK 固定重寫以修復 moved EXE；fresh 不建立 entry。新測試 `nimblerun_startup_option_test`（10 case）＋`ctest -R startup` 1/1、全套件 16/16 通過。未完成事項：無。
