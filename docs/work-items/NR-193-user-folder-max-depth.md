# NR-193 — 自訂資料夾的遞迴深度改為有限整數上限

Phase 2 · Settings contract · Depends on: NR-004、NR-013、NR-019、NR-096（皆 done）

- Source: `docs/design-spec.md` §FR-005、§10.2、§10.4、§19（第 5 點）
- Origin: 2026-08-20 使用者需求（縮短 rebuild 時間的第二軸）；`grill-with-docs`／domain-modeling 已確認合併欄位、無「無限制」選項、schema 遷移方式
- Priority: **MEDIUM**——目前使用者可設定的自訂資料夾遞迴深度無上限，大型樹（例如 `D:\Program files`）的最壞情況成本不受控制

## Goal

把 `CatalogRoot` 的 `recursive: bool` 換成一個有限整數欄位 `max_depth`，讓使用者能替每個自訂資料夾設定「往下展開幾層子資料夾」的上限,不再只有「只看第一層／整棵樹遞迴到底」兩種選擇,也不再允許「無限制」。

## 已確認的產品決策（grilling 逐輪確認，見下方逐條依據）

1. **合併，不並列**：拿掉 `CatalogRoot::recursive`，換成 `CatalogRoot::max_depth`（有號整數）。`max_depth == 0` 等於今天的 `recursive=false`（只看第一層,不展開子資料夾)；`max_depth == N（N ≥ 1）` 表示往下展開 N 層子資料夾。不新增第二個並列欄位。
2. **不保留「無限制」選項**：不設 `-1` 或任何 sentinel 代表無限遞迴。這個欄位存在的目的就是讓使用者自己設一個上限,保留無限制等於讓使用者選回今天正在被投訴的行為。
3. **範圍與預設**：合法值 `0..50`（含端點）。新增資料夾（`AddRoot`）預設 `max_depth = 20`。既有 `recursive=true` 的資料夾遷移後同樣得到 `max_depth = 20`；`recursive=false` 遷移後得到 `max_depth = 0`。20 是刻意寬鬆的預設——一般軟體安裝目錄很少超過 2-4 層,20 層以下與「無限制」在實務上沒有可觀察差異,但仍替最壞情況設了一個界。
4. **UI 沿用既有慣例**：既有的「Include subfolders」checkbox（`IDC_FOLDER_RECURSIVE`）換成一個數字輸入欄位,沿用 `NR-191`「1..1000 with blur clamp」的既有模式（純值 clamp 函式＋`EN_KILLFOCUS`),但範圍是 `0..50`。
5. **watcher 深度對齊列為 non-goal**：`CatalogWatcher` 目前對遞迴資料夾監看整棵子樹,不隨 `max_depth` 收斂監看範圍。這表示超過深度上限的變動仍會觸發一次「找不到新東西」的 debounce rebuild——是效能上的小浪費,不是正確性問題。依 `AGENTS.md`「一項工作半天到兩天,超過先拆」,本 item 不處理,若之後真的在意再開新 item。
6. **schema 遷移是本 item 最關鍵的正確性風險**：見下方「Binding constraints」與「Scope」第 2 節。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-005（本 item 會把其中「recursive flag」的描述換成深度上限)：

> 每個資料夾項目都有獨立的「包含子資料夾」選項；新增時預設勾選，使用者可取消。
> 「包含子資料夾」開啟時遞迴掃描；關閉時只掃描該資料夾第一層，不追蹤目錄 symbolic link／reparse point。

`docs/design-spec.md` §10.2（本 item 會同步這句對 `recursive flag` 的描述)：

> `settings.ini` 保存 `catalog_roots`（多個本機絕對路徑及各自的 recursive flag）與 `catalog_extensions`（受支援副檔名清單）；每個值都要經過格式與安全邊界驗證。

`docs/design-spec.md` §10.4（本 item 的 schema bump **必須**遵守這條,否則會清空所有使用者的 `settings.ini`)：

> 每種資料格式第一行包含 schema version。遇到較新且不支援的版本：不覆寫原檔。將功能退回安全預設。顯示一次錯誤提示。

`docs/design-spec.md` §19（重要實作原則摘要,第 5 點)：

> 不掃描整顆磁碟，也不直接存取受保護的 WindowsApps。

