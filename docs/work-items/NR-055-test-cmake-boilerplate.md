# NR-055 — Collapse 22 copies of the same test target into one loop

Phase 3 · Status `ready` · Depends on: —

- Source: `AGENTS.md` §Engineering rules（最小可行改動、避免樣板）／§Validation
- Origin: 2026-08-06 ponytail audit（純刪除，無行為變更）

## Why

`tests/CMakeLists.txt` 有 456 行，其中 22 個 `add_executable` 各自帶著
**逐字相同的 25 行樣板**：同一組六個 `target_compile_definitions`、
同一個 `if(MSVC ...)/elseif(Clang)` 警告與連結選項分支、同一行 `add_test`。
每個 target 之間真正不同的只有兩件事：**執行檔名對應的來源檔，以及要連結
哪些 library。**

代價不是行數，是**漂移**。目前已經有一個實例可以佐證：
`nimblerun_search_test` 的 `-Wextra` 設定與其他 21 個相同，
所以 NR-047 才必須在 `search_engine_test.cpp` 檔頭塞一個 scoped
`#pragma clang diagnostic ignored`——因為「改一個 target 的警告選項」現在
意味著在 22 個地方裡挑對的那一個改。下一次要調整 C++ 標準、加一個
sanitizer、或換一個定義，就是 22 次一模一樣的編輯，而漏改一個不會有
任何錯誤，只會讓那一個測試在不同的組態下編譯。

這是本次稽核裡**唯一純刪除**的項目：約 380 行消失，行為零變更。

## Decisions already made — do not reopen

決定於撰寫本 item 時：

1. **用 `foreach` 加一個「名稱 → 來源 → 連結項」的清單，不寫 CMake
   function／macro。** 22 個同構 target 需要的是一個迴圈，不是一個
   `nimblerun_add_test()` DSL。function 會多一層間接，讓「這個 target 到底
   吃到哪些選項」變成要跨檔追蹤的問題。
2. **不改任何測試的名稱。** `ctest -R <name>` 是本 repo 每個 item 的
   Agent check 都在用的介面；改名會讓所有既有文件失效。
3. **不改任何編譯定義、警告選項或連結選項。** 本 item 的成功定義是
   「產出的建置行為位元級不變」。
4. **不新增、不移除、不合併任何測試執行檔。**
5. **不動 `tests/integration/` 與 `tests/release/` 的腳本註冊**（若它們在
   這個檔裡註冊，原樣保留在迴圈之外）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- No boilerplate, no scaffolding "for later".
- Keep changes scoped to the requested task.

`AGENTS.md §Validation`（本 item 的唯一驗收基準）：

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Files to read and trace first

- `tests/CMakeLists.txt` **整檔（456 行）**。逐個 `add_executable` 讀完，
  **不要假設 22 個區塊真的完全相同**。已知的變異點：
  - 連結項不同（`nimblerun_catalog` 6 個、`nimblerun_icons` 5 個、
    `nimblerun_settings` 2 個，其餘各 1 個；另有三個 target 連結**兩個**
    library：`nimblerun_pins nimblerun_panel_model`、
    `nimblerun_settings nimblerun_usage`、`nimblerun_ui nimblerun_panel_model`）。
  - **任何一個 target 若多了或少了某一行，那一行就是規格**，必須在新結構
    中保留。把你找到的每一處差異列進交接區。
- `CMakeLists.txt`（根）— `nimblerun_*` library target 的定義，
  以及 `add_subdirectory(tests)` 與 `enable_testing()` 的位置。**不改。**
- `cmake/llvm-mingw.cmake` — toolchain。**不改。**
- `docs/testing.md` — 若它列出測試名稱或建置步驟，本 item 之後要確認它
  仍然正確（列表本身由 NR-056 更新，本 item 只確認沒有因為你的改動而
  變得更錯）。

## Scope

### 1. 先建立「改動前」的基準

