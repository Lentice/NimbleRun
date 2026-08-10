# NR-127 — 路徑／解析／常數 helper 的重複拷貝收斂（ToLower／FileName／FileStem／Extension ×3、ParseInt ×3、kSchemaPrefix ×5 等）

Phase 3 · Code cleanup · Depends on: NR-057（先例，done）

- Source: `AGENTS.md`（Reuse existing code before adding helpers or abstractions）、
  `docs/development.md`
- Origin: 2026-08-10 第十三次全 repo 稽核（ponytail 軸）；主 Agent 已以逐字比對驗證
- Priority: **MEDIUM**（重複 helper 的漂移風險已被 repo 自己的歷史證實——NR-057 收斂前四份檔頭
  解析已漂移出 `catalog_cache` 的 bug；同 library 內不重用是最直接違反階梯第 2 階）

## Why

同一邏輯的多份拷貝（全部 grep 驗證為逐字相同或同構）：

1. **`ToLower`／`FileName`／`FileStem`／`Extension` ×3**：
   - `src/catalog/user_folder_catalog.cpp:16-46`（匿名 namespace 私有拷貝）與
     `src/catalog/app_filter.h:9-19` 的公開 API **逐字相同**，兩者同在 `nimblerun_catalog`
     library——user_folder_catalog.cpp 甚至沒有 include app_filter.h；
   - `src/settings/settings_store.cpp:27-34, 63-69` 再拷 `ToLower`、`Extension` 兩份。
   改副檔名／大小寫規則時只改到 app_filter 會靜默分歧。
2. **`ParseCountText`（`src/app_host/settings_dialog.cpp:309-316`）vs `ParseInt`
   （`src/settings/settings_store.cpp:36-49`）**：第三份 wcstol 解析拷貝，且少了 ERANGE 檢查。
3. **`kSchemaPrefix = L"schema="` ×5**：`atomic_text_file.h:287`（擁有者）＋
   `pin_store.cpp:17`／`usage_store.cpp:18`／`settings_store.cpp:22`／`catalog_cache.cpp:19`。
4. **`kMinRecentCount`／`kMaxRecentCount`（8/40）×2**：`settings_store.cpp:24-25` 與
   `settings_editor.cpp:15-16`，同一 library。
5. **`IconStore::KeyFor`（`icon_store.cpp:15-17`）vs `IconKey::Encode`（`icon_cache.cpp:16-18`）**：
   `stable_id + L'|' + to_wstring(variant)` 逐字相同；icon_store.cpp 已 include icon_cache.h。
6. **main.cpp「全部來源」三元素 vector ×4**（`:1069-1073`、`:2935-2939`、`:3083-3087`、
   `:3783-3787`）＋ `catalog_refresh.cpp` 私有 `kSources`（`:18-22`）。
7. **`Settings` member initializer（`settings_store.h:33-41`）vs `DefaultSettings()`
   （`settings_store.cpp:123-134`）**：同一份預設值兩處，改 hotkey 預設需改兩處。

（`settings_editor.cpp:18-29` 的 `AreEqualCaseInsensitive` 與 `startup_option.cpp:33-44` 的
`PathsMatch` 也逐字相同，但 `PathsMatch` 屬 NR-128 的刪除範圍——若 NR-128 先做，本項自動消失，
實作時先確認。）

## Decisions already made — do not reopen

1. 沿用 NR-057 的收斂原則：共用函式放**純值層既有標頭**（`app_filter.h`／`atomic_text_file.h`／
   `settings_store.h`），不抽基底類別、不新增抽象層、不引入 `<windows.h>` 到純函式標頭
   （NR-057 已確認 `atomic_text_file.h` 是既有的純 header 落點）。
2. **逐字相同才收斂**；同構但語意不同的不硬併（如 `FileName` vs 其他 stem 語意差異先確認）。
3. 收斂是純搬移：行為零變更，既有測試就是回歸網；不為搬移新寫測試（除非搬移暴露了
   `ParseCountText` 缺 ERANGE 的真 bug——該檔在 `settings_dialog.cpp` 是使用者輸入，超長數字串
   是合法輸入路徑，需確認 `ParseCountText` 的邊界行為並在收斂時補上檢查）。
