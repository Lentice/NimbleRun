# NR-057 — One versioned text-store reader, not four copies

Phase 3 · Status `ready` · Depends on: —

- Source: `AGENTS.md` §Engineering rules（最小可行改動、先重用既有程式碼、先用標準函式庫）
- Origin: 2026-08-07 repo audit（重複程式碼；行為變更僅限一處明列的錯誤路徑）

## Why

`settings.ini`／`usage.tsv`／`favorites.txt`／`catalog.cache` 四個檔案共用同一套
磁碟慣例（UTF-8、第一行 `schema=<n>`、tmp＋atomic replace）。慣例的**寫入端**
已經共用了（`storage/atomic_text_file.h` 的 `AtomicWriteUtf8Text`），
**讀取端卻有四份逐字相同的實作**：

- `PinStore::Load()`（`src/pins/pin_store.cpp:86-128`）
- `UsageStore::Load()`（`src/usage/usage_store.cpp:104-143`）
- `SettingsStore::Load()`（`src/settings/settings_store.cpp:185-224`）
- `LoadCatalogCache()`（`src/catalog/catalog_cache.cpp:105-155`）

四者的前 40 行做同一件事：`ReadAllBytes` → `DecodeUtf8` → 去 BOM → 切行 →
檢查 `schema=` 前綴 → 解析版本號 → 比對版本。連同各自私有的
`Trim()`（4 份）、`SplitLines()`（3 份＋`catalog_cache.cpp` 一份 inline 展開）、
`SplitFields()`（3 份）、`ParseInt64()`（2 份），**約 130 行是複製貼上**。

代價是漂移，而且**已經發生了**：`catalog_cache.cpp` 這份沒有 `Trim` schema 行、
沒有 `schema_line.size() <= prefix.size()` 的長度檢查、用 `wcstoll`＋`errno`
而不是共用的 `ParseInt64`。四份實作對「什麼叫壞掉的檔頭」已經有四種答案，
而這條路徑決定的是**要不要把使用者的資料改名成 `.corrupt`**。

另外兩處同類問題順手處理（都在同一個主題「stdlib 已經有了」）：

- `src/search/search_engine.cpp:32` 手寫 `StartsWith()`。本專案是 C++20，
  `std::wstring_view::starts_with` 就是它。
- `storage/atomic_text_file.h:146` 的 `PreserveCorrupt()`：
  `GetFileAttributesW` 失敗回傳 `INVALID_FILE_ATTRIBUTES`（`0xFFFFFFFF`），
  與 `FILE_ATTRIBUTE_DIRECTORY` 相 and 必然非零，所以**「檔案不存在」會被
  當成「目標是目錄」而提早 return**。今天無害（沒有檔案本來就不必保存），
  但這個判斷式的意圖與行為不一致，是下一個讀它的人會踩的坑。

## Decisions already made — do not reopen

1. **新增一個共用讀取函式，不是一個 `Store` 基底類別。** 四個 store 的
   資料形狀、記憶體結構與錯誤列舉都不同，共用的只有**檔頭**。抽基底類別
   會逼出虛擬函式與模板參數；抽一個回傳 `lines` 的自由函式不會。
2. **共用函式不呼叫 `PreserveCorrupt`，只回報狀態。** 四個呼叫端對
   「壞檔要不要改名」的答案本來就不同（`catalog_cache` 對讀取失敗與舊版
   schema 都不改名），若把這件事塞進共用函式就得加一個 bool 參數——
   那是把差異藏進旗標。呼叫端各自寫一行 `if (status == Corrupt) { PreserveCorrupt(...); ... }`
   比較誠實也比較短。
3. **共用函式不決定「舊版 schema」的處置。** 它回傳 `OlderSchema`，
   由呼叫端維持**現行行為**：`catalog_cache` 直接重建（不改名），
   其餘三者維持現行的「視為 corrupt 並改名」。**本 item 不改這個決定**
   （見 Non-goals 第 1 條）。
4. **`Trim`／`SplitLines`／`SplitFields`／`ParseInt64`／`ParseUint64` 收進
   `storage/atomic_text_file.h`。** 它已經是這一族的共用標頭、已經是
   header-only inline，且已經住著 `EscapeText`／`UnescapeText`——
   同一組解析器的另一半。**不開新檔案。**
