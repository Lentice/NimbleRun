# NR-203 — 搜尋排名新增首字母縮寫層（initialism tier）

Phase 5 · Release gate · Depends on: NR-047

- Source: `docs/design-spec.md` §4.4、§4.5
- Origin: 2026-09-08 使用者實機回報。輸入 `vsc` 時 `Visual Studio Code` 排在第 10 位；
  追查後確認它落在最低的 subsequence 層
- Priority: **MEDIUM**——影響所有以縮寫查詢的使用者，但不像 NR-202 那樣汙染其他 query

## Goal

在 §4.5 的搜尋分層中，於「任一單字前綴相同」與「連續子字串相同」之間插入一層
**首字母縮寫**：query 是名稱各單字首字母串接的前綴時命中。

只改匹配分層。使用分數、tie-break、持久化與 catalog 來源都不在範圍內。

## 問題陳述 — 這是分類錯誤，不是記憶問題

`NormalizeName` 保留單字間的單一空格，所以 `Visual Studio Code` 正規化為
`"visual studio code"`。用 `vsc` 走 `src/search/search_engine.cpp` 的 `Rank()`：

| 檢查 | 結果 |
|---|---|
| `Exact` | 不符 |
| `NamePrefix`（`"visual studio code"` 以 `vsc` 開頭？）| 不符 |
| `WordPrefix`（`visual`／`studio`／`code` 任一以 `vsc` 開頭？）| 不符 |
| `Substring`（含字面 `vsc`？）| 不符 |
| `Subsequence`（**v**isual **s**tudio **c**ode）| **命中，最低層** |

`vsc` 是使用者刻意打的高精度縮寫，卻和「字母碰巧依序散落」共用同一層。這是
`Rank()` 缺一層造成的**分類錯誤**。

`usage_score` 救不了：§4.5 明文「使用分數只作為同類文字匹配的次要排序，不得讓常用
但文字匹配很差的 App 壓過明確匹配」，分數只在同層內比較。因此使用者選擇多少次，
`Visual Studio Code` 都會停在所有較高層命中之後。

**這是本 item 存在的理由，也是為什麼不該用「記住使用者選過什麼」來補救**：用記憶去
蓋過一個排錯層的匹配，等於在錯的地基上蓋房子，而且每一個縮寫查詢（`ssms`、`wt`、
`vs`）都還是會落在最低層。

## 為什麼放在 WordPrefix 之下、Substring 之上

- 在 `WordPrefix` 之下：`code` 對 `Visual Studio Code` 是明確的單字前綴，比縮寫精確。
- 在 `Substring` 之上：`vsc` 對 `AvsCleaner`（`"avscleaner"` 含字面 `vsc`）是字中意外
  撞到；對 `Visual Studio Code` 則是刻意縮寫。縮寫更能代表使用者意圖。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Use the C++ standard library or Win32 native APIs before adding dependencies.

> Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

`docs/design-spec.md` §4.5：

> 搜尋分數由高到低：1. 完全相同。2. 名稱前綴相同。3. 任一單字前綴相同。
> 4. 連續子字串相同。5. 字元依序匹配（subsequence）。
> 6. 名稱完全不匹配，但次要比對鍵（§4.4）匹配。

> 次要鍵命中一律歸為最低一層，不按其命中方式細分；任何名稱命中永遠優先於任何次要鍵命中。

> 同分時依序比較：1. 已釘選優先。2. 使用分數較高者優先。3. 名稱較短者優先。
> 4. 以不區分大小寫的名稱排序，確保結果穩定。

> 使用分數只作為同類文字匹配的次要排序，不得讓常用但文字匹配很差的 App 壓過明確匹配。

`docs/work-items.md` §已否決的方向：

> | 以「搜尋太慢」為前提的 item：debounce、incremental narrowing、搜尋移到背景執行緒、
> 結果筆數上限 | … 5000 筆 catalog：既有 `L"e"` 查詢 **603 µs** … ceiling 是 **50 ms**。 |

