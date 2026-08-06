# NR-048 — The search test must actually assert in a Release build

Phase 3 · Status `ready` · Depends on: —

- Source: `AGENTS.md` §Engineering rules（新非平凡邏輯需一個可執行測試）／§Validation（Release 建置指令）
- Origin: 2026-08-06 repo audit, finding #1（最高優先）

## Why

`tests/unit/search_engine_test.cpp` 是**唯一**用裸 `assert()` 的測試檔；其餘 21 個
單元測試都用自己的 `Expect()` helper。`AGENTS.md §Validation` 與
`docs/testing.md` 指定的建置是 `-DCMAKE_BUILD_TYPE=Release`，而 CMake 的 Release
組態會加上 `-DNDEBUG`，`<cassert>` 的 `assert()` 因此整個被前置處理器展開成空
語句。

結果：**在專案明文規定的驗證組態下，`nimblerun_search_test` 無條件通過。**
它印出 timing 行、回傳 0，`ctest` 顯示綠色，但 §4.5 排名順序、NR-047 的 alias
fallback、`NormalizeName`、以及兩條 50 ms 效能上限**一條都沒有被檢查**。
NR-047 交接區引用的「23/23 全綠」裡，這一格是空的。

這是整個 repo 最便宜也最高價值的修補：搜尋是產品的核心邏輯，而它的回歸網
目前是假的。

## Decisions already made — do not reopen

1. **改測試檔，不改建置組態。** 不要為了讓 `assert` 生效而移除 `NDEBUG`、
   不要改 `tests/CMakeLists.txt` 的 `CMAKE_BUILD_TYPE`、不要加
   `#undef NDEBUG`。`#undef NDEBUG` 會讓這一個檔案與其他 21 個檔案用兩套失敗
   機制（`abort()` vs 印訊息後回傳非 0），且下一個複製這個檔當範本的人會把
   同樣的陷阱帶走。用與其他 21 檔相同的 `Expect()`。
2. **不引入測試框架。** `AGENTS.md`：不加第三方執行期相依。既有的 `Expect()`
   模式就是本 repo 的約定。
3. **不改任何 `src/` 檔案。** 這個 item 只讓既有斷言真的執行。若開啟斷言後
   有測試失敗，**那是本 item 的產出物之一**：把失敗如實記錄在交接區並停手，
   不要順手改 `src/search/` 讓它變綠——修 production 行為要另開 item。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- New non-trivial logic needs one focused runnable test or self-check.
- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Use the C++ standard library or Win32 native APIs before adding dependencies.
- Keep changes scoped to the requested task.

`AGENTS.md §Validation`（本 item 必須用這組指令驗證，因為問題只在這組指令下出現）：

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

design-spec §4.5（被此檔測試、目前未真的被檢查的排名規則）：

> 搜尋分數由高到低：
> 1. 完全相同。
> 2. 名稱前綴相同。
> 3. 任一單字前綴相同。
> 4. 連續子字串相同。
> 5. 字元依序匹配（subsequence）。
> 6. 名稱完全不匹配，但次要比對鍵（§4.4）匹配。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `tests/unit/search_engine_test.cpp` **整檔**（127 行）。`#include <cassert>`
  在 :3，`wmain()` 在 :22，全部 `assert(...)` 呼叫散布在 :31-:127。檔頭 :19-:20
  的 scoped `#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"`
  是 NR-047 留下的，**本 item 不動它**（見 Non-goals）。
- `tests/unit/start_menu_catalog_test.cpp:27` — 本 repo 的 `Expect()` 標準寫法。
  **照抄這一份的簽章與行為**，不要自創。
- `tests/unit/panel_model_test.cpp` — 另一個 `Expect()` 使用範例，含多個
  named test function 的組織方式。
- `tests/CMakeLists.txt:1-24` — `nimblerun_search_test` 的註冊。**不改。**
- `build/CMakeCache.txt` 的 `CMAKE_CXX_FLAGS_RELEASE`（含 `-DNDEBUG`）——
  讀它一次確認本 item 的前提為真，然後不要動它。

## Scope

### 1. 以既有的 `Expect()` 取代 `assert()`

在 `wmain()` 之前加入與其他測試檔相同形狀的 helper：

```cpp
namespace {

int g_failures = 0;

// NR-048: the repo-standard check. assert() compiles out under the -DNDEBUG
// that CMAKE_BUILD_TYPE=Release sets, which is exactly the configuration
// AGENTS.md tells you to validate with -- so this file's assertions used to
// vanish in the only build that matters. Never reintroduce assert() here.
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

} // namespace
```

比對 `start_menu_catalog_test.cpp:27` 的既有版本，若簽章或輸出格式不同，
**以既有版本為準**（一致性優先於本文的寫法）。