`AGENTS.md`：

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

## 關鍵風險：`settings_store.cpp` 目前沒有處理 `OlderSchema` 的遷移邏輯

`src/settings/settings_store.cpp` 的 `kSchemaVersion` 目前是 `1`，`Load()` 的 switch 只顯式處理 `Loaded`／`Missing`／`NewerSchema`，其餘（含 `OlderSchema`）落在 `default: PreserveCorrupt(...)` 分支，等同整檔判定為 `Corrupt`。這是因為 `settings_store.cpp` 從未 bump 過 schema，`OlderSchema` 這條路目前从未真正被走到。

如果本 item 只是把 `catalog_root=path|recursive(bool)` 改成 `catalog_root=path|max_depth(int)` 並把 `kSchemaVersion` 從 `1` bump 到 `2`，**每一個現有使用者的 `settings.ini`（schema=1）在第一次用新版開啟時都會被判定成 `OlderSchema`，落入 `default: Corrupt`，整份設定（hotkey、include_windows_apps、english_input_on_show、全部自訂資料夾…）被清空重置為預設值並隔離成 `.corrupt`**——這正是 `AGENTS.md`「不得就地覆寫使用者資料」與 `design-spec.md` §10.4 要防止的事，也是 NR-058／NR-080／NR-096 三個既有 item 已經處理過的同一類問題在新欄位上的重現。

**必須**參照 `src/pins/pin_store.cpp:41-43` 已經示範過的模式（NR-062，pin_store 自己也做過一次 schema 1→2 的欄位升級）：

```cpp
case VersionedReadStatus::OlderSchema:
    break;  // 舊格式仍可解析，下次 Save() 時自動升級
```

