# NR-188 — 文件漂移同步：測試數 31→32、release evidence 重產、AGENTS.md／README 過時 baseline

Phase 5 · Docs · Depends on: —（純文件；建議在 NR-181 之後做，讓 CTest 數不再漂）

- Source: `docs/testing.md`（:9-10）、`docs/release-evidence.md`（:8）、`AGENTS.md`（§Current baseline）、`README.md`（:14-15）
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 報告 H-1／H-4；codex 報告 §4.3）
- Priority: **LOW**（不影響產品行為，但 AGENTS.md 的 Phase 0 描述是全 repo 最誤導的一段——每個 agent 進來讀的第一份檔案）

## Why

1. **測試數漂移**：`docs/testing.md:9` 寫「currently 31 checks」、`docs/release-evidence.md:8` 寫「Total Tests: 31」，實際 CTest 是 **32**（NR-180 新增 `nimblerun_cell_tooltip_test`）。NR-104 就是為這個漂移開的 item，又漂了。
2. **AGENTS.md §Current baseline 仍寫「Phase 0 foundation … Direct2D/DirectWrite rendering probe with an English fake app grid」**——實際已是多來源真 catalog、icon store、settings dialog、persistence、watcher、Phase 5 release evidence。它誤導 cold-start agent 的 scope 判斷。
3. **README.md:14-15 同樣稱 current executable 是 probe**。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Anything a later session needs must live in the repository, not in a scratchpad handoff.

## Files to read and trace first

- `docs/testing.md`、`docs/release-evidence.md`、`docs/roadmap.md`（phase 狀態的正確來源）、`README.md`、`AGENTS.md`。
- `tests/release/release_evidence.ps1` — release-evidence 的正確產生方式（claude 報告：該 script 會重寫整份報告，不要手改）。

## Scope

1. `docs/testing.md`：31 → 32（或改寫成不寫死數字的句子，若測試數還會長）。
2. `docs/release-evidence.md`：由 `tests/release/release_evidence.ps1` 重新產生（不手改）。
3. `AGENTS.md` §Current baseline：改寫成目前實際基準（真 catalog 三來源、icon store、settings dialog、persistence、watcher、tooltip、CTest 32 項），或刪除該節改指 `docs/roadmap.md`。二選一，改小的。
4. `README.md`：同步 current executable 描述。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-188 列。

## Non-goals

- 不改 `docs/work-items.md` 的 Item 總覽（那是追蹤表，本 item 只動文件描述）。
- 不重排 roadmap；不寫新功能說明。
- 不動產品程式碼與測試。

## Acceptance

- `rg "31 checks|Total Tests: 31" docs` 零命中（或 testing.md 已不寫死數字）。
- `docs/release-evidence.md` 由 script 重產且包含 32 項輸出。
- `AGENTS.md`／`README.md` 不再把目前產品描述成 probe。
- 無程式碼變更（`git diff --stat` 只有 docs/）。

## Agent checks

```powershell
rg -n "31 checks|Total Tests: 31|Phase 0|probe" docs/testing.md docs/release-evidence.md AGENTS.md README.md
ctest --test-dir build -N 2>&1 | Select-Object -Last 3
git diff --stat
```

驗證：文件與實際 CTest 數一致；`AGENTS.md`／`README.md` 的現況描述與 repo 實況相符；diff 只含 docs/。

## 交接區

（實作者填寫：testing.md 的寫法選擇、release evidence 重產命令與輸出摘要、AGENTS.md／README 改寫內容、diff 清單）