5. **收斂點採用嚴格的那一份。** 共用讀取函式實作 pin/usage/settings 的
   版本（Trim 檔頭、檢查前綴長度、用 `ParseInt64`）。`catalog_cache`
   因此對**畸形檔頭**變得更嚴格——這是本 item 唯一有意的行為變更，
   影響範圍只有本來就是錯誤路徑的輸入，且結果仍是「不使用該快取」。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md` §Engineering rules：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Use the C++ standard library or Win32 native APIs before adding dependencies.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- Do not overwrite user data in place. Use temporary files and atomic
  replacement for persistent writes.
- New non-trivial logic needs one focused runnable test or self-check.

`docs/design-spec.md` §10.2／§10.4（原文）：

> 每種資料格式第一行包含 schema version。遇到較新且不支援的版本：不覆寫原檔。
> 將功能退回安全預設。顯示一次錯誤提示。

> 快取類檔案（`catalog.cache`、`icons.cache`）遇到較新且不支援的 schema
> version 時，**不覆寫原檔**、停用該快取（僅以記憶體 LRU 運作），且**不顯示
> 錯誤提示**。使用者資料的既有規則不變。

**本 item 不實作「顯示一次錯誤提示」**——那是 NR-058 的範圍。本 item 只保證
現行的回傳值與改名行為逐項不變（`catalog_cache` 檔頭嚴格化除外）。

`AGENTS.md §Validation`：

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Files to read and trace first

行號只是導航線索，**程式碼片段與函式名才是規格**；先 grep 再改。

- `src/storage/atomic_text_file.h`（195 行，**整檔讀完**）——共用慣例的所在地。
  `ReadAllBytes`、`DecodeUtf8`、`EscapeText`／`UnescapeText`、`PreserveCorrupt`、
  `AtomicWriteUtf8Text`。新函式加在 `PreserveCorrupt` 之前。
- `src/pins/pin_store.cpp:22-128` — `Trim`／`SplitLines`／`SplitFields`／
  `ParseInt64` 的一份，以及 `PinStore::Load()` 的檔頭段。
- `src/usage/usage_store.cpp:22-143` — 同上，**外加 `ParseUint64`**（只有這裡有）。
- `src/settings/settings_store.cpp:26-224` — `Trim`（第三份）、`SplitLines`
  （第三份）。注意它**沒有** `SplitFields`（INI 是 `key=value` 不是 TSV），
  也**沒有** `ParseInt64`。
- `src/catalog/catalog_cache.cpp:28-190` — 最不一樣的一份：自己的
  `SplitFields`、inline 展開的切行迴圈、`wcstoll`＋`errno` 的版本解析、
  以及**三個與其他三者不同的分支**（讀不到檔不改名、舊 schema 不改名、
  `out.clear()` 的時機）。**逐行對照，差異就是規格。**
- `src/search/search_engine.cpp:32-77` — `StartsWith` 及它的三個呼叫點。
- `src/settings/settings_editor.cpp:16` — 第四份 `Trim`。**這一份要不要一起收**
  見 Scope §4。
- 測試：`tests/unit/pin_store_test.cpp`、`recent_usage_test.cpp`、
  `settings_store_test.cpp`、`catalog_refresh_test.cpp`（含 catalog cache 的
  round-trip）、`search_engine_test.cpp`。**它們是本 item 的安全網，先讀懂
  再改；一個都不准放寬。**

## Scope

### 1. 共用解析器搬進 `storage/atomic_text_file.h`

把下列五個函式各留**一份** inline 實作在此標頭（實作逐字取自現有的
pin/usage 版本，那兩份本來就相同）：

```cpp
inline std::wstring Trim(std::wstring_view value);
inline std::vector<std::wstring> SplitLines(std::wstring_view text);
inline std::vector<std::wstring_view> SplitFields(std::wstring_view line);  // tab
inline bool ParseInt64(std::wstring_view text, std::int64_t& out);
inline bool ParseUint64(std::wstring_view text, std::uint64_t& out);
```

刪掉 `pin_store.cpp`、`usage_store.cpp`、`settings_store.cpp`、
`catalog_cache.cpp` 的私有副本。**注意 `SplitFields` 回傳 `string_view`，
指向呼叫端傳入的字串**——搬家後生命週期不變，但檢查每個呼叫點傳入的
是具名變數而非暫存值。

### 2. 共用檔頭讀取

同一個標頭新增：

```cpp
enum class VersionedReadStatus {
    Loaded,       // 檔案讀到、UTF-8 解得開、檔頭版本 == expected_schema
    Missing,      // 檔案不存在（ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND）
    Unreadable,   // 存在但讀不到（權限、鎖定等）
    Malformed,    // 解不開 UTF-8、空檔、沒有 schema= 檔頭、版本號不是整數
    OlderSchema,  // 檔頭版本 < expected_schema
    NewerSchema,  // 檔頭版本 > expected_schema
};