`settings_store.cpp::Load()` 必須先加一個明確的 `OlderSchema` 分支（讀到舊格式的 `catalog_root=path|true/false` 這一行時，把 `true` 映射成 `max_depth=20`、`false` 映射成 `max_depth=0`），確認測試證明「schema=1 的舊檔可以正常載入、且下次 Save 會升級成 schema=2」之後，才能把 `kSchemaVersion` 改成 `2`。**這個順序不能反過來。**

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`、`docs/work-items.md` 的使用方式、Agent 交付規則、Item 總覽與「已否決的方向 — 不要重開」。
- `docs/design-spec.md` §FR-005、§10.2、§10.4、§19。
- `docs/work-items/NR-004-settings-store.md`、`NR-019-user-folder-catalog.md`、`NR-096-newer-schema-write-guard.md`、`NR-152-settings-write-side-symmetry.md`、`NR-191-recent-count-range-and-blur-clamp.md`（blur clamp 的既有模式範本）；完成 item 文件只讀取，不回頭修改歷史紀錄。
- `src/settings/settings_store.h`：`CatalogRoot`（`recursive: bool` 的定義處）、`kMaxCatalogRoots`、`kSchemaVersion`。
- `src/settings/settings_store.cpp`：`Load()` 的 switch（`OlderSchema` 目前的缺口）、`catalog_root=` 的解析（`ParseBool(raw_recursive)`）與 `Save()` 的寫出格式。
- `src/pins/pin_store.cpp:26-48`：`OlderSchema` 分支的既有範本（NR-062），照抄這個形狀。
- `src/settings/settings_editor.h/.cpp`：`AddRoot`、`SetRootRecursive`（改名／改簽名為 `SetRootMaxDepth`）、dirty tracking。
- `src/app_host/settings_dialog.cpp`：`IDC_FOLDER_RECURSIVE` 的所有使用（`:304`、`:361-362`、`:541-560`、`:586`）——checkbox 換成數字輸入的所有呼叫點。
- `src/resources/resource.h`、`src/resources/NimbleRun.rc`：`IDC_FOLDER_RECURSIVE` 的 control 定義,決定是否需要換成 `ES_NUMBER` edit control 或新增 control ID。
- `src/catalog/user_folder_catalog.cpp`：`EnumerateUserFolderCatalog` 呼叫 `WalkDirectory(root.path, {root.recursive, cancel}, ...)` 的地方——`WalkOptions` 需要從 `bool recursive` 換成深度上限。
- `src/catalog/directory_walker.h/.cpp`：`WalkOptions`、`WalkDirectory` 的遞迴呼叫（`directory_walker.cpp:32-38`）——目前只有 `recursive: bool` 一層判斷，需要改成深度計數與比較。
- `tests/unit/settings_store_test.cpp`、`tests/unit/settings_editor_test.cpp`、`tests/unit/directory_walker_test.cpp`、`tests/unit/user_folder_catalog_test.cpp`：既有 boundary/round-trip 測試，決定要更新哪些既有案例。

## Scope

1. **`CatalogRoot` 欄位**
   - `src/settings/settings_store.h`：`bool recursive = true;` 改成 `int max_depth = 20;`（新增資料夾的預設值)，新增 `kMinCatalogDepth = 0`、`kMaxCatalogDepth = 50` 兩個共用常數（比照 `kMinRecentCount`／`kMaxRecentCount` 的既有形狀，單一定義位置)。
2. **`settings_store.cpp` 的 schema 遷移（見上方「關鍵風險」，必須先做這步）**
   - `Load()` 的 switch 加入 `case VersionedReadStatus::OlderSchema: break;`（比照 `pin_store.cpp:41-43`）。
   - `catalog_root=path|value` 的解析：先嘗試 `ParseBool`（`true`→`max_depth=20`、`false`→`max_depth=0`，涵蓋 schema=1 舊檔),失敗再嘗試整數解析（`0..50`，涵蓋 schema=2 新檔),兩者都失敗則整行捨棄（沿用現行「該行不加入 catalog_roots，其餘行照常」的寬容行為)。
   - 確認新測試證明 schema=1 舊檔可以正確載入並映射，之後才把 `kSchemaVersion` 從 `1` 改成 `2`。
   - `Save()` 一律寫出 `max_depth` 整數；schema 前綴同步改成 `2`。
3. **`SettingsEditor`**
   - `SetRootRecursive` 改為 `SetRootMaxDepth(std::size_t index, int depth)`，驗證 `depth` 落在 `[kMinCatalogDepth, kMaxCatalogDepth]`，越界拒絕且不改 working value（比照 `SetRecentCount` 的既有形狀)。
   - `AddRoot` 的 `recursive: bool` 參數改為 `max_depth: int`（預設呼叫端傳 `20`,對齊「新增時預設」的產品決策)。
4. **`settings_dialog.cpp` UI**
   - `IDC_FOLDER_RECURSIVE` 的 checkbox 換成數字輸入欄位（沿用 `IDC_RECENT_COUNT_EDIT` 的 `ES_NUMBER` + blur clamp 模式，`NR-191` 已示範);純值 clamp 函式覆蓋 `[0, 50]`，空值／非數字／解析溢位在 blur 時保留原文字,Save/OK 才走既有 validation 與錯誤提示。
   - `.rc` 資源把 `IDC_FOLDER_RECURSIVE` 的 checkbox 換成 edit control（沿用既有欄位在對話框中的位置與寬度,不重排版面)。
5. **列舉端**
   - `directory_walker.h`：`WalkOptions::recursive: bool` 改成 `max_depth: int`；`directory_walker.cpp` 的遞迴呼叫（`:32-38`）改為「目前層數 < max_depth 才遞迴」，`max_depth == 0` 等於今天 `recursive=false` 的行為（不遞迴，只看第一層)。
   - `user_folder_catalog.cpp` 呼叫 `WalkDirectory(root.path, {root.max_depth, cancel}, ...)`，語意不變（只是把 bool 換成 int）。
6. **文件同步**
   - `docs/design-spec.md` §FR-005：把「包含子資料夾」的 checkbox 描述改成「深度上限（0~50，預設 20）」；§10.2 的 `recursive flag` 改成 `max_depth`。

## Non-goals

- 不修改 `CatalogWatcher` 的監看範圍（見上方決策 5，列為已知限制，非本 item 範圍）。
- 不新增「無限制」選項或任何 sentinel 值。
- 不修改 `kMaxCatalogRoots`（32）或資料夾數量上限。
- 不修改副檔名 allowlist（`catalog_extensions`）或其驗證邏輯。
- 不修改 `IsReadableRegularFile` 或開檔檢查（見 NR-194，獨立 item）。
- 不修改 `RebuildPipeline` 的 generation／執行緒模型（見 NR-192、NR-195，獨立 item）。
- 不回頭編輯已完成的 NR-004／NR-019／NR-096／NR-152／NR-191 文件或其歷史交接紀錄。
- 不新增網路、telemetry、第三方 runtime、服務、driver 或管理員權限。

## Acceptance

1. `CatalogRoot::max_depth` 存在，`recursive: bool` 已移除，全部呼叫端（含 `settings_editor`、`settings_dialog`、`user_folder_catalog`、`directory_walker`）已更新，無殘留 `recursive` 引用（grep 確認)。
2. `settings_store.cpp`：schema=1 的既有 `catalog_root=path|true` 與 `|false` 檔案可以正確 Load 並映射為 `max_depth=20`／`0`；下一次 Save 後檔案 schema 變成 2 且 `max_depth` 以整數寫出；schema=2 的 `max_depth=0..50` 可 round-trip；`max_depth<0`、`>50` 或無法解析的值该行被捨棄（不污染其他行，不整檔 Corrupt）。
3. `SettingsEditor::SetRootMaxDepth` 對 `0..50` 成功、越界失敗且不改 working value；`AddRoot` 新增資料夾預設 `max_depth=20`。
4. 設定頁：數字輸入的 blur clamp 行為比照 NR-191（`<0`→`0`、`>50`→`50`、恰為端點不改寫、空值／非數字不在 blur 時被覆寫）。
5. `directory_walker`／`user_folder_catalog`：`max_depth=0` 只列舉第一層（等同今天 `recursive=false`）；`max_depth=N` 遞迴 N 層；既有 `directory_walker_test`／`user_folder_catalog_test` 的深度案例通過。
6. `docs/design-spec.md` §FR-005、§10.2 反映新欄位；不再描述成 bool「包含子資料夾」。
7. Release build 無新增 warning；完整 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "settings|directory_walker|user_folder_catalog" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "recursive" src/settings src/app_host/settings_dialog.cpp src/catalog/user_folder_catalog.cpp src/catalog/directory_walker.h src/catalog/directory_walker.cpp
rg -n "max_depth|kMinCatalogDepth|kMaxCatalogDepth|OlderSchema" src/settings/settings_store.cpp src/settings/settings_store.h
```

