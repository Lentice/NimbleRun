# NR-128 — 死碼與 test-only 生產 API 移除（g_last_hotkey_error、GetStartupStatus、IconCache::Resolve、kQuickSelectDigits、未用 include）

Phase 3 · Code cleanup · Depends on: NR-127（FileName 收斂後才可完整刪除）

- Source: `AGENTS.md`（prefer the smallest working change；deletion over addition）
- Origin: 2026-08-10 第十三次全 repo 稽核（ponytail 軸）；主 Agent 已 grep 驗證每個候選
  零生產消費者
- Priority: **LOW**（純整理；但有兩項順帶消除真實風險——test-only 的 `GetStartupStatus` 是
  唯一會讀大 REG_SZ 的生產函式，刪掉即刪掉該輸入面）

## Why

以下全部經 grep 驗證「src 內零生產呼叫者，只有測試或無消費者」：

1. **`g_last_hotkey_error`**（`main.cpp:169` 宣告、`:3750` 唯一寫入）：註解寫「kept for NR-017
   diagnostics」，NR-017 從未消費；write-only global。
2. **`GetStartupStatus`／`StartupStatus`／`PathsMatch`**（`startup_option.{h,cpp}`：約 60 行、
   含 `EnabledMoved` 三態語意）：src 內零呼叫者，只有 `tests/unit/startup_option_test.cpp`
   使用；生產只呼叫 `SetStartupEnabled`。FR-012 的「移動 EXE 後重新建立」由 auto_start=true
   無條件重建涵蓋，三態狀態從未進 UI。**順帶效益**：這是唯一讀 `HKCU\...\Run` 大 REG_SZ 的
   路徑（每次設定對話框開啟），刪掉即移除該輸入面。
3. **`IconCache::Resolve`**（`icon_cache.h:83`、`icon_cache.cpp:31-46`）：生產路徑只有
   `Peek`＋`Insert`（Render 與 kIconReadyMessage）；miss→provider→insert 的 Resolve 路徑在
   NR-032 worker 架構下沒有消費者，`icon_cache_test.cpp` 是唯一呼叫者。
4. **`kQuickSelectDigits`**（`quick_select.h:10`）：生產只用 `QuickSelectSlotForKey`／
   `QuickSelectLabelForSlot`；該常數唯一消費者是 `ui_palette_layout_test.cpp`——測試用常數
   驗證常數自身（驗證的是字面量不是行為）。
5. **`app_filter.h::FileName`**（`app_filter.h:12`、`app_filter.cpp:16-19`）：公開 API 零外部
   呼叫者；內部使用者是 `user_folder_catalog.cpp` 的私有拷貝（NR-127 收斂後即無消費者）。
6. **未用 include**：`main.cpp:5` `<shellscalingapi.h>`（無 shellscaling API 呼叫）、
   `dedup.cpp:5` `<cwctype>`（大小寫比較走 app_filter 的 `ToLower`）。

## Decisions already made — do not reopen

1. 移除而非標 `test-only`：repo 契約是「測試注入 seam 要有真實消費者」；被刪 API 的測試段落
   （`startup_option_test.cpp` 的 GetStartupStatus 案例、`icon_cache_test.cpp` 的 Resolve 案例、
   `ui_palette_layout_test.cpp` 的 kQuickSelectDigits round-trip）依各自模組的既有測試語意
   改寫或刪除——`IconCache` 的測試改用 `Insert`＋`Peek` 組裝等價情境（生產行為不變）。
2. `SetStartupEnabled`（`startup_option.cpp` 的實作）**保留**——生產唯一使用；只刪三態讀取端。
3. `app_filter::FileName` 的刪除時機：等 NR-127 把 `user_folder_catalog.cpp` 的私有拷貝收斂
   （或本 item 先刪 `FileName`，NR-127 收斂時不重引）——兩 item 同 agent 依序處理，交接區
   記錄誰先誰後。
4. 依賴排序：本 item 大部分獨立，可與 NR-127 平行（不同檔案為主，衝突點只有
   `user_folder_catalog.cpp`／`app_filter.h`）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Deletion over addition.

## Files to read and trace first

- `src/app_host/main.cpp`（`:169`、`:3750`、`:5`）。
- `src/settings/startup_option.{h,cpp}`、`tests/unit/startup_option_test.cpp`。
- `src/icons/icon_cache.{h,cpp}`、`tests/unit/icon_cache_test.cpp`。
- `src/ui/quick_select.h`、`tests/unit/ui_palette_layout_test.cpp`。
- `src/catalog/app_filter.{h,cpp}`、`src/catalog/dedup.cpp`（`:5`）。

## Scope

1. 刪除 `g_last_hotkey_error`（宣告＋寫入行）。
2. 刪除 `GetStartupStatus`／`StartupStatus`／`PathsMatch` 與其測試段落；保留 `SetStartupEnabled`
   與既有 startup_option 測試的其餘案例。
3. 刪除 `IconCache::Resolve`，`icon_cache_test` 改用 `Insert`＋`Peek` 等價組裝。
4. 刪除 `kQuickSelectDigits`，`ui_palette_layout_test` 改由
   `QuickSelectSlotForKey`／`QuickSelectLabelForSlot` 往返驗證（或直接刪該組案例）。
5. 刪除 `app_filter.h::FileName`（依決策 §3 時機）；刪兩個未用 include。

## Non-goals

- 不改任何生產行為（`SetStartupEnabled`／`Peek`／`Insert`／`QuickSelect*` 的實作一字不動）。
- 不新增 API、不搬移函式位置（搬移屬 NR-127 範圍）。
- 不動 `startup_option.cpp` 的 `kStartupValueName` 等仍被 `SetStartupEnabled` 使用的常數。

## Acceptance

