# NR-006 — AppsFolder catalog enumeration

- Status: `planned`
- Phase: 2
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-003、§FR-005、§FR-006、AC-006

## Goal

透過 Shell AppsFolder namespace 列出 Microsoft Store／封裝 App，並保留可交給 Shell 啟動的 identity。

## Scope

- 使用 `FOLDERID_AppsFolder` Shell namespace。
- 取得 display name、icon identity 與 canonical launch identity。
- 逐項隔離 Shell failure，支援空結果與部分結果。

## Non-goals

- 不直接存取 `WindowsApps` 或封裝目錄內 EXE。
- 不保證固定的 Calculator／Settings 名稱；不在本 item 操作目標 App UI。

## Acceptance

- AppsFolder enumeration 不會因單一項目失敗而 crash 或清空其他來源。
- 每個成功項目是普通 copyable value，不讓 UI 擁有 COM pointer。
- Agent 能記錄當前測試環境的 enumeration count 與失敗 count。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R appsfolder_catalog --output-on-failure
```

測試只驗證 enumeration invariants、error isolation 與 identity 欄位；不要求 Agent 控制封裝 App 視窗。
