# NR-174 — DiagnosticLog::Write 宣稱 never throws 但無例外邊界，錯誤記錄路徑可終止 process

Phase 5 · Diagnostics · Depends on: —

- Source: `src/diagnostics/diagnostic_log.h:27`（公開契約「never throws」）、
  `docs/design-spec.md` §11（記錄失敗不得影響呼叫端）
- Origin: 2026-08-11 第十六次稽核第 4 輪（codex backend，MINOR）。主 Agent
  已重讀 `diagnostic_log.cpp:33-88`、`diagnostic_log.h` 驗證。
- Priority: **LOW**——OOM 才觸發，但「錯誤記錄路徑再拋例外」是 NR-076/097/109
  系列修掉的反模式，且 `Write()` 的公開契約明文 never throws——契約與實作
  直接矛盾。

## Why

`diagnostic_log.h`（`src/diagnostics/diagnostic_log.h:27` 一帶）註明
`Write` 失敗「never throws」、best-effort；但 `DiagnosticLog::Write`
（`diagnostic_log.cpp:33-88`）本體無任何例外邊界：

- `std::lock_guard<std::mutex>`（:39）——mutex lock 可拋 `std::system_error`；
- `std::wstring parent = directory_`（:45）、`JoinPath`（:57-58）、
  `std::wstring line`／`line.reserve`（:77-78）、`Sanitize`（:79-81）、
  `EncodeUtf8`（:84）——全為可拋 `std::bad_alloc` 的配置路徑。

呼叫端多為未包覆的 UI／WndProc 錯誤路徑（例如 NR-171 剛加的 resize-failure
診斷、launch-failure、watcher 錯誤），低記憶體時記錄動作本身可讓例外穿越
Win32 callback 造成 `std::terminate`——原始錯誤的記錄動作反而殺死 process。

## Decisions already made — do not reopen

1. **`Write` 標 `noexcept`，整個本體包最外層 `try/catch (...)`**（catch 內
   return，無任何動作）——兌現「never throws」契約，保留 best-effort 語意。
2. 不加診斷事件（在 log 寫失敗路徑記錄失敗無意義）。
3. 不新增測試 seam（OOM 不可注入，依 NR-076/167 先例）；以既有
   `diagnostic_log_test`（輪替、併發、內容）回歸覆蓋。
4. 不改 `DiagnosticLog` 其他方法、不改呼叫端、不改簽章。

## Binding constraints — quoted, do not go looking for them

`src/diagnostics/diagnostic_log.h`（既有契約，本 item 兌現它）：

> never throws（best-effort：記錄失敗不得影響呼叫端）。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

（本 item 是複製既有 catch 形狀的邊界補洞，非新邏輯；不新增測試 seam。）

## Files to read and trace first

- `src/diagnostics/diagnostic_log.cpp:33-88`（Write 本體）。
- `src/diagnostics/diagnostic_log.h`（契約註解）。
- `tests/unit/diagnostic_log_test.cpp`（既有輪替／併發測試）。

## Scope

1. `Write` 標 `noexcept`，本體包 `try/catch (...)`。
2. 既有測試全綠（行為不變）。

## Non-goals

- 不新增診斷、不新增 UI 字串、不新增設定。
- 不處理「呼叫端已包 try/catch 但記錄失敗」的雙重邊緣（catch 是空的即可）。

## Acceptance

1. `Write` 為 `noexcept`（code review 斷言）；`diagnostic_log_test` 全綠。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "diagnostic" --output-on-failure
```

```powershell
rg -n "void DiagnosticLog::Write|noexcept|catch" src/diagnostics/diagnostic_log.cpp
# expect: Write noexcept；最外層 catch 存在
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置與 build／CTest 結果。

## 交接區

- 改動檔案：`src/diagnostics/diagnostic_log.cpp`、`src/diagnostics/diagnostic_log.h`（各 1 處）。
- 改動內容：`Write` 宣告與定義標 `noexcept`（`diagnostic_log.h:29`、
  `diagnostic_log.cpp:33`）；整個本體包在最外層 `try/catch (...)`，catch 內
  僅 `return`，無任何動作（`diagnostic_log.cpp:38,93-95`）。無其他方法、呼叫端、
  簽章、診斷事件、測試 seam 變動；行為不變。
- Build／CTest（Release x64, LLVM-MinGW + Ninja）：設定與 build 成功；
  全數 31/31 tests passed（數量與先前一致）；`-R "diagnostic"` 1/1 passed。
- Warning：本改動零新增 warning（build 出現的唯一 warning 為
  `main.cpp:1410` unused variable `target_size`，git stash 驗證為改動前即存在）。
- Sanity grep：
  ```
  33:void DiagnosticLog::Write(std::wstring_view stage, std::wstring_view detail) noexcept {
  36:    // sits inside an empty catch-all: a logging failure must never abort the
  93:    } catch (...) {
  ```
- 提交：`da9c8a0`（NR-174: make DiagnosticLog::Write noexcept and catch-all）。
- 偏差：無。
