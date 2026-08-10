# NR-152 — settings 寫入端與讀取端不對稱：UI 可寫出自己讀不回來的檔案（整包設定重置陷阱）

Phase 1 · Robustness · Depends on: —（NR-140 的對稱守門；spec 條文一併補）

- Source: `docs/design-spec.md` §FR-013（自訂資料夾）、§11（設定損壞→採預設值並通知）、
  NR-140（讀取端 32-root／256-char 上限）、NR-058/NR-080（「設定自己不見了」是明文
  要消除的體驗）
- Origin: 2026-08-10 第十四次稽核第 2 輪（spec 軸，IMPORTANT）。主 Agent 已對照
  `AddRoot`／`Save`／`Load` 驗證。
- Priority: **IMPORTANT**——合法使用者操作即可觸發整包設定重置，且是 NR-058 家族
  明文要消除的陷阱。

## Why

NR-140 在 `SettingsStore::Load`（`settings_store.cpp:226`）加了 32 個 `catalog_root`
上限，第 33 個 → 整檔 `Corrupt` → `PreserveCorrupt` + `DefaultSettings()` + balloon。
但**寫入端沒有對稱守門**：

- `SettingsEditor::AddRoot`（`src/settings/settings_editor.cpp:388`）無上限；
- 設定對話框 `IDC_ADD_FOLDER`（`src/app_host/settings_dialog.cpp:486`）直接呼叫；
- `SettingsStore::Save`（`settings_store.cpp:282-285`）照單全寫。

使用者（或拖資料夾進去的任何工具）經 UI 合法加入第 33 個資料夾 → 下次啟動
settings.ini 被判定損壞 → **hotkey／theme／recent_count／全部 folders** 靜默重置
成預設、原檔改名 `.corrupt`、只留一則 balloon——NR-058/NR-080 明文要消除的
「我的設定自己不見了」以新的製造方式重現。

## Decisions already made — do not reopen

1. **寫入端守門**：`AddRoot` 在已達 32 筆時回 `false`（對話框既有
   `FolderInvalidNotice` 顯示路徑的形狀，換專屬文案或沿用）；`Save` 不為此加第二道
   守門（`AddRoot` 是唯一新增來源；`Settings` 的直接構造只存在於測試與預設值）。
2. **spec 補條文**：`docs/design-spec.md` §FR-013 資料夾列後補一句（與
   `recent_count 8～40` 的先例對齊，因為上限現在使用者可見）：
   「自訂資料夾上限 32 個（與設定檔讀取端一致；超限的 `settings.ini` 視為損壞、
   退回預設並隔離原檔）；`hotkey` 值上限 256 字元，超限同視為損壞。」
3. `kMaxCatalogRoots`／`kMaxHotkeyLength` 移到 header（`settings_store.h`）公開，
   供 `SettingsEditor` 與 `Load` 共用單一來源。
4. 不新增對話框警告以外的 UX（§11 禁止連續彈窗；既有 balloon 機制已處理損壞通知）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11（節錄，以原文為準）：

> 設定損壞…採預設值並通知，不得以連續提示騷擾使用者。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/settings/settings_store.{h,cpp}`：`:226`（Load 上限）、`:282-285`（Save）、
  `settings_store.h`（常數位置）。
- `src/settings/settings_editor.{h,cpp}`：`:388-404`（`AddRoot`）。
- `src/app_host/settings_dialog.cpp`：`:486` 一帶（`IDC_ADD_FOLDER` handler、
  `FolderInvalidNotice` 形狀）。
- `tests/unit/settings_editor_test.cpp`（`AddRoot` 既有測試）。

## Scope

1. `kMaxCatalogRoots`／`kMaxHotkeyLength` 公開到 `settings_store.h`；`Load` 引用。
2. `AddRoot` 達上限回 `false`；對話框把失敗顯示出來（沿用既有 notice 機制）。
3. spec §FR-013 補句（上文決策 2 的原文）。
4. 測試：`settings_editor_test` 新增「第 33 個 root → `AddRoot` 回 false、清單維持
   32」；`settings_store_test` 既有上限測試改用公開常數後仍綠。

## Non-goals

- 不改 `Load` 的 Corrupt 契約與 NR-140 的上限值。
- 不加「第 32 個時警告」的預警 UX（YAGNI；到頂回 false 即可）。

## Acceptance

1. 經 `AddRoot` 無法寫出超過 32 個 root 的 settings（測試斷言）。
2. spec §FR-013 含上限條文。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings" --output-on-failure
```

```powershell
rg -n "kMaxCatalogRoots" src/settings
# expect: 定義於 settings_store.h，Load 與 AddRoot 各引用一次
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
