# NR-058 — A corrupt or too-new user-data file must reach the user and the log

Phase 3 · Status `ready` · Depends on: NR-057（建議先落地，非硬相依）

- Source: `docs/design-spec.md` §10.4 Migration、§11 錯誤處理、§FR-014 診斷
- Origin: 2026-08-07 repo audit（規格已定義但未實作的行為）

## Why

三個 store 的 `Load()` 都精心回傳了一個列舉，說明這次載入為什麼失敗：

```cpp
enum class PinLoadResult { Loaded, Missing, Corrupt, NewerSchema };
enum class UsageLoadResult { Loaded, Missing, Corrupt, NewerSchema };
enum class SettingsLoadResult { Loaded, Missing, Corrupt, NewerSchema };
```

**四個呼叫端一個都沒有接。** `src/app_host/main.cpp`：

- `:2681` `settings_store.Load(settings);`
- `:2695` `usage.Load();`
- `:922` `g_pins->Load();`（在 `RefreshPins()` 裡，**面板每次開啟都跑**）
- `src/app_host/settings_dialog.cpp:367` `store.Load(current);`

結果是：使用者的 `settings.ini` 壞掉 → 檔案被改名成 `.corrupt`、設定悄悄
回到預設值、**沒有任何提示、日誌裡也沒有一行**。使用者只會看到「我的設定
自己不見了」。`favorites.txt` 被較新版本的 NimbleRun 寫過 → pin 全部消失，
同樣毫無說明。

這不是「可以更好」，是規格已經寫定而沒有實作的行為。`docs/design-spec.md`：

> §10.4：每種資料格式第一行包含 schema version。遇到較新且不支援的版本：
> 不覆寫原檔。將功能退回安全預設。**顯示一次錯誤提示。**

> §11 錯誤處理表：設定損壞 ｜ 使用者行為：**採預設值並通知** ｜
> 系統行為：原檔改名保存，不靜默覆寫。

前兩件（不覆寫、退回預設、改名保存）已經做到了。**「通知」沒有。**

同時 §FR-014 的診斷日誌是這件事的第二個出口：`DiagnosticLog` 已經在
`logs\nimblerun.log`（NR-054），但沒有任何 store 載入事件寫進去。

## Decisions already made — do not reopen

1. **通知用一次 tray balloon，不用 MessageBox。** §11 明文
   「錯誤提示不得使用會搶焦點的連續 MessageBox」，而 settings／usage 的載入
   發生在**開機自動啟動的當下**（NR-014），這時彈一個搶焦點的對話框是最糟的
   時機。tray balloon 是 §11 允許的形式，也是這個 App 唯一已經有的常駐 UI 出口
   （`AddTrayIcon` / `ShowHotkeyConflictNotice` 已經是現成範本）。
2. **一個 process 最多一則 balloon，內容彙總。** 三個檔案可能同時出事
   （例如使用者複製了整個舊 profile）。連續三則 balloon 就是 §11 禁止的
   「連續提示」的 tray 版本。彙總成一句。
3. **`Missing` 不通知。** 第一次執行時三個檔案都不存在，那是正常狀態。
4. **快取類檔案不通知。** §10.4 明文：`catalog.cache`／`icons.cache` 的
   schema 降級「對使用者不可見，不屬於需要通知的失敗」。
   `LoadCatalogCache()` 的呼叫端**不動**。
5. **每一種非 `Loaded` 的結果都寫一行日誌，包含 `Missing`。**
   日誌沒有「打擾使用者」的成本，而「這個檔案本來就不存在」正是排查時
   最想知道的一件事。日誌只寫**檔名、結果列舉名**，不寫路徑、不寫內容
   （§FR-014）。
6. **決策邏輯做成純函式並測試，Win32 呼叫留在 `main.cpp`。**
   「三個載入結果 → 要不要通知、通知文字是什麼」是純資料轉換，
   不需要 HWND 才能驗證（`AGENTS.md`：keep core logic independent of HWND）。
7. **不做「還原我的舊檔」的 UI。** `.corrupt` 檔就在資料夾裡，
   而設定視窗已經有「Open log folder」按鈕（NR-054）。
   通知文字指向那裡就夠了，做一個還原精靈是替一個罕見事件蓋一間房子。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.4：