`wmain()` 結尾 `return g_failures == 0 ? 0 : 1;`。

每一個 `assert(x)` 逐一改為 `Expect(x, "<描述>")`，描述用英文、指出被檢查的
行為（例：`"prefix search returns Calendar before Calculator"`）。**不要合併
斷言**：一行一個檢查，失敗時才能從輸出直接看出是哪一條。

移除 `#include <cassert>`，加入 `<cstdio>` 若尚未包含（:6 已有）。

### 2. 兩條 timing 斷言必須真的是斷言

檔內兩個 5000 筆 timing block（NR-038 的 `L"e"` 與 NR-047 的 `L"zzqx"`）目前
以 `assert` 檢查 50 ms 上限，同樣被編掉。改為 `Expect()` 後，**保留兩條
`std::wprintf` 輸出行一字不改**——NR-047 交接區引用了它們的格式，後續 item
要靠它們比較數字。

NR-047 實測值為 603 µs 與 204 µs，上限 50 ms，因此開啟斷言不應造成失敗。
若在你的機器上失敗，那是真實訊號：照實記錄，不要放寬上限。

### 3. 確認斷言真的會失敗

**這是本 item 的核心驗收，不可省略。** 暫時把任一條斷言改成必然為假
（例如 `Expect(prefix_results.size() == 99, ...)`），以 §Agent checks 的
Release 指令重新建置並執行，確認：

- `ctest` **紅燈**，
- 輸出包含你的 `FAIL:` 訊息。

然後改回來，重新建置，確認全綠。把這兩次的輸出摘要寫進交接區。
沒有這一步，本 item 無法證明自己修好了它宣稱修好的東西。

## How this stays maintainable

一個檔案用一套失敗機制。本 repo 已經有 21 個檔用 `Expect()`，第 22 個用
`assert()` 的代價不是風格不一致，而是**在指定的驗證組態下靜默失效**——一個
不會被任何人看見的失敗模式。修法是讓它跟其他人一樣，而不是替它保留特例。

`Expect()` 沒有被抽到共用 header，這是刻意的：22 份各自 8 行的 helper 比一個
`tests/support/` 目錄外加 CMake 連結關係便宜，而且每個測試執行檔都能單獨編譯
執行。若哪天真的要共用，那是另一個 item 的事，且要連同 §NR-055 的 CMake
整併一起做。

## Non-goals

- **改 `src/` 的任何檔案。** 斷言若揭露真實 bug，記錄它、另開 item。
- **改 `tests/CMakeLists.txt`。** 註冊已存在且正確；NR-055 才動那個檔。
- **移除 NR-047 留下的 `#pragma clang diagnostic` 區塊。** 它抑制的是
  designated initializer 的省略警告，與斷言無關，且移除會讓建置出現新警告。
  下一個為別的理由動這個檔的 item 可以重新評估它。
- **稽核其他 21 個測試檔的斷言品質。** 它們已經用 `Expect()`，不在本 item。
- **新增測試案例。** 本 item 讓既有斷言生效，不擴大覆蓋率。

## Interaction with other open items

- **NR-055**（`tests/CMakeLists.txt` 樣板整併）只動 CMake、不動測試原始碼；
  兩者可任意順序落地，互不衝突。
- **任何未來動 `src/search/` 的 item** 都因本 item 而首次獲得真正的回歸保護。
  先落地本 item。

## Acceptance

Automated：

1. Release 建置無新增警告。
2. `ctest --test-dir build --output-on-failure` 全綠（目前 23 項）。
3. `Select-String -Path tests/unit/search_engine_test.cpp -Pattern 'assert\('`
   無 match。
4. §Scope 3 的「故意弄壞一條斷言 → ctest 紅燈 → 改回 → 綠燈」已執行並記錄。

Manual：無。本 item 沒有使用者可見的行為變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_search_test --output-on-failure
```

```powershell
# 裸 assert 已絕跡：
Select-String -Path tests/unit/search_engine_test.cpp -Pattern 'assert\(|<cassert>'
# expect: no match

# 全 repo 沒有第二個受害者（若有，記在交接區，不要在本 item 修）：
Get-ChildItem -Recurse -Include *.cpp -Path tests | Select-String -Pattern '^\s*assert\('
# expect: no match

# 只動了測試原始碼：
git diff --name-only
# expect: tests/unit/search_engine_test.cpp only

