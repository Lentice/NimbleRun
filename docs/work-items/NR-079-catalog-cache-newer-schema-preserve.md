# NR-079 — A newer-schema `catalog.cache` must not be overwritten by the rebuild (spec §10.4)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.4（較新 schema 的快取檔不覆寫、停用該快取）
- Origin: 2026-08-08 第四次全 repo 稽核（catalog/settings/pins/usage/storage 子系統）

## Why

design-spec §10.4 明文：

- 快取類檔案（`catalog.cache`、`icons.cache`）遇到較新且不支援的 schema version 時，
  **不覆寫原檔**、停用該快取（僅以記憶體 LRU 運作）。

實作中 `LoadCatalogCache`（`src/catalog/catalog_cache.cpp:104-110`）把 `NewerSchema`
與 `OlderSchema` 一起 `return false`，不區分：

```cpp
case VersionedReadStatus::OlderSchema:
case VersionedReadStatus::NewerSchema:
    // NR-047: ... Leave it in place and rebuild over it ...
    return false;
```

host 端（`src/app_host/main.cpp:2343`）在每次 rebuild 完成後**無條件**
`SaveCatalogCache(...)`，把 `schema=2` 的檔覆寫到較新版本的 `schema=3` 檔上。
第一次 rebuild 完成就把使用者較新 build 寫出的快取覆寫掉。`icons.cache` 已由
NR-035/036 的正確分類（`NewerSchema → Disabled`、不動原檔）符合 §10.4，`catalog.cache`
是唯一漏網。

影響：快取可完全重建（§10.4 定位），被覆寫的實際損失為零；但這是明確的 spec
違反，且為「較新版本資料被舊 build 無聲覆寫」的同一類問題（NR-072 修的是使用者
資料版，這裡修的是快取版）。稽核新增發現。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **host 端停寫**：`LoadCatalogCache` 增加回報 `NewerSchema` 的出口（out-param
   `bool* newer_schema`，或回傳 `VersionedReadStatus`；以改動最小者為準）；啟動時
   若為 `NewerSchema`，設檔案範圍旗標 `g_catalog_cache_disable_writes = true`，
   `SaveCatalogCache` 呼叫點在旗標為真時 no-op。旗標**整段 process 生命週期不解除**
   ——磁碟上的檔仍是較新 schema，任何寫入都是 §10.4 違反；下次啟動時重新判定。
2. **`OlderSchema` 維持現狀**（`return false` 並「重建覆寫」）：舊 schema 是這個
   build 讀不懂但可重建的合法檔，與 NR-047 決策一致，不是本 item 範圍。
3. **不改 `icons.cache`**（NR-035 已正確）。
4. **測試**：`catalog_cache` 的載入分類在 `catalog_refresh_test` 已有案例（cache
   round-trip／corrupt）；新增一 case：手寫 `schema=3` 的 `catalog.cache` →
   `LoadCatalogCache` 回報 `NewerSchema`（且 `out` 不變）。host 的停寫旗標無測試
   seam，由 sanity grep＋手動驗收覆蓋。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep all user data under `%LOCALAPPDATA%\NimbleRun`; do not write beside the executable.

design-spec §10.4：

- 快取類檔案遇到較新且不支援的 schema version 時，不覆寫原檔、停用該快取
  （僅以記憶體 LRU 運作），且不顯示錯誤提示。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/catalog/catalog_cache.cpp:91-143` — `LoadCatalogCache`（`:104-110` NewerSchema
  arm）。主場之一。
- `src/catalog/catalog_cache.h` — 簽名所在。
- `src/app_host/main.cpp:2343`（`SaveCatalogCache` 呼叫點）、`:2935-2938`
  （啟動載入）。主場之二。
- `src/catalog/catalog_cache.cpp:73-89` — `SaveCatalogCache`／`WriteCache`（**只讀
  不改**，確認 host 呼叫即可）。

## Scope

### 1. `LoadCatalogCache` 回報 `NewerSchema`

`src/catalog/catalog_cache.h` 簽名加 out-param（例）：

```cpp
bool LoadCatalogCache(const std::wstring& directory, std::vector<AppEntry>& out,
                      bool* newer_schema = nullptr);
```

`catalog_cache.cpp` 的 `NewerSchema` arm：

```cpp
case VersionedReadStatus::NewerSchema:
    if (newer_schema) {
        *newer_schema = true;
    }
    return false;
