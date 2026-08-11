# NR-172 — 磁碟根目錄可被設為遞迴 Catalog source，整顆磁碟掃描違反 §19.5

Phase 1 · Settings validation · Depends on: —

- Source: `docs/design-spec.md` §19.5（`design-spec.md:1082`「不掃描整顆磁碟」）、
  FR-005（本機資料夾的驗證邊界）
- Origin: 2026-08-11 第十六次稽核第 4 輪（codex backend，IMPORTANT）。主 Agent
  已重讀 `settings_store.cpp:73-75`、`settings_editor.cpp:392-414`、
  `user_folder_catalog.cpp:81-105`、`design-spec.md:1082` 驗證。
- Priority: **IMPORTANT**——設定 UI 的 folder picker 可選磁碟根目錄（無需手改
  檔案），`recursive` 預設 true 即從根目錄遞迴列舉整顆磁碟、為根目錄建立
  recursive watcher，直接違反 spec 明文；與 NR-173 合併放大記憶體峰值。

## Why

`IsLocalAbsolutePath`（`src/settings/settings_store.cpp:73-75`，NR-164 後含
`GetDriveTypeW` 檢查）對 `C:\`、`D:\` 回傳 true——drive-letter 根目錄是合法
本機路徑，DRIVE_FIXED。`SettingsEditor::AddRoot`（`settings_editor.cpp:392-414`）
接受後存入 catalog_roots；`SettingsStore::Load`（`settings_store.cpp:217`）也接受
手寫 `catalog_root=C:\|true`。列舉端 `user_folder_catalog.cpp:81-105` 對每個
accepted root 以 `root.recursive`（預設 true）呼叫 `WalkDirectory` 遞迴走訪，
`StartWatchers` 對每個 root 建立 recursive watch。

後果：整顆磁碟被列舉（數十萬檔案、數十秒 rebuild、NR-124 的 skipped 計數暴增）、
cache 寫入可能超過上限、圖示請求暴增；違反 design-spec §19.5「不掃描整顆磁碟」。
`kMaxCatalogRoots`（32）只限制 root 數量，不限制單一 root 的涵蓋範圍。

## Decisions already made — do not reopen

1. **在 `IsLocalAbsolutePath`（唯一共用驗證入口）拒絕恰為 volume root 的路徑**：
   字形檢查＋drive-type 檢查之後，若 Trim 後的字串為 `X:\` 或 `X:/`（恰三個
   字元、無其餘路徑）→ false。Load（`:217`）與 `AddRoot`（`:393`）自動同時
   生效（兩者共用此函式），`user_folder_catalog.cpp:82` 的防禦性重驗也涵蓋。
2. **NR-164 的 mapped-drive 檢查與本判準同處共存**：先字形、再 drive type、
   再 volume-root 檢查，順序固定；`IsAcceptableDriveType` 純函式不動。
3. 子資料夾（`C:\Tools`）照舊接受；不影響 `UserDataDirFromLocalAppData`
   （LOCALAPPDATA 從不為恰三字元的根目錄）。
4. 新增 focused 測試：`C:\`→false、`C:/`→false、`C:\Tools`→true、
   `C:\Tools\`→true（尾端斜線非根目錄）、mapped-drive 案例維持。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §19.5：

> 不掃描整顆磁碟，也不直接存取受保護的 WindowsApps。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/settings/settings_store.cpp:69-75`（`IsLocalAbsolutePath`，NR-164 後含
  `GetDriveTypeW`）、`:201-224`（Load catalog_root 分支）。
- `src/settings/settings_editor.cpp:392-414`（AddRoot）。
- `src/catalog/user_folder_catalog.cpp:81-105`（列舉端守門與 recursive 使用）。
- `tests/unit/settings_store_test.cpp`／`settings_editor_test.cpp`（既有
  `TestAddRootCap` 與 NR-164 的 `TestIsAcceptableDriveType` 形狀）。

## Scope

1. `IsLocalAbsolutePath` 加 volume-root 拒絕（恰三字元 `X:` + 分隔符）。
2. 測試：根目錄拒絕（`C:\`／`C:/`）、子資料夾接受（含尾端斜線）、既有
   AddRoot／Load 案例全綠。
3. 註解更新：`IsLocalAbsolutePath` 上方補「拒絕恰為 volume root 的路徑
   （§19.5 不掃描整顆磁碟）」。
4. spec 不需改（§19.5 已寫對）。

## Non-goals

- 不拒絕子資料夾、不新增設定項、不新增 UI 字串。
- 不為「volume root 但 recursive=false」開例外（第一層掃描仍可能數萬筆，
  規格禁止的是整顆磁碟掃描的語意，root 一律拒絕最簡單一致）。
- 不改 `WalkDirectory`、不改 watcher、不驗證 sender。

## Acceptance

1. `IsLocalAbsolutePath(L"C:\\") == false`、`IsLocalAbsolutePath(L"C:\\Tools") ==
   true`（測試斷言）；既有 `TestIsAcceptableDriveType` 與 AddRoot/Load 案例全綠。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings" --output-on-failure
```

```powershell
rg -n "IsLocalAbsolutePath|volume root" src/settings/settings_store.cpp tests/unit
# expect: volume-root 拒絕在 IsLocalAbsolutePath；測試覆蓋 C:\ 與 C:\Tools
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。

## 交接區

- 實作 commit：`f26d9c9`（NR-172: reject bare volume roots as catalog sources）。
- 改動檔案：`src/settings/settings_store.cpp`、`tests/unit/settings_store_test.cpp`。
- 改動內容：`IsLocalAbsolutePath`（settings_store.cpp:90-104）在既有字形檢查
  （`IsDisplayablePath`）與 drive-type 檢查（`IsAcceptableDriveType`）之後新增
  volume-root 拒絕——Trim 後恰為三字元即為 `X:\` 或 `X:/`（字形檢查已保證
  letter+`:`+separator），回傳 false；`C:\Tools\` 等子資料夾照舊接受。函式上方
  註解補 §19.5「不掃描整顆磁碟」。`AddRoot`（settings_editor.cpp:393）與 Load
  的 catalog_root 分支（settings_store.cpp:249）共用此函式，自動同時生效，兩處
  未改。NR-164 的 drive-type 檢查順序維持：字形 → drive type → volume root。
- 新增測試：`TestIsLocalAbsolutePathRejectsVolumeRoot`（settings_store_test.cpp:152，
  放在 NR-164 的 `TestIsAcceptableDriveType` 之後，沿用 Expect/wmain 慣例）——
  `C:\`→false、`C:/`→false、`C:\Tools`→true、`C:\Tools\`→true。既有
  `TestIsAcceptableDriveType`、AddRoot（settings_editor_test.cpp）與 Load 案例全綠。
- 驗證結果：Release build（LLVM-MinGW + Ninja）零新增 warning；完整 CTest 31/31
  passed（數量與改動前相同）；`ctest -R "settings"` 2/2 passed。
- Sanity grep 輸出：volume-root 拒絕位於 `IsLocalAbsolutePath`（settings_store.cpp:90
  註解、:100 判準）；測試覆蓋 `C:\`（:153）與 `C:\Tools`（:155）。
- 偏差：無。實作時將「三字元 + letter + `:` + 分隔符」的四條件判準簡化為
  `size() == 3`——`IsDisplayablePath` 已保證三字元路徑只能是 `X:` 加分隔符，
  多餘條件不成立，行為與 item 決策 1 一致。
