# NR-080 — `SettingsStore::Load` / `UsageStore::Load` must not leak partial parse state on a corrupt file

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §11（設定損壞 → 採預設值並通知）／§10.4
- Origin: 2026-08-08 第四次全 repo 稽核（catalog/settings/pins/usage/storage 子系統）

## Why

`PinStore::Load` 的部分狀態洩漏由 NR-072（含 `pins_.clear()`）處理；`UsageStore::Load`
與 `SettingsStore::Load` 有**同一形態**的契約違反，本 item 補齊。

三個 store 的 header 契約都明寫「非 `Loaded` 則 store 為空」：

- `src/settings/settings_store.h:49-50` — 「For anything other than Loaded the out
  parameter holds DefaultSettings(), never a partial parse.」
- `src/usage/usage_store.h:21` — 「For anything other than Loaded the store is empty …」

實作在「有效前綴列 + 中段損壞列」時洩漏部分狀態：

```cpp
// usage_store.cpp:43-69 — 迴圈先 push 合法列，:49-51 / :55-59 才在損壞列回 Corrupt
for (std::size_t i = 0; i < lines.size(); ++i) {
    ...
    records_.push_back(std::move(record));   // 合法列先入庫
    ...
}
// 命中損壞列 → PreserveCorrupt + return UsageLoadResult::Corrupt  ← records_ 仍是部分
```
```cpp
// settings_store.cpp:169-184 — 同形：`out` 已累積合法鍵值，:181-184 才在無 `=` 列回 Corrupt
```

host 端**直接消費這些部分狀態**（`src/app_host/main.cpp:2927-2929`）：

```cpp
const nimblerun::SettingsLoadResult settings_result = settings_store.Load(settings);
g_settings = settings;          // corrupt 檔的部分合法鍵被當成 live 設定
...
const nimblerun::UsageLoadResult usage_result = usage.Load();
g_usage = &usage;               // partial records 進入 ranking / 常用區
```

具體後果：

- **設定**：`settings.ini` 中段損壞 → 使用者看到「採預設值」的 balloon，但 live
  `g_settings` 其實帶著前綴行的部分自訂值（theme／hotkey／recent_count 等）——通知
  與實際行為矛盾；且下次設定對話框 OK 可能把部分值落盤成新 live 檔（原檔已改名
  `.corrupt`）。
- **usage**：`usage.tsv` 中段損壞 → 部分 `records_` 被 `UsageScore`／`Recent` 用於
  ranking 與常用區排序，直到下一次成功 `Load` 才清掉；在「只開一次面板就關閉」的
  會話裡部分紀錄被採納。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **兩個 store 的 `Corrupt` 回傳前清空**：`SettingsStore::Load` 在 `Corrupt` 前
   `out = DefaultSettings();`；`UsageStore::Load` 在 `Corrupt` 前 `records_.clear();`。
   讓 header 契約成為事實。`Missing`／`NewerSchema` 路徑本就是空的（`out =
   DefaultSettings()` 在 `:153` 已先做、`records_.clear()` 在 `:28` 已先做），不需改。
2. **host 端不改**：`g_settings = settings`（`main.cpp:2929`）在修好後拿到的是乾淨的
   `DefaultSettings()`（`Corrupt`）——正是 §11「採預設值」。
3. **`PinStore` 不在本 item**（NR-072 已含其 `pins_.clear()`）；本 item 只收
   settings／usage。
4. **測試**：`settings_store_test` 與 `recent_usage_test`（usage）各自新增「有效前綴
   列＋中段損壞列」的 case，斷言回 `Corrupt` 且輸出為空／預設值。照既有「手寫
   fixture 檔 → Load → 斷言」寫法。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Never simplify away: input validation at trust boundaries, error handling that prevents data loss.
- New non-trivial logic needs one focused runnable test or self-check.

design-spec §11：

