# NR-191 — recent_count 範圍擴至 1～1000，並在 blur 自動夾限越界輸入

Phase 5 · Settings contract · Depends on: NR-004、NR-013（皆 done）

- Source: `docs/design-spec.md` §4.2、§FR-013、§FR-009、§10.2、§12.1；`docs/development.md` §Architecture rules、§Change workflow
- Origin: 2026-08-17 使用者需求；`grill-with-docs`／domain-modeling 已確認空值與非數字 blur 行為
- Priority: **MEDIUM**——設定邊界與產品規格目前不一致，且目前只在 Save/OK 驗證，越界數值不會在離開欄位時被修正

## Goal

把設定頁的 recent count（空白查詢狀態中，釘選項目之後顯示的最近啟動 App 數量）改為包含端點的 `1..1000`，預設值維持 `20`。使用者離開 recent count 欄位時，只有可解析的數值越界才自動修正：低於 `1` 改成 `1`，高於 `1000` 改成 `1000`。

## 已確認的產品決策

1. **Canonical term**：使用 `recent count` 指「空白查詢狀態顯示的 recent app rows 數量」，不是 `usage.tsv` 保留的使用紀錄數，也不是搜尋結果上限。詞彙已寫入根目錄 `CONTEXT.md`。
2. **範圍**：`kMinRecentCount = 1`、`kMaxRecentCount = 1000`，兩端都合法；default `recent_count = 20` 不變。
3. **blur clamp**：`0`、負數或其他可解析且 `< 1` 的值在 `EN_KILLFOCUS` 時顯示為 `1`；`1001` 或其他可解析且 `> 1000` 的值顯示為 `1000`；恰好 `1` 與 `1000` 不改寫。
4. **空值／非數字／解析溢位**：blur 時保留原文字，不靜默變成有效值；按 Save/OK 沿用既有 `ParseCountText`／`SetRecentCount` 錯誤提示與恢復上一個有效設定的流程。
5. **持久化檔案**：`settings.ini` 仍維持 schema=1。Load 對 `1`／`1000` 接受；對 `0`、負數、`1001` 或無法解析的值沿用目前契約，忽略該欄並保留 `DefaultSettings().recent_count == 20`，不做 migration、不把手動檔案值靜默 clamp。
6. **生效範圍**：保留既有 `UsageStore::Recent(settings.recent_count)` 與 `IconCacheCapacityFor(pin_count, recent_count)` 路徑；1000 只是合法上限，不新增第二個 cap、分頁或 cache policy。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.2：

> 未釘選常用項目，依最後一次啟動時間排序，最近啟動者在最前。

`docs/design-spec.md` §FR-013（本 item 會把其中的舊數字更新成新決策）：

> 常用 App 顯示數量：8～40，預設 20。

`docs/design-spec.md` §10.2：

> `settings.ini`：少量 key/value，使用 Win32 profile API 或受測試的自有 reader/writer。

`docs/development.md`：

> Core value types should remain copyable and testable without HWND or Shell COM ownership.

> Read the relevant design-spec section；identify the narrowest module boundary；reuse existing types and helpers；add or update one focused test for non-trivial logic；run configure, build, tests, and applicable manual checks；update relevant docs when behavior or a boundary changes.

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

