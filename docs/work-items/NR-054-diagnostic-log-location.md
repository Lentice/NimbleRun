# NR-054 — The diagnostic log belongs in `logs\`, is written from two threads, and has no way in

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.1（資料位置與格式）／§FR-014（診斷記錄）／§11（失效與復原）
- Origin: 2026-08-06 repo audit, spec-conformance findings #6 與 #7 ＋ correctness finding #9

## Why

三個小缺陷落在同一個子系統，一起修比分三次修便宜：

1. **位置不符 §10.1。** 規格說 `logs\nimblerun.log`，實作寫在
   `%LOCALAPPDATA%\NimbleRun\nimblerun.log`（`main.cpp:2590` 建構
   `DiagnosticLog` 時傳的目錄，`diagnostic_log.cpp:37` 的 `JoinPath`）。
   輪替檔 `.log.1` 也一起散在根目錄，和 `settings.ini`、`usage.tsv`、
   `favorites.txt`、`catalog.cache`、`icons.cache` 混在一起。
2. **兩條執行緒無序寫入。** `DiagnosticLog::Write`
   （`diagnostic_log.cpp:32-68`）被 UI 執行緒與 icon worker
   （`icon_store.cpp` 的 `WriteLog`）同時呼叫，而它的
   「檢查大小 → 可能輪替 → 開檔 append → 寫入」序列沒有任何序列化。
   具體失敗：worker 剛通過大小檢查、正要 `MoveFileExW(path, path.1)`，
   UI 執行緒此時已用 `FILE_APPEND_DATA` 開啟 `path` 並寫入——那一行落進
   被搬走的舊檔，這次啟動失敗的診斷紀錄靜默消失。§11 的整個要點就是
   失效時要留下可查的痕跡。
3. **§FR-014 的「開啟記錄資料夾」不存在。** 設定對話框的字串表
   （`settings_editor.h:17-50`）與 `settings_dialog.cpp:51-73` 的
   `InitLabels` 都沒有這一項，也沒有任何命令處理它。使用者要回報問題時
   找不到記錄檔——而修好 #1 之後，它還會搬到一個更深的子目錄。

## Decisions already made — do not reopen

決定於撰寫本 item 時：

1. **搬移，不遷移。** 舊的 `nimblerun.log` / `nimblerun.log.1` 留在原地不動，
   **不搬、不刪、不讀**。記錄檔是診斷產物不是使用者資料；為它寫遷移碼
   是替一個沒人會回頭看的檔案付永久的複雜度。新位置從下次寫入開始生效。
   在交接區記錄「舊檔會留在根目錄」這個事實。
2. **序列化用 `std::mutex`，不是每次寫入都重開一個具名互斥體、也不是
   改用單一 logger 執行緒。** 只有兩個寫入者、每次寫入是一次短的
   append，程序內的 `std::mutex` 是最短且正確的答案。跨程序不需要處理
   （§3.2 未要求多實例）。
3. **設定頁只加「開啟記錄資料夾」一個按鈕，不做記錄檢視器。**
   §FR-014 說的是提供入口，交給檔案總管。
4. **不改記錄格式、不改 512 KiB × 2 的輪替策略。** 兩者都符合 §FR-014。

## Binding constraints — quoted, do not go looking for them

design-spec §10.1：

> `%LOCALAPPDATA%\NimbleRun\`
> - `settings.ini`
> - `usage.tsv`
> - `favorites.txt`
> - `catalog.cache`
> - `icons.cache`
> - `logs\nimblerun.log`

design-spec §FR-014：

> - 記錄檔單檔上限 512 KiB，最多保留兩份。
> - 記錄內容僅含事件名稱與計數，不得包含路徑、App 名稱或搜尋字串。
> - 設定頁提供「開啟記錄資料夾」。

design-spec §11：

> - 任一子系統失效時，其餘功能必須續行。

`AGENTS.md`：

- Keep all user data under `%LOCALAPPDATA%\NimbleRun`; do not write beside the
  executable.
- Do not add network access, telemetry, third-party runtime dependencies,
  services, drivers, or administrator requirements.
- Launch apps through Windows Shell APIs. Never build an arbitrary command line
  from search input.
- UI strings are English and should be centralized when more than one screen
  needs them.
- Prefer the smallest working change.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/diagnostics/diagnostic_log.h` **全檔**（25 行）——建構子簽章、
  `kMaxFileBytes`、`Write` 的宣告、成員。
