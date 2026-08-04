# NimbleRun Work Items

這是 NimbleRun 的實作追蹤總覽。產品行為唯一以 [design-spec.md](design-spec.md) 為準；本計畫不修改該文件。若 Spec 後續變更，先更新 Spec，再調整受影響的 item。

需求腦暴與已確認決策：[work-item planning requirements](requirements/2026-08-04_1739_nimblerun_work_item_planning_requirements.md)。相關 hotkey 研究：[hotkey-override-research.md](hotkey-override-research.md)。

## 使用方式

1. Agent 先讀本頁、[AGENTS.md](../AGENTS.md)、來源 Spec 章節與該 item 文件。
2. 只處理一個 item 的範圍；不要順手實作相鄰 item。
3. 完成前執行 item 文件指定的 Agent checks，保留命令與結果作為證據。
4. 更新本頁的狀態與 item 文件的交接備註；若被阻塞，寫出具體原因與需要的外部決策。

## 狀態

| 狀態 | 意義 |
|---|---|
| `planned` | 已定義但依賴尚未完成 |
| `ready` | 依賴已具備，可交給 Agent |
| `in_progress` | 正在實作 |
| `blocked` | 具體外部條件阻塞，不能自行繞過 |
| `done` | Agent checks 通過且交接資料完整 |
| `deferred` | 保留在 Spec／roadmap，但刻意延後 |

## Agent 交付規則

- 每個 item 只負責一個主要成果，避免跨 item 的隱性工作。
- 必須保持既有 build／CTest 可用；不得用關閉測試來取得綠燈。
- 每個非平凡邏輯至少新增一個 focused runnable test 或 self-check。
- Agent 只需能執行命令、測試程式、啟動／終止程序；不要求操作視窗或人工確認畫面。
- UI item 的 Agent checks 應驗證建置、視窗生命週期、狀態資料、訊息與可測的 Win32 結果；視覺人工驗證不屬於本追蹤表。
- 不新增網路、第三方 runtime、服務、driver、管理員權限或超出 Spec 的功能。
- 預設每個 item 的實作範圍為半天至兩天；若超過，先拆 item。

## Item 總覽

| ID | Item | Phase | Status | Depends on | 文件 |
|---|---|---:|---|---|---|
| NR-001 | Baseline build and Agent check contract | 0 | `ready` | — | [NR-001](work-items/NR-001-baseline-contract.md) |
| NR-002 | Single instance and tray lifecycle | 1 | `planned` | NR-001 | [NR-002](work-items/NR-002-single-instance-tray.md) |
| NR-003 | Normal global hotkey and conflict handling | 1 | `planned` | NR-001, NR-002 | [NR-003](work-items/NR-003-global-hotkey.md) |
| NR-004 | Atomic local settings store | 1 | `planned` | NR-001 | [NR-004](work-items/NR-004-settings-store.md) |
| NR-005 | Start Menu catalog enumeration | 1 | `planned` | NR-001 | [NR-005](work-items/NR-005-start-menu-catalog.md) |
| NR-006 | AppsFolder catalog enumeration | 2 | `planned` | NR-001 | [NR-006](work-items/NR-006-appsfolder-catalog.md) |
| NR-007 | Stable identity and deduplication | 2 | `planned` | NR-005, NR-006 | [NR-007](work-items/NR-007-identity-dedup.md) |
| NR-008 | Shell launch adapter | 1 | `planned` | NR-005, NR-006, NR-007 | [NR-008](work-items/NR-008-shell-launch.md) |
| NR-009 | Recent usage and ordering | 1 | `planned` | NR-004, NR-008 | [NR-009](work-items/NR-009-recent-usage.md) |
| NR-010 | Launchable list vertical slice | 1 | `planned` | NR-002, NR-003, NR-007, NR-008, NR-009 | [NR-010](work-items/NR-010-list-vertical-slice.md) |
| NR-011 | Catalog refresh and immutable snapshots | 2 | `planned` | NR-005, NR-006, NR-007 | [NR-011](work-items/NR-011-catalog-refresh.md) |
| NR-012 | Lazy Shell icons and fallback | 3 | `planned` | NR-005, NR-006, NR-010 | [NR-012](work-items/NR-012-icons.md) |
| NR-013 | Settings UI integration | 4 | `planned` | NR-003, NR-004, NR-010 | [NR-013](work-items/NR-013-settings-ui.md) |
| NR-014 | Startup option | 4 | `planned` | NR-004, NR-013 | [NR-014](work-items/NR-014-startup-option.md) |
| NR-015 | DPI, theme, high contrast, accessibility | 3 | `planned` | NR-010, NR-012, NR-013 | [NR-015](work-items/NR-015-dpi-theme-accessibility.md) |
| NR-016 | Matrix panel and grid navigation | 3 | `deferred` | NR-010, NR-012, NR-015 | [NR-016](work-items/NR-016-matrix-panel.md) |
| NR-017 | Diagnostics and automated release evidence | 5 | `planned` | NR-008, NR-011, NR-012, NR-013, NR-014, NR-015 | [NR-017](work-items/NR-017-release-evidence.md) |
| NR-018 | Pin and unpin without drag | 4 | `planned` | NR-004, NR-009, NR-010 | [NR-018](work-items/NR-018-pinning.md) |

## Dependency lanes

```text
NR-001
├── NR-002 ── NR-003 ───────────────┐
├── NR-004 ── NR-009 ───────────────┤
├── NR-005 ──┐                      ├── NR-010 ── NR-012 ── NR-015 ── NR-016
├── NR-006 ──┼── NR-007 ── NR-008 ──┘       │        │
└───────────┘                              └── NR-011

NR-004 + NR-013 ── NR-014
NR-008 + NR-011 + NR-012 + NR-013 + NR-014 + NR-015 ── NR-017
```

可平行處理的前提是依賴已完成且寫入的資料／訊息邊界穩定；不要為了平行而複製同一份邏輯。

## 計畫決策紀錄

- 2026-08-04：不修改 `design-spec.md`；它是唯一原始完整 Spec。
- 2026-08-04：MVP／第一個垂直切片先採列表，顯示 Icon、名稱與有效路徑；matrix 延後為 NR-016。
- 2026-08-04：空白搜尋最多顯示 20 個最近執行 App，依最後執行時間排序，不以字母排序補位。
- 2026-08-04：不做拖曳排序。
- 2026-08-04：封裝 App 沒有有效路徑時隱藏「開啟檔案位置」。
- 2026-08-04：Agent checks 不要求操作 App 視窗；人工驗證不列入本計畫。
- 2026-08-04：UI 文字採英文；中文只用於開發與規格文件。