Focused runnable coverage 必須包含：
- `nimblerun_settings_test`：schema=1 舊檔遷移（`true`→20、`false`→0）、schema=2 round-trip、越界值捨棄、schema bump 後仍可讀回既有 schema=1 檔案。
- `nimblerun_settings_ui_test`：`SetRootMaxDepth` boundary（0/50 接受，-1/51 拒絕）、blur clamp 純值函式。
- `nimblerun_directory_walker_test`：`max_depth=0` 只列第一層、`max_depth=N` 遞迴 N 層後停止的案例。

## Handoff requirements

交接時記錄：

- schema 遷移的實際驗證順序：先証明 `OlderSchema` 分支正確,再 bump `kSchemaVersion`,並附上這個順序被遵守的證據（例如 commit 內兩步分開或測試先行）。
- 舊檔遷移（`true`/`false`→20/0）與新檔 round-trip 的測試結果。
- `.rc` 對話框版面調整前後的截圖或版面尺寸確認（若環境無法操作桌面，明確標記手動驗收未完成）。
- Agent checks 的完整命令與結果。

## 交接區（2026-08-20，實作完成）

1. **CatalogRoot 與 schema 遷移**：`src/settings/settings_store.h:20-39` 將 `recursive` 移除，改為 `CatalogRoot::max_depth = 20`，並在同一處定義 `kMinCatalogDepth = 0`、`kMaxCatalogDepth = 50`。`src/settings/settings_store.cpp:21` 將輸出 schema 設為 2；`Load()` 的 `OlderSchema` 分支在 `:172-174` 明確落在可解析舊格式的路徑，而不是 `PreserveCorrupt`。`catalog_root` 解析位於 `:248-270`：先解析 schema=1 的 `true`／`false` 為 20／0，再解析 schema=2 的整數 0..50，其他值只捨棄該行；`Save()` `:299-312` 寫出 schema=2 與整數深度。實作驗證順序是先補 `OlderSchema` 分支、舊布林映射與 `TestCatalogRootSchemaMigration`，再以 schema=2 的設定重新 configure/build/test；測試會寫入 schema=1 舊檔，確認 true→20、false→0，Save 後讀回 schema=2 整數。這次 item 保持單一最終 commit，沒有額外留下中間 commit；順序證據由 migration test 與其實際通過結果保留在測試和本交接區。