- `src/diagnostics/diagnostic_log.cpp:32-68` — `Write` 全文：
  `EnsureDirectory` → `JoinPath` → 大小檢查 → `DeleteFileW` +
  `MoveFileExW` 輪替 → `CreateFileW(FILE_APPEND_DATA)` → 寫入 → 關檔。
  **§2 的鎖要涵蓋整段，包含 `EnsureDirectory`。**
- `src/app_host/main.cpp:2590` 一帶 — `DiagnosticLog` 的建構，傳入的目錄
  與檔名。§1 只改這裡的目錄引數。
- `src/app_host/main.cpp` 裡 `%LOCALAPPDATA%\NimbleRun` 路徑的取得方式
  （`SHGetKnownFolderPath` 或既有 helper）。**§1 要重用它，
  不要自己拼路徑。** 用 `Select-String -Path src/app_host/main.cpp -Pattern
  'NimbleRun'` 找出根目錄字串的唯一出處。
- `src/storage/atomic_text_file.h:19-24` — `EnsureDirectory`。它只建一層，
  **`logs\` 是根目錄底下一層，所以父目錄必須已存在**；確認 `DiagnosticLog`
  的呼叫者已經建過根目錄，或讓 §1 依序建兩層。
- `src/icons/icon_store.cpp` 的 `WriteLog` 與其呼叫點 — worker 執行緒側的
  寫入者。**不改**，但要確認它用的是同一個 `DiagnosticLog` 實例
  （若是，§2 的鎖就足夠；若各自持有實例寫同一個檔，鎖無效，
  **在交接區回報並停手**）。
- `src/app_host/settings_dialog.cpp:51-73` — `InitLabels` 與控制項建立；
  `:127` 的 `ParseCountText`；`:191` 一帶的 Apply/rollback。
  §3 在既有按鈕（`ClearUsageButton` / `ResetSettingsButton`）旁加一個。
- `src/settings/settings_editor.h:17-50` — `SettingsString` 列舉。
  §3 加一個鍵。
- `src/settings/settings_editor.cpp` 的 `SettingsStringText` — 對應的文字表。
- `src/resources/resource.h` — 控制項 ID 常數。§3 加一個。
- `src/launch/shell_launch.h` / `.cpp` — 既有的 Shell 啟動封裝。
  **§3 開啟資料夾必須走它或同樣的 `ShellExecuteExW` 形狀，
  絕不組命令列。**
- `tests/unit/diagnostic_log_test.cpp` — 既有的暫存目錄注入與輪替測試。
  §4 在這裡加。
- `tests/unit/settings_editor_test.cpp` — 字串表被 pin 住的既有斷言方式。

## Scope

### 1. 記錄檔搬進 `logs\`

在 `main.cpp` 建構 `DiagnosticLog` 的地方，把目錄改為根目錄底下的
`logs`：

```cpp
    // NR-054: design-spec §10.1 puts the log at logs\nimblerun.log, not beside
    // settings.ini in the root. Keeps the diagnostics artifact out of the
    // user-data listing, so "delete my data" and "send me your log" are
    // different directories.
    const std::wstring log_directory = nimblerun::JoinPath(data_directory, L"logs");