> 遇到較新且不支援的版本：不覆寫原檔。將功能退回安全預設。顯示一次錯誤提示。
> 快取類檔案（`catalog.cache`、`icons.cache`）遇到較新且不支援的 schema
> version 時，不覆寫原檔、停用該快取，且**不顯示錯誤提示**。

`docs/design-spec.md` §11：

> 設定損壞 → 採預設值並通知；原檔改名保存，不靜默覆寫。
> 錯誤提示不得使用會搶焦點的連續 MessageBox。面板內提示或 tray balloon
> 只在使用者可採取動作時使用。

`docs/design-spec.md` §FR-014（診斷）：只寫 sanitized 的階段名、錯誤碼與
簡短細節；**永不寫搜尋文字、使用者名稱、個人路徑或命令列**。

`AGENTS.md` §Engineering rules：

- Prefer the smallest working change. Reuse existing code before adding helpers.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- UI strings are English and should be centralized when more than one screen
  needs them.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

行號只是導航線索，**程式碼片段與函式名才是規格**。

- `src/app_host/main.cpp:2670-2710` — `wWinMain` 的載入序：settings → catalog
  cache → usage。**注意此時主視窗尚未建立**，所以通知不能在這裡發出，
  必須先記下來、等視窗與 tray icon 就緒後再送。
- `src/app_host/main.cpp:1758-1795` — `AddTrayIcon` / `RemoveTrayIcon` /
  `ShowHotkeyConflictNotice`。**`ShowHotkeyConflictNotice` 就是本 item 要複製的
  範本**：它已經示範了 `NOTIFYICONDATAW` 的 `NIF_INFO` balloon 該怎麼填。
  照它的欄位與旗標寫，不要另發明一套。
- `src/app_host/main.cpp:915-930` — `RefreshPins()`，**每次面板開啟都呼叫
  `g_pins->Load()`**。通知必須有一次性閘門，否則壞掉的 `favorites.txt`
  會在每次按 `Alt+Space` 時彈一則 balloon。
- `src/app_host/main.cpp:160-175` — `g_context_menu_active` / `g_dialog_active` /
  `g_launch_failure_refresh`：既有的「一次性閘門」與「模態旗標」寫法，照抄風格。
- `src/app_host/main.cpp:127-140` — `dialog_strings` 命名空間，集中英文字串的
  既有位置。新字串加在這裡。
- `src/pins/pin_store.h:20-31`、`src/usage/usage_store.h:19-25`、
  `src/settings/settings_store.h:49-63` — 三個列舉的完整定義與註解。
- `src/diagnostics/diagnostic_log.h` / `.cpp`（44 / 90 行，**整檔讀完**）——
  事件的寫法與既有事件名的命名慣例。**照既有慣例命名，不要自創格式。**
- `src/app_host/settings_dialog.cpp:360-375` — 設定視窗自己的 `Load`；
  這裡的處置見 Scope §4。
- `tests/unit/diagnostic_log_test.cpp` — 新的純函式測試要加在這個檔（見 Scope §5）。

## Scope

### 1. 純決策函式

新增 `src/diagnostics/load_notice.h`（header-only，**不含 `<windows.h>`**）：

```cpp
namespace nimblerun {

// 哪一個使用者資料檔在啟動時沒有正常載入。位元旗標，因為三個檔案可能同時出事，
// 而使用者只該看到一則通知（design-spec §11：不得連續提示）。
enum class StoreLoadIssue : unsigned {
    None      = 0,
    Corrupt   = 1u << 0,   // 檔案已被改名保存，設定回到預設
    TooNew    = 1u << 1,   // 由較新版本寫入，原檔未動
};

// 三個 store 的結果彙總成一句英文通知。回傳空字串表示不必通知
// （全部 Loaded、或只有 Missing）。
inline std::wstring StoreLoadNoticeText(unsigned issues);

} // namespace nimblerun
```

實際形狀由實作者決定（旗標或一個小 struct 都可以），**硬性要求只有三條**：
不 include `<windows.h>`、不碰 HWND、可以被單元測試直接呼叫。

英文文案（放進 `main.cpp` 的 `dialog_strings`，或跟著純函式走——
**擇一，寫進交接區**）：

