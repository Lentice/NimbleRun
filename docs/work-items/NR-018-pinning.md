# NR-018 — Pin and unpin without drag

- Status: `planned`
- Phase: 4
- Depends on: NR-004、NR-009、NR-010
- Source: `docs/design-spec.md` §4.2、§4.8、§FR-010、§AC-002

## Goal

讓使用者可從 App context menu 釘選／取消釘選，且不需要拖曳排序。

## Scope

- pin／unpin action 與 stable ID persistence。
- pinned entries 優先於 recent entries；pin order 以建立順序或既定穩定順序保存。
- 暫時不存在的 pin 保留 30 天，不因第一次掃描失敗立即刪除。
- 不提供 drag-and-drop；列表可先用 context action 完成 pin workflow。

## Non-goals

- 不監控外部 App 啟動。
- 不新增資料夾分組、deck、tag 或自訂 action。

## Acceptance

- pin／unpin 後重新啟動仍保持。
- 同一 App 不重複出現在 pinned 與 recent 區域。
- 缺少 App 時 pin record 不會立即被清除。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R pinning --output-on-failure
```

以 stable ID／persistence fixtures 測試；Agent 不需要操作 context menu 視窗。
