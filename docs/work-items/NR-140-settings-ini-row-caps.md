# NR-140 — settings.ini 無行數上限：catalog_root 可驅動無限 watcher 執行緒，且無去重

Phase 1 · Untrusted input · Depends on: —（與 NR-141 同為「不受信 user-data 檔」系列，互不相干，可依序處理）

- Source: `AGENTS.md`（Keep all user data under `%LOCALAPPDATA%\\NimbleRun`… Do not overwrite
  user data in place）、`docs/design-spec.md` §10.4（user-data 檔是不受信輸入）、NR-121／NR-122
  的同型先例（其他三個 store 已有行數上限，settings.ini 從未被蓋到）
- Origin: 2026-08-10 第十四次全 repo 稽核（安全軸，MEDIUM）。主 Agent 已讀 `Load` 迴圈驗證。
- Priority: **MEDIUM**——always-on tray process 的可用性面；NR-130 的「同 user DoS」家族
  （NR-130 蓋了 full-rescan 限流與 single-instance 靜默退出，本 item 蓋的是設定檔本身）。

## Why

`SettingsStore::Load`（`src/settings/settings_store.cpp:172-251`）對 `settings.ini` 的每一行
照單全收，**沒有行數上限、沒有 `catalog_root` 去重**（`SettingsEditor::AddRoot` 有去重，
`settings_editor.cpp:388-404`，但 `Load` 沒有）。`hotkey` 值也沒有長度上限
（`settings_store.cpp:196-199`，後續 `ParseHotkey` 對每個 `+` 切一個 `parts` 元素）。

三個消費端：

- `main.cpp:1267-1301` `StartWatchers`：每個 root（含重複者）各建一個
  `CreateFileW` + `std::thread`（`catalog_watcher.cpp:179-225`）；重複 root 就是重複的
  watch 執行緒。UI 執行緒在 tray icon 與 message loop 建立前被卡住。
- `watch_sources_` 索引表保留全部 root（`rebuild_pipeline.cpp:49-54`），100k 條也不釋放。
- 每次 `Alt+Space` 重新 `Load`（`main.cpp:1817`）＋設定對話框把每個 root 填進 listbox
  （`settings_dialog.cpp:296-305`）。

**觸發**：`%LOCALAPPDATA%\NimbleRun\settings.ini` 寫
`schema=1` + 10 萬行 `catalog_root=C:\tools|true`（約 3 MB，遠低於 16 MB 讀取上限），
下次啟動即卡住。same-user 可達；NR-121/122 明文只蓋了 `catalog.cache`／`favorites.txt`／
`usage.tsv`，`settings.ini` 是漏網的一份。

## Decisions already made — do not reopen

1. **超限 = 整檔 Malformed**（與 NR-080「非 Loaded 則不洩漏部分狀態」同契約）：
   第 33 個 `catalog_root` 到達時 `out = DefaultSettings(); return SettingsLoadResult::Corrupt;`
   （走既有 `PreserveCorrupt`）。**不做**「忽略多餘行但保留前面合法值」——那是部分狀態。
2. 上限值：**32** 個 `catalog_root`（`Settings` 是普通值、設定頁清單現實地容不下更多；
   超出就是畸形檔）。`catalog_extension` 已有白名單＋去重，不設上限。
3. `hotkey` 值上限 **256 字元**，超限同為 Malformed（防止 16 MB 單行值送進
   `ParseHotkey` 的 per-`+` 向量）。
4. **不做** `Load` 內去重：超限即 Corrupt 的契約比去重更誠實——重複 root 是使用者
   （或寫入工具）犯的錯，靜默去重會讓使用者不知道 `favorites.txt` 為何指到的資料少了。
   若實作時發現去重成本為零且不影響 Corrupt 契約，可併入，但不得替代上限。
5. 不碰 `settings.ini` 的 schema 版本（schema=1 不變）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.4（節錄，以原文為準）：

> 使用者資料檔（settings.ini／favorites.txt／usage.tsv）一律視為不受信輸入…

`AGENTS.md`：

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

（本 item 不改寫入端，只改讀取端守門。）

## Files to read and trace first

