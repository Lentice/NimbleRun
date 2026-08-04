# NR-013 — Settings UI integration

- Status: `planned`
- Phase: 4
- Depends on: NR-003、NR-004、NR-010
- Source: `docs/design-spec.md` §FR-012、§4.10、§11

## Goal

讓使用者能從 tray／設定入口查看與修改既定 MVP 設定，且設定失敗不破壞舊值。

## Scope

- hotkey、recent count、hide-after-launch、theme、clear usage、reset settings。
- 英文 UI strings 集中管理。
- validation、save failure、reset default 的 state handling。

## Non-goals

- 不增加檔案搜尋、plugin、command line 或網路設定。
- 不把人工視覺檢查列為 Agent completion condition。

## Acceptance

- 設定值只接受 Spec 範圍。
- invalid hotkey 顯示一次非阻擋提醒並保留舊設定。
- reset／clear usage 只影響指定資料，不刪除 Catalog source。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R settings_ui --output-on-failure
```

以 settings command／state model self-check 驗證輸入、persist、rollback 與英文 string keys；不要求 Agent 點擊設定畫面。
