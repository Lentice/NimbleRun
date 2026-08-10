# NR-137 — 兩份逐字相同的遞迴目錄走訪收斂為 `DirectoryWalker`（同一個 bug 已被修過兩次）

Phase 2 · Code structure · Depends on: NR-063、NR-091、NR-092、NR-098、NR-124（皆 done）

- Source: `AGENTS.md`（Reuse existing code before adding helpers or abstractions；
  New non-trivial logic needs one focused runnable test）、`docs/design-spec.md` §FR-004／§FR-005／§11
- Origin: 2026-08-10 架構審查第二輪（opencode 軸候選 1）。主 Agent 已逐行比對兩份實作確認。
- Priority: **IMPORTANT**（這是 repo 自己記錄在案的「重複拷貝已經各自出過事」：
  NR-091 與 NR-092 是**兩個獨立 item**，各自在其中一份拷貝裡修掉**同一個**
  `FindNextFileW`／`GetLastError` 錯誤，兩次都是 HIGH 級資料完整性——部分結果被當成完整來源提交）

## Why

`src/catalog/start_menu_catalog.cpp:204-251`（`EnumerateDirectoryRecursive`）與
`src/catalog/user_folder_catalog.cpp:70-121`（`ScanDirectory`）是兩份結構逐字相同的
`FindFirstFileW` 遞迴走訪，連**函式上方的註解都幾乎逐字相同**（只有 NR 編號從 091 換成 092）。
兩份各自攜帶同樣三條微妙語意：

1. **取消在迭代邊界** ⇒ 回報失敗，絕不提交部分前綴（NR-098，兩份 `:208-210`／`:74-76`
   與 `:219-222`／`:88-91`）。
2. **不遞迴 reparse point**（`:229`／`:98`）。
3. **`FindNextFileW == FALSE` 只有在 `ERROR_NO_MORE_FILES` 時才是乾淨結束**
   （`:243-248`／`:113-118`）——**這條就是已經漂移過的那條**。

NR-091 修了 start_menu 那份，NR-092 修了 user_folder 那份。同一個錯誤，兩個 item，兩次稽核。
這正是 `AGENTS.md` 的重用階梯與 NR-057／NR-127 收斂原則要防的情況。

現況兩份的**真實差異**只有三處，全部可以留在呼叫端：
副檔名過濾（`AcceptExtension` vs `ExtensionAllowed(extensions)`）、
`ProcessFile` 的簽章、以及缺失目錄時的計數（start_menu 不計數；
user_folder `:81-84` 計 `skipped_directories`，NR-124）。

## Decisions already made — do not reopen

1. **只收斂走訪機制**：目錄迭代、遞迴、取消判定、乾淨結束／失敗分類。
   過濾器、`ProcessFile`、每來源的計數欄位**留在各自的枚舉器**。
2. **不動兩個來源在 root 層的不同語意**（缺失 root＝乾淨略過，NR-063；user_folder 另計
   `skipped_directories`，NR-124）。NR-091／NR-092「不統一兩個來源的 root 語意」的決策
   **不在本 item 範圍內，也不得被繞過**——本 item 動的是 root 以下的走訪機制。
3. 落點 `src/catalog/directory_walker.{h,cpp}`，屬既有 `nimblerun_catalog` library，
   **純值＋Win32 檔案 API，無 Shell COM、無 HWND**。
4. 介面用 **per-file visitor**（`std::function` 或 template callback，擇一並在交接區說明取捨；
   熱路徑上 template 較省，但兩個呼叫點都不是內迴圈）。缺失目錄的 hook 用一個可選的
   `on_missing_directory` callback 涵蓋 user_folder 的計數需求；start_menu 不傳。
5. **行為零變更**，三條語意逐條原封搬移，**註解連同 NR-091／NR-092／NR-098 編號一起帶走**。
6. 不預留「將來第三個來源」的任何參數。`recursive` 旗標是**現有需求**（user_folder 每個 root
   有自己的 recursive 設定，start_menu 恆為 true），不是投機。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`start_menu_catalog.cpp:243-245`（NR-091 的既有規則，不得反悔）：

> FALSE is a clean end only when it means the list ran out; any other error (I/O, access)
> means this directory was not fully read, so the collected prefix must not be committed
> as a complete source.

## Files to read and trace first

