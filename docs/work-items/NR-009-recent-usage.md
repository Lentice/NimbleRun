# NR-009 — Recent usage and ordering

- Status: `planned`
- Phase: 1 foundation, extended in Phase 4
- Depends on: NR-004、NR-008
- Source: `docs/design-spec.md` §4.2、§4.6、§4.7、§FR-010

## Goal

記錄 NimbleRun 自己成功發起的啟動，讓第一個列表垂直切片能顯示最多 20 個最近 App；Phase 4 再接上完整 usage scoring。

## Scope

- launch success 才增加 usage／last launch。
- stable ID 對應 recent records。
- 空白狀態最多 20 筆；不足時不以字母排序補位；無紀錄時回傳 empty state。
- 暫時不存在的 pin／usage 不在第一次掃描失敗後立即刪除。

## Non-goals

- 不監控其他程式的啟動。
- 不做拖曳排序、不改變 query ranking。

## Acceptance

- 相同 last-launch 時有穩定 tie-breaker。
- 21 筆以上只回傳最新 20 筆。
- 新啟動 App 會移到第一筆；失敗啟動不更新 recent。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R recent_usage --output-on-failure
```

使用固定時間與純 values fixtures，驗證排序、上限、空狀態與 round-trip。
