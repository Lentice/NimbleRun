# NR-202 — Uninstaller 檔名判準：收窄前綴並套用到使用者自訂資料夾

Phase 5 · Release gate · Depends on: NR-019, NR-028, NR-193

- Source: `docs/design-spec.md` §FR-004a、§FR-005；`docs/development.md` 的 `catalog`
  module boundary
- Origin: 2026-09-08 使用者實機回報。搜尋 `uni` 時前 6 名全是
  `D:\Program files\SuperTSC64\uninst.exe`、`D:\Program files\PCMan\uninst.exe`、
  `D:\Program files\FastStoneImageViewer\uninst.exe`、
  `D:\Program files\Revo Uninstaller\unins000.exe`、
  `D:\Program files\YodaoDict\unins000.exe`、`D:\Program files\PotPlayer\uninstall.exe`，
  使用者真正要的 `Revo Uninstaller` 被壓到第 10 位
- Priority: **HIGH**——這 6 筆是掃描噪音，會在**每一個**能命中它們的 query 裡擋路，
  不只 `uni`

## 覆寫聲明 — 必讀

`docs/work-items.md` §已否決的方向 有這一列：

> | 把 FR-004a 的 program-like 判準套用到 FR-005 使用者自訂資料夾 | `docs/design-spec.md:354` |
> 明文「此判準**不套用於** FR-005 的使用者自訂資料夾」。該來源的把關者是使用者自己
> 勾選的副檔名清單；二次過濾會無聲擋掉使用者手動加入的副檔名。 |

**本 item 部分覆寫該否決，並且只覆寫其中一半。**

覆寫理由：該否決的依據只證成了排除**副檔名白名單**（FR-004a 判準第 3 條）。
FR-004a 判準第 4 條的 uninstaller 檔名規則與副檔名**完全正交**——它只看檔名主體，
擋不掉任何使用者手動加入的副檔名。兩條規則被綁在同一個 `IsProgramLikeTarget()` 裡
一起豁免，是實作耦合造成的，不是產品決策要的結果。

新證據：上方 Origin 的實機截圖。使用者自訂資料夾遞迴掃描 `D:\Program files` 時，
每個 Inno Setup／NSIS 安裝的 App 都會留下一支 `unins000.exe`／`uninst.exe` 殘骸，
它們全部進了 catalog。這是該否決寫下時沒有的資料。

**本 item 明確維持否決的另一半**：副檔名白名單（判準第 1～3 條）**依然不套用**於
FR-005。使用者自訂資料夾的副檔名把關者仍然只有使用者自己的清單。

## Goal

兩件事：

1. 把 uninstaller 判準從 `unins` **前綴**收窄成**精確檔名清單**，消除誤殺。
2. 把收窄後的 uninstaller 判準（且**只有**這一條）套用到 FR-005 使用者自訂資料夾。

搜尋排名規則（§4.5）、使用分數（§4.6）與任何持久化格式都不在本 item 範圍內。

## 誤殺問題 — 這是收窄的理由

現行 `src/catalog/app_filter.cpp` 的規則是 `stem.compare(0, 5, L"unins") == 0`，
即「檔名主體以 `unins` 開頭」。這個前綴會誤殺真正的解除安裝管理工具：

| 檔名主體 | 現行前綴規則 | 應有結果 |
|---|---|---|
| `unins000` | 排除 | 排除 |
| `uninst` | 排除 | 排除 |
| `uninstall` | 排除 | 排除 |
| `uninstaller` | 排除 | 排除 |
| `Uninstall Tool` | **排除（誤殺）** | 保留 |
| `UninstallView` | **排除（誤殺）** | 保留 |
| `Uninstalr` | **排除（誤殺）** | 保留 |
| `RevoUninst` | 保留 | 保留 |

誤殺**現在就已經存在**於 Start Menu 與 AppsFolder 兩個來源。若不先收窄就把規則
擴大到第三個來源，只會把誤殺面積擴大到三倍。

漏網取捨：收窄後 `Uninstall_ProductName.exe` 這類非固定命名的殘骸會漏進 catalog。
**這是刻意接受的**：多一列噪音是可見且可忽略的；誤殺一個真 App 是靜默的，使用者
只會發現「我的程式找不到」而無從得知原因。現行註解「mistargeting a removal prompt
is too costly」高估了代價——從 launcher 啟動解除安裝程式只會開啟確認畫面，可以取消。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Use the C++ standard library or Win32 native APIs before adding dependencies.

> Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.

> New non-trivial logic needs one focused runnable test or self-check.

> Keep changes scoped to the requested task and update the relevant documentation when behavior changes.

`docs/development.md`：

> Core value types should remain copyable and testable without HWND or Shell COM ownership.

`docs/design-spec.md` §FR-004a：

> Start Menu 與 AppsFolder **共用同一份判準**，集中於單一純值模組，兩個列舉器都必須經過它。