> | 另立一套搜尋鍵抽象：`SearchKeys(entry)` 存取器或 `std::vector<std::wstring> search_keys` |
> … vector 會在熱掃描路徑上多一次 per-entry 堆積配置與一層內迴圈 … |

第二列直接約束本 item 的實作：**縮寫比對不得在熱掃描路徑上配置記憶體**。

## Files to read and trace first

- `docs/design-spec.md` §4.4、§4.5。
- `docs/work-items.md` 的 §已否決的方向（兩列都約束本 item）。
- `docs/work-items/NR-047-*.md`：`MatchRank::Alias` 與次要鍵的原始決策。
- `src/search/search_engine.cpp`：`CollapseWhitespace`、`NormalizeName`、
  `MatchRank` enum、`Rank()`、`SearchApps()` 的排序比較器。
- `src/search/search_engine.h`：`NormalizeName` 對「單一空格」的契約。
- `src/catalog/app_entry.h`：`normalized_name` 欄位。
- `tests/unit/search_engine_test.cpp`：既有分層 case 與 5000 筆效能 check。

## Scope

### 1. 新增 MatchRank 層

`MatchRank` enum 插入 `Initialism`，位於 `WordPrefix` 與 `Substring` 之間，其後
各層的數值依序遞增：

```cpp
enum class MatchRank : int {
    Exact = 0,
    NamePrefix = 1,
    WordPrefix = 2,
    Initialism = 3,   // NR-203: query is a prefix of the word initials
    Substring = 4,
    Subsequence = 5,
    Alias = 6,        // NR-047: matched the target/AUMID, not the name
    NoMatch = 7,
};
```

比較器只用 `left.rank < right.rank`，不得針對新層加任何特例。`MatchRank::Alias`
的收攏邏輯（`rank == MatchRank::NoMatch` 才試次要鍵）不變。

### 2. Rank() 加入縮寫檢查

放在 `WordPrefix` 迴圈之後、`Substring` 檢查之前。縮寫定義：名稱第一個字元，加上
每個空格後的第一個字元（`NormalizeName` 已保證空白收斂為單一空格）。query 是該
縮寫串的**前綴**即命中——因此 `vs` 能命中 `Visual Studio Code`，而既有的
「名稱較短者優先」tie-break 會讓 `Visual Studio` 排在它前面，不需額外處理。

**必須就地比對，不得建出縮寫字串**（見 §Binding constraints 第二列）：

```cpp
std::size_t initial_index = 0;
bool at_word_start = true;
bool initials_match = true;
for (const wchar_t character : name) {
    if (at_word_start) {
        if (initial_index == query.size()) {
            break;  // query already consumed: it is a prefix of the initials
        }
        if (character != query[initial_index]) {
            initials_match = false;
            break;
        }
        ++initial_index;
    }
    at_word_start = (character == L' ');
}
if (initials_match && initial_index == query.size()) {
    return MatchRank::Initialism;
}
```

複雜度 O(名稱長度)，與既有的 subsequence 掃描同量級，零配置。名稱為空時迴圈不執行，
`initial_index == 0 != query.size()`（`SearchApps` 已保證 query 非空），安全落到下一層。

### 3. focused test

`tests/unit/search_engine_test.cpp` 新增：

- `vsc` 對 `Visual Studio Code` 命中 Initialism，且排在同時存在的 `Substring`
  命中（例如名為 `AvsCleaner` 的 fixture）之前；
- `code` 對 `Visual Studio Code` 仍是 `WordPrefix`，排在任何 Initialism 命中之前；
- `vs` 同時命中 `Visual Studio` 與 `Visual Studio Code`，較短者在前；
- 多字名稱的縮寫不跨越非空格分隔（`Paint 3D` 的縮寫是 `p3`）；
- query 長於縮寫時不命中 Initialism（`vscx` 對 `Visual Studio Code`）；
- 既有 Exact／NamePrefix／WordPrefix／Substring／Subsequence／Alias 的分層 case
  全部維持原結果；
- 既有 5000 筆效能 check 維持在其 ceiling 內。

## Non-goals