```

以工作樹中根目錄變數的實際名稱為準。`DiagnosticLog::Write` 開頭已有
`EnsureDirectory(directory_)`，所以 `logs\` 會在第一次寫入時自動建立——
**但 `EnsureDirectory` 只建一層**，確認根目錄在此之前必定已存在
（`SettingsStore` 等會先建）。若不保證，在 `Write` 裡改為先確保父目錄：
**不要**寫一個遞迴建目錄的通用函式，就是多呼叫一次 `EnsureDirectory`
傳父目錄。

輪替檔 `.log.1` 由 `JoinPath(directory_, name_ + L".1")` 產生，自動跟著搬。

**舊檔**：留在原地，不處理（Decisions §1）。

### 2. 序列化寫入

`diagnostic_log.h`：

```cpp
#include <mutex>
...
private:
    // NR-054: Write is called from the UI thread and the icon worker
    // (IconStore::WriteLog). Its check-size / rotate / open-append / write
    // sequence is not atomic: a rotation between another thread's open and its
    // write drops that line into the file that just got moved aside, silently
    // losing the diagnostic record for whatever failure was being reported --
    // which is the one moment §11 needs the log to work. Two writers doing
    // short appends: a plain mutex is the whole fix.
    mutable std::mutex write_mutex_;
```

`Write` 的**整個本體**用 `std::lock_guard` 包起來（從 `EnsureDirectory`
到 `CloseHandle`）。不要只鎖輪替那一段——競賽正是跨越輪替與開檔之間的。

`DiagnosticLog` 因此不可複製／移動；確認沒有任何地方複製它
（`Select-String -Pattern 'DiagnosticLog'`），需要的話明確
`= delete` 複製建構與賦值，讓誤用在編譯期就失敗。

**best-effort 語意不變**：鎖之後所有既有的早退（`EnsureDirectory` 失敗、
`CreateFileW` 失敗）照舊直接 return，記錄失敗永遠不影響呼叫者。

### 3. 設定頁的「Open log folder」

**字串**（`settings_editor.h` 的 `SettingsString` 加一個鍵，
`settings_editor.cpp` 的文字表加對應項，英文）：

```
OpenLogFolderButton  ->  L"Open log folder"
```

放在 `ClearUsageButton` / `ResetSettingsButton` 附近，維持列舉的分組。

**控制項**：`resource.h` 加一個 ID，`settings_dialog.cpp` 的 `InitLabels`／
控制項建立處照既有兩個按鈕的形狀加第三個，版面沿用既有的按鈕列排法。

**行為**：

```cpp
    // NR-054: design-spec §FR-014. Hand the directory to the Shell; never build
    // a command line (AGENTS.md). Create the directory first so the user does
    // not get an error dialog on a clean install that has not logged anything
    // yet.
