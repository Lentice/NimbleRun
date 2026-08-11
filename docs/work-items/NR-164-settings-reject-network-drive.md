# NR-164 — 自訂資料夾驗證接受 mapped network drive，違反 FR-005「拒絕網路磁碟」

Phase 1 · Settings validation · Depends on: —

- Source: `docs/design-spec.md` FR-005（`design-spec.md:369-378`，第 373 行
  「只接受本機磁碟路徑；拒絕 UNC、網路磁碟、URI、裝置路徑及命令列內容」）
- Origin: 2026-08-11 第十六次全 repo 稽核（codex backend，IMPORTANT）。主 Agent
  已重讀 `settings_store.cpp:69-75`、`app_filter.cpp:26-35`、FR-005 全文驗證。
- Priority: **IMPORTANT**——spec 明文拒絕網路磁碟，實作只拒絕字形；mapped SMB
  drive（`Z:\`）通過驗證後被列舉、監看、stat，違反「無網路」邊界且慢速/斷線
  網路磁碟會拖住重建與 watcher。

## Why

`SettingsStore::Load`（`settings_store.cpp:217`）與 `SettingsEditor::AddRoot`
（`settings_editor.cpp:393`）都透過 `IsLocalAbsolutePath`（`settings_store.cpp:73-75`）
驗證 catalog root；`user_folder_catalog.cpp:82` 在列舉端再守一次同一函式。而
`IsLocalAbsolutePath` 只 `Trim` 後委派給 `IsDisplayablePath`（`app_filter.cpp:26-35`），
後者**只驗證字形**（`X:\`／`X:/`，純函式、無檔案系統存取——這是刻意設計，因為
`main.cpp:1639/1789` 每幀呼叫它做顯示決策）。

因此 mapped network drive（`Z:\Apps`，Z: 是 SMB 磁碟機對映）通過所有關卡：
`EnumerateUserFolderCatalog`、`DirectoryWalker`、`StartWatchers`、圖示來源的
stat 都會對該網路磁碟執行 I/O。設計 spec FR-005 第 373 行明文拒絕，實作與之
矛盾（spec 是對的，錯在實作）。此外慢速或斷線的網路磁碟可讓重建執行緒卡在
Shell/FindFirstFileW 上數秒至數分鐘。

`IsDisplayablePath` 本身**不能**加檔案系統存取（每幀熱路徑）。修法放在
`IsLocalAbsolutePath`（設定驗證入口，非熱路徑）：字形檢查通過後，對 drive
root 呼叫 `GetDriveTypeW`，`DRIVE_REMOTE` → false。其他型別（FIXED／REMOVABLE／
CDROM／RAMDISK／NO_ROOT_DIR／UNKNOWN）照舊接受——**只拒絕 REMOTE**，避免破壞
NR-092「缺失 root 先略過」語意（斷線的 USB 隨身碟 root 回 DRIVE_NO_ROOT_DIR
或 UNKNOWN，仍要能被列舉端略過而非驗證端拒絕）。

## Decisions already made — do not reopen

1. **只在 `IsLocalAbsolutePath` 拒絕 `DRIVE_REMOTE`**；`IsDisplayablePath` 一字不改
   （每幀純函式，NR-144 的收斂結論保留——兩者共用字形 predicate 的決策不變）。
2. **抽純函式 `IsAcceptableDriveType(DWORD drive_type)`** 供測試（`DRIVE_REMOTE`
   拒絕、其餘接受），`GetDriveTypeW` 的 Win32 呼叫留在薄 wrapper——mapped drive
   無法在單元測試環境實體建立，AGENTS.md「新邏輯需 focused 測試」由純函式覆蓋。
3. **不做 `GetFullPathNameW` 路徑正規化**：正規化會改變儲存的 root 字串，而
   UserFolder 項目的 stable id 以自身正規化路徑為來源（design-spec §10.3），
   root 字串改變會使既有 pin／usage 對應的 stable id 全部失聯——使用者資料
   損失，超出本 item（FR-005 第一句「保存正規化後的絕對路徑」另開 item 處理）。
4. 不驗證 sender／不做 token 化（NR-077／NR-130 決策不重開）；UNC／URI／裝置路徑
   已由字形檢查擋住，不在本 item 範圍。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-005（`design-spec.md:373`）：

> 只接受本機磁碟路徑；拒絕 UNC、網路磁碟、URI、裝置路徑及命令列內容。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/settings/settings_store.cpp:69-75`（`IsLocalAbsolutePath` 定義）、`:201-224`
  （Load 的 catalog_root 分支）、`src/settings/settings_store.h:36`（宣告）。
- `src/settings/settings_editor.cpp:392-414`（`AddRoot` 呼叫端）。
- `src/catalog/app_filter.cpp:26-35`、`src/catalog/app_filter.h:55`
  （`IsDisplayablePath`——**不改**，只讀）。
- `src/catalog/user_folder_catalog.cpp:80-84`（列舉端守門）。
- `tests/unit/settings_editor_test.cpp:355-368`（`TestAddRootCap`，既有 AddRoot
  測試形狀）。

## Scope