- `src/settings/settings_store.cpp`：`:172-251`（Load 迴圈）、`:196-199`（hotkey）、
  `:219-234`（catalog_root）。
- `src/app_host/main.cpp`：`:1267-1301`（`StartWatchers` 每 root 一執行緒）。
- `src/app_host/catalog_watcher.cpp`：`:179-225`（watch 建立）。
- `tests/unit/settings_store_test.cpp`（既有 Corrupt 契約測試的形狀）。

## Scope

1. `Load` 加入 `catalog_root` 計數，第 33 行 → `DefaultSettings()` + `Corrupt`（沿用既有
   mid-file corrupt 的處理方式，`:185-191` 為範本）。
2. `hotkey` 值長度檢查，> 256 → 同 Corrupt。
3. `tests/unit/settings_store_test.cpp` 新增**必測案例**：
   - 33 個 `catalog_root` → `Corrupt`，且 `out == DefaultSettings()`（NR-080 契約）；
   - 32 個 → `Loaded`，全部保留；
   - 257 字元 `hotkey` → `Corrupt`；
   - 既有正常檔案例全部不變（回歸）。

## Non-goals

- 不改 `SplitLines`／`ReadVersionedLines`（那是 NR-141 的範圍：分配前行數守門，蓋所有 store）。
- 不重排 `Load` 的結構、不引入新 store 型別。
- 不改 `SettingsEditor`／`SettingsDialog` 的 root 管理（AddRoot 已有去重）。

## Acceptance

1. 三個新測試存在並通過；既有 `settings_store_test` 全綠。
2. 一個 10 萬行 `catalog_root` 的檔案載入回 `Corrupt` 且不建立任何 watcher。
3. Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings_store" --output-on-failure
```

## Handoff

### 實作與順序

- 兩個上限常數在 `settings_store.cpp` 的 anonymous namespace：
  `kMaxCatalogRoots = 32`、`kMaxHotkeyLength = 256`，並附 NR-140 出處註解。
- `Load` 迴圈前置 `std::size_t catalog_root_count = 0;`；`catalog_root` 分支第一行
  `if (++catalog_root_count > kMaxCatalogRoots)` → `PreserveCorrupt(directory_, kFileName);`
  `out = DefaultSettings(); return SettingsLoadResult::Corrupt;`（與 `:185-191` 既有
  mid-file corrupt 同形）。計數在 key 比對時累加、與路徑是否合法無關——未驗證的
  root 也是惡意面的一部分。
- `hotkey` 分支先查 `value.size() > kMaxHotkeyLength`（檢查的是 **UnescapeText 後**
  的值，與送進 `ParseHotkey` 的值一致）→ 同 Corrupt 路徑。
- 不引入 helper、不重排迴圈、未動 `SplitLines`／`ReadVersionedLines`（NR-141 範圍）。

### 測試與文件證據

- `tests/unit/settings_store_test.cpp` 新增四函式：
  `TestCatalogRootCap`（33 root → Corrupt＋`out` 全 Default＋`settings.ini.corrupt` 保留）、
  `TestCatalogRootMaxOk`（32 root → Loaded，首尾 root 在）、
  `TestHotkeyLengthCap`（257 字元 → Corrupt）、`TestCatalogRootCap100k`（10 萬 root
  的原始 DoS 形狀 → Corrupt，即 acceptance 2 的證據）。既有測試零改動，CTest
  registration 維持 31（assertion 加在既有 target 內）。
- **測試陷阱（本 session 發現）**：`UnescapeText`（`storage/atomic_text_file.h:133`）
  把 `\r`／`\n`／`\t`／`\\`／`\=` 當跳脫——測試檔裡 `C:\root0` 會被解成含 CR 的
  畸形路徑而被 `IsLocalAbsolutePath` 丟棄。max-ok 案例改用 `C:\Tools0..31`
  （`\T` 非跳脫序列）；`\r`-free 是既有測試也用 `C:\Valid` 的原因。
- 驗證：Release x64（llvm-mingw + Ninja）build 零新增 warning；full CTest 31/31
  全綠（settings_store 專注 `-R nimblerun_settings_test` 亦綠）。