```

- 先 `EnsureDirectory(log_directory)`（記錄檔可能還沒被寫過）。
- 以既有的 Shell 啟動路徑開啟該目錄（`ShellExecuteExW` 帶
  `lpVerb = L"open"`、`lpFile = <log directory>`，或 `src/launch/shell_launch.h`
  提供的封裝——**優先用既有封裝**）。
- 失敗時沿用設定頁既有的 notice 機制顯示訊息，不要彈自訂對話框。
  若既有 notice 鍵不合用才加一個。

按鈕**不改變任何設定**，所以它不得把 `SettingsEditor` 標記為 dirty，
也不參與 Apply／rollback。這一點要在測試裡守住（§4）。

### 4. 測試

**`tests/unit/diagnostic_log_test.cpp`**：

- **子目錄**：把 `DiagnosticLog` 指向 `<temp>\logs`（該目錄尚不存在），
  寫一行，斷言 `<temp>\logs\nimblerun.log` 存在且內容正確。
- **輪替仍在子目錄內**：寫到超過 `kMaxFileBytes`，斷言
  `<temp>\logs\nimblerun.log.1` 出現、根目錄 `<temp>` 底下沒有任何 `.log`。
- **併發**：兩條 `std::thread` 各寫 N 行（N 取足以跨越至少一次輪替，
  例如各 2000 行短訊息），join 之後斷言：
  - 程式沒有當機，
  - `nimblerun.log` 與 `.log.1` 的**每一行都是完整的一行**
    （以 `\t` 分欄、以 `\n` 結尾，沒有交錯截斷的行），
  - 兩檔的總行數等於 2N（若輪替丟棄了更早的 `.1`，則斷言
    「不超過 2N 且每行完整」，並在註解裡說明為什麼上界即可）。

  這是本 item 唯一的非平凡邏輯，這條測試就是它的守門員。

**`tests/unit/settings_editor_test.cpp`**：

- 新的 `SettingsString` 鍵回傳非空的英文文字（照既有 pin 幾個鍵的方式）。
- **按下「開啟記錄資料夾」不影響設定狀態**：由於該動作在 dialog 層而非
  editor 層，這條在 editor 測試裡表現為「`SettingsEditor` 沒有為此新增
  任何 setter」——用 `Dirty()` 在建構後仍為 false 的既有斷言即可，
  若無法測就在交接區註明由手動驗收 #4 覆蓋。

### 5. 更新 spec

`docs/design-spec.md` §10.1 若已寫 `logs\nimblerun.log`（審計指出有），
**不需要改**——本 item 是讓程式碼追上規格。確認一次；若規格其實寫的是
根目錄，那就改規格為 `logs\`（並在交接區說明你看到的是哪一種）。

§FR-014 補一句：

> 記錄寫入須可由多執行緒安全呼叫；輪替與寫入不得交錯，以免失敗當下的
> 記錄遺失。

若 §10.1 有「舊版本的檔案位置」相關說明就補一句舊 `nimblerun.log` 會留在
根目錄且不再被寫入；若沒有這類段落，**不要新增**。

## Performance

- 鎖：每次記錄多一次無競爭的 mutex 取得（數十奈秒），而同一次呼叫要做
  一次 `GetFileAttributesExW` 與一次 `CreateFileW`。不可測量。
- 記錄本身是事件驅動的稀疏寫入，不在任何熱路徑上（§NFR-002 的閒置路徑
  不受影響：本 item 不加計時器、不加輪詢）。
- 多一個按鈕不影響設定對話框的開啟時間。

## How this stays maintainable

**使用者資料與診斷產物分屬不同目錄。** 搬進 `logs\` 之後，
「清除我的資料」與「把記錄寄給我」指向不同的地方，而 §10.1 的清單一眼
就能對照磁碟上的實況。

**跨執行緒共用的物件自己負責同步。** `DiagnosticLog` 被兩個子系統共用，
所以鎖在它自己身上，而不是要求每個呼叫者記得鎖。**新增第三個寫入者
不需要知道這件事**——這是本 item 留下的契約。

**§FR-014 的三條要求現在全部有對應的守門員**：上限與輪替有既有測試、
內容規則由 code review 與 grep（只寫事件名）守、入口有手動驗收 #4。

## Non-goals

- **搬移或刪除舊的 `nimblerun.log`。** Decisions §1。
- **記錄檢視器、記錄等級、可設定的記錄開關、記錄上傳。**
  最後一項還會撞上 `AGENTS.md` 的「不得加入網路存取或遙測」。
- **改記錄格式、欄位、時間戳或輪替策略。**
- **改 `IconStore::WriteLog` 或任何呼叫端。**
- **跨程序的記錄同步（具名 mutex）。** 未要求多實例。
- **替 `settings_dialog.cpp` 的其他缺口補測試**（審計另指出
  `ParseCountText` 與 startup rollback 未測）。記在交接區，另案處理。
- **改設定對話框的圓角或整體外觀。** 那是獨立的 UI 一致性 item。

## Interaction with other open items

- **NR-049** 也處理執行緒議題但在 `src/app_host/main.cpp` 的 rebuild 路徑；
  **本 item 會碰 `main.cpp` 的 `DiagnosticLog` 建構處（一行）**，
  兩者若同時進行需注意同檔不同段的 diff，衝突風險低。
- **NR-050** 的 `WriteLog(L"icon-store", L"grow-failed")` 是本 item §2
  要保護的寫入者之一；兩者無檔案交集，任意順序皆可，但**兩者都落地後
  那條新診斷才真的不會遺失**。
- **未來的「設定頁使用者資料管理」item**（清理逾期 pin）會在同一個按鈕列
  加控制項；本 item 的按鈕形狀是它的範本。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §4 的子目錄、輪替、併發三組測試存在且通過。
3. 新的 `SettingsString` 鍵有測試 pin 住。

Manual（Release build，逐條打勾）：

1. 乾淨啟動後檢查 `%LOCALAPPDATA%\NimbleRun\`：出現 `logs\` 子目錄，
   記錄寫在 `logs\nimblerun.log`，根目錄不再有**新的**記錄寫入
   （舊檔若存在，時間戳不再更新）。
2. 讓記錄長到超過 512 KiB（可暫時大量觸發某個事件，或直接用測試覆蓋）：
   `logs\nimblerun.log.1` 出現，兩檔都在 `logs\` 內。
3. 觸發一次 icon worker 的記錄與一次 UI 執行緒的記錄（例如把
   `icons.cache` 設為唯讀後啟動並操作面板），確認兩者的行都完整出現、
   沒有半行或交錯。
4. 開設定頁 → 按「Open log folder」：檔案總管開在 `logs\`。
   在**從未寫過記錄**的乾淨狀態下也要能開（目錄被自動建立），不彈錯誤。
5. 按下該按鈕後關閉設定頁：**不出現「設定已變更」之類的提示**，
   `settings.ini` 的修改時間不變。
6. 記錄內容抽查：只有事件名與計數，**沒有**任何路徑、App 名稱或搜尋字串
   （§FR-014）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "diagnostic_log|settings_editor" --output-on-failure
```