2. **Editor 與設定頁**：`src/settings/settings_editor.cpp:333-343` 的 `ClampCatalogDepthText` 複用既有 `ParseInt`，`SettingsEditor::AddRoot`／`SetRootMaxDepth` 在 `:437-487` 驗證 0..50 並維持 dirty tracking。`src/app_host/settings_dialog.cpp:304` 設定英文 label，`:356-366` 填入每列深度，`:437-452` 在 Save/OK 驗證並失敗時回復，`:559-570` 只在 `EN_KILLFOCUS` 對可解析越界值做純文字 clamp；空值、非數字與溢位保留原文字。`src/resources/NimbleRun.rc:63` 沿用原位置，把 checkbox 換成 `LTEXT`（16,230,116,10）與 `EDITTEXT`（136,228,40,14、`ES_NUMBER`），`IDC_FOLDER_DEPTH_LABEL` 定義在 `src/resources/resource.h:43`。本環境無法操作桌面或擷取設定頁截圖，因此實際 DPI 下的手動 UI 驗收未完成；上述 `.rc` 位置與尺寸已完成原始資源檢查，純值行為由可執行測試覆蓋。

3. **列舉與 caller trace**：`src/catalog/directory_walker.h:12` 與 `src/catalog/directory_walker.cpp:6-63` 以目前深度與 `max_depth` 比較，0 只掃 root 第一層，N 再展開 N 層；`src/catalog/user_folder_catalog.cpp` 傳入 `root.max_depth`。`src/app_host/main.cpp:1444-1446` 僅把 `max_depth > 0` 映射到既有整棵 subtree watcher，`CatalogWatcher` 本身未修改，符合本 item 的 non-goal。`src/catalog/start_menu_catalog.cpp:262` 對既有 Start Menu 內部來源傳 `std::numeric_limits<int>::max()`，保留原本完整掃描語意；這不是使用者可選的「無限制」值，設定頁仍只有 0..50，user-folder 也沒有 sentinel。

4. **測試覆蓋**：`tests/unit/settings_store_test.cpp:239-265` 驗證 schema=1 遷移與 Save→schema=2 round-trip，`:212-235` 驗證整數端點與非法值逐行捨棄；`tests/unit/settings_editor_test.cpp:139-181` 覆蓋 setter 0/50、-1/51 與 blur clamp；`tests/unit/directory_walker_test.cpp:33-57` 覆蓋深度 0/1/2；`tests/unit/user_folder_catalog_test.cpp` 更新每個 root 的深度呼叫；相關 refresh caller 也已更新。完整 CTest 中 Start Menu 深度案例、settings、settings UI、walker、user-folder、lifecycle、rebuild 全部通過。

5. **Agent checks**：
   - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：成功。
   - `cmake --build build`：成功；最後一次為 `ninja: no work to do.`，先前本 item 完成後的 Release build 亦成功且無新增 warning。
   - `ctest --test-dir build -R "settings|directory_walker|user_folder_catalog" --output-on-failure`：4/4 passed。
   - `ctest --test-dir build --output-on-failure`：32/33 passed；唯一失敗為已知、與本 item 無關的 `nimblerun_startup_option_test`（`FAILED: enable writes the entry` registry-write）。其餘 32 個通過，未嘗試修理該既知失敗。
   - `rg -n "recursive" src/settings src/app_host/settings_dialog.cpp src/catalog/user_folder_catalog.cpp src/catalog/directory_walker.h src/catalog/directory_walker.cpp`：無輸出；`rg -n "max_depth|kMinCatalogDepth|kMaxCatalogDepth|OlderSchema" src/settings/settings_store.cpp src/settings/settings_store.h`：命中共用欄位、邊界常數、遷移分支與讀寫位置。
   - `git diff --check`：無輸出。

6. **未完成事項**：只剩無桌面環境下的設定頁人工視覺／互動驗收；沒有宣稱該項通過。CatalogWatcher 深度收斂、readability probe、rebuild pipeline generation／startup ordering 均依 non-goal 留給 NR-194／NR-195 或後續 item。
