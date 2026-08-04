# NR-010 — Launchable list vertical slice

- Status: `planned`
- Phase: 1
- Depends on: NR-002、NR-003、NR-007、NR-008、NR-009
- Source: `docs/design-spec.md` §4.1、§4.3、§4.7、§4.8、AC-001、AC-003、AC-004、AC-005

## Goal

把 Phase 0 probe 收斂成第一個可使用的列表面板：顯示 Icon、App 名稱、有效路徑，支援搜尋、上下選取與 Enter 啟動。

## Scope

- 空白時顯示 recent list；有 query 時顯示 filtered App list。
- 第一項可選取但不自動啟動。
- Up／Down 移動，Enter launch，Esc 清空後再隱藏。
- 單擊啟動；列表項目只在有有效 path 時提供 Open file location。
- hotkey 顯示面板後將 focus 放到 input。

## Non-goals

- 不做 matrix、拖曳排序、lazy real icons、DPI polish 或完整 Settings page。
- 不要求 Agent 操作視窗來證明 UI。

## Acceptance

- list view 可由 snapshot 建立，不依賴 fake App data。
- keyboard state transitions 可由 focused test 驗證。
- Enter 只 launch 目前選取 entry；無結果時不會 launch。
- query、selection、launch failure 不會讓 host process crash。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R list_vertical_slice --output-on-failure
```

測試 input／selection／launch command model；host process 只需啟動與終止，不能把人工點擊列為完成條件。