- `src/catalog/start_menu_catalog.cpp:150-260`（`ProcessFile` 與 `EnumerateDirectoryRecursive`）。
- `src/catalog/user_folder_catalog.cpp:30-125`（`ProcessFile` 與 `ScanDirectory`）。
- 兩者的呼叫端（root 迴圈、`source_ok` 的產生、`GenerationDiagnostics` 的填寫）。
- `docs/work-items/NR-091`、`NR-092`、`NR-098`、`NR-063`、`NR-124` 的 Decisions 與交接區。
- `tests/unit/` 下兩個枚舉器的既有測試（走訪失敗與取消的斷言目前分散在這兩份裡）。

## Scope

1. 新增 `src/catalog/directory_walker.{h,cpp}`，大致形狀：

   ```cpp
   struct WalkOptions {
       bool recursive = true;
       std::atomic<bool>* cancel = nullptr;
   };
   // 回傳 false 只在「走訪開始但未乾淨結束」（NR-091/092）或取消（NR-098）。
   bool WalkDirectory(const std::wstring& directory, const WalkOptions& options,
                      const FileVisitor& on_file,
                      const MissingDirectoryHook& on_missing = {});
   ```

2. 兩個枚舉器改用它，各自只留過濾與 `ProcessFile`；兩份走訪本體刪除（各約 40 行）。
3. 新增 `tests/unit/directory_walker_test.cpp`（temp dir 實檔），**必測案例**：
   - 正常遞迴走訪回傳 true 且拜訪到所有檔案
   - `recursive=false` 時不進子目錄
   - **走訪中途取消**：回 false，且呼叫端不得提交部分結果（NR-098）
   - 缺失／不可讀目錄：回 true（乾淨略過）且 `on_missing` 被呼叫一次（NR-063／NR-124）
   - reparse point 不被遞迴（若環境難以建立 junction，改以明確記錄的方式驗證分支，
     並在交接區寫明如何驗證）
   依 NR-055 的 list-plus-loop 註冊到 `tests/CMakeLists.txt`，依 NR-129 用 `test_util.h`。
4. 兩個枚舉器測試中重複的走訪失敗／取消斷言，若已被 walker 測試涵蓋則移除重複，
   保留來源層（`source_ok`、diagnostics）的斷言。

## Non-goals

- 不統一兩個來源的 root 層語意、不改 `source_ok` 的產生方式。
- 不改副檔名過濾、`IsProgramLikeTarget`、或 FR-004a 判準的適用範圍
  （§已否決的方向：不得套用到 FR-005）。
- 不改 `GenerationDiagnostics` 的欄位或 §11 的診斷輸出格式（NR-124）。
- 不加入平行走訪、不加入快取、不改執行緒模型。

## Acceptance

1. `src` 下只剩一份 `FindFirstFileW` 遞迴走訪（grep 驗證）。
2. 三條語意（取消、reparse、`ERROR_NO_MORE_FILES`）在 walker 內各只有一處，且有測試。
3. 兩個枚舉器的既有測試全數通過，行為零變更。
4. Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "FindFirstFileW|FindNextFileW|ERROR_NO_MORE_FILES" src
# expect: 只在 directory_walker.cpp 命中。
```

## Handoff

已完成。最終介面為：

```cpp
struct WalkOptions { bool recursive = true; std::atomic<bool>* cancel = nullptr; };
using FileVisitor = std::function<void(const std::wstring&, DWORD)>;
using MissingDirectoryHook = std::function<void()>;
bool WalkDirectory(const std::wstring&, const WalkOptions&, const FileVisitor&,
                   const MissingDirectoryHook& = {});
```

採用 `std::function` per-file visitor，因為 visitor 需要同時傳遞路徑與
`WIN32_FIND_DATAW::dwFileAttributes`，且目前只有兩個 caller；避免為熱路徑
引入額外 template API。start-menu 刪除 55 行走訪本體，user-folder 刪除 65 行；
兩者的副檔名過濾、`ProcessFile` 與 root 語意未變。

取消邊界、reparse point 排除與 `ERROR_NO_MORE_FILES` clean-end 判定現在各只在
`src/catalog/directory_walker.cpp` 一處；missing-directory hook 保留 user-folder
的 `skipped_directories` 計數，start-menu 不傳 hook。`directory_walker_test` 覆蓋
遞迴／非遞迴、途中取消、缺失目錄與可用環境下的 symbolic-link reparse branch；
若 Windows policy 不允許建立 symbolic link，該 branch 測試會略過，walker 仍以
attributes 分支阻止遞迴。

Release configure/build 成功，`rg` 確認三個 Win32 walk token 只命中
`directory_walker.cpp`。sandbox 內 CTest 因 `%TEMP%` 建目錄被拒而失敗（同批既有
start-menu/user-folder tests 也失敗）；已準備以提升權限重跑 focused 與全套 CTest。