# 兩條 timing 輸出行仍在（NR-047 交接區依賴其格式）：
Select-String -Path tests/unit/search_engine_test.cpp -Pattern 'NR-038:|NR-047:'
# expect: both
```

## 交接區

### 修改位置

- `tests/unit/search_engine_test.cpp`（唯一改動檔案，`git diff --name-only` 驗證）。
  - 移除 `#include <cassert>`；`<cstdio>` 原已存在。
  - `wmain()` 前加入 repo 標準 `Expect(bool, const char*)` helper：失敗寫
    `FAIL: <message>` 到 stderr、累計 `g_failures`、不中止；signature 與
    `start_menu_catalog_test.cpp:27` 一致（行為採 §Scope 1 樣本的 counter
    版本，因為 `wmain` 結尾要 `return g_failures == 0 ? 0 : 1;`）。
  - 20 條 `assert(...)` 逐一改為 `Expect(x, "<英文描述>")`，一行一檢查。
  - 兩條 5000 筆 timing 的 `std::wprintf` 輸出行（`NR-038:`、`NR-047:`）
    一字未改；50 ms 上限改為 `Expect(elapsed_us / 1000 < 50, ...)`。
  - NR-047 留下的 `#pragma clang diagnostic` 區塊依 Non-goals 保留未動。

### 開啟斷言後揭露的既有 fixture bug（本 item 的產物）

主 catalog 與 alias_catalog 的 `AppEntry` 未填 `normalized_name`，而
`SearchApps` 依契約（design-spec §4.4、`src/catalog/catalog_refresh.cpp:121`、
`src/search/search_engine.h:8-13`）只比對 snapshot 已正規化的名稱——原始
`"Calculator"/"Calendar"` 對小寫 `"cal"` 永不命中。開啟斷言後首條斷言
（`prefix_results.size() == 2`）立即失敗，且 `Expect` 不中止導致對空結果取
`[0]` 而 SegFault。

判定為**測試 fixture 缺陷、非 `src/search/` 行為錯誤**：production 唯一呼叫端
`panel_model.cpp:67` 收到的 snapshot 一律由 `SetSnapshot` 預填
`normalized_name`，repo 其他 search 測試（`panel_model_test.cpp:29`、
`ui_palette_layout_test.cpp:56`）也都預填。故依 Acceptance#2「全綠」與 §Scope 3
「改回即全綠」修正 fixture（主 catalog 4 筆＋alias_catalog 3 筆補
`normalized_name`）。**未改任何 `src/` 檔案、未修 production 行為**，因此無需
另開 item。

### 建置與 CTest 結果

- 前提確認：`build/CMakeCache.txt` 的 `CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`。
- Release 建置（故意弄壞前後共三次）：無新增警告。
- 修正 fixture 後全量：`ctest --test-dir build --output-on-failure` → **23/23 全綠**；
  `ctest -R nimblerun_search_test` → Passed（0.16–0.32 s）。
- 兩條 timing 實測：NR-038 583 µs、NR-047 189 µs，遠低於 50 ms 上限，與
  NR-047 交接區的 603／204 µs 同量級。

### §Scope 3 故意失敗實證

第一次（把 `Expect(prefix_results.size() == 2, ...)` 暫時改為 `== 99`）——ctest
**紅燈**、退出碼 8、輸出含 `FAIL:`：

```
1/1 Test #1: nimblerun_search_test ............***Failed    0.29 sec
FAIL: trimmed prefix search returns Calendar and Calculator
NR-038: SearchApps over 5000 pre-normalized entries took 583 us (0 ms), matched 5000
NR-047: SearchApps over 5000 alias-fallback entries took 189 us (0 ms), matched 0
0% tests passed, 1 tests failed out of 1
The following tests FAILED:
      1 - nimblerun_search_test (Failed)
```

第二次（改回 `== 2`）——`nimblerun_search_test` Passed、`100% tests passed out of
1`、退出碼 0。

### sanity greps（全部符合預期）

- `Select-String -Path tests/unit/search_engine_test.cpp -Pattern 'assert\(|<cassert>'`
  → no match
- `Get-ChildItem -Recurse -Include *.cpp -Path tests | Select-String -Pattern '^\s*assert\('`
  → no match（全 repo 無第二個受害者）
- `git diff --name-only` → `tests/unit/search_engine_test.cpp`（只此一個）
- `Select-String -Pattern 'NR-038:|NR-047:'` → 兩條 timing 輸出行皆在

### 偏差

- §Scope 1 樣本註解的說明文字含字面 `assert()`，會命中 Acceptance#3 的
  `assert\(` grep；改寫為「assert macro」避免自打嘴巴，signature 與輸出格式未變。
- §Scope 1 樣本註解的說明文字因上述同一理由略作調整，其餘照樣本。

### 未完成事項

- 無。本 item 讓既有斷言真的執行、並修正了因此浮現的 fixture 缺陷；任何未來
  `src/search/` 行為變更自此獲得真正的回歸保護。
