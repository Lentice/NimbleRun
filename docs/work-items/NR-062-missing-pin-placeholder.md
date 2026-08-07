# NR-062 — 找不到對應 App 的釘選格顯示為缺失佔位，使用者可自行移除

- Phase: 3
- 覆寫：`docs/design-spec.md` §FR-011「30 天後仍不存在的 pin **可在設定頁清理**」
  的呈現方式。本 item 讓缺席的 pin 直接出現在格狀中（標示為缺失），使用者在該格
  右鍵即可移除，不必進設定頁。30 天保留期本身**不變**。

## 為什麼

目前一個 pin 若在 catalog 中找不到對應項目，`PanelModel::RefreshRows()` 就
默默不畫它（`panel_model.cpp:103-112` 的內層迴圈找不到就 `break`）。使用者看到的
是「我釘的東西不見了」，而 `favorites.txt` 裡紀錄還在、還占著 pin 順序中的一格
（NR-046 的 `ReorderPresent` 明文保留缺席 pin 的絕對索引）。使用者無從得知它存在，
也無從移除，只能等 30 天後去設定頁清理——而設定頁的清理入口是否存在、使用者是否
找得到，是另一回事。

使用者決策（2026-08-07，已確認）：缺席的 pin 要看得見，畫成 X 的佔位格，
右鍵可移除。

## 使用者已確認的決策（不要重新設計）

1. **佔位格畫 X，不畫問號、不畫灰色泛用圖示。** X 讀作「這個不在了」。
2. **佔位格必須顯示該 App 的名稱**，否則使用者無法判斷要不要移除。名稱來自
   釘選時記下的 display name，見範圍 §1——這是本 item 唯一的資料格式改動。
3. **不自動移除。** 30 天保留期（§FR-011）不變，缺席不等於刪除；只有使用者
   在右鍵選單選 Unpin 才會移除。
4. **佔位格不可啟動。** 點擊、Enter 都不做任何事（不彈錯誤對話框、不發出
   啟動失敗流程）。
5. **佔位格不出現在搜尋結果中。** 搜尋走 catalog，catalog 裡沒有它就是沒有。
6. **佔位格的右鍵選單只有 Unpin。** 沒有 Open file location、沒有 Properties、
   沒有 Remove from recent。

## 硬約束（引用自專案規則，不要再去翻）

- `AGENTS.md`：不得把 schema 遷移夾帶進**無關**改動。本 item 的 `favorites.txt`
  欄位新增是本 item 的核心交付，不是夾帶；但**必須**照下方的相容路徑做，不得
  讓既有 `favorites.txt` 被判為 corrupt。
- `AGENTS.md`：不得就地覆寫使用者資料；持久化寫入用暫存檔＋原子替換
  （`AtomicWriteUtf8Text` 已經是這條規則的實作，直接用）。
- `AGENTS.md`：App Catalog 資料是普通可複製值；UI 不得持有 Shell COM 指標。
- `AGENTS.md`：UI 文字一律英文。
- `AGENTS.md`：新的非平凡邏輯要留一個可執行的檢查。
- `docs/design-spec.md` §10.4：每種資料格式的第一行都要帶 schema 版本。

## 要讀與追的檔案

- `src/pins/pin_store.h:15-18`（`PinRecord`）、`:34-47`（檔案格式註解）、
  `:52-86`（`Load`/`Save`/`Pin`/`Reconcile`）。
- `src/pins/pin_store.cpp:16-18`（`kSchemaVersion = 1`）、`:26-65`（`Load` 的
  逐行解析，注意 `fields.size() != 2` 直接判 corrupt）、`:68-84`（`Save`）。
- `src/storage/atomic_text_file.h:224-287` — `VersionedReadStatus` 與
  `ReadVersionedLines`。**關鍵**：`ReadVersionedLines` 對「檔案版本 < 期望版本」
  回 `OlderSchema`，而 `PinStore::Load` 目前把它歸在 `default:` 一起判 corrupt
  並把檔案改名為 `.corrupt`。若只是把 `kSchemaVersion` 改成 2 而不處理這條路徑，
  **所有既有使用者的釘選會在升級後全部消失**。
