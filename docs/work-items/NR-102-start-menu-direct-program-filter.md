# NR-102 — Start Menu 的直接檔案也必須通過 program-like／uninstaller filter

Phase 2 · Depends on: NR-005, NR-028

- Source: `docs/design-spec.md` §FR-004、§FR-004a（Start Menu program-like 判準）
- Origin: 2026-08-09 全 repo 稽核（`AcceptExtension`→`ProcessFile` flow trace）
- Priority: MEDIUM（Start Menu 可能顯示並允許直接啟動 uninstaller executable）

## Why

`AcceptExtension` 接受 Start Menu 目錄中的 `.lnk`、`.appref-ms` 與 `.exe`，但
`ProcessFile` 目前只在 `.lnk` 分支呼叫 `IsProgramLikeTarget`。因此直接放在 Programs
folder 的 `unins000.exe`／`uninstaller.exe` 不會套用既有 `unins*` exclusion；直接 `.exe`
也沒有與 shortcut target 相同的 extension／program-like 判定。

這讓同一個 Start Menu source 依檔案型態走兩套不同規則，違反 FR-004a 的共用判準，也把
已存在的 `app_filter` 安全／UX 防線繞過。FR-005 使用者自訂資料夾的刻意例外不受此 item
影響。

## Decisions already made — do not reopen

1. 直接檔案沿用既有 `IsProgramLikeTarget`，不另寫一份 blacklist 或 stem 判斷。
2. `.lnk` 的 target resolution、website shortcut、unresolvable link 保持現況。
3. 不把 FR-004a 套到 FR-005 user-folder source（既有 rejected direction）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-004a：

> Start Menu 與 AppsFolder 共用同一份「可執行／程式型」判準；副檔名白名單為 `.exe`、`.com`、`.bat`、`.cmd`、`.lnk`、`.appref-ms`、`.msc`；stem 為 `unins*` 的 uninstaller 排除。

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/start_menu_catalog.cpp` — `AcceptExtension`、`ProcessFile`、
  `EnumerateDirectoryRecursive`。
- `src/catalog/app_filter.{h,cpp}` — `IsProgramLikeTarget` 的唯一判定來源。
- `tests/unit/start_menu_catalog_test.cpp` — existing direct fixture 與 uninstaller shortcut case。
- `tests/unit/app_filter_test.cpp` — extension／uninstaller contract。
- `docs/work-items/NR-028-appsfolder-launch-identity.md` — shared filter origin。

## Scope

1. 讓 `ProcessFile` 對非 `.lnk` 的 accepted file 也經過既有 shared program-like predicate；
   `.lnk` 仍以 resolved target 為判定輸入。
2. 在 Start Menu fixture 加入：正常 direct `.exe` 保留、`unins000.exe`／uninstaller
   direct file 排除，並確認既有 shortcut／`.appref-ms` 結果不變。
3. 確認 catalog source 的 stable id、launch identity、user-folder 行為未被順手改動。

## Non-goals

- 不修改 `IsProgramLikeTarget` 的 allowlist 或 uninstaller stem 規則。
- 不解析 `.appref-ms` 內容、不新增 Shell launch path。
- 不套用到 user-folder source，也不改 AppsFolder enumeration。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. direct launchable `.exe` 出現在 Start Menu catalog；direct `unins000.exe` 與
   `uninstaller.exe` 不出現。
3. 既有 `.lnk` website／uninstaller／unresolvable tests 原樣通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "start_menu_catalog|app_filter" --output-on-failure
```

```powershell
Select-String -Path src/catalog/start_menu_catalog.cpp -Pattern 'IsProgramLikeTarget|AcceptExtension|ProcessFile'
git diff --name-only
# expect: filter 使用既有 app_filter；不新增 blacklist helper。
```

## 交接區

實作完成（2026-08-09）。

- **Direct-file 判定位置**：`src/catalog/start_menu_catalog.cpp:150`，`ProcessFile` 的
  `if (ext == L".lnk")` 後接 `else if (!IsProgramLikeTarget(path)) { return; }`。`.lnk`
  分支原樣保留（仍以 resolved target 為判定輸入、空 target 保留）；非 `.lnk` 的直接
  `.exe`／`.appref-ms` 以完整檔案路徑送入既有共享 `IsProgramLikeTarget`（FR-004a）。
  `AcceptExtension`（line 23）維持原樣，僅作第一道便宜 gate；未新增 blacklist helper。
- **Fixture 變更**（`tests/unit/start_menu_catalog_test.cpp`）：`TestFixtureEnumeration`
  在 `Portable.exe` 之後新增 `WriteBytes(root + L"\\unins000.exe", "dummy")` 與
  `WriteBytes(root + L"\\uninstaller.exe", "dummy")`；新增斷言
  `FindByName(entries, L"unins000") == nullptr` 與 `FindByName(entries, L"uninstaller") == nullptr`
  （兩個 direct file 都被排除，故既有 `entries.size() == 8` 仍成立）；`Portable` direct
  exe 保留斷言不變、仍通過。
- **Build／CTest**：Release x64（LLVM-MinGW + Ninja）建置成功、無新增警告；
  `ctest --test-dir build --output-on-failure` **25/25 全綠**；
  `ctest --test-dir build -R "start_menu_catalog|app_filter"` **2/2**。
- **Sanity greps**：`IsProgramLikeTarget` 在 `ProcessFile` 命中兩處（.lnk target 與
  direct file path），皆呼叫既有 `catalog/app_filter.h`，無新增黑名單；
  `git diff --name-only` 僅列 `src/catalog/start_menu_catalog.cpp`、
  `tests/unit/start_menu_catalog_test.cpp`、本文件。
- **偏差**：無。Non-goals 全數遵守：未改 `app_filter.{h,cpp}`、未解析 `.appref-ms`、
  未動 AppsFolder／user-folder source、未改 `AcceptExtension` allowlist、未動 NR-047
  search_alias。status 未變更（由主控 agent 擁有）。