4. 依賴排序：本 item 與 NR-128 都動 `user_folder_catalog.cpp`／`app_filter.h`——同一個 agent
   依序處理（先 NR-128 的死碼清理再收斂，或反過來，避免同時改同一行）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values.

## Files to read and trace first

- `src/catalog/app_filter.{h,cpp}`、`src/catalog/user_folder_catalog.cpp`（`:16-46`）。
- `src/settings/settings_store.{h,cpp}`、`src/settings/settings_editor.cpp`、
  `src/app_host/settings_dialog.cpp`（`:309-316`）。
- `src/storage/atomic_text_file.h`（`:287`）、四份 store 的 `kSchemaPrefix`。
- `src/icons/icon_store.cpp`（`:15-17`）、`src/icons/icon_cache.cpp`（`:16-18`）。
- `src/app_host/main.cpp`（四處 all-sources）、`src/catalog/catalog_refresh.cpp`（`:18-22`）。
- `docs/work-items/NR-057-versioned-store-reader.md` — 收斂先例與決策。

## Scope

1. `user_folder_catalog.cpp` include `app_filter.h`、刪除 4 個私有拷貝；settings 側的
   `ToLower`／`Extension` 收進 `settings_store.h`（或既有共用 header）並刪兩份。
2. `ParseCountText` 改用共用的 `ParseInt`（或 `atomic_text_file.h` 的 `ParseInt64`＋範圍檢查），
   確認缺 ERANGE 的邊界行為後補齊，行為與測試一致。
3. `kSchemaPrefix` 在 `atomic_text_file.h` 公開一份，四份 store 刪私有副本。
4. `kMinRecentCount`／`kMaxRecentCount` 移到 `settings_store.h`，`settings_editor.cpp` 引用。
5. `IconStore::KeyFor` 改為 `IconKey{stable_id, variant}.Encode()`，刪私有函式。
6. main.cpp 四處 all-sources 改用單一 `constexpr`（建議放 `catalog_refresh.h` 的 `kSources`，
   或 main.cpp 檔案範圍一份，擇一）；`catalog_refresh.cpp` 私有 `kSources` 併入同一來源。
7. `DefaultSettings()` 與 member initializer 擇一為唯一預設值來源（建議 member init 保留、
   `DefaultSettings()` 只覆寫 catalog_extensions——先確認 `default` 建構子路徑無其他差異）。

## Non-goals

- 不抽基底類別／interface；不新增 header 檔（除非必要的最小落點）；不改任何行為語意。
- 不順手改 `app_filter.h` 的公開 API 形狀（`FileName` 的去留見 NR-128）。
- 不重開 NR-057 的「不預蓋 migration 框架」決策。

## Acceptance

