# NR-006 — AppsFolder catalog enumeration

- Status: `done`
- Phase: 2
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-003、§FR-006、§FR-007、AC-006

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

## 交接區

- Start: 2026-08-04
- Subagent scope: 以 `FOLDERID_AppsFolder` Shell namespace 列舉封裝 App；取得 display name、icon identity 與 canonical launch identity；逐項隔離 Shell failure、支援空結果與部分結果；回傳普通 copyable values，不讓 UI 持有 COM pointer；不直接存取 `WindowsApps` 或封裝目錄 EXE。
- Result: done
- Agent: general subagent
- 修改檔案：`src/catalog/appsfolder_catalog.h`＋`appsfolder_catalog.cpp`（新增）、`tests/unit/appsfolder_catalog_test.cpp`（新增）、`CMakeLists.txt`（`nimblerun_catalog` static lib 加入 appsfolder_catalog.cpp）、`tests/CMakeLists.txt`（新增 `nimblerun_appsfolder_catalog_test`）。
- 設計（design-spec §FR-006）：`SHGetKnownFolderItem(FOLDERID_AppsFolder)` 取 Shell namespace，`BindToHandler(BHID_EnumItems)` 列舉子項。每項取 `SIGDN_NORMALDISPLAY`（display name）與 `SIGDN_DESKTOPABSOLUTEPARSING`（parsing name）；任一為空則略過並計入 `failed_items`，不中斷列舉。source-level failure（COM 無法初始化／known folder 無法解析／無法 bind）回傳空結果，不影響其他來源。`launch_identity`＝`source_path`＝parsing name（同時是 §FR-006 的 canonical identity 與後續 icon query 的 identity），`stable_id`＝FNV-1a hash（同 Start Menu 方案）。回傳純 `AppEntry` copyable values，COM 物件不離開函式。
- Main-agent 確認：實作符合 §FR-006（不掃 `WindowsApps`、保存 parsing name、逐項隔離失敗）；`BuildAppsFolderEntry` 是可測試的 per-item boundary，測試直接驅動錯誤隔離分支。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build -R appsfolder_catalog --output-on-failure` → exit 0，`nimblerun_appsfolder_catalog_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，6/6 passed（search、hotkey、start_menu_catalog、settings、appsfolder_catalog、lifecycle）
  - 目前環境實測：313 entries、0 failed items。
- 已知修正：測試原斷言 parsing name 一律以 `shell:AppsFolder\` 開頭過於嚴格；實際 parsing name 形狀依 App 而異（`shell:` URI、裸 AUMID 或絕對路徑），依 §FR-006「Shell parsing name 或等價的穩定啟動識別」放寬為非空＋可重現。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_appsfolder_catalog_test.exe`。
