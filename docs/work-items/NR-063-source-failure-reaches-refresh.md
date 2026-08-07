# NR-063 — A failed source enumeration must preserve the source's old entries (§FR-008)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-008（第 399 行「單一來源失敗時保留該來源舊結果」）／§11
- Origin: 2026-08-07 第三次全 repo 稽核（main.cpp 執行緒／catalog 子系統）

## Why

`ApplySourceFailure` 是死碼：`StartRebuild` 的 worker（`src/app_host/main.cpp:1197-1218`）
從頭到尾**沒有任何一條路徑把 `result->failed` 設成 true**，所以 `main.cpp:2253` 的
`if (result->failed)` 分支在產品程式碼中不可達。而三個枚舉器對「來源級失敗」也無法
表達：

- `EnumerateStartMenuCatalog()`（`start_menu_catalog.cpp:231-247`）在 COM 初始化失敗
  或兩個 known folder 都取不到時回傳**空 vector**——與「真的沒有捷徑」無法區分。
- `EnumerateAppsFolderCatalog()` 的 `AppsFolderEnumerateResult`（`appsfolder_catalog.h:15-18`）
  只有 `failed_items`（子項目級），`SHGetKnownFolderItem`／`BindToHandler` 來源級失敗時
  回傳空 entries，`source_ok` 不存在。

後果鏈：

1. 一次性的來源級失敗（COM 初始化失敗、known folder 查詢失敗）→ worker 以**空清單**
   走 `ApplySourceResult` → `RebuildMerged` 把該來源的全部 app 從面板抹掉，直到下一次
   碰巧成功的觸發。§FR-008「單一來源失敗時保留該來源舊結果」從未生效。
2. `main.cpp:2260-2262` 對任何 applied 的 AppsFolder 結果都 `RecordAppsFolderSuccess`
   ——空結果（來源級失敗）也算「成功」，把 §FR-008 的「距上次成功列舉超過 10 分鐘
   才重列舉」的自我恢復機制壓掉（下一個 ShowPanel 不會重試）。
3. 持續性失敗下，受影響 pin 的 `last_seen` 不再被刷新，30 天後被 `Reconcile` 丟棄
   （`pin_store.cpp:201-203`）——使用者資料因失敗而流失。

同區塊的兩個小缺陷一併修（都在被動到的行內）：

4. `main.cpp:1219-1221` 的 `PostMessageW` 回傳值未檢查——失敗（訊息佇列滿）時
   `new RebuildResult` 洩漏。同 repo 的 `icon_worker.cpp:158-161` 對同一 handoff 形狀
   有 `if (!PostMessageW(...)) delete result;`，此處漏了同款。
5. `main.cpp:2252-2268` 的區域變數 `generation_complete` 名字說謊：`ApplySourceResult`
   回傳的是「generation 吻合、結果被接受」，不是「整個 generation 完成」。目前
   `OnRefreshComplete()` 在第一個來源回報時就觸發，靠 `IsRebuildInProgress()` 另一面
   旗標把行為碰巧接對；照名字信任它的未來 caller 會踩錯。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **修在枚舉器邊界：COM 來源自己回報「來源級成敗」。** `AppsFolderEnumerateResult`
   加 `bool source_ok`（來源級失敗＝known-folder 查詢或 `BindToHandler` 失敗；子項目
   失敗維持 `failed_items` 計數）。`EnumerateStartMenuCatalog` 照 AppsFolder 形狀改成
   `StartMenuEnumerateResult { std::vector<AppEntry> entries; bool source_ok; }`，
   `source_ok = false` 當 COM 不可用或兩個 known folder 都取不到。UserFolder
   （`EnumerateUserFolderCatalog`）**不改**：它的根來自使用者設定，「資料夾不存在→
   空」是正確語意（使用者自己刪了資料夾），不是失敗。
2. **worker 保持薄轉接層**：`result->entries` 照舊，新增 `result->failed = !source_ok;`。
3. **失敗的 AppsFolder 結果不記 `RecordAppsFolderSuccess`**，讓 10 分鐘 staleness 重試
   機制自然運作（§FR-008）。
4. **不做 worker 內重試**。一次失敗→保留舊資料→等下一次觸發（事件、ShowPanel
   staleness、Ctrl+R）是既有、有界、事件驅動的節奏；在 worker 加重試是回到輪詢，
   違反「idle path event-driven」。
