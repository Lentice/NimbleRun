# NR-005 — Start Menu catalog enumeration

- Status: `planned`
- Phase: 1
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-003、§FR-004、§NFR-003

## Goal

從目前使用者與所有使用者的 Programs Known Folder 建立可啟動 App values。

## Scope

- 使用 `FOLDERID_Programs` 與 `FOLDERID_CommonPrograms`。
- 遞迴處理 `.lnk`、`.appref-ms`，只保留符合 Spec 的 App entries。
- 使用 Shell link API 解析捷徑；保留 Shell 可開啟但解析不完整的項目。
- Catalog values 不持有 COM pointer。

## Non-goals

- 不掃描整顆磁碟或 `WindowsApps`。
- 不在本 item 實作 AppsFolder、dedup、icons、search UI 或 launch UI。
- 不處理使用者自訂資料夾；該來源由 NR-019 負責。

## Acceptance

- Known Folder 路徑不硬編碼英文目錄。
- 單一損壞捷徑不會中止整次 enumeration。
- 產出的 value 有 display name、source、source path 與 launch identity。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R start_menu_catalog --output-on-failure
```

測試使用隔離 fixture 與正常／損壞／Unicode／深層捷徑；不要依賴人工開啟結果。

## 交接區

- Start: 2026-08-04
- Subagent scope: 以 `FOLDERID_Programs` 與 `FOLDERID_CommonPrograms` Known Folder 遞迴列舉 `.lnk`／`.appref-ms`，只保留符合 Spec 的 App entries；用 Shell link API 解析捷徑，保留可開啟但解析不完整的項目；Catalog values 不持有 COM pointer；不掃整顆磁碟或 WindowsApps；不做 AppsFolder、dedup、icons、search/launch UI。
- Result: done
- Agent: general subagent
- 修改檔案：`src/catalog/start_menu_catalog.h`＋`start_menu_catalog.cpp`（新增）、`tests/unit/start_menu_catalog_test.cpp`（新增）、`CMakeLists.txt`（新增 `nimblerun_catalog` static lib）、`tests/CMakeLists.txt`（新增 `nimblerun_start_menu_catalog_test`）。
- 設計（design-spec §FR-003、§FR-004）：`SHGetKnownFolderPath` 取 `FOLDERID_Programs`／`FOLDERID_CommonPrograms`，`FindFirstFileW` 遞迴掃描（跳過 reparse point 防迴圈）。`.lnk` 以 `IShellLinkW`＋`IPersistFile::Load` 解析（不碰二進位），損壞捷徑跳過不中止；保留可開啟但無法完整解析的項目（launch identity = 捷徑路徑）。排除網站（PIDL→`SIGDN_URL` 非 `file:`）、uninstaller 與 `.chm/.hlp/.html/.htm` help target；接受 `.lnk`/`.appref-ms`，僅當 `.exe` 實際位於 Programs 時接受。`stable_id` = FNV-1a hash（resolved target＋args），與顯示名無關。回傳純 `AppEntry` copyable values，COM 物件不離開解析函式。
- Main-agent 確認（Spec v1.1）：與更新後 §FR-004 一致（MVP 接受 `.lnk`/`.appref-ms`、Programs 內 `.exe`、排除規則、保留半解析捷徑），並符合新增 non-goal「不處理使用者自訂資料夾（NR-019）」。
- Agent checks（2026-08-04）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build -R start_menu_catalog --output-on-failure` → exit 0，`nimblerun_start_menu_catalog_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，5/5 passed（search、hotkey、start_menu_catalog、settings、lifecycle）
  - fixture 涵蓋：正常、同 target 重複（stable id 一致）、Unicode 名稱/路徑、深層巢狀、URL、uninstaller、損壞 `.lnk`、`.appref-ms`、裸 `.exe`、`.txt` 忽略、缺目錄、真實 Start Menu smoke、兩次列舉 id 可重現。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_start_menu_catalog_test.exe`。
