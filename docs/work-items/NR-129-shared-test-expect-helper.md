# NR-129 — 25 份逐字相同的測試 `Expect` helper 收斂成一個共用標頭

Phase 3 · Test hygiene · Depends on: NR-055（done）

- Source: `AGENTS.md`（reuse existing code）；NR-055 是同一模式在 CMake 層的先例
- Origin: 2026-08-10 第十三次全 repo 稽核（ponytail 軸）；主 Agent 已比對 25 份拷貝
- Priority: **LOW**（純測試整理；沒有行為風險，但有同 NR-055 的樣板漂移問題）

## Why

`tests/unit/*.cpp` 共 25 個檔案各自拷貝同一份 6 行 `Expect(bool, const char*)`（fprintf
`FAIL: <message>` 到 stderr＋exit(1)，配 `g_failures` 累計與 `wmain` 回傳值）。NR-055 已把
CMake 層的 22 份 target 樣板收斂成迴圈；這是同一個「樣板複製」模式在測試碼層面的殘留。
漂移風險：下一個測試檔複製舊樣板後，若 repo 日後改失敗機制（如輸出格式），25 處要同步。

## Decisions already made — do not reopen

1. 新增 **`tests/unit/test_util.h`** 一份 `Expect`（與現行逐字相同的語意：`fprintf(stderr,
   "FAIL: %s\n")`＋`exit(1)`，或沿用各檔現行寫法），25 檔 `#include "test_util.h"` 並刪私有副本。
2. **不抽 `Expect` 以外的 assert 庫**（無 framework、無 fixture、無 per-function suite——
   repo 既有測試風格維持）；不動 `g_failures` 的檔案範圍計數器（若各檔現行機制不同，
   以多數形狀為準，逐一確認）。
3. 純搬移，行為零變更：測試名稱、斷言順序、`wmain` 結構都不動；`tests/CMakeLists.txt` 不需改
   （`#include "test_util.h"` 是同目錄 quoted include，不需新增 include path——實作時確認工具鏈
   行為）。
4. 不重開 NR-048 的「改測試檔而非拔 NDEBUG」決策；本 item 與斷言機制無關。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> 不得用關閉測試來取得綠燈。

## Files to read and trace first

- `tests/unit/*.cpp`（25 個檔案的 `Expect` 定義與 include 區）。
- `docs/work-items/NR-055-test-cmake-boilerplate.md` — CMake 收斂先例。

## Scope

1. 建 `tests/unit/test_util.h`，內含一份與現行逐字相同的 `Expect`。
2. 25 個測試檔刪私有 `Expect` 定義、加 `#include "test_util.h"`；逐檔確認沒有其他
   file-scope 助手與新 header 衝突。
3. Release build 後 ctest 26/26 全綠；`ctest -N` 名稱與順序不變。

## Non-goals

- 不新增測試 framework 依賴；不改任何測試的斷言語意與失敗訊息格式。
- 不動 `src/`；不動 `tests/CMakeLists.txt`（除非 include 路徑證明需要，實作時記錄）。

## Acceptance

1. `Expect` 在 repo 只剩 `test_util.h` 一份定義（grep）。
2. 25 檔編譯通過、完整 CTest 26/26 與 NR-055 的既有 `ctest -N` 比對不變。
3. Release build 無新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest -N
```

```powershell
rg -n "void Expect\(|int Expect\(|static.*Expect" tests
# expect: 只剩 test_util.h 一份。
git diff --name-only
```

## Handoff

實作者需記錄共用 `Expect` 的最終形狀（對齊多數檔的現行寫法）、是否有測試檔需要例外處理、
include 路徑結論、grep 與 CTest 證據。

### 交接區（2026-08-10，實作完成）

**共用 `Expect` 最終形狀**

`tests/unit/test_util.h`（新增）內容為 `inline void Expect(bool condition, const char* message)`，
body 與 24 個多數檔的現行寫法逐字相同（`std::fprintf(stderr, "FAILED: %s\n", message)`＋
`std::exit(1)`）。加了 `inline` 與 `#pragma once`、`#include <cstdio>/<cstdlib>`，使 header 可被
25 個 TU 各自 include 而不違反 ODR。註解保留 NR-048 的動機（Release build 下 NDEBUG 會拔掉
assert macro，Expect 是 plain function，故仍會執行）。

**需要例外處理的檔案（1 個）**

`tests/unit/search_engine_test.cpp` 是唯一形狀不同的檔案：原本的私有 Expect 用
`std::fprintf(stderr, "FAIL: %s\n", message)`＋`++g_failures` 計數、wmain 結尾回傳
`g_failures == 0 ? 0 : 1`，並非 `exit(1)`。依 item 決策「以多數形狀為準」，已刪其私有 Expect、
改用共用 header；`int g_failures` 計數器與 wmain 的 `return g_failures == 0 ? 0 : 1;` 結構保留
不動（item 決策：不動 `g_failures` 計數器、不動 wmain 結構）。行為收斂為：任一失敗即
`exit(1)`（多數形狀），CTest 判定不變。其餘 24 檔的私有 Expect 形狀與多數一致、無其他
file-scope 助手與新 header 衝突。

**include 路徑結論**

`tests/CMakeLists.txt` 不需修改。`#include "test_util.h"` 是同目錄 quoted include，clang 對
`#include "..."` 會先搜尋 include 來源檔所在目錄（tests/unit），因此不需新增 include path。
已確認未動 `tests/CMakeLists.txt`。

**grep 結果**

```
rg -n "void Expect\(|int Expect\(|static.*Expect" tests
→ 僅 tests\unit\test_util.h:13: inline void Expect(bool condition, const char* message) {
```

**build／CTest 證據**

- Configure：`cmake -S . -B build-wi-nr129 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`（Clang 22.1.8，成功）。
- Build：`cmake --build build-wi-nr129`，94/94 targets、exit 0、無新增 warning／error。
- CTest：`ctest --test-dir build-wi-nr129 --output-on-failure` → **26/26 全綠**（含
  `nimblerun_lifecycle_check`），總時間 72.17 s。
- `ctest --test-dir build-wi-nr129 -N` → 26 項，測試名稱與順序與 NR-055 既有清單一致
  （`nimblerun_search_test` #1 … `nimblerun_message_loop_test` #26）。

**改動範圍**

新增 `tests/unit/test_util.h`；編輯 `tests/unit/` 下 25 個 `.cpp`（加 include、刪私有 Expect）。
未動 `src/`、`tests/CMakeLists.txt`、`docs/work-items.md` 或其他 .md；未執行任何 git 命令。