- `src/app_host/panel_model.cpp:100-112` — 釘選解析迴圈，本 item 的主場。
- `src/app_host/panel_model.h:79-95` — `Rows()`、`RecentStartIndex()`、
  `RecentEndIndex()`。
- `src/app_host/main.cpp` 的 `DrawIconOrFallback()`（NR-059 引入，在
  `DrawDecodedIcon` 之後）與 grid 繪製迴圈（`:1333-1360` 附近）。
- `src/app_host/main.cpp:2510-2560` — 右鍵選單建構與命令分派。
- `src/app_host/main.cpp` 的啟動路徑（`kCmdPin`/`Enter`/單擊）——需要在啟動前
  擋下佔位格。
- `docs/design-spec.md:420-426` — §FR-011。

### 依賴

**NR-061 必須先完成。** NR-061 移除字母填充後，格狀中每一格都有明確語意，
佔位格才不會被誤讀成「又一個我沒開過的東西」。

## 範圍

### 1. `favorites.txt` 加入 display name 欄位（schema 2）

`PinRecord` 新增：

```cpp
    // NR-062: the display name last seen for this pin, recorded so a pin whose
    // app is missing from the catalog can still be shown by name. Empty for a
    // record loaded from a schema=1 file, which had no name column.
    std::wstring display_name;
```

`pin_store.cpp`：`kSchemaVersion` 改為 `2`；`Save()` 每行寫三欄
`<escaped stable_id>\t<last_seen_utc>\t<escaped display_name>`；
`Load()` 的解析改為接受 2 或 3 欄（2 欄＝schema 1 的舊行，`display_name` 留空），
其餘欄數仍判 corrupt。

`Load()` 的 switch 新增一條，**不要併進 `default:`**：

```cpp
    case VersionedReadStatus::OlderSchema:
        break;  // NR-062: a schema=1 file has no name column; its 2-field lines
                // are still valid and are upgraded on the next Save().
```

`Pin()` 增加一個 `std::wstring display_name` 參數（接在 `stable_id` 之後），
重複釘選時一併刷新名稱。更新 `pin_store.h` 的檔案格式註解區塊
（`:34-47`）描述三欄格式與 schema 1 的相容規則。

呼叫點：`main.cpp` 的 `kCmdPin` 處理處，把 `entry.display_name` 傳進去。
**用編譯器找出所有 `Pin(` 呼叫點**，包含測試。

### 2. `PanelModel` 產生佔位列

`AppEntry` 不加欄位。佔位列用既有欄位表達，判定規則集中在一個地方：

`panel_model.h` 新增：

```cpp
    // NR-062: a pinned row whose app is absent from the catalog. Such a row is
    // synthesized from the pin record, carries an empty launch_identity, and
    // must never be launched. The single test for "is this a placeholder";
    // do not re-derive it from empty fields at each call site.
    static bool IsMissingPin(const AppEntry& row) {
        return row.is_pinned && row.launch_identity.empty();
    }
```

`RefreshRows()` 的釘選迴圈：找不到對應 catalog 項目時不再 `break` 跳過，而是
推入一列合成的 `AppEntry`：`stable_id` = pin id，`display_name` = pin 紀錄的
名稱（為空時退回 pin id），`is_pinned = true`，`launch_identity` 與
`source_path` 留空，`usage_score = 0`。

這需要 `PanelModel` 拿得到 pin 的名稱：把 `SetPins(std::vector<std::wstring>)`
改為 `SetPins(std::vector<PinRecord>)`（`pin_store.h` 已在 `panel_model` 的
相依範圍內，`PinRecord` 是純值型別，不違反「不依賴 HWND/COM」）。
內部 `pins_` 型別隨之改變；`std::find(pins_.begin(), ...) == pins_.end()` 的
兩處比對改為比較 `record.stable_id`。呼叫點在 `main.cpp` 的 `RefreshPins()`，
把 `OrderedPins()` 換成回傳紀錄的存取器（`PinStore` 新增
`const std::vector<PinRecord>& Records() const`，與 `UsageStore::Records()` 同名同形）。
`OrderedPins()` 若在改完後仍有其他呼叫點（NR-046 的重排、`StampRankingFields`）
則保留。

