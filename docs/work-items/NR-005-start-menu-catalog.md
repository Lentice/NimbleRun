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
