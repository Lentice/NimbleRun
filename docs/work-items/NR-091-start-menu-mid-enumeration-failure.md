# NR-091 — Start Menu 中途列舉失敗不得提交部分結果

Phase 2 · Depends on: NR-005, NR-011, NR-063

- Source: `docs/design-spec.md` §FR-008、§NFR-003、§12.1／§12.2
- Origin: 2026-08-08 第八次全 repo 稽核（Win32 directory enumeration error boundary）
- Priority: HIGH（部分 Start Menu 結果會覆蓋完整舊 snapshot，連帶影響使用紀錄對帳）

## Why

`src/catalog/start_menu_catalog.cpp` 的 `EnumerateDirectoryRecursive()` 是 `void`，
在 `FindNextFileW()` 回傳 `FALSE` 後直接 `FindClose()`；它沒有區分正常的
`ERROR_NO_MORE_FILES` 與中途 I/O／權限錯誤。`EnumerateStartMenuCatalog()` 目前只以
Known Folder path 是否解析成功決定 `source_ok`，所以 Known Folder 已解析但掃描中途
失敗時，已收集的前綴 entries 仍會被 `main.cpp` worker 當成成功結果送入
`CatalogRefreshCoordinator::ApplySourceResult()`，取代原本完整的 Start Menu source。

這違反 §FR-008 的「成功後才整批替換」；後續 `RefreshPanelSnapshot()` 會把部分
snapshot 當成真實 Catalog 對帳，可能讓暫時消失的 App 從 recent 資料中被清掉。
這是 NR-090 已處理的 AppsFolder 同型問題，但 Start Menu 使用 Win32 directory API，
不能只複製 Shell HRESULT 的修法。

## Decisions already made — do not reopen

1. `FindNextFileW()` 的 `FALSE` 只有在 `GetLastError() == ERROR_NO_MORE_FILES` 時是
   clean end；其他錯誤一律令該次 Start Menu enumeration `source_ok = false`。
2. 只要已開啟的 Programs directory 在遞迴中途失敗，整個 Start Menu source 不提交
   本次 partial entries；由既有 `ApplySourceFailure()` 保留舊 source。不要新增 per-file
   或 per-directory snapshot 層級。
3. 既有 NR-063 的 Known Folder resolution／兩個根目錄均無法解析語意不改；本 item
   只補「目錄 walk 已開始後」的 clean-end／failure 分流。
4. 單一損壞 shortcut 仍是 item-level skip；不要把 `ProcessFile()` 的既有失敗升級成
   source failure。
5. 不新增重試、timer、Shell COM mock 或 UI 通知；沿用 source failure 的既有事件驅動
   retry 節奏。

## Binding constraints — quoted

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/development.md`：

> Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/start_menu_catalog.cpp:189-217` — `EnumerateDirectoryRecursive()` 的
  recursive walk 與 `FindNextFileW()` 結束判定。
- `src/catalog/start_menu_catalog.cpp:231-264` — `EnumerateStartMenuCatalog()`、
  `EnumerateProgramsDirectory()` 與 `source_ok` 的邊界。
- `src/catalog/start_menu_catalog.h` — `StartMenuEnumerateResult` 與可注入的
  `EnumerateProgramsDirectory()` contract。
- `src/app_host/main.cpp:1303-1310` — Start Menu worker 如何把 `source_ok` 轉成
  `RebuildResult::failed`。
- `src/app_host/main.cpp` 的 `kRebuildDoneMessage` handler — 失敗如何進入
  `ApplySourceFailure()`，成功才套用 entries。
- `src/catalog/catalog_refresh.{h,cpp}` — 既有 source failure preservation；不要改
  coordinator semantics。
- `tests/unit/start_menu_catalog_test.cpp` — synthetic directory fixture、missing-root
  check、live `source_ok` smoke check。
- `docs/work-items/NR-063-source-failure-reaches-refresh.md`、
  `docs/work-items/NR-090-appsfolder-mid-enumeration-failure.md` — 既有 source failure
  與同型 AppsFolder 邊界，避免重複設計。

## Scope

1. 讓 recursive directory walk 能把 clean end 與 enumeration failure 傳回呼叫端；
   recursive child 的失敗也必須一路傳回，不得被父層吞掉。
2. 在 Start Menu source result 設定 `source_ok = false`，讓既有 worker／coordinator
   failure path 保留舊 snapshot；不要在 caller 另加第二套 partial-result guard。
3. 保留現有 shortcut filtering、Shell resolution、stable id、source priority 與
   missing Known Folder 的 NR-063 語意。
4. 在 `tests/unit/start_menu_catalog_test.cpp` 增加最小 clean-end／result contract
   check；若 OS failure 無法可靠注入，使用既有 live fixture 加 source-code sanity
   check，不為製造一個 HRESULT 而建立假的 Shell／filesystem abstraction。

## Non-goals

- 不修改 AppsFolder（NR-090 已處理）或 UserFolder（NR-092 分開處理）。
- 不改 `CatalogRefreshCoordinator`、generation ordering、dedup、usage／pin schema。
- 不把單一壞 shortcut、無法解析 target 或 icon failure 升級為來源失敗。
- 不新增背景 retry、polling、取消協定、UI 文案或診斷資料格式。
- 不改 NR-063 對 Known Folder path resolution 的既有決策。

## Acceptance criteria

1. `ERROR_NO_MORE_FILES` 是唯一把 `FindNextFileW(FALSE)` 視為成功結束的情況。
2. 其他 `FindNextFileW` 錯誤，包含 recursive child 的錯誤，都會令
   `StartMenuEnumerateResult::source_ok == false`，不提交已收集的 prefix entries。
3. 既有 worker 只透過 `source_ok` 設定 `failed`；失敗結果最後由
   `ApplySourceFailure()` 保留舊 Start Menu entries。
4. 正常 synthetic／live enumeration 的 entry invariants、stable ids 與 determinism
   不變；Release build 與完整 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path src/catalog/start_menu_catalog.cpp -Pattern 'ERROR_NO_MORE_FILES|source_ok = false'
# expect: explicit clean-end and failure branches in the directory-walk path

Select-String -Path src/app_host/main.cpp -Pattern 'result->failed = !res.source_ok|ApplySourceFailure'
# expect: existing source_ok handoff remains the only caller-side failure route

git diff --check
```

## 交接區

`EnumerateDirectoryRecursive` 與 `EnumerateProgramsDirectory` 現在回傳 walk 成功狀態；
只有 `ERROR_NO_MORE_FILES` 是 clean end，recursive child failure 會一路傳回並令
`StartMenuEnumerateResult::source_ok` 為 false。既有 worker／coordinator failure path
保留舊 source snapshot。`tests/unit/start_menu_catalog_test.cpp` 覆蓋 clean fixture
重複列舉與 missing-root 的 NR-063 clean-empty contract。

Release build 成功，完整 CTest **24/24 通過**，sanity greps 與 `git diff --check`
通過。手動驗收未執行；未完成事項：無。
