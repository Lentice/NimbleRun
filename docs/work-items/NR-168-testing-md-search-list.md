# NR-168 — testing.md 的 MVP 驗收清單仍要求「非空搜尋維持 grid」，與 §4.3/§4.7 單欄清單矛盾

Phase 0 · Docs · Depends on: —

- Source: `docs/design-spec.md` §4.3（非空查詢切換單欄清單）、§4.7（清單逐列
  移動）、`docs/testing.md:34-35`（過時驗收行）
- Origin: 2026-08-11 第十六次稽核第 2 輪（codex backend，MINOR）。主 Agent
  已重讀 `testing.md:29-44` 與 design-spec §4.3/§4.7 驗證。
- Priority: **LOW**——純文件；驗收契約照表人工驗收會把正確的 list 當成失敗，
  或讓未來回歸成 grid 仍被勾選通過。

## Why

`docs/testing.md:34-35` 的 MVP acceptance checklist 寫著：

> - Non-empty search filters only launchable apps in the same grid.
> - Arrow keys move through the grid and `Enter` launches the selected app.

但 design-spec §4.3 明文非空查詢切換成**單欄清單**（`Columns() == 1`，NR-029
實作），§4.7 明文清單上下鍵逐列移動；現行程式（`panel_model.cpp` 的
`Columns()`）與之相符。文件錯、碼對——NR-029 改版時 testing.md 的對應行
漏同步。它是 release 驗收契約的一部分（`docs/testing.md` §MVP acceptance
checklist），照表操作的人會把正確行為標成失敗。

## Decisions already made — do not reopen

1. 只改 `docs/testing.md:34-35` 兩行文字，不新增行、不動其他驗收行。
2. 措辭對齊 §4.3／§4.7：非空搜尋 → 「切換為單欄搜尋清單」；方向鍵 → 清單
   逐列移動與空狀態 grid 的格狀導覽分開描述（grid 語意保留在空搜尋那一行）。
3. 不改程式、不改 design-spec（§4.3/§4.7 已是對的）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.3（NR-029 後的現況）：

> 非空查詢：單欄清單。

`AGENTS.md`：

> Update the relevant documentation when behavior changes.

## Files to read and trace first

- `docs/testing.md:29-44`（checklist，:34-35 為修改行）。
- `docs/design-spec.md` §4.3、§4.7（措辭來源）。

## Scope

1. `:34` 改為描述「非空搜尋只過濾可啟動 App，並切換為單欄清單」。
2. `:35` 改為「空搜尋的 grid 以方向鍵格狀導覽、非空搜尋的清單以方向鍵逐列
   移動，`Enter` 皆啟動選取的 App」。
3. 無其他檔案變更。

## Non-goals

- 不改 design-spec、不改程式、不重排 checklist 其他行。
- 不新增測試。

## Acceptance

1. `testing.md:34-35` 與 §4.3／§4.7 一致（人工比對）。
2. `git diff --name-only` 只含 `docs/testing.md`。

## Agent checks

```powershell
git diff --name-only
# expect: 只 docs/testing.md
```

```powershell
rg -n "grid|list|list" docs/testing.md | Select-Object -First 8
# expect: :34 提到單欄清單；:35 區分清單逐列與 grid 格狀導覽
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

### 交接區（2026-08-11 完成）

修改後的 `docs/testing.md:34-35`（逐字）：

> - [ ] Non-empty search filters only launchable apps and switches to the single-column search list.
> - [ ] Arrow keys move through the empty-state grid; in the search list they move row by row, and `Enter` launches the selected app in both.

diff 結果：`git diff --name-only` 只含 `docs/testing.md`（1 file changed, 2 insertions(+), 2 deletions(-)）。措辭已人工比對 design-spec §4.3（非空查詢切換單欄清單）與 §4.7（清單逐列移動、`Enter` 啟動選取 App）。