1. `settings_store.cpp`：`IsLocalAbsolutePath` 在字形檢查通過後，取 drive root
   （前三個字元 `X:\`，字形檢查已保證形式）呼叫 `GetDriveTypeW`；回傳
   `DRIVE_REMOTE` 時回 false。新增純函式 `IsAcceptableDriveType(DWORD)` 與
   `GetDriveTypeW` 呼叫薄層。
2. 測試：`settings_editor_test`（或 `settings_store_test`）新增
   `IsAcceptableDriveType` 案例——`DRIVE_REMOTE` → false、`DRIVE_FIXED`／
   `DRIVE_REMOVABLE`／`DRIVE_NO_ROOT_DIR`／`DRIVE_UNKNOWN` → true；
   既有 AddRoot／Load 案例全綠（行為不變）。
3. 註解：`IsLocalAbsolutePath` 上方補一行「mapped network drive 由 GetDriveTypeW
   拒絕（FR-005），IsDisplayablePath 保持純字形檢查供每幀顯示使用」。
4. 文件：spec 不需改（FR-005 已寫對，本 item 是實作對齊）。

## Non-goals

- 不做路徑正規化（`GetFullPathNameW`）——理由見 Decisions §3，會破壞 stable id
  對應的使用者資料。
- 不改 `IsDisplayablePath`、不改 `app_filter`、不改 `user_folder_catalog` 的
  列舉語意（NR-092「缺失 root 略過」保留）。
- 不新增設定項、不新增 UI 字串、不驗證 sender。

## Acceptance

1. `IsAcceptableDriveType(DRIVE_REMOTE) == false`；FIXED／REMOVABLE／NO_ROOT_DIR／
   UNKNOWN 皆 true（測試斷言）。
2. `IsLocalAbsolutePath(L"C:\\Apps")` 行為不變（既有測試全綠）；mapped drive 的
   拒絕經由 `IsAcceptableDriveType` 測試覆蓋（無法在 CI 造實體網路磁碟）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings" --output-on-failure
```

```powershell
rg -n "GetDriveTypeW|IsAcceptableDriveType" src/settings/settings_store.cpp tests/unit
# expect: IsLocalAbsolutePath 內一次 GetDriveTypeW；測試檔案覆蓋 IsAcceptableDriveType
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。

### 交接區（2026-08-11，實作完成）

改三檔：`src/settings/settings_store.cpp`、`src/settings/settings_store.h`、
`tests/unit/settings_store_test.cpp`。

**`settings_store.cpp`：** 在 anonymous namespace 新增薄層 `DriveTypeOfRoot`
（:72-76）——取字形檢查已保證的前三個字元為 drive root，`/` 正規化為 `\\`
後呼叫 `GetDriveTypeW`（唯一 Win32 呼叫點）。`IsLocalAbsolutePath`（:86-96）
改為字形檢查（`IsDisplayablePath`，**未動**）通過後回傳
`IsAcceptableDriveType(DriveTypeOfRoot(trimmed))`，並依 Scope §3 補一行註解
（mapped drive 由 GetDriveTypeW 拒絕、IsDisplayablePath 保持純字形檢查）。
`IsAcceptableDriveType(DWORD)`（:80-82，nimblerun namespace、非匿名）為純函式：
`return drive_type != DRIVE_REMOTE;`——只拒絕 REMOTE，NO_ROOT_DIR／UNKNOWN
照舊接受（NR-092「缺失 root 先略過」語意保留）。`settings_store.h` 新增宣告
（:45）並引入 `<windows.h>`（沿用 repo 內使用 Win32 型別標頭的慣例）；
`app_filter.cpp`／`user_folder_catalog.cpp` 未動，無 `GetFullPathNameW`
正規化、無新設定、無新 UI 字串（non-goals 全部遵守）。

**測試：** `settings_store_test.cpp` 新增 `TestIsAcceptableDriveType`（:138-146）
——`DRIVE_REMOTE` → false、`DRIVE_FIXED`／`DRIVE_REMOVABLE`／
`DRIVE_NO_ROOT_DIR`／`DRIVE_UNKNOWN` → true，已註冊進 `wmain`（:563）。

**驗證結果：**
- Release build（`cmake --build build --clean-first`）：僅 `main.cpp:1410`
  既有 warning（本 item 未觸碰該檔，非新增）；改動三檔 0 warnings/errors。
- CTest 全量：**31/31 通過**（測試數量不變）。
- `ctest -R "settings"`：**2/2 通過**（nimblerun_settings_test、
  nimblerun_settings_ui_test）。
- 同 item Agent checks 的 sanity grep：
  `rg -n "GetDriveTypeW|IsAcceptableDriveType" src/settings tests/unit` ——
  `GetDriveTypeW` 僅 `settings_store.cpp:75` 一處呼叫；`IsAcceptableDriveType`
  宣告／定義／測試覆蓋齊全（見 item §Agent checks 期望形狀）。

**執行環境備註：** 首次 build 曾因執行中的 NimbleRun.exe 鎖住輸出檔而 link
失敗（Permission denied），`Stop-Process` 後重建成功——與本 item 程式碼無關。

無偏離 item 決策。
