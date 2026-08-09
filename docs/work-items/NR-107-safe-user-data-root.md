# NR-107 — Per-user data root 不得 fallback 到空／相對路徑

Phase 1 · Persistence and safety boundary

- Source: `docs/design-spec.md` §10.1、§10.2、§NFR-004、§NFR-005
- Origin: 2026-08-09 全 repo 稽核；追蹤 `DefaultSettingsDir()` 的 failure return 到 `wWinMain` 所有 store 建構點
- Priority: HIGH（可能使 cache 寫到 current directory／根目錄，或讓 user data 路徑失去產品保證）

## Why

`src/settings/settings_store.cpp::DefaultSettingsDir()` 目前用固定 `MAX_PATH` buffer 讀
`LOCALAPPDATA`；讀取失敗或值過長便回傳空字串。`src/app_host/main.cpp::wWinMain`
隨後不檢查此結果，直接把空字串傳給：

- `SettingsStore`、`LoadCatalogCache`、`UsageStore`、`PinStore`、`SaveCatalogCache`；
- `JoinPath(DefaultSettingsDir(), L"logs")`；
- `std::filesystem::path(DefaultSettingsDir()) / L"icons.cache"`。

`JoinPath(L"", name)` 會形成 `\name`，而空的 filesystem path 與 `icons.cache` 組合後
可成為相對路徑。結果不是安全的「停用 persistence」：部分 user-data write 會失敗，
icon cache 則可能跟著 process current directory 寫出，若 current directory 是 exe 目錄
便違反不得寫在 executable 旁的規則。

## Decisions already made — do not reopen

1. 唯一有效的 user-data root 是已解析、非空、絕對的 `%LOCALAPPDATA%\NimbleRun`。
2. 使用 Windows Known Folder API 取得 per-user LocalAppData；若 API 也失敗，所有 persistence
   與 cache 都要安全停用或回報不可用，不得猜 current directory、exe directory、根目錄或 temp。
3. `JoinPath`／各 store 不各自發明 fallback；root resolution 是唯一入口。
4. 不遷移既有檔案、不刪除其他路徑資料、不新增 registry／network fallback。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.1：

> `%LOCALAPPDATA%\\NimbleRun\\` 為 settings、favorites、usage、catalog cache、icons cache 與 logs 的資料目錄。

`docs/design-spec.md` §NFR-005：

> 使用紀錄只儲存在目前使用者的 LocalAppData。

`AGENTS.md`：

> Keep all user data under `%LOCALAPPDATA%\\NimbleRun`; do not write beside the executable.

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

## Files to read and trace first

- `src/settings/settings_store.{h,cpp}` — `DefaultSettingsDir` and its current `MAX_PATH`/environment behavior。
- `src/storage/atomic_text_file.h` — `JoinPath`、`EnsureDirectory`、`AtomicWriteUtf8Text`。
- `src/app_host/main.cpp` — `wWinMain` 的全部 root consumers，以及 `SaveCatalogCache` call site。
- `src/catalog/catalog_cache.cpp`、`src/usage/usage_store.cpp`、`src/pins/pin_store.cpp`、
  `src/diagnostics/diagnostic_log.cpp`、`src/icons/icon_store.cpp` — each file's path/open/write behavior。
- `tests/unit/settings_store_test.cpp`、相關 store tests — existing temp-directory seams and
  missing/corrupt persistence behavior。
- `docs/design-spec.md` §10.1–10.4、`docs/work-items/NR-004-settings-store.md`、NR-054、NR-096。

## Scope

1. Replace the environment-only root lookup with one safe native root resolver and make every
   `wWinMain` persistence consumer honor an unavailable-root result.
2. Ensure an empty/overlong/malformed resolver result cannot reach `JoinPath` or a relative
   `std::filesystem::path` used for a write.
3. Add a focused runnable check using a controlled missing/invalid-root condition (or a minimal
   resolver seam) that asserts no settings, log, catalog cache, pin, usage or icon cache is
   created outside the intended per-user root.

## Non-goals

- 不改 settings／usage／pins 的 schema、atomic replace 或 newer-schema policy。
- 不為 persistence 增加第三方 library、SQLite、network sync 或 administrator privilege。
- 不把使用者自訂 catalog roots 與 app-owned data root 混為一談。

## Acceptance

1. 在 LocalAppData 正常、缺失、超長或 Known Folder API failure 的情境，app-owned files
   只會出現在有效的 `%LOCALAPPDATA%\NimbleRun`，或完全不寫；不會出現 relative、rooted-
   current-drive、current-directory 或 executable-adjacent fallback。
2. Existing stores continue to use atomic writes when the root is valid; an unavailable root
   degrades without crash or partial path creation。
3. Focused test catches a regression in the empty-root path, and all existing settings/storage
   tests pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings|recent_usage|pinning|diagnostic_log" --output-on-failure
```

```powershell
Select-String -Path src/settings/settings_store.cpp,src/app_host/main.cpp,src/storage/atomic_text_file.h -Pattern 'DefaultSettingsDir|GetEnvironmentVariableW|JoinPath|icons.cache'
git diff --name-only
# expect: root resolution/path consumers 與 focused tests；不改其他 product behavior。
```

## Handoff

實作者需記錄 Known Folder failure policy、每個 consumer 的 path invariant、controlled
failure test、build／CTest 與是否有舊資料 migration 需求（本 item 預設沒有）。

### 實作結果（2026-08-09）

- `DefaultSettingsDir()` 改用 `SHGetKnownFolderPath(FOLDERID_LocalAppData)`；API 失敗、空值、非 local absolute、含非法字元或超過 bounded path 長度時回傳空字串，沒有 environment、current directory、EXE、root 或 temp fallback。
- `wWinMain` 只解析一次 root。settings、usage、pins、catalog cache、logs 與 icons cache 均以同一個有效 root 組路徑；root 不可用時 settings／usage 只用記憶體預設值，catalog／icon cache 與 diagnostic log 停用，catalog cache write gate 永久關閉。
- `JoinPath`、`EnsureDirectory`、`ReadVersionedLines`、`PreserveCorrupt` 與 atomic text write 對空 directory fail-closed；因此空 root 不會形成 relative 或 rooted-current-drive 寫入。
- `tests/unit/settings_store_test.cpp` 新增 controlled empty-root self-check，驗證 settings、usage、pins、catalog cache、diagnostic log、icon cache 不會在 current directory 建立檔案，並覆蓋 empty／relative／UNC／malformed／overlong resolver 輸入。
- 舊資料不遷移、不刪除；沒有 migration 需求。Release build 與 focused CTest（5/5）及全量 CTest（25/25）通過。