1. 上述 7 項每項只剩一份定義（grep 驗證）；`git diff` 無行為性改動。
2. `ParseCountText` 收斂後對超長數字串的行為不劣於現況（邊界案例有測試或明確記錄）。
3. Release build 無新增 warning；完整 CTest 26/26 與 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ToLower|FileStem|kSchemaPrefix|kMinRecentCount|KeyFor|CatalogSource::StartMenu" src
# expect: 每個 helper 只剩一份定義；kSchemaPrefix 只在 atomic_text_file.h。
git diff --name-only
```

## Handoff

實作者需記錄每項收斂的落點與刪除數、`ParseCountText` 邊界行為確認、sanity grep 與
build／CTest 證據。

### 交接區（2026-08-10，實作完成）

7 項全部完成，Release build 零 warning、CTest 26/26。逐項落點與刪除數如下。

#### 1. ToLower／FileName／FileStem／Extension ×3 → 1（app_filter.h）

- `src/catalog/app_filter.h`：四個函式由宣告改為 **inline 定義**（實作逐字搬自 `app_filter.cpp`），並補 `#include <cwctype>`。
- `src/catalog/app_filter.cpp`：刪除四份定義（搬到 header inline）。
- `src/catalog/user_folder_catalog.cpp`：新增 `#include "catalog/app_filter.h"`，刪除 4 個私有拷貝（`ToLower`／`FileName`／`FileStem`／`Extension`，原 :16-46）；`<cwctype>` include 一併移除。
- `src/settings/settings_store.cpp`：新增 `#include "catalog/app_filter.h"`，刪除 2 個私有拷貝（`ToLower`／`Extension`，原 :27-34／:63-69）；`<cwctype>` include 一併移除。
- **必要偏差**：item 建議「收進 `settings_store.h`（或既有共用 header）」。採「既有共用 header = `app_filter.h`」；但因 CMake 相依是 `nimblerun_catalog → nimblerun_settings`（settings 不能 link catalog），若維持 `app_filter.cpp` 定義會造成 settings 出現未解析符號（第一次 build 即實證：`ld.lld: undefined symbol: nimblerun::ToLower/Extension`）。故把四個函式改為 header 內 inline——`app_filter.h` 仍是無 `<windows.h>` 的純值標頭（NR-057 落點準則），public API 形狀不變（`FileName` 去留仍屬 NR-128）。

#### 2. ParseCountText → ParseInt64＋int 範圍檢查（settings_dialog.cpp）

- `src/app_host/settings_dialog.cpp`：`ParseCountText` 改用共用 `ParseInt64`（`atomic_text_file.h`，settings_dialog 已 include 該檔），刪除 wcstol 拷貝；補 `<cstdint>`／`<limits>`，移除不再使用的 `<cstdlib>`。
- **邊界行為確認**：
  - 超長數字串（如 25 個 9）：舊實作 `wcstol` 設 ERANGE 回傳 `LONG_MAX`→轉 `int` 得 `2147483647`，靠呼叫端 8..40 範圍檢查拒絕；新實作 `ParseInt64` 直接因 ERANGE 回 false→`-1`→範圍檢查拒絕。**可見行為相同**（都顯示 invalid notice），但不再有隱性溢位——這是 item 明列的 ERANGE 修補。
  - `2147483648`（INT_MAX+1）：舊 → `static_cast<int>` 變負值後被範圍檢查拒絕；新 → int 範圍守衛回 `-1`，同被拒絕。結果相同。
  - 唯一同類的良性差異：`ParseInt64` 內部 `Trim`，故尾端空白（如 `"10 "`）舊被拒、新被接受為 10。屬與 ERANGE 同類的寬容修正，無測試覆蓋此案例，特此記錄。

#### 3. kSchemaPrefix ×5 → 1（atomic_text_file.h）

- `src/storage/atomic_text_file.h`：新增 namespace 層級 `inline constexpr std::wstring_view kSchemaPrefix = L"schema=";`（:24）；`ReadVersionedLines` 內的區域 constexpr（原 :298）刪除，改用共用者。
- 四份 store 各刪 1 個私有定義：`pin_store.cpp`、`usage_store.cpp`、`settings_store.cpp`、`catalog_cache.cpp`。Save() 的 `text += kSchemaPrefix;` 解析到 `nimblerun::kSchemaPrefix`，輸出逐字不變。

#### 4. kMinRecentCount／kMaxRecentCount ×2 → 1（settings_store.h）

- `src/settings/settings_store.h`：新增 `inline constexpr int kMinRecentCount = 8;`（:29）、`kMaxRecentCount = 40;`（:30）。
- 刪除 2 份私有拷貝：`settings_store.cpp`（原 :24-25）、`settings_editor.cpp`（原 :15-16，已透過 `settings_editor.h → settings_store.h` 取得）。

#### 5. IconStore::KeyFor → IconKey::Encode（icon_store.cpp）