```powershell
# 記錄目錄是 logs\：
Select-String -Path src/app_host/main.cpp -Pattern 'logs'
# expect: DiagnosticLog 建構處一次，設定頁「開啟記錄資料夾」一次（或共用一個變數）

# 鎖涵蓋整個 Write：
Select-String -Path src/diagnostics/diagnostic_log.cpp -Pattern 'lock_guard|write_mutex_'
# expect: Write 本體開頭一處；沒有在函式中段才上鎖

# DiagnosticLog 不可被複製到第二份：
Select-String -Path src/diagnostics/diagnostic_log.h -Pattern 'delete'
# expect: 複製建構與複製賦值 = delete（mutex 成員也會自然禁止，明寫較清楚）

# 開資料夾走 Shell，不組命令列：
Select-String -Path src/app_host/settings_dialog.cpp -Pattern 'ShellExecute|CreateProcess|system\('
# expect: 只有 ShellExecuteExW（或既有 shell_launch 封裝），絕無 CreateProcess/system

# 記錄內容仍只有事件名：
Get-ChildItem -Recurse -Include *.cpp -Path src | Select-String -Pattern '->Write\(L"|WriteLog\(L"'
# expect: 全部是短字串常數，無變數插值的路徑或名稱

# 沒有新增計時器或輪詢：
Select-String -Path src/diagnostics/diagnostic_log.cpp -Pattern 'SetTimer|Sleep|thread'
# expect: no match

# 改動範圍：
git diff --name-only
# expect: diagnostic_log.h/.cpp、main.cpp、settings_dialog.cpp、
#         settings_editor.h/.cpp、resource.h、diagnostic_log_test.cpp、
#         settings_editor_test.cpp、（可能）design-spec.md
```

## 交接區

（實作者填寫：修改的位置、`logs\` 的父目錄是否需要額外 `EnsureDirectory`、
UI 與 worker 是否共用同一個 `DiagnosticLog` 實例（如何確認的）、
併發測試的實際行數與是否觀察到輪替、建置與 CTest 結果、6 條手動驗收結果、
舊記錄檔的處置說明、`settings_dialog.cpp` 其他未測缺口的紀錄、
sanity greps、偏差、未完成事項。）

### 修改的位置

- `src/app_host/main.cpp`：`DiagnosticLog` 建構處改為
  `g_log_directory = nimblerun::JoinPath(nimblerun::DefaultSettingsDir(), L"logs")`
  （重用既有 `DefaultSettingsDir()`，不自己拼路徑）；新增全域
  `std::wstring g_log_directory` 並傳入 `ShowSettingsDialog` 作「Open log folder」
  用；新增 `#include "storage/atomic_text_file.h"`。
- `src/diagnostics/diagnostic_log.h`：`#include <mutex>`、private 成員
  `mutable std::mutex write_mutex_`（附 NR-054 註解）、複製建構／賦值 `= delete`。
