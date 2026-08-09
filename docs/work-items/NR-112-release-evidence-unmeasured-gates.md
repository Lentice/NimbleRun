# NR-112 — Release evidence 不得把未量測的 blocking gates 報成 PASSED

Phase 5 · Release validation

- Source: `docs/design-spec.md` §NFR-001、§12、§13、§15 Phase 5；`docs/testing.md` Release checklist
- Origin: 2026-08-09 全 repo 稽核；比對 `docs/performance-baseline.md`、`tests/release/release_evidence.ps1` 與 generated evidence
- Priority: HIGH（目前 evidence runner 可在多個 blocking metric 為 `Not measured` 時 exit 0 並寫 `PASSED`）

## Why

`docs/performance-baseline.md` 仍有未量測項目：idle CPU、visible panel with 20 icons、cold
start、warm hotkey p95、`icons.cache` file size，以及 app-owned thread census。它們在
design spec NFR-001 表內都有 target／blocking threshold，但
`tests/release/release_evidence.ps1` 目前只跑 build、CTest、process smoke、一次 idle
process-total sample 與短 soak；blocking gate 只放 CTest count/staleness，並把其餘項目寫成
「Known issues (below target, not blocking)」。因此目前的 `docs/release-evidence.md`
即使沒有任何 blocking metric 的實際測量，也能得到 `- **PASSED**`。

這不是要求把所有效能數字硬猜出來，而是 release status 必須對 unknown fail closed：
「沒有量到」不能等同「沒有超過門檻」。NR-017／NR-056 的「目前 harness 只 gate 能量的
項目」決策需要由新的 release-source-of-truth evidence contract 覆寫；不改 runtime product
behavior。

## Decisions already made — do not reopen

1. 所有 NFR-001 blocking rows 必須在 evidence 中逐項呈現 `measured / not measured / pass /
   fail`；unknown 不得標成 pass。
2. 本 item 可先讓 runner/report 回傳 `INCOMPLETE` 或 non-zero，直到 measurement profile
   完整；不得用 executable size、單次估算或 process total 取代規格要求的 measurement。
3. CTest registered count 仍以 `ctest -N` 為 authority；目前實際 suite 是 25 項，NR-104
   的歷史文件不可作為新的測量證據。
4. 不新增 telemetry、network、第三方 benchmark service 或人工修改 generated report。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001：

> 以下是 MVP 的工程目標，不是未量測的保證。所有數字均以 x64 Release、未附加 Debugger、完成初次索引並待機 60 秒後量測。

> 阻擋發布門檻

`docs/testing.md` MVP acceptance checklist：

> Release measurements pass the blocking thresholds in `docs/performance-baseline.md`.

`AGENTS.md`：

> Before release, validate the acceptance criteria and resource budgets in `docs/testing.md` and `docs/performance-baseline.md` with a Release x64 build.

> Do not replace a failed measurement with a process-size estimate or executable file size.

## Files to read and trace first

- `tests/release/release_evidence.ps1` — `$gate` construction、`$gateFailure`、measurement
  coverage、generated result and exit code。
- `docs/performance-baseline.md` — every target and blocking threshold row, including `Not measured`。
- `docs/release-evidence.md` — generated report format and current 25-test evidence。
- `docs/testing.md` — release command、MVP acceptance checklist、required environments/catalog sizes。
- `docs/work-items/NR-017-release-evidence.md`、NR-056、NR-104 — existing evidence decisions;
  new contract must explicitly override the incomplete-pass behavior。

## Scope

1. Make the release evidence runner discover or enumerate every blocking NFR-001 row and produce
   an explicit verdict; `Not measured` must make the release result `INCOMPLETE`/non-zero rather
   than `PASSED`。
2. Keep the measured values reproducible: Release x64, no debugger, recorded OS/CPU/commit,
   100/500/2,000-entry profiles and 100/150/200% DPI where the row requires them。
3. Reconcile the generated evidence, testing guide and tracker metadata around the current
   25-test suite without editing historical completed item documents。

## Non-goals

- 不在本 item 內修改 app runtime、search algorithm、icon cache 或 thread model。
- 不把一次 local sample 宣稱成 multi-machine release validation；若完整 harness 太大，先
  split into focused measurement items，而不是放寬 gate。
- 不把低於 ideal target 但已證明未超 blocking threshold 的 metric 誤報為 failure。

## Acceptance

1. A generated report has one row per blocking baseline metric, with no silent omission。
2. Any unmeasured blocking metric makes the runner/report `INCOMPLETE`/non-zero; only complete
   measured evidence can say `PASSED`。
3. A deliberately stale CTest count (registered 25 vs executed other count) still fails, while
   the current clean 25/25 suite is reported consistently in all current docs。
4. The runner does not use process total thread count, executable size or a one-off estimate as a
   substitute for app-owned/thread/resource gates。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
pwsh -NoProfile -File tests/release/release_evidence.ps1 -OutPath build/release-evidence-audit.md
```

```powershell
Select-String -Path tests/release/release_evidence.ps1,docs/performance-baseline.md,docs/release-evidence.md -Pattern 'Not measured|INCOMPLETE|PASSED|Blocking-threshold gate|Total Tests: 25'
git diff --name-only
# expect: release script/evidence/testing/work-item metadata；不改 src/。
```

## Handoff

實作者需記錄每個 blocking row 的 measurement source、pass/fail/incomplete table、25/25
CTest evidence、multi-environment limitations、runner exit code 與是否需要拆出的後續 measurement items。

