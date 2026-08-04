# NR-004 — Atomic local settings store

- Status: `planned`
- Phase: 1 foundation, extended in Phase 4
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-012、§NFR-003、§10

## Goal

先讓 recent usage 與基礎設定能在目前使用者的 LocalAppData 安全保存與恢復；Phase 4 再由 Settings UI 使用同一 store。

## Scope

- 使用既定 settings format 與 schema version。
- default、parse、validation、round-trip。
- 先寫 temporary file、flush、atomic replace。
- 損壞或未知版本採安全預設，保留原檔供診斷。

## Non-goals

- 不引入 SQLite 或新 serialization dependency。
- 不在本 item 實作設定 UI 或 Windows startup shortcut。

## Acceptance

- 合法設定重啟後值不變。
- 損壞設定不會 crash，且不會直接覆寫原檔。
- 所有 user data 位於 `%LOCALAPPDATA%\NimbleRun`。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R settings --output-on-failure
```

測試必須覆蓋 round-trip、escaping、損壞輸入、較新 schema 與 atomic write failure path。