- `src/diagnostics/diagnostic_log.cpp`：`Write` 整個本體（含 `EnsureDirectory`）包進
  `std::lock_guard`；`EnsureDirectory` 前多呼叫一次 `EnsureDirectory` 傳父目錄。
- `src/settings/settings_editor.h/.cpp`：`SettingsString` 加
  `OpenLogFolderButton → L"Open log folder"` 與 `OpenLogFolderFailedNotice`。
- `src/resources/resource.h`：`IDC_OPEN_LOG_FOLDER 2027`。
- `src/resources/NimbleRun.rc`：新增第三顆動作按鈕；底排按鈕改兩列（見「偏差」）。
- `src/app_host/settings_dialog.h/.cpp`：`ShowSettingsDialog` 加 `log_directory` 參數；
  `InitLabels` 補該鍵；`WM_COMMAND` 加 `IDC_OPEN_LOG_FOLDER` 分支（先
  `EnsureDirectory(log_directory)`，再 `ShellExecuteExW(lpVerb=open, lpFile=目錄)`，
  失敗走既有 notice 機制）。
- `tests/unit/diagnostic_log_test.cpp`：子目錄、輪替在子目錄內、併發三組新測試。
- `tests/unit/settings_editor_test.cpp`：pin 住 `OpenLogFolderButton` 文字。
- `docs/design-spec.md`：§FR-014 補「記錄寫入須可由多執行緒安全呼叫；輪替與寫入
  不得交錯，以免失敗當下的記錄遺失。」§10.1 已寫 `logs\nimblerun.log`（審計正確），
  不需改，且無「舊版本檔案位置」段落故未補舊檔說明。

### `logs\` 的父目錄是否需要額外 `EnsureDirectory`