1. 每項刪除後 grep 零殘留（`GetStartupStatus`、`Resolve(`、`kQuickSelectDigits`、
   `g_last_hotkey_error`、`FileName(` 在 src 與 tests 的合理計數）。
2. `startup_option_test`／`icon_cache_test`／`ui_palette_layout_test` 改寫後仍覆蓋同語意
   （或刪除的案例有理由記錄）。
3. Release build 無新增 warning；完整 CTest 26/26 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_last_hotkey_error|GetStartupStatus|StartupStatus|PathsMatch|::Resolve|kQuickSelectDigits|FileName\(" src tests
# expect: 被刪項目零命中（或只剩交接說明）。
git diff --name-only
```

## Handoff

實作（2026-08-10）逐項落點與決策：

1. **`g_last_hotkey_error`**（main.cpp）：宣告（原 `:169`）與唯一寫入行（原 `:3750`）已刪。
   write-only global，NR-017 從未消費。sanity grep 零命中。
2. **`GetStartupStatus`／`StartupStatus`／`PathsMatch`**（startup_option.{h,cpp}）：生產三態
   讀取端已刪（保留 `SetStartupEnabled`）。**測試段落補刪完成**（這部分在交接前只改了測試檔頭
   註解，測試案例仍在，本次補完）：刪除 `TestFreshStateIsDisabled`、`TestMovedExeDetection`、
   `TestUnterminatedRegSzDoesNotCrash`、`TestOddByteRegSzDoesNotCrash`；`TestEnableCreatesEntry`、
   `TestDisableRemovesOnlyOwnValue`、`TestRecreateAfterMove` 僅移除 `GetStartupStatus` 斷言；
   移除 `using nimblerun::StartupStatus;` 與變成未使用的 `WriteRawBytes` helper（避免
   `-Wunused-function`）。NR-069 兩個 crash 案例隨被刪的「讀 Run 值」輸入面一併移除
   （見本 item Why 的順帶效益），刪除理由記錄於測試檔註解。FR-012 的「移動 EXE 後重建」語意
   仍由 `TestRecreateAfterMove` 覆蓋。
3. **`IconCache::Resolve`**：宣告（icon_cache.h 原 `:83`）與實作（icon_cache.cpp 原 `:31-46`）
   已刪；class doc 與 `Insert` doc 中引用 Resolve 的敘述同步改寫。測試改寫：
   `TestMissThenInsertAndHit` → `TestInsertThenPeekHit`（miss→Insert→Peek hit）；
   `TestKeySeparatesVariant`／`TestDefaultCap`／`TestSetMaxItemsShrink`／`Zero`／`Grow`
   改以 `Insert`＋`Peek` 組裝等價情境；`TestLruEviction`、`TestReinsertRefreshesRecency`、
   `TestProviderFailureNotCached` 刪除——其語意已由既存 `TestInsertAddsAndEvicts`
   （自 `TestInsertAddsAndEvictsLikeResolve` 改名，Resolve 已消失故不再有 "LikeResolve"）、
   `TestInsertRefreshesRecency`、`TestInsertRejectsEmpty` 逐字覆蓋（同一 LRU/eviction 程式路徑）；
   理由註記於測試檔。同時移除 `Entry`／`FakeIconProvider` helper 與不再需要的 `<vector>`。
4. **`kQuickSelectDigits`**（quick_select.h 原 `:10`）：已刪；`kQuickSelectSlotCount` 保留
   （生產與測試皆用）。`TestQuickSelectDigitsUnique` 原本用常數驗證常數（驗字面量非行為），
   改寫為 `TestQuickSelectSlotLabelRoundTrip`：對每個 slot 取 label，再以
   `QuickSelectSlotForKey(*label)` 往返回同一 slot——同時證明 label 唯一。
5. **`app_filter.h::FileName`**：決策為**改為 header 內 file-local（匿名 namespace）而非全刪**——
   `FileStem`／`Extension`（同一 header 的 inline 函式）仍呼叫 `FileName`，若全刪需將兩者搬到
   .cpp 或重複邏輯（違反最小變更）。NR-127（commit `6b3d684`）**先完成**、已收斂
   `user_folder_catalog.cpp` 的私有拷貝：現況該檔只呼叫共享的 `FileStem`／`Extension`，不再呼叫
   `FileName`，故公開 API 已零外部呼叫者；file-local 化後達成「零 dead public API」。
6. **未用 include**：`<shellscalingapi.h>`（main.cpp，交接前已刪）、`<cwctype>`（dedup.cpp，
   本次刪除——dedup 的大小寫比較走 `app_filter` 的 `ToLower`，未直接使用 cwctype 符號）。

**NR-127 順序**：NR-127 先於本 item 提交（git log `6b3d684`），`FileName` 收斂完成後本 item
才動手，故 Decision 3 的「不重引」成立；`user_folder_catalog.cpp` 無需再動。

**Sanity grep 證據**（`rg -n "g_last_hotkey_error|GetStartupStatus|StartupStatus|PathsMatch|::Resolve|kQuickSelectDigits|FileName\(" src tests`）：
`g_last_hotkey_error`、`GetStartupStatus`、`StartupStatus`、`PathsMatch`、`kQuickSelectDigits`
零程式碼命中（僅本 item 的交接/理由註解提及）；`::Resolve` 只剩 `nimblerun::palette::ResolveColors`
（無關識別字，預期命中）；`FileName(` 只剩 app_filter.h 內 file-local 的定義與兩個內部呼叫端。

**build／CTest 證據**：Release x64（LLVM-MinGW + Ninja）build 無新增 warning；完整 CTest
**26/26 通過**（含改寫的 nimblerun_icons_cache_test、nimblerun_startup_option_test、
nimblerun_dpi_theme_accessibility_test）。未提交，交由主 Agent commit。
