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
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

交接時記錄工具版本、命令、exit code；不要附加人工畫面驗證。