**需要。** `EnsureDirectory` 只建一層；`logs\` 在根目錄下一層，所以父目錄（根目錄）
必須已存在。追蹤結果：`SettingsStore`／`UsageStore`／`PinStore` 建構與 `Load` 都只讀
不建目錄，而新安裝下第一次 `Write` 來自 icon worker 的 `IconStore::Open`
（`WriteLog(L"icon-store", L"created")`，`main.cpp` 的 worker `Start()` 立即觸發），
早於任何 `Save`。因此根目錄**不保證**已存在——在 `Write` 內
`EnsureDirectory(directory_)` 之前多呼叫一次 `EnsureDirectory` 傳父目錄
（`directory_` 去掉最後一個 `\\`／`/` 後段），兩層依序確保。

### UI 與 worker 是否共用同一個 `DiagnosticLog` 實例

**是，共用同一個實例。** `Select-String -Pattern 'DiagnosticLog'` 全 repo 只有
`main.cpp:226` 一個指標 `g_diag` 與 `main.cpp` 一處建構
`nimblerun::DiagnosticLog diag(g_log_directory, L"nimblerun.log")`；該區域物件
`diag` 在 `IconStore icon_store(..., &diag)` 注入同一個指標
（`icon_store.cpp:37` 的 `IconStore::WriteLog` 即經此呼叫 `log_->Write`），而 UI
執行緒的 `g_diag->Write` 也是同一物件的位址。只有一個實例、寫同一個檔，故 §2 的
程序內 `std::mutex` 即完整答案，無需停手。

### 併發測試實際行數與是否觀察到輪替

`TestConcurrentWritesNeverInterleave`：兩條 `std::thread` 各寫 2000 行，每行約 136
位元組（detail 固定 128 字元），總量約 544 KB——大於 512 KiB（必觸發一次輪替）、
小於 1 MiB（不會二次輪替覆蓋 `.1`）。**實際觀察到輪替**（斷言 `.log.1` 存在），
最終 `.log`（後段約 144 行）＋`.log.1`（前段約 3856 行）總行數**恰為 4000**，
每行 `\t` 分欄、`\n` 結尾、stage 完整、detail 符合 `writer-index-xxx…` 形狀，
無交錯或截斷。測試耗時約 5–6 秒（包含輪替寫入）。

### 建置與 CTest 結果

- Configure：`cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake
  -DCMAKE_BUILD_TYPE=Release` — 成功。
- Build：`cmake --build build` — 成功，無新增警告。
- `ctest --test-dir build --output-on-failure` — **23/23 全綠**（66 s）。
- `ctest --test-dir build -R "diagnostic_log|settings_editor"` — 註：設定 editor 的
  註冊名是 `nimblerun_settings_ui_test`，`-R settings_editor` 不會命中；實際跑
  `ctest -R "nimblerun_diagnostic_log_test|nimblerun_settings_ui_test"` 兩組都過。

### 6 條手動驗收

屬人工操作（執行應用程式、開設定頁、操作檔案總管），Agent 不執行，未逐條打勾。
對應的程式碼路徑已由自動化測試覆蓋的部分：§1（`logs\` 位置）由
`TestWritesIntoLogsSubdirectory`／`TestRotationStaysInsideLogsSubdirectory` 覆蓋，
§2（併發安全）由 `TestConcurrentWritesNeverInterleave` 覆蓋，§3 的字串由
`settings_editor_test` 的 pin 覆蓋；「乾淨狀態按鈕可開」的
`EnsureDirectory` 前置與「按鈕不標 dirty」的 editor 無 setter 由程式碼審視確認。

### 舊記錄檔的處置說明

舊 `%LOCALAPPDATA%\NimbleRun\nimblerun.log`／`nimblerun.log.1` **留在原地**，
不搬、不刪、不讀（Decisions §1）。新位置從下次寫入開始生效；兩者並存，
舊檔時間戳不再更新。

### `settings_dialog.cpp` 其他未測缺口

按 item Non-goals 明列：`ParseCountText`（:127）與 startup rollback 未測，
記入交接區另案處理，本 item 未補。

### sanity greps

全部符合預期：

- `logs` in `main.cpp`：建構處註解＋`JoinPath(..., L"logs")` 共用變數（`g_log_directory`）兩處。
- `lock_guard|write_mutex_` in `diagnostic_log.cpp`：`Write` 本體開頭一處，未在函式中段上鎖。
- `delete` in `diagnostic_log.h`：複製建構與複製賦值 `= delete` 明寫。
- `ShellExecute|CreateProcess|system\(` in `settings_dialog.cpp`：僅 `ShellExecuteExW`，無 `CreateProcess`／`system`。
- `->Write\(L"|WriteLog\(L"` in `src/**/*.cpp`：全部短字串常數，無變數插值的路徑或名稱。
- `SetTimer|Sleep|thread` in `diagnostic_log.cpp`：零命中（註解已避免這些字）。

### 偏差

1. **按鈕列版面**：320 寬的對話框底排原本 4 顆按鈕（Clear 100＋Reset 80＋Cancel 50＋OK
   50）已恰好填滿，加第三顆動作鍵放不下。改為動作鍵一列（Clear／Reset／Open log
   folder，各 100／80／112，佔 8–312）＋Cancel／OK 第二列（y=348），狀態列移到
   y=366，對話框高度 366→384。這是「一列塞不下」的版面必然結果，未更動其他控制項。
2. **`ShowSettingsDialog` 加參數**：`kSettingsMessage` 在 `WindowProc`，而
   `log_directory` 原是 `wWinMain` 區域變數，故提為全域 `g_log_directory`
   （沿用既有 `g_*` 全域模式）並以參數傳入 dialog。
3. **新增 notice 鍵**：既有 notice 皆不合用（`SaveFailedNotice` 說的是「Could not
   save settings」），依 item 授權新增 `OpenLogFolderFailedNotice`。
4. **併發測試的 detail 長度**：item 的「各 2000 行短訊息」字面不滿足「足以跨越至少
   一次輪替」（短行總量 <512 KiB）；改為 detail 128 字元（單行仍短、一欄），
   使總量 544 KB 精準觸發一次輪替、不丟行，總行數可斷言恰為 2N。
5. 註冊名是 `nimblerun_settings_ui_test`，故 `-R "diagnostic_log|settings_editor"`
   只命中 diagnostic_log；不屬本 item 範圍（NR-055 清理測試 CMake 樣板）。

### 未完成事項

6 條手動驗收屬人工操作；`ParseCountText`／startup rollback 測試缺口另案處理。