// 讀 <directory>\<name>，去 BOM，切行，驗證第一行的 schema= 檔頭。
// 成功時 lines 收到「不含檔頭」的資料行（原樣，未 Trim）。
// 本函式不改名、不寫入、不刪除任何檔案：處置由呼叫端決定。
inline VersionedReadStatus ReadVersionedLines(std::wstring_view directory,
                                              std::wstring_view name,
                                              int expected_schema,
                                              std::vector<std::wstring>& lines);
```

`Malformed` 涵蓋現行四份實作中所有「一律 corrupt」的檔頭錯誤；把
`Unreadable` 與 `Missing` 分開，是因為 pin/usage/settings 現行就是這樣分的
（`GetLastError()` 判斷），而 `catalog_cache` 兩者都回 `false`——分開之後
兩種現行行為都表達得出來，不需要旗標。

### 3. 四個呼叫端改寫

每個 `Load()` 的檔頭段變成一次 `ReadVersionedLines` 加一個 `switch`，
**逐案對應到它今天回傳的值與今天是否呼叫 `PreserveCorrupt`**。
以 `PinStore::Load()` 為例（其餘兩個 store 同形，只換列舉名）：

```cpp
std::vector<std::wstring> lines;
switch (ReadVersionedLines(directory_, kFileName, kSchemaVersion, lines)) {
case VersionedReadStatus::Loaded:
    break;
case VersionedReadStatus::Missing:
    return PinLoadResult::Missing;
case VersionedReadStatus::NewerSchema:
    return PinLoadResult::NewerSchema;   // 原檔不動（design-spec §10.4）
default:                                  // Unreadable / Malformed / OlderSchema
    PreserveCorrupt(directory_, kFileName);
    return PinLoadResult::Corrupt;
}
```

`LoadCatalogCache()` 的 `switch` **不一樣，照它今天的行為寫**：
`Missing`／`Unreadable` → `return false`（**不改名**）；
`Malformed` → `PreserveCorrupt` 後 `return false`；
`OlderSchema`／`NewerSchema` → `return false`（**不改名**，保留
`catalog_cache.cpp:150` 既有註解的理由：例行 schema 升版不該替每個使用者
生出一個 `.corrupt` 檔）。

各 store 的**資料行迴圈原樣保留**（欄位數、`UnescapeText`、重複 id 規則、
`out.clear()` 時機都不准動），只是改用共用的 `SplitFields`／`ParseInt64`。

### 4. `settings_editor.cpp` 的第四份 `Trim`

`src/settings/settings_editor.cpp` 是純邏輯模組，**不應該為了一個 `Trim`
去 include 一個帶 `<windows.h>` 的持久化標頭**。先確認它現有的 include：
若 include `atomic_text_file.h` 會把 `<windows.h>` 拉進這個模組，**就把這份
`Trim` 留在原地**，並在它上方加一行註解說明為什麼它是刻意的例外。
**做了哪個選擇要寫進交接區。**

### 5. 兩個順手修正

- `search_engine.cpp`：刪掉 `StartsWith`，三個呼叫點改用
  `value.starts_with(prefix)`（`std::wstring_view`，C++20）。
  注意 `name.substr(word_start).starts_with(query)` 的語意與原本一致。
- `atomic_text_file.h:146`：`PreserveCorrupt` 的屬性判斷改成先驗 sentinel：

```cpp
const DWORD attributes = GetFileAttributesW(path.c_str());
if (attributes == INVALID_FILE_ATTRIBUTES ||
    (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return;   // 沒有檔案，或那個名字是目錄：兩者都沒有東西可以保存
}
```

### 6. 一個測試

在 `tests/unit/settings_store_test.cpp`（已經是檔頭錯誤路徑覆蓋最完整的
那一個）加**一組**針對 `ReadVersionedLines` 的直接案例，每個狀態一例：
不存在、UTF-8 壞掉、空檔、沒有 `schema=`、版本號非整數、版本較舊、
版本較新、正常（並驗證回傳的 `lines` **不含檔頭行**）。
**不新增測試執行檔**（NR-055 剛把 22 份樣板收成一個迴圈，加一個 target
就是把樣板加回來）。

## Performance

四個 `Load()` 都在啟動或面板開啟時各跑一次，檔案是數十 KB 等級。
共用函式回傳 `std::vector<std::wstring>`（與現行四份實作相同的配置行為），
**沒有新增任何複製**。不需要量測。

## How this stays maintainable

**慣例只有一份實作，就不會有四種答案。** 這一族檔案的規格是「UTF-8＋
`schema=` 檔頭＋atomic replace」；寫入端早就是一個函式，讀取端也該是。
下一次調整檔頭規則（例如加註解行、允許 BOM 以外的前導空白），改一處。

**差異留在呼叫端，不進參數。** `catalog_cache` 對舊版 schema 的處置與
使用者資料不同，這是**產品決定**（快取可重建、使用者資料不可），
所以它應該長在 `LoadCatalogCache()` 的 `switch` 裡被看見，而不是縮成
共用函式的一個 `bool preserve_on_corrupt`。旗標會讓下一個人以為那是
可調的選項。

## Non-goals

- **改任何「舊版 schema 該怎麼辦」的決定。** 今天四個 store 沒有任何
  migration 程式碼，而 `kSchemaVersion` 只有 `catalog.cache` 升過（=2）。
  design-spec §10.4 的「舊版本升級需有單元測試」在真的要升版之前無事可做，
  預先蓋一套 migration 框架就是替不存在的需求寫程式。**要升版時再開 item。**
- **實作 §10.4 的「顯示一次錯誤提示」。** 那是 NR-058，且它需要
  `main.cpp` 的對話框流程，與本 item 的純模組改動零重疊。
- **合併四個 `*LoadResult` 列舉。** 它們是各 store 的公開介面，
  被測試與呼叫端具名使用；為了少三個列舉去改公開介面不划算。
- **改任何磁碟格式、欄位、跳脫規則或檔名。**
- **改任何 store 的資料行解析語意**（重複 id 規則、欄位數檢查、
  `out.clear()` 時機）。
- **抽共用的測試 helper／測試框架。**
- **動 `icons.cache`／`icon_pack_format`**。那是二進位格式，不屬於這一族。

## Interaction with other open items

- **NR-058** 消費這些 `Load()` 的回傳值，**不改 store 內部**。
  兩者零重疊，但**建議先落地 NR-057**：NR-058 要對 `NewerSchema`／`Corrupt`
  做分流，在四份實作收斂成一份之後，它要相信的行為只有一處可讀。
- **NR-059** 只動 `main.cpp` 的 `Render()`。零重疊。

## Acceptance

Automated：

1. `Trim`／`SplitLines`／`SplitFields`／`ParseInt64`／`ParseUint64` 在
   `src/` 底下各只有**一份定義**（`settings_editor.cpp` 的 `Trim` 若依
   Scope §4 保留為例外，則為兩份且有註解）。
2. `src/` 底下 `schema=` 檔頭的解析只有**一處**。
3. `search_engine.cpp` 不再有 `StartsWith`。
4. Release 建置成功、**無新增警告**、`ctest` 全綠（目前 23 項）。
5. 既有測試**一個都沒改**（新增案例除外，不得放寬既有斷言）。

Manual：

1. 手動把 `%LOCALAPPDATA%\NimbleRun\favorites.txt` 的第一行改成 `schema=99`，
   叫出面板：pin 全部消失但**檔案原樣還在**（沒有 `.corrupt`）。改回 `schema=1` 復原。
2. 把 `favorites.txt` 第一行改成垃圾字串，叫出面板：出現
   `favorites.txt.corrupt`，原檔消失，App 不崩潰。
3. 刪掉 `catalog.cache`，重啟：正常重建，沒有 `.corrupt` 產生。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 解析器各只剩一份定義：
Select-String -Path src/*/*.cpp,src/*/*.h -Pattern 'std::wstring Trim\(|SplitLines\(std::wstring_view|SplitFields\(std::wstring_view|bool ParseInt64\(|bool ParseUint64\('
# expect: 全部命中在 src/storage/atomic_text_file.h（settings_editor.cpp 的 Trim
#         若依 Scope §4 保留，額外一行且上方有註解）

# 檔頭解析只剩一處：
Select-String -Path src -Pattern 'kSchemaPrefix' -Recurse
# expect: 各 store 只有「定義自己的版本號常數」，比對邏輯不在其中

(Select-String -Path src -Pattern 'ReadVersionedLines' -Recurse).Count
# expect: 1 個定義 + 4 個呼叫點

# 手寫 StartsWith 消失：
Select-String -Path src/search/search_engine.cpp -Pattern 'StartsWith'
# expect: 無輸出

# PreserveCorrupt 的 sentinel 檢查存在：
Select-String -Path src/storage/atomic_text_file.h -Pattern 'INVALID_FILE_ATTRIBUTES'
# expect: 1 行

# 淨刪除：
git diff --stat
# expect: 刪除行數明顯多於新增行數（預期淨 -80 行以上）
```

## 交接區

（實作者填寫：逐檔改動位置、四個 `switch` 與原行為的逐案對照、
Scope §4 的選擇與理由、`ctest` 結果、上列 sanity greps 的實際輸出、
任何必要偏差。）