- `src/icons/icon_store.cpp`：刪除私有 `KeyFor`（原 :15-17）；`:385` 改為 `pending_[IconKey{stable_id, variant}.Encode()]`；新增顯式 `#include "icons/icon_cache.h"`（原先僅靠 icon_store.h→icon_pack_format.h 轉遞取得）。

#### 6. 全部來源 vector ×4 ＋ catalog_refresh kSources → 1（catalog_refresh.h）

- `src/catalog/catalog_refresh.h`：新增 `inline constexpr CatalogSource kSources[] = {StartMenu, AppsFolder, UserFolder};`（:22）。
- `src/catalog/catalog_refresh.cpp`：刪除私有 `kSources`（原 :18-22），三處迴圈改用共用者。
- `src/app_host/main.cpp`：四處 all-sources vector（原 :1086、:3011、:3160、:3865 一帶，各 5 行）改為 `StartRebuild(window, {std::cbegin(nimblerun::kSources), std::cend(nimblerun::kSources)})`（各 2 行）；補 `#include <iterator>`。

#### 7. DefaultSettings() → member initializer 為唯一預設來源

- `src/settings/settings_store.cpp`：`DefaultSettings()` 保留 member initializer（`settings_store.h`）為唯一預設值來源，函式體只留 `Settings settings;` 與 `settings.catalog_extensions = DefaultExtensions();`（依 item 建議保留該覆寫）。刪除 7 個逐字重複的 assignment（hotkey/auto_start/theme/recent_count/hide_after_launch/include_windows_apps/catalog_roots.clear——後者對空 vector 為 no-op）。已確認 `default` 建構子路徑與舊 `DefaultSettings()` 產出逐值相同。

#### Sanity greps（`rg -n "ToLower|FileStem|kSchemaPrefix|kMinRecentCount|KeyFor|CatalogSource::StartMenu" src`）

```text
ToLower        定義僅 app_filter.h:10（inline）；呼叫：app_filter.cpp:45,66、dedup.cpp:97、
               settings_store.cpp:41,74、user_folder_catalog.cpp:137
FileStem       定義僅 app_filter.h:26（inline）；呼叫：start_menu_catalog.cpp:162,173、
               user_folder_catalog.cpp:51、app_filter.cpp:66
kSchemaPrefix  定義僅 atomic_text_file.h:24；Save 使用：pin_store.cpp:111、usage_store.cpp:108、
               settings_store.cpp:262、catalog_cache.cpp:74；ReadVersionedLines 內部 :305-310
kMinRecentCount 定義僅 settings_store.h:29；使用 settings_store.cpp:208、settings_editor.cpp:307
kMaxRecentCount 定義僅 settings_store.h:30；使用 settings_store.cpp:208、settings_editor.cpp:307
KeyFor          src 下無定義（icon_store.cpp 私有版已刪）；main.cpp 的 IconKeyFor 是不同函式，未動
kSources        定義僅 catalog_refresh.h:22；使用 catalog_refresh.cpp:31,44,227、
               main.cpp:1087,3008,3153,3854
CatalogSource::StartMenu  main.cpp 殘餘均為單來源用途（WatchIndexToSource 預設、switch case、
               add_root 呼叫），四份 all-sources vector 已不存在
```

#### Build／CTest 證據

- 於 `build-wi-nr127`：`cmake -S . -B build-wi-nr127 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release` → configure OK。
- `cmake --build build-wi-nr127 --clean-first`（全量重建，`-Wall -Wextra -Wpedantic`）→ **零 warning、零 error**；後續 `ninja: no work to do.`。
- `ctest --test-dir build-wi-nr127 --output-on-failure` → **26/26 Passed**（含 `nimblerun_lifecycle_check`）。
- 附註：首次整包 ctest 曾出現 `nimblerun_pinning_test` 一次閃退（`Save() after a Loaded load succeeds`），單獨執行與單獨 ctest 皆通過、最終全量重跑 26/26 通過；與本次改動無關（pin Save 輸出逐字不變，閃退屬檔案系統時序）。