> 4. 檔名主體為 `unins*` 者無論副檔名一律排除（解除安裝程式誤觸代價過高）。

> 此判準**不套用於 FR-005 的使用者自訂資料夾**。該來源的把關者是使用者自己勾選的
> 副檔名清單；若對它二次過濾，使用者手動加入的副檔名會被無聲擋掉。

`docs/design-spec.md` §FR-004：

> 排除解除安裝、說明、網站捷徑等非 App 項目；判斷依目標型別（URL scheme／副檔名），
> 不以名稱黑名單作唯一依據。

## Files to read and trace first

- `docs/design-spec.md` §FR-004、§FR-004a、§FR-005。
- `docs/work-items.md` 的 §已否決的方向（本 item 覆寫其中一列）。
- `docs/work-items/NR-028-appsfolder-launch-identity.md`：FR-004a 判準的原始決策。
- `docs/work-items/NR-019-user-folder-catalog.md`：FR-005 來源的原始決策。
- `src/catalog/app_filter.h`、`src/catalog/app_filter.cpp`：`FileStem`、`ToLower`、
  `IsProgramLikeTarget` 的現有判斷順序。
- `src/catalog/user_folder_catalog.cpp`：`ExtensionAllowed`、`IsRegularFile`、
  `ProcessFile` 與 `WalkDirectory` callback。
- `src/catalog/start_menu_catalog.cpp`、`src/catalog/appsfolder_catalog.cpp`：
  現有的 `IsProgramLikeTarget` caller。
- `tests/unit/app_filter_test.cpp`：既有 `TestUninstallers` 三個 case。

## Scope

### 1. 收窄並抽出 uninstaller 判準

在 `app_filter.h`／`app_filter.cpp` 新增一個純值 predicate：

```cpp
// True when the file stem is a setup-generated uninstaller stub (design-spec
// §FR-004a rule 4). Matches the fixed names Inno Setup and NSIS produce, not a
// "unins" prefix family: a prefix also kills real uninstaller-manager apps such
// as "Uninstall Tool" and "UninstallView".
bool IsUninstallerStem(std::wstring_view target);
```

判斷方式：取 `FileStem(target)` 並 `ToLower`，命中以下任一即為 true——

- 精確等於 `uninst`、`uninstall`、`uninstaller`；
- `unins` 後接**一個以上且全部**為十進位數字（`unins000`、`unins001`、`unins123`）。

`unins` 單獨一字、`uninstall tool`、`uninstallview`、`uninstalr`、`revouninst`
一律不命中。

`IsProgramLikeTarget` 的第 4 條改為呼叫 `IsUninstallerStem`，判斷順序不變
（AUMID → URL scheme → uninstaller → 副檔名白名單）。不得改動其餘三條。

### 2. 套用到使用者自訂資料夾

`user_folder_catalog.cpp` 的 `WalkDirectory` callback 在既有 `ExtensionAllowed`
通過之後、`ProcessFile` 之前，加入 `IsUninstallerStem(path)` 的排除。

- **只**呼叫 `IsUninstallerStem`，不得呼叫 `IsProgramLikeTarget`——後者含副檔名
  白名單，會擋掉使用者自己加入的副檔名，那正是本 item 維持否決的那一半。
- 不改 `ExtensionAllowed`、`DefaultExtensions()`、`IsRegularFile`、
  `settings.catalog_extensions`、`catalog_roots`、`max_depth` 或 walker 行為。
- 被排除的檔案是正常過濾，**不計入** `result.skipped_directories`，也不寫診斷。

### 3. focused test

`tests/unit/app_filter_test.cpp` 的 `TestUninstallers` 擴充為兩組：

- 仍應排除：`unins000.exe`、`unins001.exe`、`Uninstall.exe`、`uninstaller.exe`、
  `uninst.exe`、大小寫混合形式；
- **必須保留**（收窄的迴歸保護）：`Uninstall Tool.exe`、`UninstallView.exe`、
  `Uninstalr.exe`、`RevoUninst.exe`、`unins.exe`。

`IsUninstallerStem` 本身也需直接覆蓋，因為它現在有第二個 caller。

## Non-goals

- **不**把副檔名白名單或 FR-004a 的其他任何一條套用到 FR-005。
- 不改 §4.5 排名分層、§4.6 使用分數、搜尋比較器或任何排序 tie-break。
- 不加入選擇記憶、query affinity、per-query 權重或任何新的持久化狀態。
- 不改 `settings.ini` schema、UI label、設定頁或預設副檔名清單。
- 不新增使用者可見的過濾開關（§FR-004a 明文「不提供設定開關」）。
- 不改 dedup、stable id、icon、catalog refresh、snapshot assembler 或 walker。
- 不處理 `Uninstall CMake` 這類 Start Menu 捷徑——它的 resolved target 是
  `msiexec.exe`，屬於「捷徑名稱 vs 解析目標」的獨立問題，不在本 item 範圍。

## Acceptance

