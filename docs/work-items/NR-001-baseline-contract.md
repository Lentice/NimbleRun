# NR-001 — Baseline build and Agent check contract

- Status: `ready`
- Phase: 0
- Depends on: none
- Source: `AGENTS.md` Validation; `docs/testing.md`; `docs/roadmap.md` Phase 0

## Goal

讓後續低階 Agent 有一個可重複的 Release build、CTest 與 process lifecycle 檢查基線。

## Scope

- 驗證 LLVM-MinGW、CMake、Ninja 的 Release x64 命令可用。
- 保留目前 Phase 0 probe 行為與既有 search unit test。
- 若測試 target 或命令缺漏，補上最小的 CTest wiring。

## Non-goals

- 不重寫 probe UI。
- 不新增產品功能或第三方測試框架。

## Acceptance

- Clean Release configure、build、CTest 可在同一組命令完成。
- 既有 search test 持續通過。
- 任何後續 item 都能引用本頁的命令作回歸檢查。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

交接時記錄工具版本、命令、exit code；不要附加人工畫面驗證。

## 交接區

- Start: 2026-08-04
- Subagent scope: 驗證 LLVM-MinGW/CMake/Ninja 的 Release x64 configure/build/CTest 一組命令可完成；保留 Phase 0 probe 與既有 search unit test；僅在 test target 或命令缺漏時補最小 CTest wiring。
- Result: done
- Agent: general subagent (ses_033ccf0dfffeoTwiwFT8EkpmTM)
- 修改檔案：`AGENTS.md`、`docs/testing.md`、本文件 — 將 `-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake` 改為 `-D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake"`。原因：PowerShell 對未加引號的 `-D...=...` 參數會在首個 `.` 處切分（`llvm-mingw.cmake` → `llvm-mingw` + `.cmake`），導致 toolchain file 找不到；quote 後可正確傳遞。無其他 code 變更。
- 工具版本：cmake 4.4.2、ninja 1.13.2、clang 22.1.8（LLVM-MinGW，target x86_64-w64-windows-gnu）。
- Agent checks（clean Release，2026-08-04）：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` → exit 0
  - `cmake --build build` → exit 0
  - `ctest --test-dir build --output-on-failure` → exit 0，1/1 passed（`nimblerun_search_test`）
- 證據：`build\NimbleRun.exe`、`build\tests\nimblerun_search_test.exe`；CTest 結果於 `build\Testing\`。
- 備註：本機 `cmake` 未在 PATH（`C:\Program Files\CMake\bin`），AGENTS.md 已要求 shell 需含 LLVM-MinGW/CMake/Ninja，屬環境前置條件而非專案問題。
