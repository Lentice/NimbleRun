# NR-092 — UserFolder 中途列舉失敗不得提交部分結果

Phase 2 · Depends on: NR-011, NR-019, NR-063

- Source: `docs/design-spec.md` §FR-005、§FR-008、§NFR-003、§11／§12.1
- Origin: 2026-08-08 第八次全 repo 稽核（UserFolder walk status 不會到 worker）
- Priority: HIGH（部分自訂資料夾結果會取代完整舊 source snapshot）

## Why

`src/catalog/user_folder_catalog.cpp` 的 `ScanDirectory()` 是 `void`，
`FindNextFileW()` 回傳 `FALSE` 時沒有檢查 `GetLastError()`。因此一個已開啟的
使用者資料夾在遞迴中途遇到 I/O 或權限錯誤時，函式會把已收集的前綴 entries 當成
完整結果返回。`EnumerateUserFolderCatalog()` 只回傳 `std::vector<AppEntry>`，而
`src/app_host/main.cpp:1324-1327` 的 UserFolder worker 永遠不設定
`RebuildResult::failed`，所以 partial vector 會直接取代舊 UserFolder source。

這違反 §FR-008 的完整 snapshot 契約，也會讓後續 usage reconciliation 把暫時不可見
的自訂 App 誤判為已不存在。`user_folder_catalog.h` 目前的註解已承諾「bad
subdirectory ... 不清除其他 roots」，但沒有任何 status 能讓 worker 知道整次 walk
是否完整，文件承諾與實際 call path 不一致。

## Decisions already made — do not reopen

1. `FindNextFileW(FALSE)` 只有在 `GetLastError() == ERROR_NO_MORE_FILES` 時算 clean
   end；其他錯誤令該次 UserFolder source 回報 `source_ok = false`。
2. 已開啟 root 的 recursive child walk 失敗時，整個 UserFolder enumeration 不提交
   partial vector；先沿用既有 source failure path 保留舊 UserFolder snapshot。暫不引入
   per-root snapshot，以免為一次錯誤處理擴大 catalog model。
3. **覆寫 NR-063 的範圍僅限於中途 walk failure 的新證據，不覆寫其缺失 root 決策**：
   設定中的 nonexistent／非本機 root 仍依既有行為略過；本 item 不把「使用者刪掉
   自己設定的資料夾」改成新的錯誤 UX。
4. 單一不可讀檔案仍依 FR-005 skip and continue；不要把 per-file anomaly 變成 source
   failure。
5. 不新增 retry、timer、root watcher 變更或 UI 通知；失敗後等既有事件／manual refresh
   再試。

## Binding constraints — quoted

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/design-spec.md` §NFR-003：

> 任一使用者資料夾不存在、無權限或無法列舉時，不得清空其他 Catalog 來源。

`docs/development.md`：

> Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/user_folder_catalog.cpp:96-145` — `ScanDirectory()`、recursive call、
  `EnumerateUserFolderCatalog()`。
- `src/catalog/user_folder_catalog.h` — current vector return contract and its claim that
  bad roots do not clear other roots.
- `src/app_host/main.cpp:1324-1327` — UserFolder worker；確認目前沒有 `source_ok`／
  `failed` handoff。
- `src/app_host/main.cpp` 的 `kRebuildDoneMessage` handler — existing failure path and
  source snapshot application。
- `src/catalog/catalog_refresh.{h,cpp}` — `ApplySourceFailure()` preservation；不要改
  coordinator semantics。
- `tests/unit/user_folder_catalog_test.cpp` — multiple roots、recursive／flat、missing
  root 與 unreadable file fixtures。
- `docs/work-items/NR-063-source-failure-reaches-refresh.md` — prior decision；本 item
  必須保留其 missing-root 語意。

## Scope

1. 讓已開啟目錄的 recursive walk 回報是否完整；把 child walk failure 傳回 root，
   再由 `EnumerateUserFolderCatalog()` 組成一個 source-level result（plain entries
   加 `source_ok` 或等價值型別）。
2. 修改 UserFolder worker 只做薄轉接：`result->failed = !res.source_ok`、entries
   仍以 value move 傳回；失敗結果走既有 `ApplySourceFailure()`。
3. 保留 extension allowlist、recursive flag、readable regular file 判斷、stable id、
   duplicate root 行為及 missing／non-local root 的 NR-063 語意。
4. 在 `tests/unit/user_folder_catalog_test.cpp` 增加最小 result contract／clean-end
   check，並以 source-code sanity check 鎖定 `ERROR_NO_MORE_FILES` 與 worker handoff。
   不為製造 OS-level `FindNextFileW` failure 建立大型 fake filesystem layer。

## Non-goals

- 不修改 Start Menu（NR-091）、AppsFolder（NR-090）或 watcher debounce。
- 不引入 per-root catalog source、partial merge、取消協定或 background retry。
- 不改缺失 root 的既有「略過」行為，不新增設定欄位、通知或錯誤對話框。
- 不改 UserFolder 的副檔名 allowlist、Shell launch、stable id、dedup 或 usage schema。

## Acceptance criteria

1. `ERROR_NO_MORE_FILES` 是唯一正常結束；其他 `FindNextFileW` 錯誤會令
   `source_ok == false`，且不提交任何該次 partial UserFolder vector。
2. Recursive child failure 能傳到 source result；worker 將其轉成 `failed`，
   `CatalogRefreshCoordinator` 保留上一份 UserFolder entries。
3. 既有 multiple-root、missing-root、flat／recursive、extension 與 stable-id 測試
   語意不變；Release build 與完整 CTest 通過。
4. 沒有新增常駐執行緒、timer、retry、第三方依賴或 UI text。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path src/catalog/user_folder_catalog.cpp -Pattern 'ERROR_NO_MORE_FILES|source_ok = false'
# expect: explicit clean-end and failure branches in the directory-walk path

Select-String -Path src/app_host/main.cpp -Pattern 'EnumerateUserFolderCatalog|result->failed = !res.source_ok|ApplySourceFailure'
# expect: UserFolder result status reaches the existing source failure route

git diff --check
```

## 交接區

`ScanDirectory` 現在只把 `ERROR_NO_MORE_FILES` 視為 clean end，recursive child
failure 會傳回 `UserFolderEnumerateResult::source_ok = false`；missing、unreadable、
non-local roots 仍是 NR-063 clean skip。UserFolder worker 只轉接 `failed` 與 value
entries，失敗會沿用既有 `ApplySourceFailure` 保留舊 snapshot。測試新增 source-code
sanity check 並保留 multiple-root／recursive／allowlist 回歸。

Release build 成功，完整 CTest **24/24 通過**，sanity greps 與 `git diff --check`
通過。手動驗收未執行；未完成事項：無。
