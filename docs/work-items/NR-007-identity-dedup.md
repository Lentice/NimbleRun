# NR-007 — Stable identity and deduplication

- Status: `done`
- Phase: 2
- Depends on: NR-005、NR-006、NR-019
- Source: `docs/design-spec.md` §FR-007、§10.3

## Goal

讓同一 App 在不同來源出現時能穩定識別、去重，並讓 pins／usage 不依賴顯示名稱或排序。

## Scope

- Start Menu、AppsFolder 與 UserFolder identity normalization。
- stable ID 產生與版本化測試 fixtures。
- dedup precedence：可啟動性、icon 品質、User Start Menu、Common Start Menu。
- UserFolder 同一路徑／同一 Shell launch identity 的重複項目只保留一筆；不得因 display name 相同合併。
- 無法可靠判定時保留項目並記錄診斷值。

## Non-goals

- 不以 display name 作唯一 merge key。
- 不在本 item 修改搜尋 ranking 或 usage formula。

## Acceptance

- 相同 fixture 產生相同 stable ID。
- 不同 App 不會只因同名被錯誤合併。
- dedup 結果排序穩定且可複製測試。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R identity --output-on-failure
```

使用純 values fixtures；不需要 HWND、Shell UI 或人工判讀。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-007、§10.3、`docs/work-items.md`、本文件，並 trace `src/catalog/stable_id.h`、`src/catalog/app_entry.h`、`src/catalog/start_menu_catalog.cpp`、`src/catalog/appsfolder_catalog.cpp`、`src/catalog/user_folder_catalog.cpp`。實作跨來源 identity normalization 與 dedup：Start Menu／AppsFolder／UserFolder 的 stable ID 與 launch identity 正規化；dedup precedence（可啟動性、icon 品質、User Start Menu 優先於 Common Start Menu）；UserFolder 同一路徑／同一 launch identity 的重複項目只保留一筆；不得以 display name 作唯一 merge key；無法可靠判定時保留並記錄診斷值。不動搜尋 ranking 或 usage formula。新增純 values fixture 測試（相同 fixture 相同 stable ID、同名不誤合併、dedup 結果排序穩定可複製）。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。新增純值 `src/catalog/dedup.h/.cpp`（`DeduplicateCatalog(std::vector<AppEntry>) -> DedupResult { entries, removed_duplicates, ambiguous_kept }`）與共享路徑正規化 `NormalizePathKey`（`stable_id.h`）；三個來源的 stable ID 改為對正規化後 identity key 取雜湊，使同一實體 App 在不同來源產生相同 stable ID。dedup 依 stable ID 合併並依來源優先序取勝者（AppsFolder > UserStartMenu > UserFolder > CommonStartMenu），絕不依顯示名稱合併；Start Menu 捷徑與 AppsFolder 項目無法可靠判定相同時兩者都保留並計入 `ambiguous_kept`。新增 `tests/unit/identity_dedup_test.cpp`（CTest：`nimblerun_identity_dedup_test`），9 個純 values fixture 測試全綠；全測試套件 8/8 通過。