`recent_start_` 的語意不變：佔位格仍在釘選區內，計入 `recent_start_`。

### 3. 佔位格的繪製

grid 繪製迴圈中，`DrawIconOrFallback()` 之前先判斷：

```cpp
if (nimblerun::PanelModel::IsMissingPin(row)) {
    DrawMissingPinTile(icon_rect);
} else {
    DrawIconOrFallback(...);
}
```

新增檔案範圍 helper `DrawMissingPinTile(const D2D1_RECT_F& icon_rect)`，
緊接 `DrawIconOrFallback` 之後：用既有的 `g_dim_brush` 畫兩條對角線構成 X
（`g_render_target->DrawLine`，線寬取 `2.0f * scale`），內縮約圖示區的 25%。
**不新增筆刷、不載入圖檔、不加字型資源。** 名稱一行照既有格子邏輯繪製，
不另外加「(missing)」後綴——X 已經是標示。

footer 的 path bar（§4.9）在佔位格上顯示什麼：`source_path` 為空，沿用既有
「路徑為空則顯示來源標籤」的分支即可，不新增字串。**實作時確認該分支存在；
若不存在則新增 `list_strings::kMissingApp = L"App not found"` 供 footer 使用。**

### 4. 擋下啟動與收斂右鍵選單

- 所有啟動入口（Enter、單擊、釘選格放開左鍵啟動）在取得 row 之後、呼叫啟動
  函式之前，`if (PanelModel::IsMissingPin(row)) return;`。**用
  `Select-String -Path src/app_host/main.cpp -Pattern 'LaunchEntry|ShellExecute'`
  之類的搜尋列出全部入口，逐一加，不要只改 Enter 那一條**——漏掉任何一個都會
  讓空的 `launch_identity` 走進 Shell 呼叫。
- 右鍵選單：`IsMissingPin` 為真時只 `AppendMenuW` 一個 Unpin，直接跳過
  `Remove from recent` 與 `IsPathIdentity` 那一段（`launch_identity` 為空時
  `IsPathIdentity` 本來就是 false，但明確早退比依賴巧合好）。
- Unpin 的既有處理不改：`g_pins->Unpin(entry.stable_id)` + `Save()` +
  `RefreshPanelSnapshot()`，佔位格隨即消失。

### 5. 修改 `docs/design-spec.md` §FR-011

`:426` 那條改寫為：

```
- Catalog 中找不到對應 App 的 pin 仍顯示於釘選區，以缺失標記（X）呈現並保留其
  順序位置；該格不可啟動，右鍵只提供取消釘選。使用者未主動移除時，30 天保留期
  與自動恢復規則照舊，不得在第一次掃描失敗時刪除。
```

並在 `:425` 之後補一條：

```
- pin 紀錄一併保存釘選當下的顯示名稱，供缺失時標示；名稱僅供顯示，不參與
  stable ID 或去重。
```

## 非目標

- 不做設定頁的批次清理 UI。
- 不做「30 天到期自動刪除」的排程；`PinStore::Reconcile` 的既有行為不動。
- 不在搜尋結果中顯示佔位列。
- 不為佔位格做圖示快取、預熱或 tooltip。
- 不改 `usage.tsv` 的格式。
- 不動 NR-046 的拖曳重排規則（佔位格可被拖曳，因為它就在釘選區內且有順序）。

## 驗收條件

1. 舊的 schema=1 `favorites.txt` 升級後仍完整載入，**不會**產生
   `favorites.txt.corrupt`，且下一次 `Save()` 寫出 `schema=2` 的三欄格式。
2. 釘選一個 App 後，`favorites.txt` 該行第三欄是其顯示名稱。
3. 把已釘選 App 的來源移走並重建 catalog → 該格仍在原位，畫著 X，名稱正確。
4. 佔位格按 Enter 或點擊 → 什麼都不發生，沒有錯誤對話框，沒有啟動失敗流程。
5. 佔位格右鍵 → 選單只有 Unpin；選 Unpin → 該格消失且 `favorites.txt` 少一行。
6. 把 App 放回原處並重建 catalog → 該格恢復成正常項目，順序位置不變（§FR-011）。
7. 搜尋任何字串都不會出現佔位列。
8. 正常釘選格的圖示、拖曳重排、Open file location、Properties 全部未回歸。