```

（`OlderSchema` arm 不動，也不設旗標。）

### 2. host 停寫旗標

`src/app_host/main.cpp` 檔案範圍：

```cpp
// NR-079: set when the on-disk catalog.cache is a newer schema than this build
// reads. design-spec §10.4 forbids overwriting that file; writes stay off for
// the run (a re-read next launch re-decides).
bool g_catalog_cache_disable_writes = false;
```

啟動載入（`:2936`）改成：

```cpp
bool cache_newer = false;
if (nimblerun::LoadCatalogCache(nimblerun::DefaultSettingsDir(), cached, &cache_newer)) {
    refresh.SetSnapshot(std::move(cached));
}
g_catalog_cache_disable_writes = cache_newer;
```

`kRebuildDoneMessage` 的 `SaveCatalogCache`（`:2343`）包守門：

```cpp
if (!g_catalog_cache_disable_writes) {
    nimblerun::SaveCatalogCache(nimblerun::DefaultSettingsDir(),
                                g_refresh->Snapshot());
}
```

（配合 NR-073：該呼叫已收進 `GenerationComplete` 區塊；若兩者同批實作，以 NR-073
的合併後位置為準。）

### 3. 測試

`catalog_refresh_test`（或既有 `catalog_cache` 相關 case 所在檔）：手寫 `schema=3`
的 `catalog.cache` → `LoadCatalogCache` 回 `false` 且 `newer_schema == true`、
`out` 未變。既有 `schema=1`（OlderSchema）案例回歸：`newer_schema` 仍為
false（未設）、行為不變。

### 4. 更新 spec？

不需。§10.4 描述的行為層級未動——本次讓實作符合既有規格。

## How this stays maintainable

停寫判定集中於載入一次（啟動）、旗標整段有效——不依賴「每次寫前重讀磁碟」。
`icons.cache` 的處理已示範「正確分類 → 停用」的形狀，本 item 讓 `catalog.cache`
跟上同一語意。

## Non-goals

- **不改 `OlderSchema` 的重建覆寫行為（NR-047 決策）。**
- **不改 `icons.cache` 的既有分類。**
- **不顯示錯誤提示**（§10.4 快取類不通知）。
- **不新增設定或 UI。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項＋新增 case）。
2. §Scope 3 新 case 通過（NewerSchema 被回報、`out` 不變、OlderSchema 不受影響）。
3. sanity grep：`SaveCatalogCache` 呼叫點被 `g_catalog_cache_disable_writes` 守門包住。

Manual：

4. 手寫 `schema=3` 的 `catalog.cache` 到 `%LOCALAPPDATA%\NimbleRun\`，啟動後等首次
   rebuild 完成：`catalog.cache` 內容與時間戳不變（未被覆寫為 schema=2）；面板與
   搜尋照常（記憶體 snapshot 運作）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# host 停寫守門存在：
Select-String -Path src/app_host/main.cpp -Pattern "g_catalog_cache_disable_writes"
# expect: 宣告 1 + 設值 1 + 守門 1

# LoadCatalogCache 回報 NewerSchema：
Select-String -Path src/catalog/catalog_cache.cpp -Pattern "newer_schema"
# expect: 至少 1 處

# 改動範圍：
git diff --name-only
# expect: src/catalog/catalog_cache.h、src/catalog/catalog_cache.cpp、
#         src/app_host/main.cpp、相關測試檔
```

## 交接區

（實作者填寫：out-param 的實際形狀、旗標的宣告位置與守門形狀、新 case 的 fixture
寫法、建置與 CTest 結果、sanity greps、手動驗收 4 的實際觀察、偏差、未完成事項。）

實作（2026-08-08）：

- **out-param**：`catalog_cache.h` 簽名改
  `bool LoadCatalogCache(const std::wstring&, std::vector<AppEntry>&, bool* newer_schema = nullptr)`；
  `NewerSchema` arm 拆分（`OlderSchema` 獨立保留 NR-047 註解），只在 `NewerSchema`
  時 `*newer_schema = true`。`Missing/Unreadable/Malformed/OlderSchema` 皆不設。
- **旗標**：main.cpp 檔案範圍 `bool g_catalog_cache_disable_writes = false;`
  （NR-077 的 `g_rebuild_handoffs` 之後）；啟動載入傳 `&cache_newer` 並
  `g_catalog_cache_disable_writes = cache_newer;`（在 `g_refresh` 設好後）；
  `kRebuildDoneMessage` 的 `SaveCatalogCache`（NR-073 已收進 `GenerationComplete`
  區塊）包 `if (!g_catalog_cache_disable_writes)`。
- **測試**：`catalog_refresh_test` 新增 `TestNewerSchemaCacheReportsAndLeavesOutUntouched`
  （schema=3 → 回 false＋`newer_schema==true`＋`out` 未變＋檔未動＋無 `.corrupt`）；
  `TestOlderSchemaCacheRebuilds` 加 `!newer_schema` 斷言。**測試 bug 修正**：cache
  fixture 的 `std::ofstream` 改 block-scope（寫完即析構 flush）再 `Load`——原測試
  的 ofstream 緩衝未 flush，`LoadCatalogCache` 讀到的是空檔（Malformed 路徑），
  舊測試只因「開啟中的檔擋住 PreserveCorrupt 改名」而僥倖通過。
- **建置與 CTest**：Release build 無新增警告；`ctest` 23/23 全綠。
- **sanity greps**：`g_catalog_cache_disable_writes` 於 main.cpp＝宣告 1＋守門 1＋
  設值 1；`newer_schema` 於 catalog_cache.cpp＝簽名＋arm 各 1＋；`git diff
  --name-only`＝catalog_cache.h、catalog_cache.cpp、main.cpp、catalog_refresh_test.cpp。
- **手動驗收**：schema=3 快取不被覆寫為實機驗證，本工作區未實跑。
- **偏差**：無。未完成事項：無。
