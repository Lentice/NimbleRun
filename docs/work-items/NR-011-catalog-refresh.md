# NR-011 — Catalog refresh and immutable snapshots

- Status: `planned`
- Phase: 2
- Depends on: NR-005、NR-006、NR-007
- Source: `docs/design-spec.md` §FR-007、§NFR-002、§NFR-003、AC-007

## Goal

讓 Start Menu 變更與手動 refresh 能在不阻塞列表的情況下更新 Catalog，並以完整 snapshot 一次替換。

## Scope

- Start Menu directory notification 與 500 ms debounce。
- AppsFolder on-demand refresh，不做高頻 polling。
- generation／cancellation，舊工作不得覆蓋新結果。
- 重建期間保留舊 snapshot，成功後 atomic swap。

## Non-goals

- 不重做 catalog enumeration rules。
- 不建立常駐 thread pool 或固定小於 60 秒的 timer。

## Acceptance

- 密集檔案事件只觸發一次合併 refresh。
- refresh failure 保留可用舊 snapshot。
- 舊 generation 完成後不能覆蓋較新的結果。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R catalog_refresh --output-on-failure
```

使用 fixture event stream 與 deterministic generation assertions；不要求人工修改真實 Start Menu。