## Agent 檢查（可執行）

`tests/unit/pinning_test.cpp`（既有檔案）新增並在 `main()` 呼叫：

- `TestLoadSchema1File()`：手寫一個 `schema=1` 兩欄檔案 → `Load()` 回
  `Loaded`，pin 數正確，`display_name` 為空，且目錄下**沒有** `.corrupt` 檔。
- `TestSaveRoundTripsDisplayName()`：`Pin(id, name, now)` → `Save()` → 新的
  `PinStore` `Load()` → `Records()[0].display_name == name`。
- `TestSaveWritesSchema2()`：`Save()` 後檔案第一行是 `schema=2`。

`tests/unit/panel_model_test.cpp` 新增並呼叫：

- `TestMissingPinBecomesPlaceholder()`：catalog 不含 `p1`，pins 含
  `{p1, "Gone App"}` → `Rows()[0].stable_id == L"p1"`、
  `display_name == L"Gone App"`、`PanelModel::IsMissingPin(Rows()[0])` 為真。
- `TestMissingPinKeepsOrder()`：pins `{p1(缺), p2(在)}` → `Rows()[0]` 是佔位、
  `Rows()[1]` 是 `p2`、`RecentStartIndex() == 2`。
- `TestMissingPinNotInSearch()`：同上設定加 `SetQuery(L"gone")` →
  `Rows()` 不含 `p1`。
- `TestPresentPinIsNotMissing()`：catalog 含該 pin → `IsMissingPin` 為假。

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

手動驗收（Release build，八條逐條在交接區記錄結果）。**驗收第 1 條前先備份
自己的 `%LOCALAPPDATA%\NimbleRun\favorites.txt`**，那是升級路徑的唯一真實樣本。

## 交接區

### `Pin()` 全部呼叫點（改成三參數 `Pin(stable_id, display_name, now)`）

- `src/app_host/main.cpp`（WM_RBUTTONDOWN 的 kCmdPin 分支，唯一的生產程式呼叫點）
  ：改傳 `entry.display_name`。
- `tests/unit/pin_store_test.cpp`：22 個呼叫點，全部用 `sed` 批次改成
  `Pin(id, L"", timestamp)`（這些測試只驗證 id/順序/到期邏輯，名稱留空即可）。
- `src/app_host/panel_model.cpp`、`src/app_host/panel_model.h`：不呼叫 `Pin()`
  （只讀 `PinRecord`），不受影響。

### 啟動入口清單與防護位置

用 `Select-String -Pattern 'LaunchEntry|ShellExecute'` 加人工追蹤呼叫鏈確認：

- 唯一的 `nimblerun::LaunchEntry(...)` 呼叫點在 `main.cpp` 的 `ActivateRow()`
  （原本行號 789 附近，現為加上守門後的區塊開頭）。Enter（VK_RETURN）、
  Alt+數字快選、格狀單擊、格狀放開左鍵（NR-046 拖曳判定為「非拖曳」時）**全部**
  透過這一個函式呼叫啟動，沒有第二條路徑繞過它——所以只在 `ActivateRow()`
  進入點加一次 `if (nimblerun::PanelModel::IsMissingPin(entry)) return;`
  就覆蓋了全部啟動入口，不需要在四個呼叫端各加一次。
- `ShellExecuteExW`（`ShowItemProperties`）與 `SHOpenFolderAndSelectItems`
  （`OpenFileLocation`）都已經用 `IsPathIdentity(entry.launch_identity)` 擋下
  空 `launch_identity`，且缺失格的右鍵選單已不再提供這兩個命令的入口，所以
  這兩處不需要額外加 `IsMissingPin` 判斷。

### `OlderSchema` 路徑的實測結果

