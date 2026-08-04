# NR-016 — Matrix panel and grid navigation

- Status: `deferred`
- Phase: 3
- Depends on: NR-010、NR-012、NR-015
- Source: `docs/design-spec.md` §4.2、§4.3、§4.7、§7、AC-001、AC-004

## Goal

在列表垂直切片穩定後，提供 icon matrix 的 App Drawer 呈現與鍵盤網格導覽。

## Scope when enabled

- Icon、App name、fixed cell geometry 與 deterministic row／column movement。
- `Left`／`Right`／`Up`／`Down` 移動，`Enter` 啟動，保留 Esc 行為。
- 可見項目 tooltip 顯示完整 path；沒有有效 path 的 packaged App 不提供 Open file location。
- reuse NR-009 ordering、NR-010 launch state、NR-012 icons、NR-015 DPI state。

## Non-goals

- 不做拖曳排序、動畫背景、模糊或新搜尋來源。

## Enable condition

只有列表垂直切片、icons、DPI state 與 Agent input self-check 已完成，才把本 item 從 `deferred` 改為 `ready`。

## Acceptance

- 在 item 仍為 `deferred` 時，不得修改目前列表垂直切片。
- 啟用後，固定 matrix fixture 的四方向移動與 `Enter` launch state 必須通過 self-check。
- matrix item 不新增搜尋來源或拖曳排序。

## Agent checks when enabled

```powershell
cmake --build build
ctest --test-dir build -R matrix --output-on-failure
```

測試用固定 cell geometry 與 input reducer fixtures；不要求 Agent 操作畫面。