- 僅 corrupt：`"Some settings could not be read and were reset. The original files were kept next to them with a .corrupt suffix."`
- 僅 too-new：`"Some data files were written by a newer version of NimbleRun and were not used. They were left unchanged."`
- 兩者皆有：兩句串接。

### 2. `wWinMain` 收集結果

三個 `Load()` 的回傳值存進一個檔案範圍的旗標變數（跟 `g_last_hotkey_error`
同一層，就在它旁邊）。**每一種非 `Loaded` 的結果都立刻寫一行日誌**，
形如 `settings_load` / `result=Corrupt`，**只有檔名與列舉名**。
注意 `g_diag` 的建立時機晚於 settings 載入——若日誌尚未就緒，
就把事件連同旗標一起延後到日誌可用時再寫，**不要為此調整既有的初始化順序**。

### 3. 視窗就緒後送出一則 balloon

在 tray icon 加入之後（`AddTrayIcon` 之後、訊息迴圈之前，或第一次
`WM_TIMER`／既有的初始化尾端——**選你能證明 tray icon 已存在的那一點**），
呼叫 `StoreLoadNoticeText`，非空就送一則 balloon，用
`ShowHotkeyConflictNotice` 一模一樣的 `NOTIFYICONDATAW` 填法。
送出後把旗標清成 `None`，**保證一個 process 只送一次**。

### 4. `RefreshPins()` 的每次載入

`g_pins->Load()` 改成接住回傳值：

- `Loaded` / `Missing`：照舊，什麼都不做。
- `Corrupt` / `NewerSchema`：寫一行日誌，並**只在該 process 尚未通知過
  pin 問題時**設定旗標、送出 balloon。用一個 `bool g_pins_notified` 即可，
  不要為此蓋一個閘門類別（`g_launch_failure_refresh` 那種形狀是為了
  「合併進行中的 rebuild」，這裡沒有那個問題）。

`settings_dialog.cpp:367` 的 `Load`：**只加日誌，不通知**。
那是使用者主動打開設定視窗時的重讀，畫面本身就會顯示當下的值；
在一個已經開啟的對話框上再疊一則 balloon 沒有增加任何資訊。

### 5. 一個測試

在 `tests/unit/diagnostic_log_test.cpp` 加一組 `StoreLoadNoticeText` 的案例：
無問題 → 空字串；只有 corrupt；只有 too-new；兩者皆有 → 兩句都在。
**不新增測試執行檔**（NR-055 剛把 22 份樣板收成一個迴圈）。
若 `nimblerun_diagnostics` library 需要新增來源檔才看得到新標頭，
**優先讓它維持 header-only**，不改 `CMakeLists.txt`。

### 6. design-spec 增補

`docs/design-spec.md` §11 的錯誤處理表，在「設定損壞」列**之後**插入一列：

```
| 使用者資料由較新版本寫入 | 該功能回到預設，原檔保留 | 啟動後以單一 tray balloon 通知一次，日誌記錄一行；快取類檔案不通知 |
```

並在 §10.4 的第一段末尾補一句中文：

> 「顯示一次錯誤提示」在本產品實作為**單一 tray balloon**，於主視窗與 tray
> icon 就緒後送出，一個 process 至多一則，多個檔案同時出問題時彙總為一則。

## Performance

三個載入各發生一次（pin 的載入本來就每次開面板都跑，本 item 不改頻率）。
balloon 至多一則。**沒有新增任何計時器或輪詢**，不需要量測。

## How this stays maintainable

**回傳值有人接，列舉才有意義。** 今天這三個列舉是「寫了但沒人讀」的介面，
下一個人有兩種合理反應：以為它已經被處理了，或把它刪掉。接上之後，
新增一種載入結果的人會被編譯器的 `switch` 提醒有一個 UI 決定要做。

**通知的決定是純函式，出口是 Win32。** 要驗證「兩個檔案同時壞掉只出現一則
訊息」不需要開視窗，只要呼叫一個函式。這也是為什麼文案彙總邏輯不寫在
`ShowNotice()` 裡面。

## Non-goals

- **實作 schema migration。** §10.4 的「舊版本升級需有單元測試」在真的要升版
  之前無事可做。本 item 只處理**通知**。
