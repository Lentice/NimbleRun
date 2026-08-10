# NR-144 — settings_store 的 helper 收斂：IsLocalAbsolutePath≡IsDisplayablePath、ParseInt 併入 ParseInt64

Phase 1 · Code structure · Depends on: —（NR-127 收斂運動的漏網兩件，同檔兩處）

- Source: `AGENTS.md`（Prefer the smallest working change. Reuse existing code before adding
  helpers or abstractions）、NR-127（路徑／解析 helper 收斂——本 item 是其殘餘）、
  NR-057（四份檔頭解析收斂的同樣動機：漂移**已經發生**）
- Origin: 2026-08-10 第十四次全 repo 稽核（ponytail 軸，MEDIUM）。主 Agent 已對照兩檔驗證。
- Priority: **MEDIUM**——同一判定兩份拷貝且**已漂移一次**（Trim 差異）；第四份手寫整數解析。

## Why

**（a）`IsLocalAbsolutePath` vs `IsDisplayablePath`**：

`src/settings/settings_store.cpp:88-101` 與 `src/catalog/app_filter.cpp:26-35` 是同一判定
「drive-letter 絕對路徑」的兩份拷貝（`X:` + 字母 + `\`／`/`，逐行等價）。唯一差異：
前者先 `Trim`。`settings_store.cpp:1` 已 include `app_filter.h`（同 TU 可呼叫）。
漂移**已經發生**：`" C:\tools"` 在 settings_store 判定為絕對路徑、在 app_filter 判定為
非絕對路徑。NR-127 收斂了 `FileName`／`FileStem`／`Extension`，這對漏網了。

**（b）`ParseInt`**：

`src/settings/settings_store.cpp:25-38` 手寫 `wcstol`＋`errno` 迴圈；同一 storage 層已有
共用的 `ParseInt64`（`src/storage/atomic_text_file.h:214-227`）。`settings_dialog.cpp:310-321`
已示範正確的收斂形狀（`ParseInt64`＋int 範圍守門）。`ParseInt` 是收斂後的第四份殘留。

## Decisions already made — do not reopen

1. `IsLocalAbsolutePath(v)` 改為 `return IsDisplayablePath(Trim(v));`（一行；`Trim` 保留——
   呼叫端 `:132` 與 `Load` 的 `:227` 依賴已 Trim 語意）。
2. `ParseInt` 改為 `ParseInt64` 內層 + `INT_MIN/INT_MAX` 範圍守門（行為等價：原實作
   `wcstol` 超 int 範圍設 `ERANGE` 回 false，`ParseInt64` 的 int64 不設 ERANGE 但超出
   int 時由守門擋下）。若實作時發現既有呼叫端傳入值永在 int 範圍，守門仍必須保留
   （那是語意的一部分，不是死守）。
3. 兩處都是**行為零變更**的收斂；`settings_store_test` 即回歸網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/settings/settings_store.cpp`：`:25-38`（`ParseInt`）、`:88-101`（`IsLocalAbsolutePath`）、
  `:1`（include `app_filter.h` 已存在）、`:132` 與 `:227`（呼叫端）。
- `src/catalog/app_filter.{h,cpp}`：`:26-35`（`IsDisplayablePath`）。
- `src/storage/atomic_text_file.h`：`:214-227`（`ParseInt64`）。
- `src/app_host/settings_dialog.cpp`：`:310-321`（收斂形狀範本）。
- `tests/unit/settings_store_test.cpp`（回歸網）。

## Scope

1. `IsLocalAbsolutePath` 收斂為一行；刪除其迴圈本體。註解引用本 item。
2. `ParseInt` 收斂為 `ParseInt64`＋範圍守門；若 `ParseInt` 變成一行轉發且無其他呼叫端
   需求，保留函式（`settings_store.cpp` 內部多處使用）。
3. 驗證：`git diff` 只含上述兩處；全部測試通過。

## Non-goals

- 不移動 `IsDisplayablePath` 的位置、不改其語意（Trim 由呼叫端負責是既有契約）。
- 不把 `settings_store` 的 `Trim` 行為併進 `IsDisplayablePath`（其他呼叫端未 Trim）。
- 不順手收斂其他檔案的同類殘留（若有，列回候選）。

## Acceptance

1. `settings_store.cpp` grep 不到 `wcstol`。
2. `IsLocalAbsolutePath` 內 grep 不到 drive-letter 判定迴圈（只剩 `IsDisplayablePath` 呼叫）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings_store" --output-on-failure
```

```powershell
rg -n "wcstol|wcstoll" src/settings/settings_store.cpp
# expect: 零命中
```

## Handoff

2026-08-10 完成（commit 見 `docs/work-items.md` 歷史）。實作如決策所述，行為零變更：

- `IsLocalAbsolutePath` 收斂為 `return IsDisplayablePath(Trim(value));`（含 NR-144 註解；
  Trim 保留，呼叫端 `UserDataDirFromLocalAppData` 與 `Load` 依賴其語意）。
- `ParseInt` 收斂為 `ParseInt64`＋`std::numeric_limits<int>` 範圍守門（比照
  `settings_dialog.cpp` `ParseCountText` 形狀）；`<cerrno>`／`<cstdlib>` include 移除，
  新增 `<cstdint>`／`<limits>`。

**與 item 預期的偏差（build 層級，item 未涵蓋）**：item 驗證了 include 層級
（`app_filter.h` 已 include），但 `IsDisplayablePath` 的定義在 `nimblerun_catalog`
靜態庫，`nimblerun_settings` 原本不連結它——收斂後三個只連結 settings 的測試
（settings_ui／startup_option／hotkey_capture）產生 undefined symbol。修法：
`CMakeLists.txt` 的 `nimblerun_settings` 加 `PRIVATE nimblerun_catalog`（與既有的
`nimblerun_catalog PUBLIC nimblerun_settings` 形成靜態庫循環，CMake 對 static
library cycle 會重複展開，lld 正常解析；NimbleRun.exe 已自帶 catalog 不受影響）。
若未來有人想拆掉這個循環：把 `IsDisplayablePath` 移到更低層的共用庫（本 item
Non-goals 不允許，留作候選）。

- 驗證：Release Ninja llvm-mingw 建置零新增 warning；`ctest` 31/31 全綠（數量不變）；
  `ctest -R settings` 2/2（settings_test、settings_ui_test）全綠；
  `rg -n "wcstol|wcstoll" src/settings/settings_store.cpp` 零命中（註解用
  "wide-string-to-long" 措辭以免命中檢查）。
- 未加新測試：`settings_store_test` 的 recent_count／catalog_root 案例即為回歸網，
  未發現未涵蓋邊界。

完成後在文件底部補齊本 item 的 Handoff 交接備註。
