# NR-113 — Catalog cache 項目未經來源驗證不得啟動

Phase 3 · Catalog trust boundary · Depends on: NR-008, NR-011, NR-079

- Source: `docs/design-spec.md` §FR-008、§FR-010、§NFR-004、§10.2、§10.3
- Origin: 2026-08-09 第十次全 repo audit；沿 `LoadCatalogCache` → startup snapshot → `ActivateRow` → `LaunchEntry` 追蹤
- Priority: CRITICAL（可修改 user-writable cache，讓使用者點擊時啟動不屬於目前 Catalog source 的 identity）

## Why

`src/catalog/catalog_cache.cpp:91-135` 讀取 `catalog.cache` 時只確認 `stable_id` 非空、`source`
可解析，以及欄位格式可讀；`launch_identity` 可以是任意非空字串。`src/app_host/main.cpp:3594-3600`
會把成功載入的 cache 直接交給 `CatalogRefreshCoordinator::SetSnapshot`，在背景完整 enumeration
完成前即可顯示與選取。`ActivateRow()`（約 `src/app_host/main.cpp:1040-1052`）所有滑鼠、Enter、
Alt+digit 路徑都直接把 row 交給 `LaunchEntry()`，而 `src/launch/shell_launch.cpp:7-24` 只拒絕空字串，
其餘直接傳給 `ShellExecuteExW`。

`catalog.cache` 位於 `%LOCALAPPDATA%\NimbleRun`，是可修改的本機快取，不是真實來源，也沒有能驗證
內容的秘密。因而手動編輯或被同一使用者權限下的其他程式修改後，cache-only row 的任意 path、URI
或命令解譯器 identity 可能在使用者點擊時被啟動。這不是「Shell 是否成功」問題，而是 launch
provenance 缺失；`stable_id` 也不能補足它，因為 Spec 明定 stable ID 只用於識別，不作安全信任判斷。

## Decisions already made — do not reopen

1. 保留 FR-008 的 startup cache UX：有效 cache 可先顯示，背景 rebuild 完成前不必阻塞面板。
2. cache-only 或尚未被目前 source enumeration 驗證的項目不得啟動；啟動必須使用目前成功 source
   result 的 launch identity。驗證失敗沿用既有 launch-failure／refresh 行為，不能繞過 guard。
3. 不把 `stable_id`、display name、source enum 或字串格式檢查當成安全驗證；需要以目前來源產生的
   entry／identity 做 provenance gate。AppEntry 保持普通可 copy 的值，不持有 Shell COM pointer。
4. 不把搜尋輸入或 cache 欄位拼成任意 command line；仍只呼叫 Windows Shell API。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008：

> 啟動時先載入有效的 Catalog cache，立即提供舊結果；再背景完整建立一次最新 Catalog。

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。

`docs/design-spec.md` §FR-010：

> 使用 Unicode 版本 `ShellExecuteExW` 或 Shell item verb，傳入 `AppEntry::launch_identity`。

> 啟動層不對 launch identity 做任何加工。命名空間前綴等組裝工作屬於各 catalog 來源的責任。

`docs/design-spec.md` §NFR-004：

> 搜尋只過濾既有 Catalog，不把輸入當成命令列或 URI 執行。

> 自訂來源只接受本機資料夾及受支援的可啟動副檔名，不執行任意使用者輸入或未知副檔名。

`docs/design-spec.md` §10.2／§10.3：

> `catalog.cache`：可選的版本化二進位 cache，只用於加速，不是真實來源。

> hash 用於識別，不作安全信任判斷。

`AGENTS.md`：

> Launch apps through Windows Shell APIs. Never build an arbitrary command line from search input.

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

## Files to read and trace first

- `src/catalog/catalog_cache.{h,cpp}` — `LoadCatalogCache` 欄位解析與 cache acceptance boundary。
- `src/catalog/app_entry.h` — 可 copy 的 entry value；如新增狀態欄位，確認 save／dedup／model 皆一致。
- `src/catalog/catalog_refresh.{h,cpp}` — `SetSnapshot`、source snapshot、generation 完成與目前來源的可信資料。
- `src/app_host/main.cpp` — startup cache load、`RefreshPanelSnapshot`、`ActivateRow` 的所有 launch callers、
  `StartRebuild`／generation completion。
- `src/launch/shell_launch.{h,cpp}` — `LaunchEntry` 的單一 Shell 呼叫邊界。
- `tests/unit/catalog_refresh_test.cpp`、`tests/unit/shell_launch_test.cpp`、相關 CMake target。
- `docs/work-items/NR-008-shell-launch.md`、NR-011、NR-079 — 保留既有 launch、cache refresh 與 newer-schema 決策。

## Scope

1. 定義並實作 cache entry 的「目前來源已驗證可啟動」狀態或等價的單一 activation guard；cache
   仍可提供 display/search 的舊 snapshot，但未驗證 row 的所有啟動入口都必須安全失敗。
2. 完整 source generation 成功後，以 enumeration 產生的 launch identity 取代 cache 欄位；不得因 stable
   ID 相同就沿用 cache 內的 launch identity。
3. Trace pin／recent、filtered list、empty grid、Enter、滑鼠與 Alt+digit，確認它們全部經同一 guard，
   不在 caller 各自加重複判斷。
4. 新增一個 focused runnable test：合法格式但竄改 `launch_identity` 的 cache entry 可被載入／顯示，
   但在來源驗證前不得觸發 Shell launch；fresh source result 驗證後才可啟動。測試不得真的啟動未受控
   程式，沿用既有 controlled helper／seam。

## Non-goals

- 不改 `catalog.cache` schema 來保存秘密或做簽章；本機 cache 仍是可重建 accelerator。
- 不新增網路、反惡意軟體掃描、服務、管理員權限或第三方 runtime。
- 不改搜尋排名、dedup 規則、source enumerator 的 program-like filter 或 Shell namespace 組裝規則。
- 不把 cache 直接丟棄而破壞 startup 舊結果 UX；目標是禁止未驗證啟動，不是禁止顯示。
- 不把 raw HWND／COM object 放進 catalog value，也不把 coordinator pointer 交給 worker。

## Acceptance

1. 任意可解析的 cache row 在目前 source 尚未成功驗證前，Enter、滑鼠、Alt+digit 與 context-menu
   的 launch path 都不會呼叫 `ShellExecuteExW`，也不會更新成功 usage。
2. Cache 仍可在背景 rebuild 期間顯示；generation 完成後，只有目前 source 產出的 launch identity
   可啟動，cache 竄改的 identity 不會被帶入 fresh snapshot。
3. 空 identity、cache-only identity、stale generation 與正常 source entry 的行為都有 focused assertion；
   既有 NR-008 launch failure 行為、pins／recent 與 search UI 不回歸。
4. Release build 無新增 warning；focused test、完整 CTest 與既有 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|shell_launch" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "LoadCatalogCache|SetSnapshot|ActivateRow|LaunchEntry|ShellExecuteExW" src tests
git diff --name-only
# expect: catalog/host/launch 與 focused tests；不改 settings、pins、usage schema。
```

## Handoff

實作者需記錄 cache row 的 display-vs-launch 狀態、fresh source 驗證邊界、四個 launch caller 的
追蹤結果、竄改 fixture、Shell call 是否被阻擋、build／CTest 結果與任何未涵蓋的跨版本 Shell 行為。