原本 `ReadVersionedLines`（`src/storage/atomic_text_file.h`）在 schema 不符時
（`OlderSchema` 與 `NewerSchema`）都在填入 `lines` **之前**就回傳，也就是說
即使 `PinStore::Load()` 的 switch 加了 `case OlderSchema: break;`，实际拿到的
`lines` 仍是空的——等於把舊資料當成「這個檔案是空的」載入，`Reconcile`/`Save`
接著會用一個空的 pin 清單覆蓋掉使用者原本的 `favorites.txt`。這是比"整個檔案
變成 .corrupt"更隱蔽的資料遺失，且不會被舊有測試發現（舊測試都只檢查
`PinLoadResult`，不檢查資料是否還在)。

修法：把 `lines` 的填入搬到 schema 版本比較**之前**，讓 `OlderSchema` 分支也
拿得到資料行；`NewerSchema`／`Malformed`／`Unreadable` 等分支即使順便拿到
`lines` 也不影響——它們的呼叫端本來就不讀這個分支的 `lines`。用
`TestLoadSchema1File()`（`tests/unit/pin_store_test.cpp`）驗證：手寫一個
`schema=1` 兩欄 favorites.txt，`Load()` 回 `Loaded`、兩筆 pin 都讀到、
`display_name` 為空、且目錄下沒有產生 `.corrupt` 檔——測試通過。

### 手動驗收（8 條，環境無互動式 GUI 存取，全部待人工驗證）

1-8：**全部待人工在互動式 Alt+Space GUI 階段驗證**，本次工作階段無法操作
GUI、無法模擬滑鼠右鍵/拖曳、也無法備份使用者的
`%LOCALAPPDATA%\NimbleRun\favorites.txt`（沒有互動式檔案系統存取這台機器的
使用者設定檔）。**人工驗收前務必先備份自己的 `favorites.txt`**——這是升級
路徑（schema=1 → schema=2）唯一的真實使用者樣本，一旦升級路徑有本次未覆蓋到
的邊角情況，備份是唯一還原手段。

### spec 的實際修改行號

`docs/design-spec.md` §FR-011（修改前原文第 424-430 行區塊）：
- 在「項目右鍵可釘選或取消。」之後新增一條 pin 顯示名稱的敘述。
- 在「App 暫時不存在時保留 pin 紀錄 30 天...」之後、原本「30 天後仍不存在的
  pin 可在設定頁清理...」那條的位置，改寫為缺失格顯示與右鍵僅 Unpin 的敘述
  （整條取代，不是新增）。

### 建置與測試結果

```
cmake --build build   -> 42/42 targets 成功（含 NimbleRun.exe 全量重編）
ctest --output-on-failure -> 23/23 測試通過，包含：
  - nimblerun_pinning_test（pin_store_test.cpp，含新增的
    TestLoadSchema1File / TestSaveRoundTripsDisplayName / TestSaveWritesSchema2）
  - nimblerun_list_vertical_slice_test（panel_model_test.cpp，含新增的
    TestMissingPinBecomesPlaceholder / TestMissingPinKeepsOrder /
    TestMissingPinNotInSearch / TestPresentPinIsNotMissing）
```

### 偏離範圍之處

- `EmptyStatePrewarmIds()` 額外加了「跳過 `IsMissingPin` 列」的判斷，範圍文件
  的非目標裡寫「不為佔位格做圖示快取、預熱」，但原本的程式碼結構若不擋，
  佔位格的空 `stable_id` 對應查詢會照樣被送進圖示預熱佇列（浪費一次快取
  查詢，不會壞掉，但違反非目標）。判斷為範圍內的必要修正，不算新功能。
- `tests/unit/pin_store_test.cpp` 的 `TestPanelModelHidesAbsentPin` 直接
  斷言舊行為（缺席 pin 不顯示），與本 item 的決策矛盾，已改名為
  `TestPanelModelShowsAbsentPinAsPlaceholder` 並改寫斷言；
  `tests/unit/panel_model_test.cpp` 的
  `TestEmptyStatePrewarmIdsAbsentPinSkipped` 同理更新註解與斷言說明（斷言
  本身的數值不變，因為佔位格恰好從別的路徑被排除，但語意已不同）。這兩處
  改動不在範圍條列的檔案清單裡明寫，但都是「新增/覆寫決策造成舊測試斷言
  失真」的必然結果，不改會讓測試繼續斷言一個已經被否決的行為。