1. `IsUninstallerStem` 存在於 `app_filter` 純值模組，不含 `<windows.h>`。
2. `IsProgramLikeTarget` 的 uninstaller 條件改用 `IsUninstallerStem`，其餘三條
   判斷與順序不變；既有三個 `TestUninstallers` case 仍通過。
3. `Uninstall Tool`、`UninstallView`、`Uninstalr`、`RevoUninst`、`unins` 在三個
   來源都**不再**被排除。
4. `user_folder_catalog` 排除 uninstaller 殘骸，且**不**呼叫 `IsProgramLikeTarget`；
   使用者自訂副檔名（含非白名單型別）依然能進 catalog。
5. 被過濾的檔案不計入 `skipped_directories`、不產生診斷、不影響 `source_ok`。
6. Release build 無新增 warning；`ctest --test-dir build --output-on-failure` 全通過。
7. `docs/design-spec.md` §FR-004a 第 4 條改為精確清單，末段豁免說明改為
   「僅副檔名白名單不套用於 FR-005；uninstaller 檔名判準一律套用」。
8. `docs/work-items.md` §已否決的方向 該列更新為「已由 NR-202 部分覆寫」，並保留
   原否決理由中仍然有效的那一半。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```powershell
ctest --test-dir build -R "app_filter|catalog" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "IsProgramLikeTarget" src/catalog/user_folder_catalog.cpp
# expect: zero hits -- the extension whitelist must not reach FR-005.
rg -n "compare\(0, 5" src/catalog/app_filter.cpp
# expect: zero hits -- the prefix rule is gone.
rg -n "IsUninstallerStem" src/catalog
# expect: app_filter.h, app_filter.cpp, user_folder_catalog.cpp.
```

## Handoff requirements

Record:

- `IsUninstallerStem` 的最終判準與確切匹配清單；
- 兩個 caller 的呼叫點與順序；
- 收窄後刻意漏網的殘骸命名形式；
- 新增／更新的 test case 與實際結果；
- Release build、focused CTest、full CTest、`git diff --check` 輸出。

## 交接區

### 落地結果（2026-09-08）

- 新增 `nimblerun::IsUninstallerStem(std::wstring_view)` 於 `src/catalog/app_filter.h`
  （宣告）與 `app_filter.cpp`（定義）。判準：`ToLower(FileStem(target))` 精確等於
  `uninst`／`uninstall`／`uninstaller`，或等於 `unins` 後接一個以上且全為
  `iswdigit` 的字元。純值邏輯，模組仍不含 `<windows.h>`。
- `IsProgramLikeTarget` 的第 4 條由 `stem.compare(0, 5, L"unins") == 0` 改為呼叫
  `IsUninstallerStem(target)`；AUMID → URL scheme → uninstaller → 副檔名白名單的
  判斷順序未變。
- `user_folder_catalog.cpp` 的 `WalkDirectory` callback 改為
  `if (ExtensionAllowed(path, extensions) && !IsUninstallerStem(path))`。
  該檔**未**引入 `IsProgramLikeTarget`，副檔名白名單依然不觸及 FR-005。
  被過濾的檔案只是不呼叫 `ProcessFile`，不計入 `skipped_directories`。
- 刻意漏網：`Uninstall_ProductName.exe`、`uninstall-app.exe` 這類非固定命名的殘骸。
  取捨理由見本文件 §誤殺問題。

### 驗證證據

- `cmake -S . -B build-nr202 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：通過。
- `cmake --build build-nr202`：通過，無新增 warning。
- `ctest --test-dir build-nr202 -R "app_filter" --output-on-failure`：
  `nimblerun_app_filter_test` 通過。
- `ctest --test-dir build-nr202 --output-on-failure`：33/33 全數通過，42.97s。
- `git diff --check`：無輸出。
- 兩個 `rg` check 的**字面結果與文件預期不符，但意圖已達成**，如實記錄：
  - `rg -n "IsProgramLikeTarget" src/catalog/user_folder_catalog.cpp`：1 命中，
    在新增註解的 `// IsProgramLikeTarget() must NOT be used here` 一行，非呼叫。
    該檔沒有任何對它的呼叫。
  - `rg -n "compare\(0, 5" src/catalog/app_filter.cpp`：1 命中，在
    `IsUninstallerStem` 內部。它已不是原本的前綴規則——前面有
    `stem.size() <= 5` 的守門，後面接全數字檢查，整體語意是「`unins` 後接
    一個以上且全為數字」的精確匹配，而非「以 `unins` 開頭」。
- `rg -n "IsUninstallerStem" src/catalog`：命中 `app_filter.h`、`app_filter.cpp`、
  `user_folder_catalog.cpp`，符合預期。

### 手動驗證

實機 `uni` 查詢的前 6 名殘骸是否消失，需在有 `D:\Program files` 掃描根的桌面環境
重建 catalog 後確認；本次 Agent session 未執行。