- **改任何 store 的載入行為、改名行為或回傳值。** 本 item 只消費它們。
  （檔頭解析的收斂是 NR-057。）
- **對 `catalog.cache` / `icons.cache` 通知。** §10.4 明文禁止。
- **做「還原 .corrupt 檔」的 UI 或設定頁的資料管理入口。**
  後者是一個獨立、更大的 item（逾期 pin 清理也屬於它）。
- **改成 MessageBox 或面板內橫幅。** Decisions §1。
- **在 balloon 裡寫出檔案路徑。** §FR-014；文案只說「設定檔」與 `.corrupt` 後綴。
- **改初始化順序**去讓日誌更早可用。若順序真的擋路，記進交接區，
  不要順手重排 `wWinMain`。

## Interaction with other open items

- **NR-057** 收斂四份檔頭解析。**零重疊**（它改 `src/pins`、`src/usage`、
  `src/settings`、`src/catalog`、`src/storage`；本 item 改 `src/app_host`、
  `src/diagnostics`、`docs/design-spec.md`）。
  **建議 NR-057 先落地**：本 item 要相信「什麼情況回傳 `Corrupt`」，
  在只剩一份實作之後這件事只需要讀一處。若本 item 先落地也不會衝突。
- **NR-059** 只動 `main.cpp` 的 `Render()`；本 item 動 `wWinMain`、
  `RefreshPins()` 與檔案範圍變數。**同檔不同區段，可並行但會有 diff 相鄰**；
  兩者都落地時後做的那個要重跑完整 `ctest`。

## Acceptance

Automated：

1. `main.cpp` 與 `settings_dialog.cpp` 中，四處 `Load` 呼叫**全部接住回傳值**。
2. `StoreLoadNoticeText` 的四個案例測試通過。
3. Release 建置成功、**無新增警告**、`ctest` 全綠（目前 23 項＋新案例）。
4. `src/diagnostics/load_notice.h` 不 include `<windows.h>`，不出現 `HWND`。

Manual（每一項做完都把檔案還原）：

1. 把 `%LOCALAPPDATA%\NimbleRun\settings.ini` 第一行改成垃圾字串 → 啟動 App：
   出現**一則** tray balloon 說明設定已重設，資料夾裡有 `settings.ini.corrupt`，
   `logs\nimblerun.log` 有對應的一行且**不含任何路徑**。
2. 把 `favorites.txt` 與 `usage.tsv` 的檔頭同時改成 `schema=99` → 啟動：
   **只出現一則** balloon，內容同時涵蓋兩者，兩個原檔都**未被改名**。
3. 承上不重啟，連按三次 `Alt+Space` 開關面板：**不再出現任何 balloon**。
4. 三個檔案都正常時啟動：**沒有任何 balloon**。
5. 第一次執行（三個檔案都不存在）：**沒有任何 balloon**，但日誌有三行 `Missing`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_diagnostic_log_test --output-on-failure
```

```powershell
# 回傳值不再被丟棄：
Select-String -Path src/app_host/main.cpp,src/app_host/settings_dialog.cpp -Pattern '^\s*(g_pins->|usage\.|settings_store\.|store\.)Load\('
# expect: 無輸出（每一處都改成有接住回傳值的形式）

# 純函式沒有沾到 Win32：
Select-String -Path src/diagnostics/load_notice.h -Pattern 'windows\.h|HWND|NOTIFYICONDATA'
# expect: 無輸出

# balloon 只有一個送出點：
(Select-String -Path src/app_host/main.cpp -Pattern 'NIF_INFO').Count
# expect: 2（既有的熱鍵衝突通知 + 本 item 的一處），或 1（若共用同一個送出函式）

# design-spec 增補到位：
Select-String -Path docs/design-spec.md -Pattern 'tray balloon'
# expect: §10.4 與 §11 各至少一處命中
```

## 交接區

（實作者填寫：逐檔改動位置、balloon 送出點的選擇與「tray icon 已存在」的
證明、日誌事件名、文案最終放置位置、五項手動驗收的實測結果、
`ctest` 結果、上列 sanity greps 的實際輸出、任何必要偏差。）
