# NR-019 — User-folder catalog source

- Status: `done`
- Phase: 2
- Depends on: NR-004
- Source: `docs/design-spec.md` §FR-003、§FR-005、§NFR-003、§NFR-004、AC-012

## Goal

從設定中的多個本機資料夾建立可啟動 App values，讓使用者指定的工具與 App 能和 Start Menu／AppsFolder 項目進入同一個 Catalog。

## Scope

- 讀取 settings store 的多個 `catalog_roots`（每個包含 path 與 recursive flag）與 `catalog_extensions`。
- 依每個 root 的 recursive flag 列舉本機資料夾；只接受 `.exe`、`.cmd`、`.bat`、`.lnk`、`.appref-ms` 等既定 allowlist 中被選取的副檔名。
- 拒絕 UNC、網路磁碟、URI、裝置路徑、未知副檔名與任意命令列輸入。
- 單一根目錄、子目錄或檔案失敗時隔離錯誤，繼續產生其他來源的 values。
- 產出普通可複製的 `AppEntry`，包含 `UserFolder` source、display name、source path 與可交給 Shell 的 launch identity。

## Non-goals

- 不在本 item 實作設定頁；路徑與副檔名 UI 由 NR-013 提供。
- 不在本 item 實作 watcher、debounce 或 snapshot replacement；由 NR-011 負責。
- 不在本 item 實作跨來源 dedup 或 launch adapter。
- 不做一般檔案搜尋、檔案內容搜尋、網路路徑、外掛或 arbitrary extension execution。

## Acceptance

- 兩個以上根目錄可產出合併的 Catalog values。
- 深層子目錄中的符合項目會被列出；未選取或不支援的副檔名不會進入 Catalog。
- recursive 關閉時只列出根目錄第一層；recursive 開啟時才列出子目錄項目。
- 重複根目錄、大小寫不同的副檔名與不存在／無權限根目錄有 deterministic handling。
- Unicode 路徑與檔名可正確保存、顯示，並保留有效 launch identity。
- 單一 root failure 不會清空其他來源結果或使 scan worker crash。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R user_folder_catalog --output-on-failure
```

使用隔離 fixture 測試多根目錄、遞迴列舉、allowlist、Unicode、重疊路徑與錯誤隔離；不依賴真實使用者資料夾或人工操作設定頁。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-003/§FR-005/§NFR-003/§NFR-004/AC-012、`docs/work-items.md`、本文件與 `docs/work-items/NR-004-settings-store.md`、`src/settings/settings_store.h`、`src/catalog/app_entry.h`。依 settings store 的 `catalog_roots`（path＋recursive）與 `catalog_extensions` allowlist 列舉本機資料夾，接受 `.exe`/`.cmd`/`.bat`/`.lnk`/`.appref-ms`；拒絕 UNC、網路磁碟、URI、裝置路徑、未知副檔名與任意命令列輸入；recursive 關閉只掃第一層，開啟才遞迴且不追蹤 reparse point；單一 root／子目錄／檔案失敗時隔離並繼續；產出普通可複製 `AppEntry`（`UserFolder` source、display name、source path、launch identity）。不實作設定頁（NR-013）、watcher/debounce/snapshot（NR-011）、dedup（NR-007）、launch adapter（NR-008）。新增 focused fixture test。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: subagent
- 修改檔案：`src/catalog/user_folder_catalog.h`＋`src/catalog/user_folder_catalog.cpp`（新增）、`src/catalog/stable_id.h`（新增，共享 FNV-1a stable-id）、`src/catalog/app_entry.h`（`AppSource` 新增 `UserFolder`）、`src/settings/settings_store.h/.cpp`（公開 `IsLocalAbsolutePath` 供列舉器防禦性複用）、`src/catalog/start_menu_catalog.cpp`＋`src/catalog/appsfolder_catalog.cpp`（改用共享 `HashStableId`，移除重複副本）、`CMakeLists.txt`（`nimblerun_catalog` 加入 user_folder 來源並 link `nimblerun_settings`）、`tests/CMakeLists.txt`（新增 `nimblerun_user_folder_catalog_test`）、`tests/unit/user_folder_catalog_test.cpp`（新增 fixture test）。
- 設計：API 採 `EnumerateUserFolderCatalog(const Settings&)`，fixture 直接組 `Settings`（`catalog_roots`＋`catalog_extensions`）驅動，不需真實使用者資料夾。allowlist 大小寫不敏感（列舉器內部 lowercase 正規化，空清單退回 `DefaultExtensions()`）；root 先過 `IsLocalAbsolutePath` 防禦性拒除 UNC/URI/裝置路徑；`recursive` 控制是否掃子目錄且不追蹤 reparse point；`.exe/.cmd/.bat` 以 `CreateFileW` 驗證可讀普通檔案，`.lnk/.appref-ms` 直接保留；`stable_id` 用共享 FNV-1a（§10.3）雜湊完整路徑。重複 root 各掃一次、產生重複 entry，留待 NR-007 dedup（ponytail 註記）。單一 root／子目錄／檔案失敗一律 skip，不清空其他來源。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0，無 warning
  - `ctest --test-dir build -R user_folder_catalog --output-on-failure` → exit 0，`nimblerun_user_folder_catalog_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，7/7 passed（search、hotkey、start_menu_catalog、settings、appsfolder_catalog、user_folder_catalog、lifecycle）
  - 非 vacuous 驗證：暫時讓列舉器直接 `return {}` → 測試如預期 FAIL（`FAILED: exe listed`），還原後全綠。
- 測試覆蓋：多根合併計數、深層子目錄列出、unsupported 副檔名排除、recursive off 只掃第一層／on 掃子目錄、混合大小寫 allowlist、Unicode 路徑與檔名保存、重複 root（各掃一次、同 stable_id）、不存在 root／file-as-root／UNC 防禦性跳過、locked file（不可讀）跳過、單一 root 失敗不清空其他結果。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_user_folder_catalog_test.exe`。
- 未完成：無。跨來源 dedup（NR-007）、watcher/snapshot（NR-011）、設定頁（NR-013）、Shell launch adapter（NR-008）依規劃留待各自 item。
