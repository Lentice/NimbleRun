# NR-104 — Testing guide 與 release evidence 必須反映目前 24 項 CTest

Phase 5 · Depends on: NR-017, NR-056, NR-089

- Source: `docs/testing.md`、`docs/release-evidence.md`、`tests/CMakeLists.txt`
- Origin: 2026-08-09 全 repo 稽核（registered CTest count 與 release documents 對照）
- Priority: MEDIUM（錯誤的 release evidence 會讓驗收與回歸範圍失真）

## Why

目前 `tests/CMakeLists.txt` 已註冊 24 個 CTest，包含 NR-089 的
`nimblerun_hotkey_capture_test`；Release build 的 `ctest -N` 也列出 24 項。可是：

- `docs/testing.md` 仍寫「currently 23 checks」；
- `docs/release-evidence.md` 是舊的 23-test、2026-08-07、舊 commit evidence；
- release evidence script 的輸出契約需要重新確認，避免下一次報告再次落後。

這不是產品 code failure，但會讓冷讀者以為 hotkey capture 沒被測，或把 stale release
report 當成目前分支的證據。

## Decisions already made — do not reopen

1. CTest registration 是唯一 count authority；以 Release build 的 `ctest -N` 為準，不手寫
   測試數字到多處。
2. release evidence 必須標示產生時間、commit、toolchain、CTest count；不可保留舊 commit
   的報告冒充目前結果。
3. 本 item 只修 test/release documentation 與產生流程，不改產品 code 或刪測試。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Before release, validate the acceptance criteria and resource budgets in `docs/testing.md`
> and `docs/performance-baseline.md` with a Release x64 build.

> Keep changes scoped to the requested task and update the relevant documentation when behavior changes.

`docs/work-items.md`：

> 必須保持既有 build／CTest 可用；不得用關閉測試來取得綠燈。

## Files to read and trace first

- `tests/CMakeLists.txt` — all `add_test` registrations and conditional lifecycle test。
- `tests/release/release_evidence.ps1` — report generation and count extraction。
- `docs/testing.md` — stated suite count and acceptance checklist。
- `docs/release-evidence.md` — generated evidence artifact。
- `docs/work-items/NR-017-release-evidence.md`、NR-056 — existing evidence/document drift decisions。

## Scope

1. 讓 testing guide 的 suite count／test names 與 `ctest -N` 一致。
2. 重新產生或修正 release evidence pipeline，使報告使用當次 Release build、當次 commit
   與當次 CTest count；不要手動保留舊結果。
3. 加一個最小 sanity check，當 registered count 與 report count 不一致時使 evidence
   產生失敗或明確標記 stale。

## Non-goals

- 不新增、刪除、重命名或關閉任何 CTest。
- 不把本機 sandbox 的 temp/GUI 限制寫成產品 failure；若環境限制測試，報告必須如實標明。
- 不修改產品 UI、catalog、storage 或 runtime dependency。

## Acceptance

Automated：

1. `ctest -N` 的 count 與 `docs/testing.md`、新產生的 release evidence 一致（目前基線為
   24，若測試註冊改變則由命令重新決定）。
2. Release evidence 的 commit、timestamp、build type、toolchain 與 CTest result 都來自
   當次執行；不再引用 2026-08-07 的舊 count/commit。
3. Build／CTest 仍依照 repository validation command 執行，沒有以關閉 test 取得綠燈。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path docs/testing.md,docs/release-evidence.md -Pattern '23|24|CTest count|Total Tests'
git diff --name-only
# expect: docs、release script 與必要的 evidence metadata；無 src/ 變更。
```

## 交接區

（2026-08-09 由實作者填寫）

- **當次 CTest count：25**（`ctest --test-dir build -N` → `Total Tests: 25`）。注意：
  ticket 撰寫時是 24（NR-089 hotkey capture 之後），但實作當下 `tests/CMakeLists.txt`
  已多出 NR-101 的 `nimblerun_catalog_watcher_test`，故以 live `ctest -N` 為準（decision 1
  的 count authority），docs 與 evidence 都記為 25。
- **Evidence 產生命令**：`pwsh -NoProfile -File tests/release/release_evidence.ps1`（repo root）。
- **Evidence commit**：`5d14c07d3010228dc59da88e54f9a21704d1aa25`（當次 HEAD，
  `git rev-parse HEAD`）。
- **Evidence timestamp**：Generated `2026-08-09 12:12:51 +08:00`（UTC `2026-08-09T12:12:51Z`）。
- **Build／CTest 結果**：`cmake --build build` = no work to do（無 src/ 變更）；release
  evidence script 內建的 full suite = 25/25 passed，`100% tests passed out of 25`；
  script exit code = 0（`All gates passed.`）。Idle thread count 17、soak 3/3 OK。
- **Sanity check 設計**：script 新增兩段（均在 `tests/release/release_evidence.ps1`）——
  1) header 前把 `ctest -N` 的 `Total Tests:` 行抓進 `$registeredTests`（也維持 header
  的 `- CTest count:` 原格式）；2) full suite 執行後從其輸出以 regex
  `out of\s+(\d+)(?:\s*tests?)?` 解析 `$executedTests`（相容本 ctest 4.4.2 的
  `100% tests passed out of 25` 與 `25 out of 25 tests` 兩種格式）。若 executed 無法解析
  或與 registered 不符 → `$stale` 為真：gate table 加一行
  `| CTest registration vs executed | registered == executed | registered N vs executed M | STALE |`、
  `$gateFailure` 設真（exit 1）、結尾印出明確 STALE 訊息。已用四組樣本驗證邏輯
  （match / mismatch / unparsed / alt format）。
- **環境限制**：實作過程一次 `ctest` 管線（pipe 到 `Select-String`）在 300 秒 timeout
  後被中斷，判斷為環境 flake（無殘留 `NimbleRun.exe` 進程，重跑即 25/25 通過）；
  不視為產品 failure。其餘無。
- **偏差**：無功能偏差。唯一與 ticket 文字不同處是 count 為 25 而非 24，屬上文所述的
  上游新增測試（NR-101），非本 item 變更。
- **未完成事項**：無。