> UI strings are English；all user data writes stay under `%LOCALAPPDATA%\\NimbleRun` and use temporary files plus atomic replacement；do not add network access, telemetry, third-party runtime dependencies, services, drivers, or administrator requirements.

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`、`docs/development.md`。
- `docs/design-spec.md` §4.2、§FR-009、§FR-013、§10.2、§11、§12.1；確認 recent ordering、icon working-set formula、settings format、損壞輸入與測試要求。
- `docs/testing.md` 的 manual smoke test 與 settings／release validation。
- `docs/work-items.md` 的使用方式、Agent 交付規則、Item 總覽與「已否決的方向 — 不要重開」。
- `docs/work-items/NR-004-settings-store.md`、`NR-009-recent-usage.md`、`NR-013-settings-ui.md`、`NR-127-duplicate-helper-convergence.md`、`NR-140-settings-ini-row-caps.md`、`NR-152-settings-write-side-symmetry.md`；完成 item 文件只讀取，不回頭修改歷史紀錄。
- `src/settings/settings_store.h/.cpp`：`Settings`、`DefaultSettings`、`kMinRecentCount`／`kMaxRecentCount`、Load 的 `recent_count` range check、Save key。
- `src/settings/settings_editor.h/.cpp`：`SetRecentCount`、`RecentCountNotice`、dirty tracking 與 Apply rollback。
- `src/app_host/settings_dialog.cpp`：`Populate`、`ParseCountText`、`SettingsDialogProc` 的 `WM_COMMAND`／IDOK 路徑；trace `EN_KILLFOCUS` 入口與所有 recent control notifications。
- `src/resources/resource.h`、`src/resources/NimbleRun.rc`：recent EDIT control 的 ID、`ES_NUMBER` 與欄位寬度。
- `src/usage/usage_store.h/.cpp`、`src/app_host/snapshot_assembler.cpp`：`Recent(cap)` 的排序與 cap 消費者。
- `src/icons/icon_cache.h/.cpp`、`src/app_host/main.cpp`：derived LRU capacity 的所有 `recent_count` callers；確認 1000 不溢位且不需另加 cap。
- `tests/unit/settings_store_test.cpp`、`tests/unit/settings_editor_test.cpp`、`tests/CMakeLists.txt`：既有 validation、round-trip、Release 可執行檢查與 CTest 名稱。
- `docs/settings-concepts.html`：搜尋並同步所有仍顯示 `8–40` 的設定概念畫面。

## Scope

1. **規格與文件同步**
   - `docs/design-spec.md` §FR-013 改為「常用 App 顯示數量：1～1000，預設 20」，並補一句：設定頁輸入可解析的越界數值在失去焦點時夾至端點；空值／非數字不在 blur 時改寫，Save/OK 才走既有 validation。
   - `docs/testing.md` 的 manual smoke test 增加 recent count 矩陣：`1`、`1000`、`0`、`1001`、空值；驗證 blur 後文字與 Save/Cancel 語意。
   - `docs/settings-concepts.html` 只更新過時的 `8–40` 文案為 `1–1000`；不重做概念畫面。
2. **SettingsStore／SettingsEditor 共用邊界**
   - `settings_store.h` 將既有 shared constants 改為 `1`／`1000`，同步 `Settings::recent_count` 與 `SetRecentCount` 註解；不要在另一個 `.cpp` 重新定義。
   - `settings_store.cpp` 繼續使用 shared constants 驗證 Load；`Save` 格式、key、schema=1、atomic write 不變。
   - `settings_editor.cpp/.h` 讓 setter 接受 1／1000、拒絕 0／1001；更新 `RecentCountNotice` 英文文字為 `1 and 1000`。
3. **Settings dialog blur 行為**
   - 沿用現有 `IDC_RECENT_COUNT_EDIT`、`ES_NUMBER`、`ParseCountText` 與 `Populate`；在現有 `SettingsDialogProc` 接住 recent EDIT 的 `EN_KILLFOCUS`。
   - 只有 `ParseCountText` 成功時才依 shared constants 判斷並以 `SetDlgItemTextW` 寫回 `1` 或 `1000`；不在 blur 呼叫 `Apply`、不寫 `settings.ini`、不改 editor working copy。
   - 空值、非數字、解析溢位保持原文字；IDOK 仍由既有路徑顯示 `RecentCountNotice` 並 `Populate` 回復有效值。不得加入 timer、background worker、第三方 numeric control 或新的 dialog abstraction。
   - 1000 必須完整顯示；只有真的被現有 EDIT 寬度裁切時才做最小 `.rc` 寬度調整，不改整體設定頁版面。
4. **衍生消費者**
   - 只 trace 並以 focused test／sanity check 證明 `UsageStore::Recent`、`SnapshotAssembler` 與 `IconCacheCapacityFor` 能消費 1000；除非發現直接的整數或容量 bug，不修改 usage、snapshot、icon policy。
5. **測試**
   - `settings_editor_test` 更新既有 boundary case：0／1001 拒絕，1／1000 接受，default 20 與 accepted value 保留。
   - `settings_store_test` 更新原本把 `recent_count=1000` 當越界的案例；新增／擴充 exact 1／1000 Load 與 round-trip，以及 0／1001／負數仍回 default 20 的案例。
   - UI blur 由 `docs/testing.md` 的手動 smoke matrix 驗證；若實作為可獨立測試的純值判定，將測試放進既有 settings UI target，不新增測試框架或無必要的測試執行檔。

## Non-goals

- 不修改預設值 20。
- 不把 `recent_count` 變成 usage history 保留筆數，不修改 `usage.tsv` schema、`UsageStore::Records()` 或搜尋結果上限。
- 不新增 spinner、slider、numeric-control dependency、timer、背景執行緒或資料 migration。
- 不在 SettingsStore::Save 另建第二道 clamp policy；UI 仍透過 `SettingsEditor` 驗證，Load 對手動檔案維持安全預設 fallback。
- 不修改 `UsageStore::Recent` 的 newest-first 排序、不修改 icon LRU 公式、不替 1000 再設一個未經需求授權的上限。
- 不回頭編輯已完成的 NR-004／NR-009／NR-013／NR-127／NR-140／NR-152 文件或 NR-013 的歷史交接紀錄。
- 不新增其他設定、非英文 UI、網路、telemetry、第三方 runtime、服務、driver 或管理員權限。

## Acceptance

1. `DefaultSettings().recent_count == 20`；新值域是包含端點的 1..1000。
2. `SettingsEditor::SetRecentCount(1)` 與 `(1000)` 成功；`(0)`、`(-1)` 與 `(1001)` 失敗且 working value 不被改動。
3. `settings.ini` 的 `recent_count=1`／`1000` 可 Loaded；`0`／負數／`1001`／無法解析值不會污染設定，仍使用 default 20；schema 仍為 1，既有 atomic persistence 與 newer/corrupt 行為不回歸。
4. 設定頁手動驗收：
   - 輸入 `0` 或負數後 blur，欄位顯示 `1`；
   - 輸入 `1001` 後 blur，欄位顯示 `1000`；
   - 輸入 `1`／`1000` 後 blur，文字不變；
   - 清空或輸入無法解析內容後 blur，原文字不被靜默替換；按 Save/OK 時仍顯示既有錯誤並恢復上一個有效值；
   - Save 後重新開啟，1 與 1000 都能 round-trip；Cancel 不寫入 blur 造成的文字變更。
5. `recent_count=1000` 經 `SnapshotAssembler`／`UsageStore::Recent` 與 derived icon-cache capacity 路徑不 crash、不截斷、不整數溢位；既有排序與 cache working-set 語意不變。
6. `docs/design-spec.md`、`docs/testing.md`、`docs/settings-concepts.html` 不再把 8～40 描述成現行範圍；所有新 UI 文字為英文。
7. Release build 無新增 warning；focused settings tests 與完整 CTest 全部通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "settings" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kMinRecentCount|kMaxRecentCount|SetRecentCount|ParseCountText|EN_KILLFOCUS|IDC_RECENT_COUNT_EDIT|recent_count" src/settings src/app_host/settings_dialog.cpp tests/unit/settings_editor_test.cpp tests/unit/settings_store_test.cpp
rg -n "8.?40|1.?1000|recent count|Recent apps" docs/design-spec.md docs/testing.md docs/settings-concepts.html docs/work-items/NR-191-recent-count-range-and-blur-clamp.md
```

Focused runnable coverage 必須包含：

- `nimblerun_settings_test`：default、exact boundary、out-of-range fallback、round-trip。
- `nimblerun_settings_ui_test`：setter boundary、reject 不改 working value、既有 apply／rollback regression。
- 若新增純值 blur 判定函式，該 target 必須以 `Expect` 類可執行檢查覆蓋 `<1`、`1`、`1000`、`>1000`、空值／非數字；不得只靠 Release 會被移除的 `assert`。

## Handoff requirements

交接時記錄：

- shared constants 的唯一定義位置與所有 callers；確認沒有第二份 8／40 或 1／1000。
- blur 事件入口、空值／非數字處理、exact boundary 行為，以及沒有在 blur 寫檔的證據。
- settings store／editor／UI focused tests、完整 CTest、Release build 的命令與結果。
- `UsageStore::Recent`、`SnapshotAssembler`、`IconCacheCapacityFor` 的 trace 與是否需要修改（預期不修改）。
- 手動設定頁矩陣的實測結果；若 Agent 無法操作桌面，必須標記未完成，不得宣稱通過。
