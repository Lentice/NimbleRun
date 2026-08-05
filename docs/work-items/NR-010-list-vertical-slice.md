# NR-010 — Launchable list vertical slice

- Status: `done`
- Phase: 1
- Depends on: NR-002、NR-003、NR-007、NR-008、NR-009
- Source: `docs/design-spec.md` §4.1、§4.3、§4.7、§4.8、AC-001、AC-003、AC-004、AC-005

## Goal

把 Phase 0 probe 收斂成第一個可使用的列表面板：顯示 Icon、App 名稱、有效路徑，支援搜尋、上下選取與 Enter 啟動。

## Scope

- 空白時顯示 recent list；有 query 時顯示 filtered App list。
- 第一項可選取但不自動啟動。
- Up／Down 移動，Enter launch，Esc 清空後再隱藏。
- 單擊啟動；列表項目只在有有效 path 時提供 Open file location。
- hotkey 顯示面板後將 focus 放到 input。

## Non-goals

- 不做 matrix、拖曳排序、lazy real icons、DPI polish 或完整 Settings page。
- 不要求 Agent 操作視窗來證明 UI。

## Acceptance

- list view 可由 snapshot 建立，不依賴 fake App data。
- keyboard state transitions 可由 focused test 驗證。
- Enter 只 launch 目前選取 entry；無結果時不會 launch。
- query、selection、launch failure 不會讓 host process crash。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R list_vertical_slice --output-on-failure
```

測試 input／selection／launch command model；host process 只需啟動與終止，不能把人工點擊列為完成條件。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §4.1、§4.3、§4.7、§4.8、AC-001、AC-003、AC-004、AC-005、`docs/work-items.md`、本文件；trace `src/app_host/main.cpp`（現有 probe render/tray/hotkey）、`src/catalog/`（`app_entry.h`、`stable_id.h`、各來源、`dedup.h`）、`src/usage/usage_store.h`、`src/search/search_engine.h/.cpp`、`src/launch/shell_launch.h`。把 Phase 0 probe 收斂成第一個可用列表垂直切片：以真實 catalog snapshot 建立 list view（不再用 fake App data）；空白時顯示 recent list（最多 20，依最後啟動排序）、有 query 時顯示 filtered list；第一項可選取但不自動啟動；Up／Down 移動選取、Enter 啟動、Esc 先清空再隱藏；單擊啟動；只有有效 path 的項目提供「開啟檔案位置」；hotkey 顯示面板後 focus 到 input；launch 成功後依設定隱藏並更新 usage，失敗保持面板顯示不 crash。keyboard state transitions（query、selection、launch command model）以 focused test 驗證，不需操作視窗。不做 matrix、拖曳排序、lazy real icons、DPI polish 或完整 Settings page（NR-011/NR-012/NR-013/NR-015/NR-016）。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: 主 Agent 實作（兩次 subagent 皆未產出變更）
- 修改檔案：`src/app_host/panel_model.h`＋`panel_model.cpp`（新增，純值 interaction model）、`tests/unit/panel_model_test.cpp`（新增）、`CMakeLists.txt`（新增 `nimblerun_panel_model` static lib，NimbleRun 連結 panel_model/catalog/settings/launch/usage）、`tests/CMakeLists.txt`（新增 `nimblerun_list_vertical_slice_test`）、`src/app_host/main.cpp`（probe render 改為真實 catalog 列表）。
- 設計：`PanelModel` 是純值 command/state model（無 HWND/Shell/COM）：持有 caller 的 catalog snapshot 參考與 recent list；`SetQuery`（空→recent、非空→`SearchApps`）、`MoveSelection(delta)`（wrap）、`Activate()`（回傳 launch identity，空/無選取→no-op）、`Esc()`（有 query 先清空，空 query 回傳 hide）。main.cpp 啟動時載入 settings、以三來源＋`DeduplicateCatalog` 建 snapshot、載入 usage 算 recent，建立 child EDIT（subclass 轉送 Up/Down/Enter/Esc 給 model），render 用 D2D/DWrite 顯示列表（tile placeholder＋名稱＋source_path，NR-012 換真實 icon）。Enter/單擊只啟動選取項；成功後 `RecordLaunch`＋依 `hide_after_launch` 隱藏；失敗保持面板並顯示錯誤文字；右鍵只有 `IsPathIdentity` 有效路徑才 `SHOpenFolderAndSelectItems`（AppsFolder parsing name 略過）。
- Main-agent 確認：範圍僅 NR-010；未偷渡 NR-011 refresh（tray Refresh 仍是 no-op dispatch）、NR-012 真實 icon（placeholder 註記）、NR-013 設定頁、NR-015 DPI polish。單一實例、tray、hotkey 衝突處理（NR-002/003）原樣保留。
- Agent checks（2026-08-05）：
  - `cmake --build build` → exit 0，無 warning
  - `ctest --test-dir build -R list_vertical_slice --output-on-failure` → exit 0，`nimblerun_list_vertical_slice_test` passed
  - `ctest --test-dir build --output-on-failure` → exit 0，11/11 passed（含 lifecycle_check）
  - panel model 測試涵蓋：空 query 顯示 recent（cap＋新→舊）、無紀錄空狀態、query 切到 filtered rows、首列可選取不自動啟動、MoveSelection clamp/wrap、Enter 只啟動選取項、空結果不啟動、Esc 先清空再隱藏、query 變更重設選取、失敗後 model 狀態完好。
- 證據：`build\Testing\Temporary\LastTest.log`；`build\tests\nimblerun_panel_model_test.exe`。