- 不改 §4.6 使用分數、`UsageScore()`、`usage.tsv` schema 或任何持久化格式。
- 不改同分 tie-break 的四條順序，也不讓使用分數跨層（§4.5 明文禁止）。
- 不加入選擇記憶、query affinity、per-query 權重或任何新的持久化狀態。
- 不加入模糊比對、編輯距離、評分權重模型或次要鍵的細分層級。
- 不加入中文拼音／注音／同義詞展開（§已否決的方向 明文須先問使用者）。
- 不做 debounce、incremental narrowing、背景搜尋或結果筆數上限（前提已被實測推翻）。
- 不新增 `search_keys` vector 或 `SearchKeys(entry)` 抽象（已否決）。
- 不把縮寫預先算進 `AppEntry`——就地比對已足夠，預算欄位屬未經量測的優化。
- 不改 catalog 來源、過濾、dedup、icon 或 UI。

## Acceptance

1. `MatchRank` 含 `Initialism`，位於 `WordPrefix` 與 `Substring` 之間；後續各層
   數值連續遞增，`NoMatch` 仍為最大值。
2. `Rank()` 的縮寫比對不配置記憶體、不建立中間字串，複雜度 O(名稱長度)。
3. `vsc` 對 `Visual Studio Code` 回傳 `Initialism`；`vscx` 不回傳 `Initialism`。
4. 排序比較器未針對新層加入特例；`Alias` 收攏邏輯不變。
5. 既有所有分層 case 結果不變；5000 筆效能 check 仍在 ceiling 內。
6. Release build 無新增 warning；`ctest --test-dir build --output-on-failure` 全通過。
7. `docs/design-spec.md` §4.5 的分層清單同步加入該層並說明其定位。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```powershell
ctest --test-dir build -R "search" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "Initialism" src/search/search_engine.cpp
# expect: the enum entry plus the single Rank() check -- no comparator special case.
rg -n "std::wstring initials|initials\s*\+=|initials.push_back" src/search/search_engine.cpp
# expect: zero hits -- the hot scan must not build an initials string.
```

## Handoff requirements

Record:

- 最終 `MatchRank` 順序與縮寫判準；
- 縮寫比對確為零配置的證據（實作片段）；
- 新增／更新的 test case 與實際結果；
- 5000 筆效能 check 的實測數字與 ceiling 比較；
- Release build、focused CTest、full CTest、`git diff --check` 輸出。

## 交接區

### 落地結果（2026-09-08）

- `MatchRank` 插入 `Initialism = 3`，`Substring`／`Subsequence`／`Alias`／`NoMatch`
  依序改為 4／5／6／7。比較器仍只用 `left.rank < right.rank`，無特例。
- `Rank()` 在 `WordPrefix` 迴圈與 `Substring` 檢查之間加入就地縮寫比對，實作與本
  文件 §Scope 2 的片段一致：單一 `for` 迴圈掃過名稱，只在 word-start 位置比對
  query 的下一個字元，零字串建構、零堆積配置。
- 縮寫採「query 是縮寫串的前綴」而非「完全相等」，因此 `vs` 也能命中
  `Visual Studio Code`；`Visual Studio` 由既有的「名稱較短者優先」tie-break 勝出，
  未新增排序規則。

### 驗證證據

- 與 NR-202 共用同一次 Release build（`build-nr202`），無新增 warning。
- `ctest --test-dir build-nr202 -R "app_filter|search" --output-on-failure`：
  `nimblerun_search_test`、`nimblerun_app_filter_test` 皆通過。
- `ctest --test-dir build-nr202 --output-on-failure`：33/33 全數通過，42.97s。
- `rg -n "initials.push_back" src/search/search_engine.cpp`：零命中。
  `rg -n "Initialism" src/search/search_engine.cpp`：enum 一處、`Rank()` 一處，
  比較器無特例。
- 5000 筆效能 check 實測：`NR-038 p95 727 us, max 855 us`；
  `NR-047 alias-fallback 196 us`。ceiling 為 **50 ms**，仍差約兩個數量級。
  新增的縮寫比對在最壞情況（query 第一個字元就與首字母不符）第一次比較即
  `break`，未改變該 check 的量級。
- `git diff --check`：無輸出。