**這一步不可省略**，它是本 item 唯一能證明「行為零變更」的方法。

```powershell
cmake -S . -B build_before -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
ctest --test-dir build_before -N > before_tests.txt
```

`ctest -N` 只列出測試不執行。另外把每個 target 的編譯命令列存下來：

```powershell
cmake -S . -B build_before -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
Copy-Item build_before/compile_commands.json before_commands.json
```

若 Ninja 產生器不吐 `compile_commands.json`，退而求其次用
`build_before/build.ninja` 本身當基準。**兩份基準檔都放進 scratchpad，
不要 commit。**

### 2. 一份清單 + 一個迴圈

替換整個檔案的重複段落。形狀（以實際讀到的差異為準調整）：

```cmake
# NR-055: 22 test targets used to carry a verbatim copy of the same 25 lines of
# definitions, warning flags and link options. The only thing that actually
# varied was the source file and the libraries. A list plus one loop keeps the
# per-target facts visible in one place and makes a toolchain change a
# one-line edit instead of 22 identical ones -- the drift this prevents is
# silent: a missed target simply compiles under different flags with no error.
#
# Each entry: <test name>|<source file>|<libraries, space separated>
set(NIMBLERUN_TESTS
    "nimblerun_search_test|unit/search_engine_test.cpp|nimblerun_search"
    "nimblerun_hotkey_test|unit/hotkey_registration_test.cpp|nimblerun_hotkey"
    # ... 其餘 20 筆，順序與原檔一致
)

foreach(entry IN LISTS NIMBLERUN_TESTS)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 test_name)
    list(GET parts 1 test_source)
    list(GET parts 2 test_libs)
    string(REPLACE " " ";" test_libs "${test_libs}")

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ${test_libs})
    target_compile_definitions(${test_name}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
    )
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_compile_options(${test_name} PRIVATE /W4 /permissive- /EHsc)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(${test_name} PRIVATE -municode -static)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
```

要求：

- **六個定義、兩個分支、所有選項逐字照抄原檔**，不要順手「改善」。
- **清單順序與原檔的 `add_executable` 順序一致**，讓 diff 可讀。
- 任何**不符合這個模式的 target**（多一行、少一行、額外的
  `target_include_directories` 等）**留在迴圈之外，保持原樣**，
  並在它上方加一行註解說明為什麼它是例外。寧可有兩個例外，
  也不要把例外硬塞進迴圈的參數裡。
- 分隔符用 `|`：路徑與 target 名都不含它。若某個項目真的含 `|`，
  換一個不會出現的字元並在交接區說明。

### 3. 逐位元比對

```powershell
cmake -S . -B build_after -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ctest --test-dir build_after -N > after_tests.txt
Compare-Object (Get-Content before_tests.txt) (Get-Content after_tests.txt)
```

**`Compare-Object` 必須沒有輸出**（測試名稱與數量完全相同）。

編譯命令列同樣比對；`compile_commands.json` 的欄位順序或路徑前綴
（`build_before` vs `build_after`）會造成雜訊，比對時把建置目錄名正規化掉。
**目標是：每個測試的 `command` 字串在正規化後完全相同。**
若有任何差異，那就是行為變更，**必須修到沒有差異為止**，
或在交接區逐條解釋為什麼該差異是無害的。

比對完刪掉 `build_before` / `build_after` 與兩份基準檔。

## Performance

CMake configure 時間可能略微改變（迴圈 vs 展開），數十毫秒等級，
與建置本身無關。**不需要量測。**

## How this stays maintainable

**同構的東西寫成清單，異構的東西寫成例外。** 22 個測試 target 只有兩個
自由度（來源、連結項），把它們列成 22 行資料之後，「有哪些測試」變成
一眼看得完的一張表，而「測試怎麼建」只有一份定義。
**未來加一個測試是加一行**，不是複製 25 行——而複製 25 行的人有 22 個
範本可挑，其中任何一個若已經漂移，漂移就會被繼續複製。