- 設定損壞 → 採預設值並通知；原檔改名保存，不靜默覆寫。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/settings/settings_store.cpp:152-166`（`Load` 檔頭 switch）與 `:169-184`
  （資料迴圈，`:181-184` Corrupt 回傳）。主場之一。
- `src/usage/usage_store.cpp:27-71`（`Load`，`:43-69` 迴圈，`:49-51`／`:55-59`
  Corrupt 回傳）。主場之二。
- `src/settings/settings_store.h:49-50`、`src/usage/usage_store.h:21` — 契約。
- `src/pins/pin_store.cpp:40-42,53-56,59-62` — NR-072 已示範的 `pins_.clear()`
  形狀。**只讀參考，本 item 不動 pin_store。**
- `tests/unit/settings_store_test.cpp`、`tests/unit/recent_usage_test.cpp` —
  新 case 的家。

## Scope

### 1. `SettingsStore::Load`（`src/settings/settings_store.cpp`）

`Corrupt` 回傳點（`:165`、`:183`）在 `return SettingsLoadResult::Corrupt;` 之前加
`out = DefaultSettings();`（`:153` 的初始值被後續部分解析覆寫過，故回傳前必須重設）。

### 2. `UsageStore::Load`（`src/usage/usage_store.cpp`）

`Corrupt` 回傳點（`:51`、`:60`）在回傳前加 `records_.clear();`（`:28` 的初始清空
在 `Missing`／`NewerSchema` 早退路徑已足夠，`Corrupt` 需重設因迴圈已 push 部分列）。

### 3. 測試

- `settings_store_test`：手寫 `settings.ini` 含 2 筆合法鍵值列＋1 列無 `=` 的中段損壞
  → `Load` 回 `Corrupt`，斷言 `out == DefaultSettings()`（逐欄位或代表性欄位，
  以既有案例形狀為準）。
- `recent_usage_test`：手寫 `usage.tsv` 含 2 筆合法列＋1 列 3-field 錯誤 → `Load`
  回 `Corrupt`，斷言 `Records()` 為空（非 2 筆）。
- 既有 `Corrupt`／`Missing`／`NewerSchema` 案例回歸全綠。

### 4. 更新 spec？

不需。§11 描述的行為層級未動。

## How this stays maintainable

三個 store（pin/usage/settings）現在都兌現「非 Loaded 則空」的 header 契約；host 的
「直接採用 Load 輸出」路徑因此安全。日後任何 store 只要遵守同一契約，新增消費者
不必知道部分解析的細節。

## Non-goals

- **不改 `PinStore`**（NR-072 範圍）。
- **不改 host 端 `main.cpp:2927-2942` 的採用邏輯**（修好 store 後自動正確）。
- **不新增列舉值、不換 schema、不寫遷移碼。**
- **不檢查「設定檔案其實是好的、只是缺 `=` 那列」的特殊復原**（§11 語意是隔離）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項＋新增 case）。
2. §Scope 3 兩個新 case 通過。

Manual：

3. 手寫 `settings.ini` 含合法前綴列＋中段損壞列，啟動：balloon「採預設值」一次，
   面板主題／hotkey 等行為確為預設（前綴行的自訂值不再生效），無 crash。
4. 手寫 `usage.tsv` 含合法前綴列＋中段損壞列，啟動後開面板：常用區不顯示前綴列的
   部分紀錄（store 為空），`usage.tsv` 被改名 `.corrupt` 保留。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 兩個 store 的 Corrupt 回傳前都重設輸出：
Select-String -Path src/settings/settings_store.cpp -Pattern "out = DefaultSettings\(\)"
Select-String -Path src/usage/usage_store.cpp -Pattern "records_\.clear\(\)"
# expect: settings 至少 2 處（初始 + Corrupt 重設）；usage 至少 2 處（初始 + Corrupt 重設）

# 改動範圍：
git diff --name-only
# expect: src/settings/settings_store.cpp、src/usage/usage_store.cpp、
#         tests/unit/settings_store_test.cpp、tests/unit/recent_usage_test.cpp
```

## 交接區

（實作者填寫：兩個 Corrupt 回傳點的重設方式、新 case 的 fixture 寫法與斷言、建置與
CTest 結果、sanity greps、偏差、未完成事項。）

實作（2026-08-08）：

- **settings 重設**：`SettingsStore::Load` 兩個 `Corrupt` 回傳點各在 `return` 前加
  `out = DefaultSettings();`（default arm `:165`——該處其實仍是 `:153` 的初始值，
  item 要求加上以備未來；資料迴圈的 `equals == npos` `:187`——此處才是真正被部分
  解析覆寫過的點）。`Missing`／`NewerSchema` 早退路徑未改。
- **usage 重設**：`UsageStore::Load` 資料迴圈兩個 `Corrupt` 回傳點（`fields.size()!=3`
  `:51`、`stable_id/欄位解析失敗` `:60`）各在 `return` 前加 `records_.clear();`；
  default arm 不需改（`:28` 已清、迴圈未開始）。`Missing`／`NewerSchema` 未改。
- **測試**：`settings_store_test` 新增 `TestCorruptMidFileUsesDefaults`（schema=1 含
  2 筆合法鍵值列＋1 列無 `=` 損壞列 → Corrupt＋`hotkey==Alt+Space`＋`theme==System`
  ＋`recent_count==20`＋`.corrupt` verbatim 保留）；`recent_usage_test` 新增
  `TestCorruptMidFileClearsRecords`（schema=1 含 2 筆合法列＋1 列非數字欄位 → Corrupt
  ＋`Records()` 空＋`Recent()` 空＋`.corrupt` verbatim 保留）。既有案例一字未改。
- **建置與 CTest**：Release build 無新增警告；`ctest` 23/23 全綠。
- **sanity greps**：`out = DefaultSettings()` 於 settings_store.cpp 3 處（初始 1＋
  Corrupt 重設 2）；`records_.clear()` 於 usage_store.cpp 4 處（初始 1＋Corrupt 重設
  2＋其他 1）——都 ≥2；`git diff --name-only`＝settings_store.cpp、usage_store.cpp、
  settings_store_test.cpp、recent_usage_test.cpp，符合 item 預期。
- **偏差**：無。未完成事項：無。
