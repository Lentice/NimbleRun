# NR-018 — Pin and unpin without drag

- Status: `done`
- Phase: 4
- Depends on: NR-004、NR-009、NR-010
- Source: `docs/design-spec.md` §4.2、§4.8、§FR-011、§AC-002

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

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §4.2、§4.8、§FR-011、§AC-002、`docs/work-items.md`、本文件；trace `src/catalog/app_entry.h`（is_pinned 欄位）、`src/usage/usage_store.h`（stable ID 持久化模式）、`src/settings/settings_store.h`、`src/storage/atomic_text_file.h`、`src/app_host/panel_model.h`（recent list 排序）、`src/app_host/main.cpp`（NR-010 右鍵 handler）。實作 pin／unpin without drag：以 context menu 的 pin／unpin action 與 stable ID persistence；pinned entries 優先於 recent entries、pin order 以建立順序或既定穩定順序保存；同一 App 不重複出現在 pinned 與 recent 區域；暫時不存在的 pin 保留 30 天、不因第一次掃描失敗立即刪除；不提供 drag-and-drop。建議以純值 pin store（stable ID 順序清單，比照 usage.tsv 的 atomic write 模式）＋PanelModel 整合（pinned 優先、dedup against recent）。不監控外部啟動、不加資料夾分組/deck/tag/自訂 action。回報修改檔案、測試命令、結果與未完成事項。
- Result: 新增純值 `pins/pin_store.{h,cpp}`（`nimblerun_pins` 庫）：`favorites.txt` 為版本化 UTF-8 TSV（首行 `schema=1`，其後每行 `<escaped stable_id>\t<last_seen_utc epoch>`，行序即 pin order；§10.2 名稱／§10.4 versioned 首行／FR-011 last_seen 的取捨已在 header 註解文件化）；`Pin(stable_id, now)`（重 pin 冪等、只刷新 last_seen、不換位）、`Unpin`、`IsPinned`、`OrderedPins`、`Reconcile(catalog, now)`（present 刷新 last_seen；absent 且超過 30 天即丟棄；空 catalog 一律不動 pin，避免第一次掃描失敗誤刪）；沿用 tmp＋flush＋atomic replace，corrupt→改名 `favorites.txt.corrupt`、newer schema→原檔不動。PanelModel 新增 `SetPins`：空白查詢改為 pinned（依 catalog snapshot 解析、absent 的 pin 不顯示但紀錄保留）→ recent（已 pin 者跳過，dedup 符合 AC-002）。main.cpp：WM_RBUTTONDOWN 改為 TrackPopupMenu context menu（依目前 pin 狀態顯示 Pin／Unpin，有效路徑才加「Open file location」，集中式字串表 `context_menu_strings`）；pin/unpin 後寫 store、`SetPins` 刷新＋invalidate；ShowPanel 與 snapshot swap 時 `RefreshPins()`（load＋reconcile＋save＋SetPins）；context menu modal loop 期間以 flag 抑制 WM_KILLFOCUS 隱藏。新測試 `nimblerun_pinning_test`（13 case：round-trip、pin order 跨 reload、unpin 只刪該項、重 pin 冪等＋last_seen 刷新、absent pin 存活、空 catalog 不刪 pin、30 天過期丟棄、corrupt／malformed／newer schema、atomic write failure 原檔保留、panel model pinned 優先＋不重複、absent pin 不顯示）；`ctest -R pinning` 1/1、全套件 17/17 通過。
