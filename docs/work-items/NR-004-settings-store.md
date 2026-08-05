# NR-004 — Atomic local settings store

- Status: `planned`
- Phase: 1 foundation, extended in Phase 4
- Depends on: NR-001
- Source: `docs/design-spec.md` §FR-013、§NFR-003、§10

## Goal

先讓 recent usage 與基礎設定能在目前使用者的 LocalAppData 安全保存與恢復；Phase 4 再由 Settings UI 使用同一 store。

## Scope

- 使用既定 settings format 與 schema version。
- 保存 `catalog_roots` 多值本機路徑及每筆的 recursive flag，與 `catalog_extensions` allowlist 選項，並提供 default／parse／validation。
- default、parse、validation、round-trip。
- 先寫 temporary file、flush、atomic replace。
- 損壞或未知版本採安全預設，保留原檔供診斷。

## Non-goals

- 不引入 SQLite 或新 serialization dependency。
- 不在本 item 實作設定 UI、Catalog enumeration 或 Windows startup shortcut。

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

## 交接區

- Start: 2026-08-04
- Subagent scope: 在 `%LOCALAPPDATA%\NimbleRun` 以既定 settings format 與 schema version 實作 atomic 本地設定 store（default、parse、validation、round-trip、temporary file＋flush＋atomic replace、損壞／未知版本採安全預設並保留原檔供診斷）；不引入 SQLite 或新 serialization dependency，不做設定 UI 或 startup shortcut。
- Result: done
- Agent: general subagent（首個 subagent 回傳空結果且未動任何檔案，已以全新 subagent 重做）
- 修改檔案：`src/settings/settings_store.h`＋`src/settings/settings_store.cpp`（新增）、`tests/unit/settings_store_test.cpp`（新增）、`CMakeLists.txt`（新增 `nimblerun_settings` static lib）、`tests/CMakeLists.txt`（新增 `nimblerun_settings_test`）。
- 設計（design-spec §10 / FR-013）：UTF-8 line-oriented `settings.ini`，第一行 `schema=1`；keys：`hotkey`、`auto_start`、`theme`(system/light/dark)、`recent_count`(validated 8–40)、`hide_after_launch`、多值 `catalog_root`（`<escaped path>|<recursive true/false>`，`|` 為安全分隔符）、多值 `catalog_extension`；values 以 backslash escape（`\\`、`\=`、`\n`、`\r`、`\t`）。Save：ensure dir → `settings.ini.tmp` → `WriteFile` → `FlushFileBuffers` → `MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`，失敗即刪 tmp 且不動原檔。Load：missing→defaults；unreadable/undecodable/malformed/schema<1→defaults 並將原檔改名 `settings.ini.corrupt` 保留診斷；schema>1→defaults 且原檔原封不動。`catalog_extension` 檔中清單具決定性：首筆取代預設 allowlist 後續 append；`catalog_root` 只接受 drive-letter 本機絕對路徑（拒絕 UNC／URI／裝置路徑），無 flag 時 recursive 預設 true；extension 只接受 allowlist（`.exe`/`.cmd`/`.bat`/`.lnk`/`.appref-ms`）且大小寫不敏感、去重。
- Main-agent 修正：subagent 交付的 settings test 全程使用 `assert()`，在 Release（`-DNDEBUG`）會被剝離而形同空測試；改為與 `hotkey_registration_test.cpp` 一致的 `Expect()`＋`exit(1)`。改後首跑抓到測試隔離 bug（`TestCorrupt` 留下的 `.corrupt` 污染共享 temp dir，導致 `TestNewerSchema` 失敗）→ 改為每個測試獨立 temp dir。修正後測試真實失敗於邏輯錯誤，證明非 vacuous。
- Main-agent 補齊（Spec v1.1 更新後）：更新後的 NR-004 scope 要求保存 `catalog_roots`（多路徑＋recursive flag）與 `catalog_extensions`（allowlist），原始交付未含 → 補實作 `CatalogRoot`＋`Settings::catalog_roots/catalog_extensions`、serialize/parse/validation，並新增 `TestCatalogRootsRoundTrip`／`TestCatalogRootsValidation`。修補過程中抓到 `Unescape` 對未知 escape（如 `C:\Valid` 的 `\V`）會吞掉反斜線致路徑錯亂 → 改為未知 escape 保留反斜線，並把 `\\`、`\=` 視為正式 escape。
- Agent checks（2026-08-04）：
  - `cmake --build build` → exit 0
  - `ctest --test-dir build -R settings --output-on-failure` → exit 0，`nimblerun_settings_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，5/5 passed（search、hotkey、start_menu_catalog、settings、lifecycle）
  - settings test 覆蓋：defaults、round-trip、escaping（含 on-disk bytes）、validation、catalog_roots round-trip／validation（本機路徑保留、UNC/URI 拒絕、無 flag 預設 recursive、allowlist 大小寫不敏感與去重）、corrupt→`.corrupt` 保留、newer schema→原檔不動、atomic write failure（以 `.tmp` 目錄擋寫）、原值在失敗後保留。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_settings_test.exe`。
- 備註：`nimblerun_settings` 尚未 link 進 NimbleRun exe，待 NR-013 接入。