**例外要顯眼。** §2 明文要求不合模式的 target 留在迴圈外並註明原因。
把例外塞進參數（多一個「額外選項」欄位）會讓清單開始長成一個
迷你建置系統；讓它待在外面，數量就會保持在人看得住的範圍。

**不做 function/macro** 的理由同上：迴圈就在原地，讀清單的人不必跳去
別的檔案才知道 `-Wextra` 從哪來。

## Non-goals

- **改任何編譯／連結選項、C++ 標準、警告等級。** 包含「順手」把
  `-Wpedantic` 拿掉或加上 `-Werror`。
- **重新命名任何測試。** Decisions §2。
- **合併、拆分、新增或刪除測試執行檔。**
- **把 `Expect()` 抽成共用的測試支援程式庫。** 那需要一個新的
  library target 與 include 路徑，是與本 item 相反方向的改動；
  22 份各 8 行的 helper 比一個測試框架便宜。若哪天真要做，那是新的 item。
- **改根 `CMakeLists.txt`、toolchain 檔或任何 `src/` 檔案。**
- **改 `tests/` 底下任何 `.cpp` 或 `.ps1`。**
- **移除 NR-047 在 `search_engine_test.cpp` 留下的 `#pragma clang
  diagnostic`。** 它是原始碼層的事；本 item 不碰原始碼。
  （NR-048 會動那個檔，但也明文不碰那個 pragma。）

## Interaction with other open items

- **NR-048** 改 `tests/unit/search_engine_test.cpp` 的內容，不改 CMake；
  本 item 改 CMake，不改任何 `.cpp`。**零重疊，可並行。**
- **NR-050 / NR-052 / NR-053 / NR-054** 都可能在既有測試檔內新增案例，
  不新增執行檔，因此不碰本 item 的清單。**若其中任何一個 item 真的新增了
  測試執行檔**，落地順序就有意義：先落地本 item，那個 item 只需在清單加
  一行；反之則要在 22 份樣板裡再複製一份。**建議先落地本 item。**

## Acceptance

Automated：

1. `tests/CMakeLists.txt` 的行數從 456 降到約 80 以下。
2. `ctest -N` 的輸出在改動前後**完全相同**（§3 的 `Compare-Object` 無輸出）。
3. 完整 Release 建置成功、**無新增警告**、`ctest` 全綠（目前 23 項）。
4. `git diff --stat` 顯示只有 `tests/CMakeLists.txt` 被改動。

Manual：無。本 item 沒有使用者可見的行為變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -N
```

```powershell
# 樣板真的消失了：
(Get-Content tests/CMakeLists.txt | Measure-Object -Line).Lines
# expect: < 80

(Select-String -Path tests/CMakeLists.txt -Pattern 'target_compile_definitions').Count
# expect: 1（加上任何 §2 明文保留的例外）

(Select-String -Path tests/CMakeLists.txt -Pattern 'add_executable').Count
# expect: 1（在迴圈內；加上任何例外）

(Select-String -Path tests/CMakeLists.txt -Pattern 'add_test').Count
# expect: 1（在迴圈內；加上任何例外）

# 22 個測試都還在清單裡：
(Select-String -Path tests/CMakeLists.txt -Pattern '_test\|').Count
# expect: 22（以你讀到的實際數量為準）

# 選項一字未改：
Select-String -Path tests/CMakeLists.txt -Pattern '/W4|/permissive-|/EHsc|-Wall|-Wextra|-Wpedantic|-municode|-static'
# expect: 與原檔相同的選項集合，各出現一次

# 只動了一個檔：
git diff --name-only
# expect: tests/CMakeLists.txt
```

## 交接區

（實作者填寫：改動前後的行數、`ctest -N` 比對結果、編譯命令列比對的方法與
結果、22 個區塊中發現的所有差異、留在迴圈外的例外及理由、建置與 CTest
結果、sanity greps、偏差、未完成事項。）
