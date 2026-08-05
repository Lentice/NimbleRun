# NR-013 — Settings UI integration

- Status: `done`
- Phase: 4
- Depends on: NR-003、NR-004、NR-010、NR-019
- Source: `docs/design-spec.md` §FR-013、§4.10、§11、AC-012

## Goal

讓使用者能從 tray／設定入口查看與修改既定 MVP 設定，且設定失敗不破壞舊值。

## Scope

- hotkey、recent count、hide-after-launch、theme、clear usage、reset settings。
- 多個本機資料夾的加入／移除、每個資料夾的 `Include subfolders` 選項與受支援副檔名勾選。
- 英文 UI strings 集中管理。
- validation、save failure、reset default 的 state handling。

## Non-goals

- 不增加一般檔案搜尋、plugin、command line、未知副檔名執行或網路設定。
- 不把人工視覺檢查列為 Agent completion condition。

## Acceptance

- 設定值只接受 Spec 範圍。
- invalid hotkey 顯示一次非阻擋提醒並保留舊設定。
- reset／clear usage 只影響指定資料，不刪除 Catalog source。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R settings_ui --output-on-failure
```

以 settings command／state model self-check 驗證輸入、persist、rollback 與英文 string keys；不要求 Agent 點擊設定畫面。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-013、§4.10、§11、AC-012、`docs/work-items.md`、本文件；trace `src/settings/settings_store.h/.cpp`（Settings、CatalogRoot、DefaultExtensions、Save/Load、atomic write）、`src/app_host/hotkey.h/.cpp`（NR-003 註冊衝突處理）、`src/app_host/main.cpp`（tray 入口 kSettingsMessage、NR-010 面板）、`src/usage/usage_store.h`（clear usage）、`docs/work-items/NR-003-global-hotkey.md`。實作 Settings UI integration：tray「Settings」開啟設定入口；hotkey、recent count（8～40）、hide-after-launch、theme、clear usage、reset settings；多個本機資料夾加入／移除、每資料夾 Include subfolders、受支援副檔名勾選（預設全選）；英文 UI strings 集中管理；validation、save failure、reset default 的 state handling；invalid hotkey 顯示一次非阻擋提醒並保留舊值；reset／clear usage 只影響指定資料，不刪除 Catalog source。建議以純 values settings command／state model self-check 驗證輸入、persist、rollback 與英文 string keys；可接原生 modal／property sheet 或自繪視窗。不增加一般檔案搜尋、plugin、command line、未知副檔名執行或網路設定；不把人工視覺檢查列為完成條件。回報修改檔案、測試命令、結果與未完成事項。
- Result: `done`（2026-08-05，主 Agent 直接實作）。

### 實作摘要

- 新增純 values 可測模型 `src/settings/settings_editor.{h,cpp}`（加入 `nimblerun_settings` 庫）：持有 working copy 的 `Settings`＋集中式英文 string-key 表（`SettingsString`／`SettingsStringText`），typed getter/setter 含驗證（recent_count 只接受 8–40、extension 只接受 allowlist 成員、`ParseHotkey` 拒絕空值／無修飾鍵／含 Windows 鍵），dirty tracking；`Apply(store, swapper)` 先以注入式 `HotkeySwapper`（`std::function`）swap hotkey（register-new-first 語意），成功才 `SettingsStore::Save`，任一失敗都把 working copy、執行中的 hotkey 與已存設定一起 rollback 回舊值。
- 新增 `src/app_host/settings_dialog.{h,cpp}`：tray「Settings」經 `kSettingsMessage` 開啟原生 modal `DialogBox`（模板在 `src/resources/NimbleRun.rc`，控制項 ID 在 `src/resources/resource.h`）。所有 user-visible 字串由 string table 注入；OK 時把編輯值寫回 editor 並 `Apply`，失敗用 dialog 內 status 文字（非阻擋）提示並保留舊值。Clear usage 即時呼叫 `UsageStore::Clear`；Reset 把 working copy 設為 `DefaultSettings()`（含清空 `catalog_roots`），OK 才持久化。
- `src/usage/usage_store.{h,cpp}` 新增 `Clear()`：清空 records 並以空檔 atomic save；save 失敗時還原 in-memory records。
- `src/app_host/main.cpp`：`kSettingsMessage` 改為開啟設定 dialog（`g_settings_store` 為新增 global，指向 wWinMain 的 store）；啟動 hotkey 改由 `ParseHotkey(settings.hotkey)` 初始化（不可解析回退 `Alt+Space` 預設）；Apply 成功後重載 settings 更新 `g_hide_after_launch`。
- 新增測試 `tests/unit/settings_editor_test.cpp`（CTest name `nimblerun_settings_ui_test`，`ctest -R settings_ui` 命中），12 個 case：recent_count 邊界驗證、extension allowlist／大小寫／最末一個不可停用、dirty＋Apply 經 temp-dir store round-trip 持久化、save 失敗（.tmp 目錄阻礙）rollback 且 old hotkey swap 回來、無 hotkey 變更時 save 失敗、OS 拒絕 hotkey（fake seam）不持久化且舊值保留、invalid combo 不 dirty 不持久化、Parse/Format hotkey round-trip、reset 還原 `DefaultSettings()` 且目錄只有 `settings.ini`、clear usage 只清 usage.tsv 且 settings bytes 不變、clear 失敗還原 records、string keys 集中且穩定。
- 建置/測試：Release x64（LLVM-MinGW）`cmake --build build` 無 warning；`ctest --test-dir build -R settings_ui --output-on-failure` 1/1；`ctest --test-dir build --output-on-failure` 13/13（含既有 lifecycle）。

### 設計決策與已知事項（交接給後續 item）

- **Reset 語意**：`ResetToDefaults()` 以 `DefaultSettings()` 為準，包含清空 `catalog_roots`、還原 extension 全選、recent 20、theme system、hide on。僅經 `SettingsStore` 寫入 `settings.ini`，不觸碰 usage.tsv 或任何 catalog 來源（測試以「reset 後目錄只有 settings.ini」證明）。
- **Hotkey rollback seam**：editor 不直接呼叫 `GlobalHotkey`（保持無 HWND 依賴），host 傳入 `std::function`；測試用 shared-state fake。Save 失敗後的 swap-back 為 best-effort：若 `original_.hotkey` 不可解析（如手改 garbage），回退 swap 被略過（極端邊緣，註記於 `Apply` 註解）。
- **熱更新範圍**：Apply 成功後立即生效 `hide_after_launch`；`recent_count`／`theme` 於下次啟動生效；新增/移除 user folder 或改 extension 後，Catalog 重建屬 NR-011（`kRefreshMessage` 仍為 no-op dispatch target）。
- **`settings.ini` 空 allowlist 限制（既有格式）**：editor 拒絕停用最後一個 extension，因為 `SettingsStore::Load` 對「無 `catalog_extension` 行」視為全選預設，空清單無法持久化。
- **Clear usage 即時生效**：點擊即寫入空檔並顯示 status；屬明確破壞性動作，非「OK 才套用」。
- **Dialog**：單一 `DialogContext` file-scope（單實例＋modal，安全）；未做視覺檢查（Non-goal）。
