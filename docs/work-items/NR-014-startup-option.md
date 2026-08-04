# NR-014 — Startup option

- Status: `planned`
- Phase: 4
- Depends on: NR-004、NR-013
- Source: `docs/design-spec.md` §FR-011、AC-001、AC-008

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