5. **`PostMessageW` 失敗時 `delete result`**，照 `icon_worker.cpp:158-161` 的既有形狀。
6. **`generation_complete` 改名並修正觸發點**：改名 `result_applied`；`OnRefreshComplete`
   只在 `g_refresh->GenerationComplete(generation)` 為真時呼叫（coordinator 已暴露此
   查詢，`catalog_refresh.h`）。啟動失敗閘門的語意不變：重建進行中仍有
   `IsRebuildInProgress()` 合併，完成後閘門重新武裝。
7. **不加 UI、不加通知**：來源失敗對使用者不可見（保留舊資料就是正確行為），只留
   既有診斷日誌路徑。

## Binding constraints — quoted, do not go looking for them

design-spec §FR-008：

> - 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

design-spec §FR-008（AppsFolder staleness）：

> - AppsFolder 不做背景輪詢；當面板被叫出且距上次成功列舉超過 10 分鐘時，在背景低優先序重新列舉。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/catalog/start_menu_catalog.cpp:231-247` — `EnumerateStartMenuCatalog`：COM 不可用
  即回空。要改成回傳成敗。
- `src/catalog/appsfolder_catalog.h:15-28` 與 `appsfolder_catalog.cpp:134-145` —
  `AppsFolderEnumerateResult`：`failed_items` 是子項目級；`source_ok` 要加在這裡，
  來源級失敗點在 `SHGetKnownFolderItem`／`BindToHandler` 失敗處。
- `src/app_host/main.cpp:1197-1224` — worker lambda。**只改三行**：`result->failed`
  的設定與 `PostMessageW` 失敗時的 `delete result`（照 `icon_worker.cpp:158-161`）。
- `src/app_host/main.cpp:2246-2275` — `kRebuildDoneMessage`。`generation_complete`
  改名＋`GenerationComplete(generation)` 條件。`RecordAppsFolderSuccess` 移進
  `!failed` 成功臂內（現在在 `ApplySourceResult` 回傳 true 的臂內）。
- `src/catalog/catalog_refresh.h` — `GenerationComplete(uint64_t)` 已是 public。
- `tests/unit/catalog_refresh_test.cpp:114-161` — `TestFailureKeepsOldSnapshot`／
  `TestSingleSourceFailureIsolation` 已證明 coordinator 層行為正確（保留舊 entries、
  其他來源照常套用）。**coordinator 不用改**，這些測試是回歸網。
- `tests/unit/start_menu_catalog_test.cpp` 與 `appsfolder_catalog_test.cpp` —
  新測試的家。既有 fixture 是 temp 目錄＋真實 COM 呼叫，可以跑。

## Scope

### 1. 枚舉器回報來源級成敗

- `AppsfolderCatalog`：`AppsFolderEnumerateResult` 加 `bool source_ok = true;`；
  來源級失敗處設 `false`（找出 `SHGetKnownFolderItem`／`BindToHandler` 失敗的
  早退點，逐點檢查）。`failed_items` 語意不動。
- `StartMenuCatalog`：`EnumerateStartMenuCatalog()` 回傳 `StartMenuEnumerateResult
  { std::vector<AppEntry> entries; bool source_ok = true; }`（照 AppsFolder 形狀，
  兩者共用同一種語意，不抽共用型別）。COM 不可用或兩個 known folder 都空時
  `source_ok = false`；**至少一個 root 成功（即使結果為空）＝成功**。
- `EnumerateUserFolderCatalog` 簽名不動。

### 2. worker 轉接

`main.cpp` worker lambda 的 `switch` 各 case 接住新回傳值，設定
`result->failed = !res.source_ok; result->entries = std::move(res.entries);`。
`include_windows_apps` 關閉的 AppsFolder case（`main.cpp:1206-1212`）維持
「刻意空清單、不失敗」——那是產品決策（清掉舊 packaged entries），不是失敗，
`failed` 保持 false。

### 3. handler 修正

`kRebuildDoneMessage`：

- `generation_complete` 改名 `result_applied`；
- `OnRefreshComplete()` 改在 `g_refresh->GenerationComplete(result->generation)`
  為真時呼叫（成功與失敗臂共用）；
- `RecordAppsFolderSuccess` 只在 `!result->failed` 時呼叫（留在成功臂內）；
- 失敗臂（`ApplySourceFailure`）**一字不改**——它已是正確的「保留舊 entries」。

### 4. `PostMessageW` 失敗洩漏

`main.cpp:1219-1221`：照 `icon_worker.cpp:158-161` 檢查回傳值，失敗即
`delete result`（不要 `continue`——同一執行緒只發一則）。

### 5. 更新 spec？

`design-spec.md` §FR-008 已寫明「單一來源失敗時保留該來源舊結果」——本 item 是把
既有規格變成可達路徑，**spec 不需改**。若 §11 或 §FR-008 有「枚舉失敗」的日誌
條目要對照實作，照現況描述即可。

## How this stays maintainable

**「來源成敗」的判定只有一個出口**（枚舉器的回傳值），worker 只是轉接，coordinator
的 `ApplySourceResult/Failure` 是既有唯一分派。日後新增枚舉器（例如新的 catalog
來源）只要照 AppsFolder 的 struct 形狀回報，失敗隔離自動成立；不會再出現
「誰該設 failed」的隱性約定。

## Non-goals

- **不做 worker 內重試／backoff**（Decisions §4）。
- **不改 `EnumerateUserFolderCatalog`**（Decisions §1）。
- **不改 coordinator**（`catalog_refresh.cpp` 的 merge／failure 語意已是對的）。
- **不加 UI、不加 balloon、不加設定**。
- **不處理 pin 流失**：那是本 item 的動機之一，但修好來源失敗後不會再發生；
  既有 pin 的保留機制不動。
- **不把 `StartMenuEnumerateResult` 抽成共用 template**——兩個 struct 的欄位
  相同但語意各自獨立，抽共用是為了不存在的第三個使用者。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋新增）。
2. `start_menu_catalog_test` 新增：正常 fixture 列舉（`EnumerateProgramsDirectory`
   路徑，與既有案例同形）→ 新回傳值的 `source_ok == true` 且 entries 數目不變
   （釘住 struct 契約與正路徑回歸）。
3. `appsfolder_catalog_test` 若有直接呼叫 `EnumerateAppsFolderCatalog()` 的既有
   案例（或可加一個），斷言 `source_ok == true`；沒有就只做 struct 預設值
   （`= true`）的編譯期保證＋交接區說明。
4. `catalog_refresh_test` 既有 `TestFailureKeepsOldSnapshot`／
   `TestSingleSourceFailureIsolation` 原樣通過（回歸網，證明 coordinator 側沒被動到）。

**失敗路徑（`source_ok == false`）不強制單元測試**：COM 初始化失敗與
known-folder 查詢失敗都無法在單元測試中注入（`ComGuard` 對 `RPC_E_CHANGED_MODE`
是 `usable_ = true`，MTA 預先初始化**不是**失敗路徑，不要用那個當測試手法——它
會跑出非決定性結果）。這與 NR-050 的 `GrowView` 先例相同（OS 失敗路徑不加注入
seam，靠不變式與程式碼複查），在交接區載明。回歸保護由「正路徑 `source_ok ==
true`」＋「coordinator 對 failure 的反應已有測試」＋ Agent checks 的 sanity grep
共同構成。

Manual：

5. 設定 `Include Windows apps` 關閉再開啟 → packaged apps 正常出現（空清單成功路徑
   不誤判為失敗）。
6. 正常使用一段時間後，`Ctrl+R` 重建 → 三來源 app 都在（無誤判失敗）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# failed 不再只有欄位而無寫入者：
Select-String -Path src/app_host/main.cpp -Pattern 'result->failed'
# expect: 3 處——RebuildResult struct 定義、worker 成功臂、worker 失敗臂（或 2+1，視形狀）

# ApplySourceFailure 有真呼叫者（不再死碼）：
Select-String -Path src/app_host/main.cpp -Pattern 'ApplySourceFailure'
# expect: 1 處呼叫（kRebuildDoneMessage 失敗臂）

# PostMessageW 失敗時有 delete：
Select-String -Path src/app_host/main.cpp -Pattern 'PostMessageW\(window, kRebuildDoneMessage'
# expect: 1 處，且下方幾行內有失敗處理

# 成功才記 staleness：
Select-String -Path src/app_host/main.cpp -Pattern 'RecordAppsFolderSuccess'
# expect: 1 處，位於 !result->failed 分支內

# OnRefreshComplete 只在 generation 完成時觸發：
Select-String -Path src/app_host/main.cpp -Pattern 'OnRefreshComplete'
# expect: 1 處，前一行是 GenerationComplete 檢查

# 改動範圍：
git diff --name-only
# expect: src/catalog/start_menu_catalog.{h,cpp}、src/catalog/appsfolder_catalog.{h,cpp}、
#         src/app_host/main.cpp、tests/unit/start_menu_catalog_test.cpp（及 appsfolder_catalog_test.cpp，視案例）
```

## 交接區

（實作者填寫：修改的位置、`source_ok` 的判定點清單、`GenerationComplete` 呼叫時機、
建置與 CTest 結果、sanity greps、偏差、未完成事項。）
