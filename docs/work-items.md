# NimbleRun Work Items

這是 NimbleRun 的實作追蹤總覽。產品行為唯一以 [design-spec.md](design-spec.md) 為準；本頁與 item 文件只同步拆分、依賴與驗收證據。若 Spec 後續變更，先更新 Spec，再調整受影響的 item。

需求腦暴與已確認決策：[work-item planning requirements](requirements/2026-08-04_1739_nimblerun_work_item_planning_requirements.md)。相關 hotkey 研究：[hotkey-override-research.md](hotkey-override-research.md)。

## 使用方式

1. Agent 先讀本頁、[AGENTS.md](../AGENTS.md)、來源 Spec 章節與該 item 文件。
   撰寫新 item 前另需讀本頁的 [§已否決的方向](#已否決的方向--不要重開)。
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
| `superseded` | 曾完成，但產品決策改變後由另一個 item 取代；文件與完成紀錄保留作為決策軌跡 |

**狀態與依賴只存在於本頁的 Item 總覽表格，item 文件不得自行宣告。** 同一份狀態存兩個地方必然會分岔：2026-08-07 清理前，15 個 item 文件的檔頭寫著自己的狀態，其中 12 個寫 `ready` 而表格早已是 `done`——冷讀該檔的 agent 會以為那是待辦工作。item 文件的檔頭只寫撰寫當下的定位（Phase、Depends on），狀態一律看本頁。

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
| NR-001 | Baseline build and Agent check contract | 0 | `done` | — | [NR-001](work-items/NR-001-baseline-contract.md) |
| NR-002 | Single instance and tray lifecycle | 1 | `done` | NR-001 | [NR-002](work-items/NR-002-single-instance-tray.md) |
| NR-003 | Normal global hotkey and conflict handling | 1 | `done` | NR-001, NR-002 | [NR-003](work-items/NR-003-global-hotkey.md) |
| NR-004 | Atomic local settings store | 1 | `done` | NR-001 | [NR-004](work-items/NR-004-settings-store.md) |
| NR-005 | Start Menu catalog enumeration | 1 | `done` | NR-001 | [NR-005](work-items/NR-005-start-menu-catalog.md) |
| NR-006 | AppsFolder catalog enumeration | 2 | `done` | NR-001 | [NR-006](work-items/NR-006-appsfolder-catalog.md) |
| NR-019 | User-folder catalog source | 2 | `done` | NR-004 | [NR-019](work-items/NR-019-user-folder-catalog.md) |
| NR-007 | Stable identity and deduplication | 2 | `done` | NR-005, NR-006, NR-019 | [NR-007](work-items/NR-007-identity-dedup.md) |
| NR-008 | Shell launch adapter | 1 | `done` | NR-005, NR-006, NR-007 | [NR-008](work-items/NR-008-shell-launch.md) |
| NR-009 | Recent usage and ordering | 1 | `done` | NR-004, NR-008 | [NR-009](work-items/NR-009-recent-usage.md) |
| NR-010 | Launchable list vertical slice | 1 | `done` | NR-002, NR-003, NR-007, NR-008, NR-009 | [NR-010](work-items/NR-010-list-vertical-slice.md) |
| NR-011 | Catalog refresh and immutable snapshots | 2 | `done` | NR-005, NR-006, NR-007, NR-019 | [NR-011](work-items/NR-011-catalog-refresh.md) |
| NR-012 | Lazy Shell icons and fallback | 3 | `done` | NR-005, NR-006, NR-010, NR-019 | [NR-012](work-items/NR-012-icons.md) |
| NR-013 | Settings UI integration | 4 | `done` | NR-003, NR-004, NR-010, NR-019 | [NR-013](work-items/NR-013-settings-ui.md) |
| NR-014 | Startup option | 4 | `done` | NR-004, NR-013 | [NR-014](work-items/NR-014-startup-option.md) |
| NR-015 | DPI, theme, high contrast, accessibility | 3 | `done` | NR-010, NR-012, NR-013 | [NR-015](work-items/NR-015-dpi-theme-accessibility.md) |
| NR-016 | Matrix panel and grid navigation | 3 | `superseded` | NR-010, NR-012, NR-015 | [NR-016](work-items/NR-016-matrix-panel.md) |
| NR-017 | Diagnostics and automated release evidence | 5 | `done` | NR-008, NR-011, NR-012, NR-013, NR-014, NR-015, NR-019 | [NR-017](work-items/NR-017-release-evidence.md) |
| NR-018 | Pin and unpin without drag | 4 | `done` | NR-004, NR-009, NR-010 | [NR-018](work-items/NR-018-pinning.md) |
| NR-020 | List panel replaces icon matrix | 3 | `done` | NR-010, NR-012, NR-015, NR-018 | [NR-020](work-items/NR-020-list-panel-restore.md) |
| NR-021 | Paged navigation and footer hint band | 3 | `done` | NR-020 | [NR-021](work-items/NR-021-paged-navigation-footer.md) |
| NR-022 | Launch failure dialog and one-shot catalog refresh | 3 | `done` | NR-008, NR-011, NR-020 | [NR-022](work-items/NR-022-launch-failure-dialog.md) |
| NR-023 | Search field style and typography | 3 | `done` | NR-015, NR-020 | [NR-023](work-items/NR-023-search-field-style.md) |
| NR-024 | Alt+digit quick select with per-row key hints | 3 | `done` | NR-020, NR-021 | [NR-024](work-items/NR-024-quick-select-digits.md) |
| NR-028 | AppsFolder launch identity and program-like item filter | 2 | `done` | NR-005, NR-006, NR-008, NR-013, NR-019 | [NR-028](work-items/NR-028-appsfolder-launch-identity.md) |
| NR-029 | Empty-state icon grid and footer path bar | 3 | `done` | NR-020, NR-021, NR-023, NR-024 | [NR-029](work-items/NR-029-empty-state-grid.md) |
| NR-030 | Spec amendment: persistent icon cache and relaxed resource budget | 3 | `done` | — | [NR-030](work-items/NR-030-icon-cache-spec-amendment.md) |
| NR-031 | Icon variant key, draw-time downscaling, derived LRU capacity | 3 | `done` | NR-030 | [NR-031](work-items/NR-031-icon-variant-key.md) |
| NR-032 | Icon worker thread with single-ownership handoff | 3 | `done` | NR-031 | [NR-032](work-items/NR-032-icon-worker-thread.md) |
| NR-033 | Icon pack format codec and corruption classification | 3 | `done` | NR-030 | [NR-033](work-items/NR-033-icon-pack-format.md) |
| NR-034 | WIC PNG codec for IconBitmap | 3 | `done` | NR-030 | [NR-034](work-items/NR-034-png-codec.md) |
| NR-035 | File-backed icon store: mmap read, append write, eviction, compaction | 3 | `done` | NR-033 | [NR-035](work-items/NR-035-icon-store-file.md) |
| NR-036 | Wire the icon store into the worker and its flush points | 3 | `done` | NR-032, NR-034, NR-035 | [NR-036](work-items/NR-036-icon-store-wiring.md) |
| NR-037 | Prewarm the first empty-state page after the panel hides | 3 | `done` | NR-036 | [NR-037](work-items/NR-037-icon-prewarm.md) |
| NR-038 | Normalize catalog names once, rank on indices | 3 | `done` | — | [NR-038](work-items/NR-038-search-normalize-once.md) |
| NR-039 | Drag the panel by its search box and empty chrome | 3 | `done` | — | [NR-039](work-items/NR-039-panel-drag.md) |
| NR-040 | Context menu: Properties and Remove from recent | 3 | `done` | — | [NR-040](work-items/NR-040-context-menu-properties-forget.md) |
| NR-041 | Visual marker for pinned items | 3 | `done` | — | [NR-041](work-items/NR-041-pinned-marker.md) |
| NR-042 | Search caret erased by the panel repaint | 3 | `done` | — | [NR-042](work-items/NR-042-search-caret-clipped.md) |
| NR-043 | Key-hint box labels: centered, border-colored, keycap font | 3 | `done` | — | [NR-043](work-items/NR-043-key-box-label-polish.md) |
| NR-044 | Rounded panel corners via DWM | 3 | `done` | — | [NR-044](work-items/NR-044-rounded-panel-corners.md) |
| NR-045 | Grid quick-select hints appear only while Alt is held | 3 | `done` | — | [NR-045](work-items/NR-045-alt-gated-grid-hints.md) |
| NR-046 | Drag pinned cells to reorder them in the grid | 3 | `done` | NR-018, NR-029, NR-040 | [NR-046](work-items/NR-046-pin-drag-reorder.md) |
| NR-047 | Search also matches the shortcut's resolved target name | 3 | `done` | NR-038 | [NR-047](work-items/NR-047-target-name-search.md) |
| NR-048 | The search test must actually assert in a Release build | 3 | `done` | — | [NR-048](work-items/NR-048-search-test-assertions.md) |
| NR-049 | Rebuild threads must not read `g_settings` or outlive the window | 3 | `done` | — | [NR-049](work-items/NR-049-rebuild-thread-lifetime.md) |
| NR-050 | A corrupt `icons.cache` header must not grow the file or crash | 3 | `done` | — | [NR-050](work-items/NR-050-icon-pack-hardening.md) |
| NR-051 | One COM guard, balanced on `S_FALSE`, no uninitialized buffers | 3 | `done` | — | [NR-051](work-items/NR-051-shared-com-guard.md) |
| NR-052 | Esc clears the search box; whitespace-only query stays in grid | 3 | `done` | — | [NR-052](work-items/NR-052-escape-clears-search-box.md) |
| NR-053 | Empty state orders by usage score and fills the visible grid | 3 | `done` | NR-052 | [NR-053](work-items/NR-053-empty-state-fill-and-order.md) |
| NR-054 | Diagnostic log moves to `logs\`, is thread-safe, and has a way in | 3 | `done` | — | [NR-054](work-items/NR-054-diagnostic-log-location.md) |
| NR-055 | Collapse 22 copies of the same test target into one loop | 3 | `done` | — | [NR-055](work-items/NR-055-test-cmake-boilerplate.md) |
| NR-056 | Make the docs describe the product that actually ships | 3 | `done` | — | [NR-056](work-items/NR-056-docs-match-shipped-product.md) |
| NR-057 | One versioned text-store reader, not four copies | 3 | `done` | — | [NR-057](work-items/NR-057-versioned-store-reader.md) |
| NR-058 | A corrupt or too-new user-data file reaches the user and the log | 3 | `done` | — | [NR-058](work-items/NR-058-store-load-failures-surface.md) |
| NR-059 | `Render()` paints the same icon fallback and empty state twice | 3 | `done` | — | [NR-059](work-items/NR-059-render-duplicate-paint.md) |
| NR-060 | Right-clicking the panel's empty area offers Refresh / Settings / About | 3 | `done` | NR-013, NR-018 | [NR-060](work-items/NR-060-panel-empty-area-context-menu.md) |
| NR-061 | Empty state shows only pins and recents, no alphabetical filler | 3 | `done` | NR-053 | [NR-061](work-items/NR-061-empty-state-no-filler.md) |
| NR-062 | A pin with no matching app shows as a removable missing tile | 3 | `done` | NR-061 | [NR-062](work-items/NR-062-missing-pin-placeholder.md) |
| NR-063 | Source enumeration failure reaches the failure path (§FR-008) | 3 | `done` | — | [NR-063](work-items/NR-063-source-failure-reaches-refresh.md) |
| NR-064 | Hit-testing selects only visible rows (footer / margins safe) | 3 | `done` | — | [NR-064](work-items/NR-064-hittest-visible-rows-only.md) |
| NR-065 | File events during an in-flight rebuild are not dropped | 3 | `done` | — | [NR-065](work-items/NR-065-inflight-events-not-dropped.md) |
| NR-066 | Mouse wheel accumulates sub-notch deltas | 3 | `done` | — | [NR-066](work-items/NR-066-wheel-delta-accumulator.md) |
| NR-067 | Device-resource recreation does not leak text formats | 3 | `done` | — | [NR-067](work-items/NR-067-text-format-leak.md) |
| NR-068 | IconStore rejects writes while not Ready (pending bounded) | 3 | `done` | — | [NR-068](work-items/NR-068-icon-store-readonly-pending.md) |
| NR-069 | GetStartupStatus treats the Run value as untrusted input | 3 | `done` | — | [NR-069](work-items/NR-069-startup-regsz-robust.md) |
| NR-070 | Store data files are untrusted input (ParseUint64 `-`, Reconcile overflow) | 3 | `done` | — | [NR-070](work-items/NR-070-store-files-untrusted.md) |
| NR-071 | 常用區依最後啟動時間排序，最新在最前 | 3 | `done` | NR-061 | [NR-071](work-items/NR-071-recent-ordered-by-recency.md) |
| NR-072 | RefreshPins 不得覆寫較新 schema／部分解析的 favorites.txt | 3 | `done` | — | [NR-072](work-items/NR-072-refreshpins-preserve-newer-schema.md) |
| NR-073 | rebuild 只在整代完成時刷新面板並寫 catalog.cache | 3 | `done` | — | [NR-073](work-items/NR-073-rebuild-complete-refresh-once.md) |
| NR-074 | 監看錯誤每段連續期只報一次 full-rescan，不再 1 Hz 重建 | 3 | `done` | — | [NR-074](work-items/NR-074-watcher-error-once-per-episode.md) |
| NR-075 | IconStore 記憶體守衛：Compact remap 失敗降級、payload_end 上限 | 3 | `done` | — | [NR-075](work-items/NR-075-icon-store-memory-guards.md) |
| NR-076 | 背景 worker 補 §11 例外捕捉邊界 | 3 | `done` | — | [NR-076](work-items/NR-076-worker-exception-boundary.md) |
| NR-077 | WM_APP+8/9 改用 token registry，不再解參考未驗證的 lParam | 3 | `done` | — | [NR-077](work-items/NR-077-message-payload-token-registry.md) |
| NR-078 | Context Menu／Shift+F10 開啟選取列的項目選單 | 3 | `done` | — | [NR-078](work-items/NR-078-keyboard-item-menu.md) |
| NR-079 | 較新 schema 的 catalog.cache 不被 rebuild 覆寫 | 3 | `done` | — | [NR-079](work-items/NR-079-catalog-cache-newer-schema-preserve.md) |
| NR-080 | SettingsStore／UsageStore Load 損壞時不得洩漏部分狀態 | 3 | `done` | — | [NR-080](work-items/NR-080-store-load-partial-state.md) |
| NR-081 | ShowPanel 的 on-demand AppsFolder refresh 不得取代進行中的 rebuild 世代 | 3 | `done` | NR-011, NR-063 | [NR-081](work-items/NR-081-appsfolder-on-demand-supersedes-rebuild.md) |
| NR-082 | CellAtPoint 在面板高度被 clamp 時命中未繪製的列 | 3 | `done` | NR-064 | [NR-082](work-items/NR-082-hittest-clamped-panel.md) |
| NR-083 | 建一次 stable_id 索引，取代熱鍵路徑上三次全 catalog 線性掃描 | 3 | `done` | — | [NR-083](work-items/NR-083-catalog-index-hotkey-path.md) |
| NR-084 | 格狀翻頁永遠碰不到最後一頁的尾端項目 | 3 | `done` | — | [NR-084](work-items/NR-084-grid-tail-unreachable.md) |
| NR-085 | 搜尋框持有焦點時，點擊面板外不會自動隱藏面板 | 3 | `done` | — | [NR-085](work-items/NR-085-outside-click-hide.md) |
| NR-086 | 熱鍵解析接受 shell 保留組合（Alt+Tab／Alt+Esc／Ctrl+Esc） | 3 | `done` | — | [NR-086](work-items/NR-086-shell-reserved-hotkey.md) |
| NR-087 | usage.tsv／catalog.cache 容許新增未預期的尾端欄位，而非整檔隔離 | 3 | `done` | — | [NR-087](work-items/NR-087-tsv-tolerate-trailing-fields.md) |
| NR-088 | ParseHotkey 允許 Win 修飾鍵（降級為警告）＋新增唯讀衝突探測函式 | 4 | `done` | — | [NR-088](work-items/NR-088-hotkey-win-modifier-and-probe.md) |
| NR-089 | 設定頁快速鍵改為唯讀顯示＋按鍵擷取小對話框 | 4 | `done` | NR-088 | [NR-089](work-items/NR-089-hotkey-capture-dialog.md) |
| NR-090 | AppsFolder 中途列舉失敗不得提交部分結果 | 2 | `done` | NR-006, NR-011, NR-063 | [NR-090](work-items/NR-090-appsfolder-mid-enumeration-failure.md) |
| NR-091 | Start Menu 中途列舉失敗不得提交部分結果 | 2 | `done` | NR-005, NR-011, NR-063 | [NR-091](work-items/NR-091-start-menu-mid-enumeration-failure.md) |
| NR-092 | UserFolder 中途列舉失敗不得提交部分結果 | 2 | `done` | NR-011, NR-019, NR-063 | [NR-092](work-items/NR-092-user-folder-mid-enumeration-failure.md) |
| NR-093 | 快速鍵擷取要分別追蹤左右修飾鍵的放開狀態 | 4 | `done` | NR-089 | [NR-093](work-items/NR-093-hotkey-physical-modifier-tracking.md) |
| NR-094 | 統一 MVP UI 語言規格，消除 English／雙語衝突 | 0 | `done` | — | [NR-094](work-items/NR-094-ui-language-source-of-truth.md) |
| NR-095 | AppsFolder 從未成功列舉時，下一次顯示面板應立即重試 | 2 | `done` | NR-006, NR-011, NR-063, NR-081 | [NR-095](work-items/NR-095-appsfolder-first-success-retry.md) |
| NR-096 | NewerSchema 載入後，所有 user-data Save 路徑仍須保護原檔 | 1 | `done` | NR-004, NR-009, NR-013, NR-018, NR-058, NR-072, NR-080 | [NR-096](work-items/NR-096-newer-schema-write-guard.md) |
| NR-097 | 補完 worker setup／handoff 的例外邊界，不讓背景例外終止 process | 3 | `done` | NR-076, NR-077 | [NR-097](work-items/NR-097-worker-setup-exception-boundary.md) |
| NR-098 | Catalog rebuild 在關閉與新世代前必須具備可控取消路徑 | 3 | `done` | NR-049, NR-063, NR-090, NR-091, NR-092 | [NR-098](work-items/NR-098-rebuild-shutdown-cancellation.md) |
| NR-099 | Icon worker queue 要有上限，並可取消過期工作 | 3 | `done` | NR-032, NR-036, NR-037 | [NR-099](work-items/NR-099-icon-queue-bound-and-stop.md) |
| NR-100 | Rebuild result delivery 失敗時，不得讓 generation 永久卡住 | 3 | `done` | NR-063, NR-073, NR-077 | [NR-100](work-items/NR-100-rebuild-completion-handoff.md) |
| NR-101 | Directory watcher 的 PostMessage 失敗不得遺失 catalog 變更 | 2 | `done` | NR-011, NR-063, NR-065, NR-074 | [NR-101](work-items/NR-101-watcher-notification-delivery.md) |
| NR-102 | Start Menu 的直接檔案也必須通過 program-like／uninstaller filter | 2 | `done` | NR-005, NR-028 | [NR-102](work-items/NR-102-start-menu-direct-program-filter.md) |
| NR-103 | PMv2 面板初次定位改用 per-window DPI API | 3 | `done` | NR-015 | [NR-103](work-items/NR-103-pm-dpi-query.md) |
| NR-104 | Testing guide 與 release evidence test count | 5 | `done` | NR-017, NR-056, NR-089 | [NR-104](work-items/NR-104-release-evidence-test-count.md) |
| NR-105 | Directory watcher 保留通知不可因無後續事件而永久沉默 | 2 | `done` | NR-101 | [NR-105](work-items/NR-105-watcher-pending-notification-liveness.md) |
| NR-106 | Rebuild setup／handoff failure 必須完成 generation | 3 | `done` | NR-097, NR-100 | [NR-106](work-items/NR-106-rebuild-setup-completion.md) |
| NR-107 | Per-user data root 不得 fallback 到空／相對路徑 | 1 | `done` | NR-004, NR-054, NR-096 | [NR-107](work-items/NR-107-safe-user-data-root.md) |
| NR-108 | IconStore write path 必須遵守 whole-pack byte budget | 3 | `done` | NR-035, NR-050, NR-075 | [NR-108](work-items/NR-108-icon-store-whole-pack-budget.md) |
| NR-109 | Icon worker 的 store lifecycle／flush 不能逃出例外邊界 | 3 | `done` | NR-097, NR-099 | [NR-109](work-items/NR-109-icon-worker-store-exception-boundary.md) |
| NR-110 | Single-instance wake-up 不得有 HWND 建立前競態 | 1 | `done` | NR-002 | [NR-110](work-items/NR-110-single-instance-startup-race.md) |
| NR-111 | Owner-drawn App rows 必須提供真正的 Windows accessibility tree | 3 | `done` | NR-015, NR-020 | [NR-111](work-items/NR-111-owner-drawn-accessibility-provider.md) |
| NR-112 | Release evidence 不得把未量測的 blocking gates 報成 PASSED | 5 | `done` | NR-017, NR-104 | [NR-112](work-items/NR-112-release-evidence-unmeasured-gates.md) |
| NR-113 | Catalog cache 項目未經來源驗證不得啟動 | 3 | `done` | NR-008, NR-011, NR-079 | [NR-113](work-items/NR-113-catalog-cache-launch-provenance.md) |
| NR-114 | IconStore 開啟時拒絕超過 whole-pack budget 的實體檔案 | 3 | `done` | NR-075, NR-108 | [NR-114](work-items/NR-114-icon-store-open-budget-guard.md) |
| NR-115 | Rebuild failure wake-up 失敗仍必須完成 generation | 3 | `done` | NR-100, NR-106 | [NR-115](work-items/NR-115-rebuild-failure-wakeup-reliability.md) |
| NR-116 | Cold-start cache ＋ 首輪 source failure 保留該來源快取行與 usage | 3 | `done` | NR-011, NR-063, NR-113 | [NR-116](work-items/NR-116-cold-start-cache-source-failure-retention.md) |
| NR-117 | Message loop 必須正確處理 GetMessageW error，不得 dispatch 未定義 MSG | 3 | `done` | NR-115 | [NR-117](work-items/NR-117-message-loop-getmessage-error.md) |
| NR-118 | Watcher/debounce 的部分 rebuild 不得取代冷啟動的完整 rebuild | 3 | `done` | NR-065, NR-081, NR-116 | [NR-118](work-items/NR-118-watcher-supersede-full-rebuild.md) |
| NR-119 | 設定對話框重入覆寫 g_dialog → null deref crash | 4 | `done` | NR-013, NR-089 | [NR-119](work-items/NR-119-settings-dialog-reentrancy-crash.md) |
| NR-120 | 高度 clamp 在高 DPI 小螢幕裁掉 footer；§4.9 的 70% 上限未實作 | 3 | `ready` | NR-015, NR-082, NR-103 | [NR-120](work-items/NR-120-panel-height-clamp-footer.md) |
| NR-121 | catalog.cache 無行數上限＋dedup O(n²) 在 UI 執行緒 | 2 | `ready` | NR-011, NR-057, NR-073, NR-079, NR-113 | [NR-121](work-items/NR-121-catalog-cache-row-cap-dedup-index.md) |
| NR-122 | favorites/usage 無行數上限：load O(n²)＋Reconcile O(pins×catalog)＋ReadAllBytes 無上限 | 1 | `ready` | NR-057, NR-072, NR-080, NR-096 | [NR-122](work-items/NR-122-store-row-caps-and-hash-loads.md) |
| NR-123 | Rebuild thread join 無界：hung Shell call 卡死關閉（§9.4） | 3 | `ready` | NR-049, NR-077, NR-098 | [NR-123](work-items/NR-123-rebuild-join-bounded-wait.md) |
| NR-124 | §11 診斷缺口：損壞捷徑／缺失資料夾靜默略過、dedup 歧義計數丟棄 | 3 | `ready` | NR-054, NR-063, NR-090, NR-091, NR-092 | [NR-124](work-items/NR-124-catalog-diagnostic-gaps.md) |
| NR-125 | design-spec §4.1／FR-002 回寫 NR-088 的 Win 鍵決策 | 4 | `ready` | NR-086, NR-088, NR-094 | [NR-125](work-items/NR-125-spec-winkey-sync.md) |
| NR-126 | 文件殘餘同步：§4.6 公式、§10.2 格式、testing/roadmap、註解、release-evidence | 5 | `ready` | NR-056, NR-061, NR-094, NR-104 | [NR-126](work-items/NR-126-docs-residual-sync.md) |
| NR-127 | 路徑／解析／常數 helper 重複拷貝收斂（×3/×3/×5 等） | 3 | `ready` | NR-057 | [NR-127](work-items/NR-127-duplicate-helper-convergence.md) |
| NR-128 | 死碼與 test-only API 移除（g_last_hotkey_error、GetStartupStatus、Resolve 等） | 3 | `ready` | NR-127 | [NR-128](work-items/NR-128-dead-code-removal.md) |
| NR-129 | 25 份逐字相同的測試 Expect helper 收斂 | 3 | `ready` | NR-055 | [NR-129](work-items/NR-129-shared-test-expect-helper.md) |
| NR-130 | 同 user DoS 面：full-rescan 限流＋single-instance 靜默退出 | 1 | `ready` | NR-002, NR-077, NR-110 | [NR-130](work-items/NR-130-same-user-dos-surface.md) |

## Dependency lanes

```text
NR-001
├── NR-002 ── NR-003 ───────────────┐
├── NR-004 ── NR-009 ───────────────┤
│   └────── NR-019 ──┐              ├── NR-010 ── NR-012 ── NR-015 ── NR-016
├── NR-005 ───────────┼── NR-007 ── NR-008┘       │        │
└── NR-006 ───────────┘                           └── NR-011

NR-003 + NR-004 + NR-010 + NR-019 ── NR-013
NR-004 + NR-013 ── NR-014
NR-008 + NR-011 + NR-012 + NR-013 + NR-014 + NR-015 ── NR-017

NR-016（superseded）
NR-010 + NR-012 + NR-015 + NR-018 ── NR-020 ──┬── NR-021 ── NR-024
                                              ├── NR-022（另需 NR-008、NR-011）
                                              └── NR-023

NR-020 + NR-021 + NR-023 + NR-024 ── NR-029（空白狀態 icon grid，部分恢復 NR-016 的意圖）

NR-005 + NR-006 + NR-008 + NR-013 + NR-019 ── NR-028
（NR-025～NR-027 為搜尋對齊評分／結果上限／最近清單固定 20 的預留編號，尚未撰寫）

NR-030 ── NR-031 ── NR-032
NR-030 ── NR-033
NR-030 ── NR-034
（NR-035 依賴 NR-033；NR-036 依賴 NR-032＋NR-034＋NR-035；NR-037 依賴 NR-036）

圖示效能 lane（NR-030 為文件前置，其餘皆為程式碼）：
NR-030 ──┬── NR-031 ── NR-032 ──┐
         ├── NR-033 ── NR-035 ──┼── NR-036 ── NR-037
         └── NR-034 ────────────┘
（NR-033／NR-034 可與 NR-031／NR-032 平行；三者都是純值或無 UI 依賴的模組）

稽核修補 lane（NR-048～NR-056，2026-08-06 全 repo 稽核產出）：
NR-048（測試斷言失效）── 無依賴，最先做，它是其他 item 的回歸保護前提
NR-049（rebuild 執行緒 UAF）  ┐
NR-050（icons.cache 強化）    ├── 三者互不相干、可完全平行（不同子系統）
NR-051（COM guard／未初始化）  ┘
NR-052（Esc／空白查詢）── NR-053（空狀態排序與填充）  ← 同函式相鄰行，依此序
NR-054（記錄檔位置／執行緒／入口）── 獨立
NR-055（測試 CMake 樣板）── 獨立純刪除；若其他 item 要新增測試執行檔，先做它
NR-056（文件對齊實況）── 獨立；與 NR-054 同動 design-spec 但條文不重疊

稽核修補 lane 2（NR-063～NR-070，2026-08-07 第三次全 repo 稽核產出）：
NR-063（來源失敗死碼）── 無依賴，最先做；§FR-008「單一來源失敗保留舊結果」目前是死碼
NR-064（hit-test 越界）── 獨立；點 footer／邊緣空白會啟動看不見的 App，使用者可見的高嚴重度
NR-065（掃描期間事件遺失）── 獨立；與 NR-063 都動 catalog refresh 週邊但不同函式，可平行
NR-066（滾輪餘數）── 獨立
NR-067（text format 洩漏）── 獨立
NR-068（icon store pending 增長）── 獨立
NR-069（startup REG_SZ）── 獨立；latent（目前無 production caller），接上 UI 前修掉
NR-070（store 不受信輸入）── 獨立；兩個 one-line 修補，僅手改資料檔可達
```

可平行處理的前提是依賴已完成且寫入的資料／訊息邊界穩定；不要為了平行而複製同一份邏輯。

## 稽核修補 lane 3（NR-072～NR-080，2026-08-08 第四次全 repo 稽核產出）

```
NR-072（pins 較新 schema 覆寫）── 無依賴，最先做；使用者資料損失（HIGH）
NR-073（rebuild 完成前每來源刷新）── 獨立；與 NR-079 都動 main.cpp:2343 一帶
                                        （不同行），可平行；同 agent 建議依序避免改同一 handler
NR-074（watcher 錯誤 1 Hz 迴圈）── 獨立
NR-075（icon store 記憶體守衛）── 獨立
NR-076（worker 例外邊界）── 獨立；與 NR-077 都動 icon_worker.cpp 送訊端（不同行）
NR-077（訊息 token registry）── 獨立
NR-078（鍵盤項目選單）── 獨立
NR-079（catalog.cache 較新 schema）── 獨立
NR-080（settings/usage 部分狀態）── 獨立；與 NR-072 同「非 Loaded 則空」契約主題
```

## 稽核修補 lane 4（NR-081～NR-082，2026-08-08 第五次全 repo 稽核產出）

```
NR-081（on-demand AppsFolder 取代進行中 rebuild）── 無依賴，最先做；catalog 收縮
        ＋ usage 資料損失（HIGH）；修 coordinator 的 ShouldRefreshAppsFolder 守門
NR-082（CellAtPoint clamp 後命中未繪製列）── 獨立；覆寫 NR-064 Decisions §1
        （新證據：面板高度被 clamp 時繪製範圍不再止於 footer）；修 CellAtPoint 加
        viewport 下界，NR-064 其餘決策沿用
```

## 稽核修補 lane 5（NR-090，2026-08-08 第七次全 repo 稽核產出）

```
NR-090（AppsFolder `Next()` 中途失敗被當成正常結束）── 依賴 NR-006、NR-011、NR-063（皆 done）；HIGH；
        `source_ok` 既有 failure path 已由 NR-063 建立，本 item 只補 Shell
        enumerator 的 `S_FALSE`／failure 分流，避免部分結果覆蓋舊 snapshot
```

## 稽核修補 lane 6（NR-091～NR-094，2026-08-08 第八次全 repo 稽核產出）

```
NR-091（Start Menu `FindNextFileW` 中途失敗被當成正常結束）── 依賴 NR-005、NR-011、NR-063（皆 done）；HIGH；
        只補 Win32 directory enumerator 的 clean-end／failure 分流，避免部分結果覆蓋舊 snapshot
NR-092（UserFolder `FindNextFileW` 中途失敗沒有 source status）── 依賴 NR-011、NR-019、NR-063（皆 done）；HIGH；
        沿用「缺失設定 root 先略過」的既有決策，只補已開啟目錄的中途失敗不得提交部分結果
NR-093（HotkeyCaptureState 以 category bit 取代 physical modifier state）── 依賴 NR-089；MEDIUM；
        兩個同類左右修飾鍵同時按住時，放開其中一個不得提前完成擷取
NR-094（design-spec NFR-006 寫雙語，AGENTS／development 寫 English-only）── 無依賴；MEDIUM；
        先釘定 MVP 唯一語言政策，再讓後續 UI／字串工作有單一依據
```

## 稽核修補 lane 7（NR-095～NR-104，2026-08-09 第九次全 repo 稽核產出）

```
NR-095（AppsFolder 從未成功時的首次重試）── 依賴 NR-006、NR-011、NR-063、NR-081（皆 done）；HIGH；
        覆寫 NR-081 Decisions §3 的 no-success 表示法，不改 running-generation guard
NR-096（settings／pins／usage 的 NewerSchema write guard）── 依賴 NR-004、NR-009、NR-013、NR-018、NR-058、NR-072、NR-080（皆 done）；HIGH；
        修在三個 store 的 Save 入口，避免只保護 RefreshPins 而漏掉 runtime mutation
NR-097（worker setup／handoff exception boundary）── 依賴 NR-076、NR-077（皆 done）；HIGH；
        補 NR-076 已完成 task-body catch 之外的配置、thread 建立與 registry 例外
NR-098（catalog rebuild cancellation）── 依賴 NR-049、NR-063、NR-090、NR-091、NR-092（皆 done）；HIGH；
        解決 join 可等待不可控 Shell／directory scan 的 shutdown 缺口
NR-099（icon queue upper bound／stale cancellation）── 依賴 NR-032、NR-036、NR-037（皆 done）；MEDIUM；
        補 §9.2 明文要求，不新增 thread 或 timer
NR-100（rebuild completion post failure）── 依賴 NR-063、NR-073、NR-077（皆 done）；HIGH；
        NR-063 已修 payload leak，本 item 補 source completion 不得永久 pending
NR-101（watcher notification post failure）── 依賴 NR-011、NR-063、NR-065、NR-074（皆 done）；MEDIUM；
        保留 dirty／full-rescan intent，不重開 NR-074 的 error episode backoff
NR-102（Start Menu direct file shared filter）── 依賴 NR-005、NR-028（皆 done）；MEDIUM；
        只重用 IsProgramLikeTarget，不把規則套到 FR-005 user folders
NR-103（PMv2 initial DPI query）── 依賴 NR-015（done）；MEDIUM；修初次 ShowPanel 的 API mismatch
NR-104（CTest／release evidence count drift）── 依賴 NR-017、NR-056、NR-089（皆 done）；MEDIUM；純測試／文件，不改產品 code
```

## 稽核修補 lane 8（NR-113～NR-115，2026-08-09 第十次全 repo 稽核產出）

```
NR-113（catalog.cache launch provenance）── 依賴 NR-008、NR-011、NR-079（皆 done）；CRITICAL；
        cache 可供啟動前顯示，但 cache-only／未經目前來源驗證的項目不得進入 Shell launch
NR-114（IconStore Open-time physical whole-pack budget）── 依賴 NR-075、NR-108（皆 done）；IMPORTANT；
        補 NR-108 只保護成功 Flush、NR-075 只保護 payload_end 的 read-side 缺口
NR-115（rebuild failure wake-up PostMessage failure）── 依賴 NR-100、NR-106（皆 done）；IMPORTANT；
        覆寫兩項交接區「記錄留在 vector 等下次 drain」的安全假設：醒醒訊息本身失敗時沒有下次事件
```

## 稽核修補 lane 9（NR-117，2026-08-09 第十一次 post-implementation audit 產出）

```
NR-117（GetMessageW error result 被誤當成可 dispatch）── 依賴 NR-115（done）；IMPORTANT；
        NR-115 重寫 message loop 後，-1 error 只用 truthiness 判斷，會把未定義 MSG 送進 Translate／Dispatch
```

## 已否決的方向 — 不要重開

寫新 item 前先讀這節（[AGENTS.md](../AGENTS.md) §Work item authoring rules 要求）。以下方向都已有明確依據被否決；**要重開是允許的，但新 item 內必須寫出覆寫與新證據**，不要在此節之外默默開一個。此節只收「有依據的否決」，純粹的優先序取捨屬於下面的 §計畫決策紀錄。

| 方向 | 依據 | 否決理由 |
|---|---|---|
| 以「搜尋太慢」為前提的 item：debounce、incremental narrowing、搜尋移到背景執行緒、結果筆數上限 | NR-047 交接區的兩條實測 | 5000 筆 catalog：既有 `L"e"` 查詢 **603 µs**，NR-047 最壞路徑（名稱全不命中＋每筆都有 alias）**204 µs**，ceiling 是 **50 ms**。差三個數量級，前提不成立。要重開需先提出新的量測。 |
| 另立一套搜尋鍵抽象：`SearchKeys(entry)` 存取器或 `std::vector<std::wstring> search_keys` | NR-047 §How this stays maintainable | vector 會在熱掃描路徑上多一次 per-entry 堆積配置與一層內迴圈，換來的是目前不存在的擴充性。新增搜尋鍵請從既有的 `search_alias` 欄位與 `MatchRank::Alias` 層級接。 |
| 中文拼音／注音／同義詞展開 | `docs/design-spec.md:180`（§4.4）、`:1046` | MVP 明文排除。這是**唯一真正通用**解決「不想打中文」的方案，但要重開屬於 spec 層級決策（需要對照資料與 §4.5 的新層級），**先問使用者是否把它移出 MVP 排除清單，不要逕自開 item**。 |
| 用 Catalog 項目把空白狀態的格狀填滿（NR-053 的 §4.2 規則 3） | NR-061 的使用者決策（2026-08-07） | 實機上填出 40 格 `3D Vision 相...`／`AccessPort`／`AlertMail48` 這類從未開過的項目，把「我釘的或我用過的」這個唯一語意稀釋掉，且這些格子的右鍵「Remove from recent」按了毫無反應。空白狀態的內容一律只來自釘選清單與使用紀錄；沒有就顯示一行提示。**NR-053 依 `usage_score` 排序的那一半保留。**（2026-08-07 由 NR-071 覆寫：常用區改依最後啟動時間排序、最新在最前，`usage_score` 僅留給 §4.5 搜尋結果的次要排序。「不用其他 App 填充」這條不受影響，仍然有效。） |
| 把 FR-004a 的 program-like 判準套用到 FR-005 使用者自訂資料夾 | `docs/design-spec.md:354` | 明文「此判準**不套用於** FR-005 的使用者自訂資料夾」。該來源的把關者是使用者自己勾選的副檔名清單；二次過濾會無聲擋掉使用者手動加入的副檔名。 |

## 稽核修補 lane 10（NR-118，2026-08-09 第十二次 fresh audit 產出）

```
NR-118（watcher/timer 部分 rebuild 取代冷啟動完整 rebuild）── 依賴 NR-065、NR-081、NR-116（皆 done）；
IMPORTANT；NR-081 守 ShowPanel on-demand、NR-116 守 failure retention，但 WM_TIMER debounce 與
full-rescan 分支都無 IsRebuildInProgress 守門，可取代首輪完整 rebuild：有 cache 時被取代來源只剩
unverified 行（顯示不可啟動、無後續完整 rebuild 保證）、無 cache 時來源消失＋usage 清除。修 coordinator
純值守門 ShouldStartRebuild，watcher/timer 在途時以既有 500 ms debounce timer 延後服務。
```

## 稽核修補 lane 11（NR-119～NR-130，2026-08-10 第十三次全 repo 稽核產出）

```
NR-119（設定對話框重入 crash）── 依賴 NR-013、NR-089（done）；CRITICAL；
        tray→Settings 在對話框 modal loop 內重入 ShowSettingsDialog，內層關閉清掉 g_dialog，
        外層任何控制項互動即 null deref。修 ShowSettingsDialog 入口 re-entrancy guard
NR-120（高度 clamp 裁 footer）── 依賴 NR-015、NR-082、NR-103（done）；IMPORTANT；
        200% DPI 小螢幕 path bar＋key hints 整條不可見；spec §4.9「70% 上限」從未實作。
        採「放不下時壓縮可見列數、footer 恆可見」＋spec 同步（第五輪曾記為低度發現，本輪升級）
NR-121（cache 無行數上限＋dedup O(n²)）── 依賴 NR-011、NR-057、NR-073、NR-079、NR-113（done）；MEDIUM；
        不受信 cache 檔可讓冷啟動凍結數秒以上；dedup.cpp 註解的「FR-003 有界」不涵蓋 cache 檔。
        行數上限走既有 Malformed 路徑；name-collision 掃描改 name-keyed 分桶（逐位元等價）
NR-122（store 行數/大小無上限）── 依賴 NR-057、NR-072、NR-080、NR-096（done）；MEDIUM；
        favorites/usage 的 O(n²) load＋O(pins×catalog) Reconcile 在每次 Alt+Space 的 UI 執行緒；
        ReadAllBytes 無大小上限。與 NR-121 同根因不同檔案，故拆開
NR-123（rebuild join 無界）── 依賴 NR-049、NR-077、NR-098（done）；MEDIUM；
        hung Shell call 使 WM_DESTROY／Ctrl+R 卡死 UI（§9.4 違反）；只補 shutdown 路徑 bounded wait，
        不用 TerminateThread，Ctrl+R 的 supersede join 維持原樣
NR-124（§11 診斷缺口）── 依賴 NR-054、NR-063、NR-090/091/092（done）；MEDIUM；
        損壞捷徑／缺失資料夾「記錄錯誤」從未實作、dedup 歧義計數被丟棄；計數帶回 RebuildResult、
        UI 執行緒完成 handler 寫 sanitized 一行式，枚舉器不碰 DiagnosticLog
NR-125（spec Win 鍵矛盾）── 依賴 NR-086、NR-088、NR-094（done）；MEDIUM；
        NR-088 覆寫後 spec §4.1 仍寫「一律拒絕 Win 鍵」——唯一「規格說謊」級發現；純改 spec
NR-126（文件殘餘同步）── 依賴 NR-056、NR-061、NR-094、NR-104（done）；LOW；
        §4.6 公式、§10.2 favorites 格式、testing/roadmap 過時、main.cpp 註解、release-evidence 重產
NR-127（helper 重複收斂）── 依賴 NR-057（done）；MEDIUM；ToLower/FileName/FileStem/Extension ×3、
        ParseInt ×3、kSchemaPrefix ×5、kMinRecentCount ×2、KeyFor、all-sources ×4、預設值 ×2
NR-128（死碼移除）── 依賴 NR-127；LOW；g_last_hotkey_error、GetStartupStatus/StartupStatus/PathsMatch
        （唯一讀大 REG_SZ 的輸入面，刪掉即移除）、IconCache::Resolve、kQuickSelectDigits、FileName、未用 include
NR-129（測試 Expect ×25）── 依賴 NR-055（done）；LOW；NR-055 的 CMake 收斂在測試碼層的對應
NR-130（同 user DoS 面）── 依賴 NR-002、NR-077、NR-110（done）；LOW；
        full-rescan marker 無限流可被偽造事件驅動 rebuild storm；single-instance 逾時靜默退出。
        採限流＋逾時 MessageBoxW 回饋，不驗證 sender、不搶回 mutex
```

## 計畫決策紀錄

- 2026-08-10（NR-119～NR-130 ready，第十三次全 repo 稽核產出）：NR-118 完成後派四個平行唯讀
  subagent 分軸審計（ponytail 過度設計／正確性穩健性／spec 符合度／安全性不受信輸入），主 Agent
  對所有 CRITICAL/IMPORTANT 與跨軸交叉發現逐一重讀原始碼驗證後收斂成 12 個 item。**排序依
  「先修 crash、再修使用者看得到的中斷、再修資料面與資源、再修文件與整理」**。逐項決策與
  「為什麼不那樣做」：**NR-119（CRITICAL）**——`g_dialog` 是檔案範圍全域（`settings_dialog.cpp:47`，
  `:613` 設／`:619` 清空），`kSettingsMessage` handler（`main.cpp:3058-3064`）無 re-entrancy
  守門；設定對話框 modal loop 期間 tray→Settings（`ShowTrayMenu` 的 `PostMessageW(kSettingsMessage)`）
  重入 `ShowSettingsDialog`，內層關閉清掉外層 context → 外層任何控制項互動 null deref 殺死常駐
  process。`g_dialog_active` 只涵蓋 MessageBox 前後、語意是「面板不因 KILLFOCUS 隱藏」，不是守門。
  修法：`ShowSettingsDialog` 入口 `if (g_dialog.editor != nullptr) return false;` 一行；不搬
  `GWLP_USERDATA`（搬移是選修）；WM_HOTKEY 在對話框期間叫出面板（焦點竊取）是同族不同問題，
  不 crash，列 Non-goal。**NR-120（IMPORTANT）**——`ClampWindowSize`（`panel_layout.cpp:37-43`）
  是 work-32px；200% DPI 小螢幕（1366×768）488 DIP 面板被 clamp 到 696px，footer band
  （462~482 DIP，第五輪已量）整條不可見，§4.2 path bar 與 §4.9 key hints 都是 spec 承諾；
  spec 的「上限 70%」與「高度依內容調整」從未實作、三方矛盾。採 Option A：放不下時壓縮
  list/grid 可見列數、footer 恆可見，clamp 維持 work-32px（70% 在 200% 小螢幕更小、仍裁
  footer），spec 同步改寫；NR-064/082 的 hit-test 界限不動。**NR-121（MEDIUM）**——cache 檔
  是磁碟上不受信輸入卻無行數上限（`catalog_cache.cpp:122-155` 全收），`DeduplicateCatalog`
  的 name-collision 掃描是 O(n²)（`dedup.cpp:96-106`，每對兩次 `ToLower`）；dedup.cpp 註解的
  「FR-003 有界」約束的是枚舉器輸出，不涵蓋 cache 檔；呼叫點在 message loop 前的冷啟動與每次
  generation 完成（UI 執行緒）。修法：`kMaxCacheRows` 超限走既有 Malformed 路徑（NR-050 形狀），
  name-collision 改 lowercased name 分桶（`UnjudgeableNameCollision` 只在名字相等時才可能 true，
  分桶結果逐位元等價）；不重開「搜尋太慢」的已否決方向（本 item 是 dedup＋cache 界，與
  `SearchApps` 無關）。**NR-122（MEDIUM）**——favorites/usage 的 load 是 O(n²)（`pin_store.cpp:76`、
  `usage_store.cpp:68`），Reconcile 是 O(pins×catalog)（`pin_store.cpp:210`），每次 Alt+Space
  都在 UI 執行緒跑，兩檔無行數上限、`ReadAllBytes`（`atomic_text_file.h:61-84`）無大小上限；
  與 NR-121 同根因但檔案/演算法/測試不同，拆開。修法：行數/位元組上限走既有 corrupt 路徑＋
  load/Reconcile hash 化（O(n+m)），schema 與 Save 格式一字不改。**NR-123（MEDIUM）**——
  `JoinRebuildThreads`（`main.cpp:1587-1596`）在 Ctrl+R、設定套用與 WM_DESTROY 都無界 join；
  NR-098 的 cancel 只在迭代邊界檢查，hung Shell call 卡死關閉（§9.4 違反）。第四輪曾把 icon
  worker Stop() 記為已知限制；本 item 只補 shutdown 路徑 bounded wait（建議 5 s，逾時放棄 join；
  安全論證＝NR-049 捕獲清單＋NR-077 未知 token 忽略＋process 結束 OS 回收執行緒），Ctrl+R 的
  supersede join 維持原樣（NR-118 決策 §4），**不用 TerminateThread**（持 COM 鎖的執行緒上
  強制終止可能 deadlock 全 process）。**NR-124（MEDIUM）**——§11 明列的「單一捷徑損壞→記錄
  錯誤」「缺失資料夾→記錄一次」從未實作（`start_menu_catalog.cpp:138-139`、`user_folder_catalog.cpp:
  111-113` 靜默），dedup 的 `ambiguous_kept`/`removed_duplicates` 計數在 `catalog_refresh.cpp:232`
  被丟棄；grep 證實枚舉器路徑零 DiagnosticLog。修法：計數帶回 RebuildResult、UI 執行緒完成
  handler 寫 sanitized 一行式（FR-014 格式，每 generation 每來源至多一行、零計數不寫）；
  `source_ok` 語意（NR-063/090/091/092）不改。**NR-125（MEDIUM）**——NR-088 把 Win 鍵從硬拒絕
  降級為警告可確認後，spec §4.1（`:135`）與 FR-002 仍寫「一律拒絕」，出貨行為與唯一真相直接
  矛盾；本 item 是覆寫的落點，只改 spec（NR-086 的 shell-reserved 三組合硬拒絕保留）。
  **NR-126（LOW）**——§4.6 公式（spec 寫 30d/7d 視窗、實作是 lifetime total＋bonus，schema
  算不出公式；第四輪已記刻意省略，本 item 只把 spec 同步為實際公式並註記 schema 限制，不 bump）、
  §10.2 favorites 格式（寫每行一個 ID、實際 schema=2 三欄 TSV）、testing.md 的 NR-053 殘句
  （NR-061 已否決填充）、roadmap Phase 標記、main.cpp:2328 註解（640x432 vs 488）、
  release-evidence 重產（25→26）。**NR-127（MEDIUM，ponytail 軸唯一 MEDIUM）**——ToLower/
  FileName/FileStem/Extension 三份拷貝（`user_folder_catalog.cpp:16-46` 與同 library 的
  `app_filter.h` 逐字相同、settings 再拷兩份）、ParseCountText vs ParseInt 第三份 wcstol 且缺
  ERANGE、kSchemaPrefix ×5、kMinRecentCount ×2、KeyFor vs IconKey::Encode、main.cpp all-sources
  ×4、Settings 預設值雙份；沿用 NR-057 的收斂原則（放既有純值標頭、逐字相同才收斂、行為零變更）。
  **NR-128（LOW）**——`g_last_hotkey_error`（write-only）、`GetStartupStatus`/`StartupStatus`/
  `PathsMatch`（src 零呼叫者、唯一讀大 REG_SZ 的輸入面，刪掉即移除）、`IconCache::Resolve`
  （NR-032 後無生產消費者）、`kQuickSelectDigits`（測試用常數驗證常數）、`app_filter::FileName`
  （零外部呼叫者，時機與 NR-127 協調）、兩個未用 include；`SetStartupEnabled` 保留。**NR-129
  （LOW）**——25 份逐字相同 Expect 收斂成 `tests/unit/test_util.h`，NR-055 的 CMake 收斂在
  測試碼層的對應。**NR-130（LOW）**——full-rescan marker 無限流（同 user process 可偽造事件
  驅動 rebuild storm）＋single-instance 逾時靜默退出；採「限流併入既有 debounce＋逾時
  MessageBoxW 回饋」，不驗證 sender（NR-077 決策不重開）、不搶回 mutex（NR-110 競賽不重開）。
  跨軸交叉：B.M1（footer 裁切）＝C.F4（70% 未實作）合併為 NR-120；B.M2（dedup O(n²)）＝
  D.F1（cache 無上限）合併為 NR-121；D.F6（REG_SZ 大小）因 GetStartupStatus 是死碼而由
  NR-128 一併消除，不另開。未成 item 的低嚴重度發現（記錄備查）：(1) `RefreshPins` 每次
  ShowPanel 都整檔重寫 favorites.txt（Reconcile 把全部 last_seen 重寫為 now，熱路徑 I/O）；
  (2) `g_pins->Save()`/`g_usage->Save()` 回傳值被忽略（失敗時無診斷）；(3) ShowPanel 的
  settings Load 回傳值未檢查（執行中損壞→設定靜默回預設一次）；(4) EN_UPDATE 查詢截斷在
  1023 字元（EDIT 顯示與搜尋不一致）；(5) fallback initial 對 surrogate pair 只取高半代理字
  （純視覺）；(6) `PanelModel::Columns()` 每次呼叫重跑 NormalizeName（微量）；(7) §8.3
  Clang 分支無 LTO/CFG/HEVA/PDB（工具鏈支援與否屬 release 決策）。全部 12 個 item 依賴皆
  done、皆 `ready`；NR-127 與 NR-128 同動 user_folder_catalog/app_filter，建議同一 agent 依序。
  未 commit。

- 2026-08-09（NR-118，第十二次 fresh audit 產出）：NR-117 完成後再派 fresh independent read-only audit，
  未在 NR-113～NR-117 內找到新 bug；但沿 `kWatchChangedMessage`→`WM_TIMER`→`StartRebuild` 追蹤發現一個
  **未覆蓋的 IMPORTANT**：`StartRebuild` 一律 join＋`BeginGeneration` 取代在途 generation，而 watcher 的
  兩個入口（`WM_TIMER` debounce `:3034-3041`、full-rescan `:2950-2955`）沒有 NR-081 那樣的守門。watcher
  在首輪完整 rebuild 之前啟動（`:3694` vs `:3775`），因此首輪期間任何監看 root 事件會在 debounce 到期時
  以單一來源部分 cycle 取代完整 rebuild：冷啟動下有 cache 時被取代來源只剩 NR-116 seed 的 unverified 行
  （顯示但不可啟動、無機制再排完整 rebuild），無 cache 時來源行被 `RebuildMerged` 丟棄 → snapshot 縮水 →
  usage Reconcile 刪除該來源紀錄＋縮水 cache 覆寫。NR-065 只保證事件不遺失、NR-081 只守 ShowPanel、
  NR-116 只修 failure 保留，皆未覆蓋。NR-118 修法沿用 NR-081 形狀：coordinator 新增純值守門
  `ShouldStartRebuild = !IsRebuildInProgress() && HasDueRebuild(now)`；watcher/timer 在途時以既有
  500 ms debounce timer 延後（目前 generation 完成後的下一個 tick 服務 pending），不新增 timer／polling，
  完整 rebuild 的 caller（Ctrl+R、launch-failure、settings、首輪）維持可取代。未 commit 於本決策段落；
  ticket 文件與 tracker 行另 commit。

- 2026-08-09（NR-117，第十一次 post-implementation audit）：獨立驗證 NR-113～NR-116 的 Release
  build／CTest 全綠後，重新檢查 NR-115 的 message loop。原本的 `while (GetMessageW(...) > 0)`
  正確把 `-1`（Win32 message retrieval error）與 `0`（WM_QUIT）都排除；NR-115 改成
  `if (!GetMessageW(...)) break` 後，`-1` 的 C++ truthiness 會使分支不 break，接著以未定義的
  `MSG` 呼叫 `TranslateMessage`／`DispatchMessage`。這是 NR-115 新引入的 UI lifecycle/correctness
  回歸，不是重開已完成的 wake-up ticket；NR-117 只修 return-value 分流，不改 event-driven signal。

- 2026-08-09（NR-116，fresh audit 產出）：NR-113～NR-115 完成後對整個 repo 做 fresh read-only audit，
  未在三個新 commit 內找到 critical/important 問題；但追蹤冷啟動路徑發現一個**未覆蓋的 IMPORTANT**
  缺口：`SetSnapshot` 只把 cache 寫進 `merged_`、從不 seed `source_entries_`，因此 valid cache 冷啟動後
  **首輪** rebuild 若某來源失敗，`RebuildMerged` 只從 `source_entries_` 重建 → 該來源的 cache 行被整批
  丟棄 → `RefreshPanelSnapshot` 對縮水的非空 snapshot 跑 usage Reconcile 並 Save → **該來源的 usage.tsv
  紀錄被永久刪除**（§10.2 使用者資料）＋縮水版寫回 catalog.cache。違反 §FR-008「單一來源失敗保留該來源
  舊結果」；NR-081 只修 on-demand 守門、NR-063 依賴 `source_entries_` 已有舊值，皆未覆蓋。
  **覆寫 NR-113 交接區「首輪失敗時 cache row 會留在 snapshot 直到重新驗證」的假設**（新證據：行會被
  丟棄）。NR-116 修法：啟動 cache load 後 seed `source_entries_`（依 AppSource→CatalogSource，保留
  `launch_verified=false`），並讓 dedup `Beats` 對同一 stable_id 的 verified 行優先於 unverified 行——
  失敗來源保留 cache 行（顯示、不可啟動）、成功來源的 fresh verified 行不被未驗證行 dedup 掉。
  未 commit 於本決策段落；ticket 文件與 tracker 行另 commit。

- 2026-08-09（NR-113～NR-115，第十次全 repo 稽核）：cache 與 icon pack 都是可重建快取，
  但「可重建」不等於「可把未驗證內容當成啟動信任來源」或「可在 Open 時接受超額實體檔案」。
  NR-113 保留有效 cache 的即時顯示，只把目前 Catalog source 的成功結果視為 launch provenance；
  NR-114 以既有 `kPackByteBudget` 同時約束 payload_end 與 physical file size，不新增第二個 magic number。
  另有 NR-115 擴充 NR-100／NR-106：它不改 event-driven、token registry 或 generation semantics，
  只修正 failure record 已入列但 wake-up `PostMessageW` 失敗時沒有任何後續 drain 事件的缺口。

- 2026-08-09（NR-095，首次 AppsFolder 失敗重試）：**覆寫 NR-081 Decisions §3 的
  no-success 表示方式**。NR-081 正確保留了 running-generation guard，也正確要求
  failed AppsFolder 不更新成功時間；但目前 `last_appsfolder_success_ms_ = 0` 同時表示
  「從未成功」與「在 monotonic t=0 成功」。`GetTickCount64()` 讓啟動後 10 分鐘內的首次
  failure 在下一次 ShowPanel 不會重試，與 §FR-008 的「下一次叫出時再試」決策不符。NR-095
  只新增可區分 no-success／success timestamp 的純 coordinator state；不 baseline 啟動
  時間、不加 timer／輪詢、不改 NR-081 的 generation guard 與失敗保留舊結果語意。

- 2026-08-09（NR-094，MVP application UI 語言決策）：產品決策——**MVP application UI 一律為英文（English-only）**。規格 `docs/design-spec.md` §NFR-006 原本寫「MVP UI 至少提供英文與繁體中文」，與 `AGENTS.md` §Language rules、`docs/development.md` §UI language 的「All user-visible NimbleRun UI text is English」矛盾；現有 `src/app_host/main.cpp` 的 `list_strings`／`footer_strings`／`dialog_strings`／`context_menu_strings`、`src/settings/settings_editor.cpp` 字串表與 `src/resources/` 皆為英文，且無 locale selector、翻譯資源或本地化測試。故 NFR-006 改寫為 English-only MVP 政策並保留「字串集中管理」規則；後續 UI item 一律以 §NFR-006 為唯一 authority，`docs/development.md` §UI language 與 `AGENTS.md` §Language rules 文字不變，三者不再互相矛盾。雙語未被否決，但屬規格層級決策：若產品需要繁體中文等第二語言，須另開實作 item 先定義 locale 來源、預設／fallback、字串資產位置與驗收語言範圍。本決策不新增任何翻譯、locale 機制、資源檔或 runtime dependency。未 commit。

- 2026-08-08（NR-091～NR-094，第八次全 repo 稽核產出）：在 NR-090 修正
  AppsFolder `IEnumShellItems::Next()` 後，重新沿著三個 catalog source 的
  「enumerator → worker → `CatalogRefreshCoordinator` → `RefreshPanelSnapshot`」
  流程追蹤 Win32 目錄列舉。發現 Start Menu 的 `EnumerateDirectoryRecursive()` 與
  UserFolder 的 `ScanDirectory()` 都把 `FindNextFileW == FALSE` 直接視為正常結束，
  沒有檢查 `GetLastError() == ERROR_NO_MORE_FILES`；前者的 `source_ok` 只看 Known
  Folder path 是否解析，後者根本回傳 `std::vector<AppEntry>`、worker 也永遠標記成功。
  兩條路徑都可能以部分 entries 取代舊 source snapshot，違反 §FR-008 的完整替換
  契約，因此分成 NR-091／NR-092，不把兩種 source 的 root 語意硬抽成共用型別。
  **NR-092 不重開 NR-063 的既有決策**：設定中的缺失 root 仍先略過；新證據只針對
  已成功開啟目錄後的 `FindNextFileW` 中途錯誤，該次 UserFolder source 應回報失敗
  而非提交 partial snapshot。
  另發現 NR-089 的純狀態機以 `MOD_CONTROL` 等 category bit 保存 held state；
  `LControl down → RControl down → E down → LControl up` 會清掉整個 bit，於
  `RControl` 仍按住時提前完成擷取，故開 NR-093。最後，`design-spec.md` NFR-006
  的「英文與繁體中文」與 `AGENTS.md`／`docs/development.md` 的 English-only
  規則互相矛盾，開 NR-094 先解決文件 authority；在此之前不推導任何本地化實作。

- 2026-08-08（NR-088～NR-089 planned，使用者需求討論產出）：使用者要求設定頁
  快速鍵欄位從自由輸入文字改成「按鍵擷取」UI（唯讀顯示＋Change 按鈕開小
  對話框，按鍵即時偵測、放開順序不拘、衝突只警告不阻擋），並要求擷取邏輯
  支援 Win 鍵。**覆寫** `docs/design-spec.md:135`（§4.1）與 `:296-302`
  （§FR-002）「Windows 鍵…註冊失敗一律拒絕」對 Win 鍵組合的適用性（新證據：
  使用者明確要求可設定含 Win 鍵的快捷鍵，衝突與否由使用者自行判斷）——NR-086
  的 shell-reserved 靜態拒絕清單（`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc`）**不**
  一併覆寫，維持硬拒絕。拆成兩個 item：**NR-088**（後端，`ParseHotkey` 放行
  Win token＋新增 `TryRegisterHotkey` 唯讀探測函式，無 UI 改動）→
  **NR-089**（UI，唯讀欄位＋按鍵擷取對話框，依賴 NR-088；技術上需要在對話框
  生命週期內安裝／移除低階鍵盤 hook 才能可靠攔截 Alt/Win 等系統鍵，已用
  GitHub 上 Wox／Flow Launcher 的既有 issue 交叉確認為業界共通做法，非本專案
  自創）。用 grilling 技巧逐輪確認：修飾鍵判定時機（主鍵按下當下仍按住，非
  按過即算）、不允許純修飾鍵組合、Esc 單獨按下＝取消對話框、衝突偵測雙軌
  （靜態清單＋即時試註冊探測）、小對話框沿用既有 native Win32 dialog resource
  做法。兩個 item 皆未開始實作。
- 2026-08-08（NR-084～NR-086 ready，第六次全 repo 稽核產出）：backlog 清空後對
  整個 repo 做第六輪稽核（正確性／spec 對照＋**使用者操作情境流程追蹤**），主
  Agent 從頭重讀 `main.cpp`（3,283 行）與全部模組後，再以「顯示→輸入→點擊／
  鍵盤→啟動→隱藏→設定→關閉」等使用者流程逐條追蹤訊息流，收斂成 3 個 item。
  **NR-085（HIGH，最常見的關閉手勢失效）**——§4.8「點擊面板外，面板自動隱藏」
  的唯一機制是 `WindowProc` 的 `WM_KILLFOCUS`（`main.cpp:2920-2928`），但
  `WM_KILLFOCUS` 只送給失去鍵盤焦點的那個視窗，而 `ShowPanel` 把焦點放在搜尋
  EDIT（`main.cpp:1979`），面板本身從未持焦——「顯示後直接點別處」與「輸入後
  點別處」兩個高頻路徑上，焦點從 EDIT 移走、面板收到的是 `WM_ACTIVATE(WA_INACTIVE)`
  而 `WindowProc` 沒有 `WM_ACTIVATE` 分支，面板賴在 TOPMOST 不關。修法：新增
  `case WM_ACTIVATE`（`WA_INACTIVE` 且既有的 `g_context_menu_active`／
  `g_dialog_active` 兩旗標皆 false → `HidePanel`），既有 `WM_KILLFOCUS` 保留
  互補（面板自持焦的 `Alt+Tab` 路徑）。**為什麼不那樣做**：不改 EDIT subclass
  的 `WM_KILLFOCUS`（需複製 modal 旗標判斷，且 EDIT 不知道 app 層 modal 狀態）；
  不加單元測試（訊息層行為，依 NR-060 先例以 sanity grep＋手動驗收覆蓋）。
  **NR-084（MEDIUM，釘選/常用 App 從空白狀態消失）**——`ClampFirstVisible`
  （`panel_model.cpp:122-131`）「夾 `[0, RowCount-page]` 再向下取整到 columns
  倍數」在總數不是一頁（24）整數倍時把尾端項目永久擋在視窗外：50 筆 → 上界
  26 → 取整 24 → 可見最遠 `[24,48)`，第 49、50 筆不可達，`PgDn` 停在 24 再也
  翻不動；鍵盤 `SelectRow(49)` 後選取落在未繪製格上、`Enter` 仍啟動看不見的
  App。**覆寫** NR-029 引入並被 `TestGridFirstVisibleAlignedToColumns`（
  `panel_model_test.cpp:541`，50 筆 → 24）鎖定的「尾端取整」決策（新證據：
  §4.2「翻頁」的前提是全部項目可達）。修法：上界改
  `max(0, (ceil(count/columns) - viewport_rows) * columns)`，允許最後一頁的
  最後一列部分填入，spec §4.2「不得出現半列」改寫為「起點對齊整列；最後一頁
  內容可不足一頁」。**為什麼不那樣做**：不改版面常數（頁面高度永遠完整）；
  不改 `ScrollBy`/`EnsureSelectionVisible`（上界修正後其公式自動正確）。
  **NR-086（MEDIUM，spec §4.1 違反）**——`ParseHotkey`（`settings_editor.cpp:
  177-236`）只拒絕格式錯誤與 `Win` 組合，`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc`
  通過解析後 `RegisterHotKey` 對它們照樣成功（shell 用自己鍵盤處理實作
  `Alt+Tab`，不在 SAS 保留清單），設定頁一存就把 Windows 工作切換／開始
  功能表劫持掉。「註冊失敗一律拒絕」的防線攔不到 OS 層不失敗的組合，攔截點
  在解析端。修法：`ParseHotkey` 組出組合後對這三組回 false（走既有
  `HotkeyRejectedNotice`），`hotkey.cpp` 一字不改。**為什麼不那樣做**：不擴充
  保留清單（`Alt+F4` 是應用層慣例、`Ctrl+Alt+Del` 由 OS 拒絕、`Win` 已在解析端
  拒絕）；不加新設定/文案。**未成 item 的低嚴重度發現**（記錄備查）：(1)
  啟動時 `RefreshPanelSnapshot` 對「cache 載入的 snapshot」先跑
  `g_usage->Reconcile`，若 on-disk cache 比 usage 舊（cache 寫入失敗被
  `SaveCatalogCache` 忽略回傳值、或重建中斷）會在首輪完整 rebuild 前刪掉
  其實仍存在於 catalog 的 usage 紀錄——鏈路罕見（需 cache 落盤失敗＋該期間
  有 launch），spec §4.2 也允許對 snapshot 對帳，未達 item 門檻；(2)
  `PrewarmEmptyStatePage` 不把 key 寫入 `g_pending_icon_keys`，prewarm 與
  Render 可能重複 post 同一 key（結果都進 LRU，僅多一次 Shell fetch，NR-037
  交接區已載）。(3) 並行 agent 同時撰寫 item 造成編號碰撞：我方原取 NR-083
  與對方 `NR-083-catalog-index-hotkey-path.md`（熱鍵路徑 catalog 索引，
  效能類，內容零重疊）相撞，依 AGENTS.md「確認 docs/work-items/ 無該編號檔」
  規則重編為 NR-084～NR-086，對方 item 未動。全部 3 個 item 無依賴、皆
  `ready`。未 commit。

- 2026-08-08（NR-081～NR-082 ready，第五次全 repo 稽核產出）：backlog 清空後對整個
  repo 做第五輪稽核（正確性／穩健性、spec 對照），主 Agent 從頭到尾重讀
  `main.cpp`（3,270 行）與全部模組，聚焦先前四輪未覆蓋的「世代取代時機」與
  「clamp 後幾何」兩個邊角，收斂成 2 個 item。**NR-081（HIGH）**——`ShowPanel`
  的 on-demand AppsFolder refresh（`main.cpp:1960-1963`）不檢查 `IsRebuildInProgress`，
  而 `BeginGeneration` 的新世代會把進行中完整重建的 StartMenu／UserFolder 結果當
  stale 丟棄（`catalog_refresh.cpp:90-92`）；`last_appsfolder_success_ms_` 初始 0
  （`catalog_refresh.h:102`）使機器開機 >10 分鐘時**第一次** ShowPanel 就觸發——
  首啟動競賽（按 Alt+Space 早於背景完整重建完成）或 Ctrl+R 後立刻按 Alt+Space，
  都會讓 merged snapshot 收縮成只剩 Store App、`RefreshPanelSnapshot` 對空掉的
  來源跑 `g_usage->Reconcile` **永久刪除 usage.tsv 紀錄**、`SaveCatalogCache` 把
  收縮版寫進 cache，且無 watcher 事件可自癒。修在 coordinator 的
  `ShouldRefreshAppsFolder` 加 `IsRebuildInProgress()` 守門（可測、單一出口、
  ShowPanel 一字不改）。**為什麼不那樣做**：不改 ShowPanel guard（守門分散到呼叫端）；
  不 baseline `last_appsfolder_success_ms_`（會壓掉「啟動重建的 AppsFolder 失敗→
  下次 ShowPanel 重試」的 §FR-008 語意）；不讓 StartRebuild 合併進行中來源結果
  （watcher 路徑的取代有 `pending_` 自癒，唯一該改的是「不該取代的時機」）。
  **NR-082（MEDIUM）**——NR-064 只補 `y >= footer_top` 幾何上界，前提「grid 4 列／
  list 8 列都止於 456 DIP」只在面板全高成立；`ClampWindowSize` 在小螢幕＋高 DPI
  把面板夾短後，最後一列提早結束，`footer_top` 到 client 底邊的空白帶被
  `CellAtPoint` 當有效 index，單擊啟動看不見的 App（§4.8 明令命中僅限實際繪製的
  格／列）。修法：在 `CellAtPoint` 的 grid/list 分支各加
  `row >= ViewportRows()` 下界，與既有三條幾何檢查並存——**覆寫** NR-064
  Decisions §1「不寫第二套可見列數判斷」的決策（新證據：clamp 後繪製範圍不再
  止於 footer），其餘 NR-064 決策（修在唯一入口、呼叫端不改、不加測試抽象）
  沿用。**未成 item 的低嚴重度發現**（記錄備查）：高度被 clamp 時 footer 鍵位框
  （462~482 DIP）落在 client（約 437 DIP）之外被裁掉，屬 `ClampWindowSize` 與
  footer 幾何的版面設計決策，非 hit-test 範圍；`PrewarmEmptyStatePage` 不把 key
  寫入 `g_pending_icon_keys` 使 prewarm 與 Render 可能重複 post 同一 key
  （結果都進 LRU，無 reflow，僅多一次 Shell fetch）。兩者都未達 item 門檻。
  兩個新 item 皆無依賴、皆 `ready`。未 commit。

- 2026-08-08（NR-072～NR-080 planned，第四次全 repo 稽核產出）：backlog 清空後對整個
  repo 做了第四輪四軸稽核（正確性／穩健性、spec 對照、執行緒與生命週期、不受信輸入），
  由三個平行子 agent 分別深讀 main.cpp 全檔（3,100 行）、catalog/settings/pins/usage/
  storage、icons 子系統，主 Agent 逐一重讀原始碼驗證後收斂成 9 個 item。**排序依「先修
  使用者資料損失、再修使用者看得到的中斷、再修資源與穩健性、再修 latent」**。逐項決策
  與「為什麼不那樣做」：**NR-072**——`RefreshPins`（`main.cpp:1141-1143`）在 `Load()`
  後無條件 `Reconcile`＋`Save`；`PinStore::Load` 對 `NewerSchema` 回傳時 `pins_` 已空
  （`:27`），對 `Corrupt` 回傳時保留部分解析列。於是較新 build 寫的 `favorites.txt`
  （schema=3）被空 `schema=2` 檔覆寫（§10.4「不覆寫原檔」直接違反，pins 是 30 天保留
  契約的使用者資料），損壞檔的合法前綴 pin 被當新真相落盤、損壞列之後的 pin 從 live
  store 消失（只剩 `.corrupt`）。此路徑在每次開面板（ShowPanel→RefreshPanelSnapshot）
  與每次 rebuild 都跑。修法：host 只在 `Loaded`/`Missing` 才 Reconcile＋Save；
  `PinStore::Load` 的 `Corrupt` 回傳前 `pins_.clear()`（兌現 `pin_store.h:24-25`
  「非 Loaded 則空」契約）。**為什麼不那樣做**：不把守門塞進 `Save()`（Save 是純
  序列化器，不知道 Load 結果）；不改 balloon 文案（修好後文案才為真）；不為
  `RefreshPins` 加測試 seam。**NR-073**——`kRebuildDoneMessage` 對每份來源結果無條件
  `RefreshPanelSnapshot()`＋`SaveCatalogCache()`，而 coordinator 只在整代完成才重算
  merged（`catalog_refresh.h:52-54`）；前 1..n-1 份結果每次把 `selected_`/`first_visible_`
  重置（rebuild 中瀏覽舊 snapshot 的選取被偷走 2~3 次）、並在 UI 執行緒重寫
  favorites.txt/usage.tsv/多 MB catalog.cache。修法：兩者收進 `GenerationComplete`
  區塊，`InvalidateRect` 留在外。**為什麼不那樣做**：不加 snapshot 深比較（完成時重算
  一次正確且必要）；不改 coordinator。**NR-074**——`WatchLoop` 對持續性錯誤（root 被
  拔、ACCESS_DENIED）每秒 `PostMessageW(full-rescan marker)`，UI 端繞過 debounce 每
  秒開新 generation（`StartRebuild` 先 join 上輪）——恆定 ~1 Hz 重建忙碌迴圈，違反
  NFR-002「事件驅動、不輪詢」。修法：錯誤連續期只報一次（`reported` 旗標，成功路徑
  重置），Sleep 退避保留。**為什麼不那樣做**：不取消 watch（root 恢復要自動續監）；
  不改 UI 端（無從得知錯誤持續）。**NR-075**——兩缺口同屬「IconStore 記憶體無界」：
  (1) `Compact` 成功 replace 後 `MapFile()` 失敗（`:614-616`）留下 `Ready`＋null
  view，`Put`（`:344` 只查 state）繼續收、`Flush`（`:364`）拒而不清 → `pending_`
  再次無界增長（NR-068 守衛形狀的缺口，NR-050 交接區自指此邊緣但只堵 Flush 端）；
  (2) `DecodeHeader` 的 `payload_end` 上界只有檔案大小，CRC 全對的 GB 級 `icons.cache`
  被當 Ready，`Lookup` 單筆最多拷近 4 GB 進 vector。修法：`Compact` 失敗出口降級
  `Disabled`＋`pending_.clear()`（「Ready ⟺ 活 view」成對，`Put`/`Flush` 守衛自動
  完整）；`DecodeHeader` 加 pack 預算上界，32 MiB 常數單一來源放純值層
  `icon_pack_format.h`、`icon_store.h::kMaxPackBytes` 引用它。**為什麼不那樣做**：
  不在 `Put` 再堆一層 view 檢查（修根源而非症狀）；不換格式、不加列舉值。**NR-076**——
  §11「Worker 發生例外→UI 不崩潰、捕捉邊界、記錄並丟棄」完全未實作：`IconWorker::Run`
  與 `StartRebuild` lambda 都無 try/catch，`std::bad_alloc`（GB 級 Lookup 拷貝等）即
  `std::terminate` 殺掉常駐 process。修法：兩處 `catch (...)`——icon worker 照常 post
  空 bitmap（清 pending key、維持 fallback），rebuild 設 `result->failed=true` 走既有
  `ApplySourceFailure`（保留舊結果）。**為什麼不那樣做**：不分類例外型別（都是丟棄）；
  rebuild 枚舉器不加可拋 seam（`source_ok`/`failed` 已覆蓋）；icon worker 用 fake
  provider 可測。**NR-077**——`kRebuildDoneMessage`/`kIconReadyMessage` 把 `lParam`
  直接 reinterpret_cast 成堆積指標後解參考：任何同 integrity process 可 `PostMessage`
  到我們 HWND（UIPI 只擋較高者）用 `lParam=0`/垃圾值當場 crash 常駐 tray 程式——整條
  訊息路徑唯一不受信輸入 crash 向量。修法：UI 側 token registry（mutex 保護的
  `map<token, unique_ptr>`），訊息只扛指標位址作 token，接收端查得到才用、查不到
  `return 0`；`WM_DESTROY` 排空後 clear 兩 map。**為什麼不那樣做**：不驗 PID（要
  跨 integrity 判斷成本與誤報）、不引遞增計數器（指標位址已唯一）、不記錄未知 token
  （防日誌灌爆）。**NR-078**——§4.7 鍵盤表「Context Menu／Shift+F10 開啟項目選單」
  未實作（grep 零 `VK_APPS`/`WM_CONTEXTMENU`），NFR-006「鍵盤可完成全部核心操作」
  違反：Pin/Unpin/Remove from recent/Properties 只能滑鼠。修法：把 `WM_RBUTTONDOWN`
  的項目分支抽成 `ShowItemMenu(window, cell, screen_pos)` helper，鍵盤路徑在
  `SearchEditProc` 攔 `VK_APPS`／shift+F10 用 `SelectionIndex()` 呼叫。**為什麼不
  那樣做**：不攔父視窗 `WM_CONTEXTMENU`（會壞掉搜尋框滑鼠右鍵的剪貼簿選單，§4.8/4.9）；
  不新增選單結構/字串。**NR-079**——`LoadCatalogCache` 把 `NewerSchema` 與 `OlderSchema`
  一起 `return false`，host 的 `SaveCatalogCache`（`:2343`）在首次 rebuild 就把較新
  build 的 `catalog.cache`（schema=3）覆寫成 `schema=2`；§10.4 明文快取類檔案較新
  schema「不覆寫原檔、停用該快取」。`icons.cache` 已正確（NR-035），catalog.cache 是
  唯一漏網。修法：`LoadCatalogCache` 加 `bool* newer_schema` 出口，host 啟動時設
  `g_catalog_cache_disable_writes`、`SaveCatalogCache` 守門 no-op 整段 run。
  **為什麼不那樣做**：`OlderSchema` 維持 NR-047 的重建覆寫（合法可重建檔，非本 item）；
  不顯示通知（快取類不通知）。**NR-080**——`SettingsStore::Load`/`UsageStore::Load`
  對中段損壞檔洩漏部分狀態：有效前綴列先寫入 `out`/`records_`，損壞列才回 `Corrupt`，
  違反兩檔 header 的「非 Loaded 則空」契約（`settings_store.h:49-50`、
  `usage_store.h:21`）；host 直接採用（`main.cpp:2927-2942`）→「採預設值」balloon 與
  實際 live 設定矛盾、partial usage 進 ranking。修法：`Corrupt` 回傳前 `out =
  DefaultSettings()`／`records_.clear()`，與 NR-072 的 pin 契約一致。**未成 item 的
  低嚴重度發現**（記錄備查）：(1) 關閉時 icon worker `Stop()` 的 `join()` 無限等待——
  卡死的 Shell extension 可讓 §9.4「關閉不得無限卡住」失效，但 Shell 呼叫不可中斷、
  沒有乾淨的 0.5~2 天修法（process 結束時 OS 會回收執行緒），先記限制不開 item；
  (2) `FindFirstFileW` 對 >MAX_PATH 深路徑靜默回空（兩個枚舉器），頻率極低且 `\\?\`
  前綴要一路改到 watcher 與 settings 驗證，超出 item 尺寸，有量測需求再開；(3)
  `UsageScore` 未實作 §4.6 公式（lifetime total＋bonus，缺 7d×3 項）——刻意註解
  的偏差，修正是 schema bump 屬產品決策，需使用者拍板，不逕自開 item；(4)
  `SettingsEditor::Apply` 的回滾 swap 結果被丟棄（存檔失敗＋回滾也失敗的雙重邊緣，
  主路徑 FR-002 register-new-first 正確）；(5) `PreserveCorrupt` 的
  `MOVEFILE_REPLACE_EXISTING` 會覆寫已存在的 `.corrupt` 備份（診斷工件層級，非使用者
  資料）；(6) 死碼 `g_last_hotkey_error` 與 `CatalogRefreshCoordinator::SourceEntries`。
  第三輪的低嚴重度 trio（icon 驅逐只清記憶體、idle/final flush 傳空 pinned 清單、
  grid footer Alt+1~4 vs 10 格）維持不變。全部 9 個 item 無依賴、皆 `planned`；
  NR-073/NR-079 共用 `main.cpp:2343` 一帶、NR-076/NR-077 都動 `icon_worker.cpp`
  送訊端——可平行，但同一 agent 分批時建議依序。未 commit。

- 2026-08-07（NR-063～NR-070 ready，第三次全 repo 稽核產出）：backlog 清空後對整個 repo
  做了第三輪四軸稽核（正確性／穩健性、spec 對照、執行緒與生命週期、不受信輸入），
  由三個平行子 agent 分別深讀 main.cpp 全檔、catalog/settings/pins/usage/storage、
  icons 子系統，主 Agent 逐一重讀原始碼驗證後收斂成 8 個 item。**排序依「先修使用者
  看得到的、再修會漏資料的、再修硬體相容、再修資源與 latent」**。逐項決策與「為什麼
  不那樣做」：**NR-063**——`ApplySourceFailure` 是死碼：`StartRebuild` worker 從不設
  `result->failed`，三個枚舉器對來源級失敗只能回傳空清單（`start_menu_catalog.cpp:231-247`
  COM 不可用即回空；`AppsFolderEnumerateResult` 只有子項目級 `failed_items`），所以
  §FR-008「單一來源失敗時保留該來源舊結果」從未生效——一次性 COM 失敗會把該來源的
  app 從面板抹掉，且空結果也記 `RecordAppsFolderSuccess`，把 10 分鐘 staleness 重試
  壓掉；持續性失敗下受影響 pin 的 retention clock 不再刷新，30 天後被丟棄。修在枚舉器
  邊界（各回報 `source_ok`，UserFolder 不改——「資料夾不存在→空」是正確語意），worker
  保持薄轉接（`failed = !source_ok`），失敗不記成功。**為什麼不那樣做**：不做 worker 內
  重試（回到輪詢，違反 event-driven）；coordinator 一字不改（merge/failure 語意已對，
  `catalog_refresh_test` 的 `TestFailureKeepsOldSnapshot` 等已證明）；不加 UI 通知
  （保留舊資料就是正確行為）。同區塊順手修 `PostMessageW` 失敗洩漏 `RebuildResult`
  （照 `icon_worker.cpp:158-161`）與 `generation_complete` 名實不符（改名
  `result_applied`、`OnRefreshComplete` 改在 `GenerationComplete` 時觸發）。**NR-064**——
  `CellAtPoint` 命中範圍大於繪製範圍：無 `y >= footer_top` 下界（list 點 footer 算出
  `first+8` 啟動第 9 筆未繪製結果；grid 算出 `first+24+col`），list 無右界，grid 左緣
  因整數除法向零取整（-7/112=0）命中第 0 欄——點一下 footer／邊緣就啟動一個看不到的
  App。修在 `CellAtPoint` 唯一入口補三個界限，Render 與所有呼叫端不改；不加測試
  （吃 g_model＋HWND，NR-060 明載不為測試點發明抽象）。**NR-065**——`ApplySourceResult`
  無條件清 `pending_[source]`：掃描途中抵達的事件（`last_event_ms_` 已更新、timer 重設
  到 T1+500）在 T2 收尾時被清掉，T1+500 的 timer 看到 pending=false 不重建，變更永久
  漏掉直到外部觸發。修法：`BeginGeneration` 快照各 source 的 `last_event_ms_`，
  `ApplySourceResult/Failure` 只在時間戳未變時才清 pending——既有 timer 自然接住下一輪。
  不做 worker 內合併（把「掃描開始後的事件」併進掃描中的結果是狀態機複雜化的開始）。
  **NR-066**——`WM_MOUSEWHEEL` 每次 delta 獨立除以 WHEEL_DELTA、無累積器：精準觸控板
  （每則 30–60 delta）整數除法恆為 0，面板完全無法捲動。加檔案範圍 `g_wheel_delta_carry`
  （累加→取商→留餘），`lines` 與 `ScrollBy` 語意一字不改。**NR-067**——`CreateDeviceResources`
  進入守衛要求 render target 與五支 text format 全非空，但 `DiscardDeviceResources` 只
  釋放 target＋brushes，不釋放 formats（device-independent，本就不該釋放）；主題切換
  （`:1329`）與 `D2DERR_RECREATE_TARGET`（`:1770`）後重進函式，五個 `CreateTextFormat`
  無條件覆寫仍存活的全域指標——每次裝置重建洩漏 5 個 COM 物件。修法：五個呼叫各加
  `if (!g_*_format)` 守衛（與 `g_dash_style`／`g_ellipsis_sign` 同形）；不改
  `DiscardDeviceResources` 釋放清單。**NR-068**——`IconStore::Put` 只拒絕 `Disabled`，
  但 `Flush` 只在 `Ready` 消化 `pending_`；`FlushViewOfFile` 失敗降級 `ReadOnly`
  （`icon_store.cpp:492/504/520`）後，worker 每次 `Put` 都讓 `pending_` 無界增長（每筆
  完整 PNG payload，持續故障可達數十 MB）。修法：`Put` 守衛改 `state_ != StoreState::Ready`，
  三個失敗出口各 `pending_.clear()`；`ReadOnly` 契約（`icon_store.h:54`「writes
  rejected」）從此與實作一致。不加測試 seam（OS 失敗路徑不可注入，NR-050 先例）。
  **NR-069**——`GetStartupStatus` 的 REG_SZ buffer 配置 `size/2` 個 wchar：無 NUL 的
  值（RegQueryValueExW 不保證帶終止字元、HKCU Run 任何同使用者 process 可寫）→
  `find` 回 npos → `resize(npos)` 拋例外終止 process；奇數 byte 大小 → 1-byte 越界寫。
  目前無 production caller 是 latent，接上 UI 即 high。修法：buffer `+1` 預填 NUL、
  npos fallback 截斷到讀取長度；返回值不變、不加列舉值。**NR-070**——兩個手改資料檔
  缺口：`ParseUint64`（`atomic_text_file.h:206-219`）接受 `-` 前綴（C 標準 `wcstoull`
  無號回繞不設 ERANGE），`usage.tsv` 的 `-1` 被當合法載入、釘在 usage 排序頂端且
  `Save` 寫回自我永續；`PinStore::Reconcile` 的 `now - last_seen_utc` 對手改的
  `INT64_MIN` 是 signed overflow UB（usage_store 已用比較式防護、此處漏掉）。修法：
  各一行——拒絕 `-` 前綴（走既有 corrupt 路徑）、改 `last_seen_utc >= now - retention`
  比較式。**未成 item 的低嚴重度發現**（記錄備查）：icon 驅逐只清記憶體不落盤、
  重啟後復活（自我收斂，compact 會物理清除，perf only）；idle/final flush 傳空 pinned
  清單使 pin 豁免只對 HidePanel 那次有效（perf only）；grid 模式 footer 顯示
  `Alt+1~4` 但實際綁定繪製 10 格（視覺不一致，LOW）。三者都未達 item 門檻，若日後
  實測有影響再開。全 repo 其他類別（原子寫入、escaping round-trip、COM 平衡、
  mmap 邊界、執行緒 handoff、診斷日誌消毒）複查為乾淨。未 commit。

- 2026-08-07（NR-059 done）：`Render()` 515 行裡 grid 與 list 兩個分支的兩段逐字重複（「圖示或 fallback」與「空白狀態提示」）收斂成兩個檔案範圍 helper。逐字比對確認兩段**並非**完全相同：grid fallback 用 `grid_icon_needed_px`＋4 行 NR-032 註解＋`DrawText(tile, g_text_brush)` 單行，list 版用 `layout.tile_size`＋2 行 Fallback-tile 註解＋`DrawText` 換行版（`tile,`／`g_text_brush);`）；空白提示兩段只差 `kCellHeightDip` vs `kRowHeightDip` 與各自分支註解——差異即規格，只抽共用部分，其餘原樣。新增 `DrawIconOrFallback(const AppEntry&, const D2D1_RECT_F&, int, float, float)`（在 `DrawDecodedIcon` 之後，照 item 正文）與 `DrawEmptyStateHint(float row_height)`，兩者都吃 `g_render_target`／`g_icon_cache`／`g_text_brush`／`g_dim_brush`／`g_text_format` 五個檔案範圍變數（Decisions §1）；grid 呼叫點傳 `kCellHeightDip`、list 傳 `kRowHeightDip`。**drag ghost（`:1432-1452`）保留原樣**——它未命中時只畫 dim 方塊、不畫首字母也不重新請求（請求已由該格繪製發出），命中時 `DrawDecodedIcon(…, 0.6f)` 帶透明度，故不是 `DrawIconOrFallback` 呼叫點，上方加一行 NR-059 註解說明。**清尾隨空白只清 `Render()` 內**：list 分支 6 行只含 4 空格的縮排殘留整行刪除（item 範例 `:1404/:1430/:1443/:1462/:1493/:1512` 對應 `:1491/:1517/:1530/:1549/:1580/:1599`），全檔尾隨空白 0。**偏差**：Render() 從 515 降到 **449**（<450 達成，但並非 item 預期的約 450——因另刪了 ghost 上方原有空行與兩處 `rows.empty()` 分支註解）；`RequestVisibleIcon(` 計數 2（宣告＋`DrawIconOrFallback` 內各一，呼叫點只剩 1 處）；`kNoMatchingApps`／`kBuildingCatalog` 各 2（常數定義＋`DrawEmptyStateHint` 內）。呼叫次數未變：`Peek` 仍是 grid＋list＋ghost 各一次、`CreateBitmap` 只在 `DrawDecodedIcon`（命中路徑）內、`DrawText` 的 fallback/empty 各只剩一份但每繪製呼叫一次，冷熱快取路徑逐項比對與改動前相同（Performance §不變）。Release 建置無新增警告、`ctest` **23/23 全綠**。手動驗收為視覺比對（item 明文不加測試），未在本工作區實跑。未 commit。

- 2026-08-07（NR-058 done）：三個 store 的載入結果列舉終於被消費（§11「設定損壞→採預設值並通知」與 §10.4「較新 schema→顯示一次錯誤提示」從規格變實作）。新增 header-only 純函式 `src/diagnostics/load_notice.h`（`StoreLoadIssue` bitflags ＋ `StoreLoadNoticeText(unsigned)`，不含 `<windows.h>`、不碰 HWND、被單元測試直接呼叫）；三個英文句子**放在 header 跟著純函式走**（測試要斷言精確句子、文案只有 balloon 一個出口，不觸發「多畫面共用需集中」）。`main.cpp`：`settings_store.Load`／`usage.Load` 的回傳值保留於區域變數、`g_diag` 建立後（未調整初始化順序）累積 `g_store_load_issues` 並各寫一行 `settings_load`／`usage_load`／`result=<列舉名>` 日誌（只檔名＋列舉名，§FR-014）；`AddTrayIcon` 後、訊息迴圈前送出一則彙總 tray balloon（NIF_INFO 填法照 `ShowHotkeyConflictNotice` 逐欄複製），送出後清 `None`；`RefreshPins()` 接住 `g_pins->Load()`，`Corrupt`/`NewerSchema` 寫 `pins_load` 日誌＋一次性 `bool g_pins_notified` 閘門送 balloon（`Loaded`/`Missing` 照舊），`settings_dialog.cpp` 的 `Load` 只加日誌不通知（`ShowSettingsDialog` 增加 `DiagnosticLog*` 參數）。**balloon 送出點**：`AddTrayIcon` 之後、訊息迴圈之前，證明＝同 HWND＋uID 的 `NIM_ADD` 剛執行完畢、`g_tray_icon_active` 由 NIM_ADD 回傳值置真，且 `ShowHotkeyConflictNotice` 在同一點依同一假設；pin 首輪載入先於 tray 存在（啟動 `RefreshPanelSnapshot`），故 `RefreshPins` 以 `g_tray_icon_active` 判斷 tray 未就緒就把 pin 問題併入啟動彙總 balloon（手動驗收 2「多檔同時太新→單一 balloon 涵蓋兩者」由此成立）。**一處矛盾**：Scope §4「pin Missing 照舊不做事」與手動驗收 5「首次執行日誌有三行 Missing」衝突，依 Scope §4 實作 pin `Missing` 不寫日誌（首次執行得兩行 Missing），已在交接區標記請作者定奪。測試：`diagnostic_log_test` 新增 `TestStoreLoadNoticeText` 四案例（無→空、僅 corrupt、僅 too-new、兩者→兩句都在），未新增測試執行檔、未改 CMakeLists。design-spec §11 錯誤表插「使用者資料由較新版本寫入」列、§10.4 第一段補「單一 tray balloon」句。Release 建置無新增警告、`ctest` **23/23 全綠**（首輪 lifecycle_check 因 NR-049 冷啟動掃描逾時一次，重跑即過，非本 item 所致）、`ctest -R nimblerun_diagnostic_log_test` 1/1。sanity greps 全符合：四處 `Load` 無被丟棄、`load_notice.h` 無 `windows.h|HWND|NOTIFYICONDATA`、`NIF_INFO` 計數 2、design-spec `tray balloon` 於 §10.4 與 §11 各命中。未 commit。

- 2026-08-07（NR-057 done）：四個 versioned text store（`settings.ini`／`usage.tsv`／`favorites.txt`／`catalog.cache`）的讀取端從四份逐字相同的檔頭解析收斂成一個共用讀取函式。`storage/atomic_text_file.h`（寫入端 `AtomicWriteUtf8Text` 早已共用）新增五個 inline 解析器（`Trim`／`SplitLines`／`SplitFields`／`ParseInt64`／`ParseUint64`，逐字取自 pin/usage 版本）、`VersionedReadStatus` 列舉（`Loaded/Missing/Unreadable/Malformed/OlderSchema/NewerSchema`）與 `ReadVersionedLines()`（讀檔→去 BOM→切行→驗 `schema=` 檔頭→比版本→成功回傳不含檔頭的資料行；不改名、不寫入、不刪除），並刪除 pin/usage/settings/catalog 四份私有副本（含 catalog_cache 的 `wcstoll`＋`errno` 內聯版）。四個 `Load()` 檔頭段改為一次 `ReadVersionedLines`＋一個 `switch`，逐案對回今天的回傳值與 `PreserveCorrupt` 行為：pin/usage/settings 走同一個 default arm（`Unreadable/Malformed/OlderSchema`→`PreserveCorrupt`＋`Corrupt`；`Missing`→`Missing`；`NewerSchema`→`NewerSchema` 不改名）；`catalog_cache` 唯獨 `Malformed` 才 `PreserveCorrupt`，`Missing/Unreadable/Older/Newer` 一律不改名直接 `return false`（NR-047 註解原樣保留）。資料行迴圈本體一字未動（欄位數、`UnescapeText`、重複 id 規則、`out.clear()` 時機），僅起點 `i=1`→`i=0`（檔頭已剔除）。順手修正兩處：`search_engine.cpp` 手寫 `StartsWith` 改 `std::wstring_view::starts_with`（C++20）；`PreserveCorrupt` 先驗 `INVALID_FILE_ATTRIBUTES` sentinel（`GetFileAttributesW` 失敗原本會被誤判成目錄而提早 return）。Scope §4 選擇「收進共用標頭」：`settings_editor.cpp` 第 3 行本就直接 include `<windows.h>`（熱鍵 `VK_*` 常數需要），引入共用標頭不會新增 `<windows.h>` 依賴，item 決策規則的「留在原地」條件不成立，故刪除該檔私有 Trim。測試：`settings_store_test` 新增一組 `ReadVersionedLines` 直接案例（不存在／壞 UTF-8／空檔／無 `schema=`／非整數版本／較舊／較新／正常，正常案例驗證回傳行不含檔頭），未新增測試執行檔。Release 建置無新增警告（clean 基準與改後皆零 warning）、`ctest` **23/23 全綠**。淨刪除：8 檔、244 insertions／374 deletions（淨 -130 行）。sanity greps 全符合：五個解析器各只剩 `atomic_text_file.h` 一份定義；`kSchemaPrefix` 四個 store 只剩「定義＋Save 字面」、比對邏輯只在標頭；`ReadVersionedLines` 1 定義＋4 呼叫點；`StartsWith` 於 `search_engine.cpp` 零命中；`INVALID_FILE_ATTRIBUTES` 1 行。偏差：PS7 的 `Select-String` 無 `-Recurse`，sanity grep 改用 `Get-ChildItem … -Recurse -Include *.cpp,*.h | Select-String`；ok 測試 fixture 刻意不含結尾換行（結尾 `\n` 會讓 `SplitLines` 多產一個空行，store 資料迴圈本就會跳過空行，僅 fixture 調整）。未 commit。

- 2026-08-07（NR-057／058／059 ready）：第二次全 repo 稽核（ponytail 過度設計 ＋ AGENTS 工程規則 ＋ design-spec 流程一致性）產出三個 item，backlog 從空的回到三項。**NR-057**：四個 store（`settings.ini`／`usage.tsv`／`favorites.txt`／`catalog.cache`）的讀取端有四份逐字相同的檔頭解析，外加 `Trim`×4／`SplitLines`×3／`SplitFields`×3／`ParseInt64`×2，約 130 行複製貼上；漂移**已經發生**（`catalog_cache.cpp` 那份沒 `Trim` 檔頭、沒長度檢查、用 `wcstoll`＋`errno`），而這條路徑決定的是「要不要把使用者資料改名成 `.corrupt`」。收斂成 `ReadVersionedLines()` ＋ 解析器搬進既有的 `storage/atomic_text_file.h`（寫入端 `AtomicWriteUtf8Text` 早就共用了，本 item 只是補上讀取端）。**為什麼不那樣做**：不抽 `Store` 基底類別（四者共用的只有檔頭，抽基底會逼出虛擬函式）；共用函式**不呼叫 `PreserveCorrupt`、不決定舊 schema 的處置**，只回報狀態，因為 `catalog_cache` 對讀取失敗與舊 schema 都刻意不改名——把這個差異塞成 `bool` 參數就是把產品決定藏進旗標；**不預先蓋 migration 框架**（`kSchemaVersion` 只有 catalog.cache 升過，其餘無事可做，要升版時再開 item）。順手處理 `search_engine.cpp` 手寫的 `StartsWith`（C++20 有 `starts_with`）與 `PreserveCorrupt` 把 `INVALID_FILE_ATTRIBUTES` 誤判為目錄的 sentinel bug。**NR-058**：三個 store 精心回傳的載入結果列舉**四個呼叫端一個都沒接**（`main.cpp:2681`／`:2695`／`:922`、`settings_dialog.cpp:367`），所以 §11「設定損壞→採預設值並通知」與 §10.4「較新 schema→顯示一次錯誤提示」兩條規格從未實作，日誌也沒有一行——使用者只會看到「我的設定自己不見了」。改為 balloon 通知＋日誌。**為什麼不那樣做**：不用 MessageBox（§11 明文禁止搶焦點的連續提示，而載入發生在開機自動啟動當下，是最糟的彈窗時機），用 tray balloon（§11 允許，且 `ShowHotkeyConflictNotice` 已是現成範本）；一個 process **至多一則、多檔彙總**（連續三則就是 §11 禁止的連續提示的 tray 版本）；`Missing` 不通知（首次執行是正常狀態）但**要寫日誌**（日誌沒有打擾成本，而「檔案本來就不存在」正是排查時最想知道的）；快取類檔案不通知（§10.4 明文「對使用者不可見」）；`RefreshPins()` 每次開面板都 `Load()`，所以要有一次性閘門否則每按一次 `Alt+Space` 就彈一則；決策文案做成不含 `<windows.h>` 的純函式以便測試（AGENTS：core logic independent of HWND），Win32 出口留在 `main.cpp`；不做「還原 .corrupt 檔」的 UI（`.corrupt` 就在資料夾裡，設定視窗已有 Open log folder）。**NR-059**：`Render()` 515 行裡 grid 與 list 兩個分支各有一份逐字相同的「圖示或 fallback」（`:1278-1296` vs `:1444-1461`）與「空白狀態提示」（`:1369-1381` vs `:1516-1528`），差別只有矩形與所需像素尺寸；危險在於 fallback 那段同時驅動 NR-032 的「一個 key 只請求一次」去重不變式，改一漏一的症狀是某個 DPI 下多打擾 Shell 幾次，**不會有編譯錯誤也不會有測試失敗**。抽兩個檔案範圍 helper，像素級零變更。**為什麼不那樣做**：**不切分 `main.cpp`／不把渲染搬進 `src/ui/`**——兩個 helper 吃五個檔案範圍變數，搬走的成本是把它們公開出去，在渲染狀態還是全域的前提下該做的是縮短函式不是搬走函式；不合併 grid／list 兩個分支本身（幾何、裝飾、z-order 都不同，硬合會生出吃 `bool is_grid` 的函式）；drag ghost 不納入共用函式（它未命中時刻意不畫首字母也不重新請求）。稽核中另外量到但**刻意不開 item** 的兩件事：`DrawDecodedIcon` 每格每幀 `CreateBitmap`、`EN_UPDATE` 每次按鍵整窗失效——兩者都是效能假設而非重複，且目前無任何量測支持，要做先量再開「量測並決定」的 item，不得夾帶進宣稱零變更的項目。前次交接列出的 alias／設定頁資料管理／UserFolder `.lnk` 解析等候選**不在本次範圍**：那些是功能 backlog，本次只產出稽核發現的修補項。

- 2026-08-06（NR-056 done）：六處文件與程式碼的偏差全部收斂，設計 spec 的每一條重新描述實際出貨行為。**§1 `docs/testing.md`**：以 `ctest -N` 實測 23 項重寫測試敘述（unit／`lifecycle_check.ps1` 整合／`release_evidence.ps1` 發布證據三類、各自何時跑），刪掉 Phase 0 probe 手動冒煙測試，換成 7 條現況「操作 → 預期」冒煙測試；全 repo `Phase 0|probe|fake app` 逐處判斷：testing.md 與 performance-baseline.md 的殘留已處理，其餘（roadmap 階段名、設計規格 Phase 0 backlog、requirements、NR 歷史文件、AGENTS.md Current baseline）是準確歷史敘述保留。**§2 `docs/performance-baseline.md`**：只回填真量到的——Idle working set 37.2 MiB、Idle private bytes 7.7 MiB（`release_evidence.ps1` 2026-08-07，handle 394 記入 notes）、Filter 500 apps 603 µs（5,000 筆、`L"e"`，標註規模大於門檻故為保守上界，2026-08-06）；其餘 5 列維持 `Not measured` 並各補一句缺什麼（idle CPU 抽樣、可見面板 census、冷啟動計時器、熱鍵到首幀計時器、icons.cache 實測），表格列與單位未動，刪除末段 stale 的「Phase 0 empty-window probe」句。**§3 design-spec 四處**：§10.3 補 AppsFolder resolved-path 規則、UserFolder `.lnk` 以自身正規化路徑為 identity（舊條目「resolved target」與程式碼矛盾，以新條目取代）；§4.8 右鍵選單補「自常用清單移除」「內容（Shell properties verb）」兩項並補面板空白處拖曳（含「搜尋輸入框內的按壓一律屬於文字選取，不觸發拖曳」句，先寫進規格）；§4.10「關於」補行為描述（產品名＋版本的訊息框）。**§4**：`SearchEditProc` 的 `WM_LBUTTONDOWN` case 整個移除（含 `DragDetect`＋原 :1901 的 `SendMessageW(…WM_NCLBUTTONDOWN, HTCAPTION, 0)`），搜尋框回到原生 EDIT 的 caret／焦點／滑鼠選取，面板空白處拖曳保留。**§5**：About 空實作（`ponytail: about dialog` 註解）改為 `ShowAboutDialog`——`MessageBoxW(owner=面板 HWND, MB_OK|MB_ICONINFORMATION)`、英文、含 `dialog_strings::kTitle`（既有集中字串）＋版本；RC 原無 `VS_VERSION_INFO`，本 item 新增 `0,1,0,0`（與 manifest 一致，成為版本唯一出處），main.cpp 用 `GetFileVersionInfoW` 系讀取、無硬寫第二份版本，CMake 唯一變更＝NimbleRun 連結 `version.lib`；`build\NimbleRun.exe` 實測 FileVersion `0.1.0.0`。**§6**：`release_evidence.ps1:22` 的 `Get-CmdVersion` 參數名 `$args`（PowerShell 自動變數，`@args` 展開呼叫者空陣列、`--version` 從未送出）改名 `$Arguments` 並同步 `@Arguments`，全腳本再無 `$args`；重新執行後 `docs/release-evidence.md` 的 cmake 4.4.2／ninja 1.13.2／clang 22.1.8／ctest 4.4.2 皆真實版本字串。**§7**：版本讀取整段是 Win32 呼叫，未抽純函式，由手動驗收覆蓋。Release build 無新增警告、`ctest` 23/23 全綠、`release_evidence.ps1` exit 0。偏差：§10.3 舊 `.lnk` 條目以新條目取代（不採「補述」以免自相矛盾）；item 提供的註解含 `$args`／`HTCAPTION` 與 agent check 衝突——ps1 註解改寫避開 `$args` 字面，main.cpp HTCAPTION 註解保留原文；`Usage|error` grep 命中測試名 `recent_usage` 的子字串屬誤報。未完成：6 條人工手動驗收未執行。

- 2026-08-06（NR-055 done）：把 `tests/CMakeLists.txt` 的 22 份逐字相同測試 target 樣板（同六個 compile definitions、同 `if(MSVC...)/elseif(Clang)` 警告與連結選項分支、同 `add_test`，共 25 行 ×22）整併成**一份清單＋foreach 迴圈**。逐字讀完 22 個區塊並程式化比對後確認：六個 definitions 與兩個分支的所有選項**全部逐字相同**，每個 target 只有兩個自由度（來源檔、連結項）；`nimblerun_panel_model_test` 是**唯一例外**——它的 `add_test` NAME 是 `nimblerun_list_vertical_slice_test`（與執行檔名不同，供 `ctest -R vertical_slice` 命中），不符迴圈的 `add_test(NAME ${test_name} COMMAND ${test_name})`，故留在迴圈外原樣保留並加註解說明；其餘 21 個進迴圈，清單順序與原檔一致，分隔符用 `|`，NR-032/034/035/018 的四個命名理由註解隨行保留在清單內。**兩處必要調整**：(1) 例外 target 必須留在**原註冊位置**（`recent_usage_test` 與 `icons_cache_test` 之間）否則 ctest 的 `Test #N:` 編號會位移、`ctest -N` 輸出不再逐位元相同，故迴圈用 `list(SUBLIST ... 0 10 / 10 11)` 拆成 head/tail 兩個 pass 包住例外塊；(2) 原檔實為 **566 行**（item 寫 456，實際多出 110 行），故「降到約 80 以下」未達成——改後 126 行，因拆兩個 pass＋例外塊。§Scope 3 逐位元比對：configure `build_before`／`build_after`（Release＋`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`），`ctest -N` 輸出在正規化建置目錄名後 **Compare-Object 零輸出**（23 個測試名稱與順序完全相同）；`compile_commands.json` 在正規化 `build_before`→`build_after` 後**整檔逐位元相同**、22 個測試 command 字串零差異。比對完刪除兩份基準檔與 `build_before`/`build_after`。正式驗證：Release clean build（85/85、exit 0、無新增警告）、`ctest` **23/23 全綠**、`ctest -N` 23 項。sanity greps：行數 126（非 <80，因例外塊＋雙 pass）、`target_compile_definitions` 3（head pass＋例外＋tail pass）、`add_executable` 3、`add_test` 5（兩 pass＋例外＋lifecycle＋註解字串）、`_test\|` 21（迴圈清單；例外 `nimblerun_panel_model_test` 不在清單故 grep 不含它）、選項集合未變（各出現於兩 pass＋例外三處）；`git diff --name-only` 只動 `tests/CMakeLists.txt`（99 insertions / 528 deletions）。未改任何測試名、未增刪合併測試執行檔、未動 `tests/integration/`／`tests/release/` 註冊、未動根 `CMakeLists.txt`、toolchain、任何 `src/`、任何 `tests/*.cpp` 或 `.ps1`、未抽 `Expect()` 共用庫、`search_engine_test.cpp` 的 pragma 未動。未 commit。

- 2026-08-06（NR-054 done）：記錄檔搬到 §10.1 指定的 `%LOCALAPPDATA%\NimbleRun\logs\`（`main.cpp` 的 `DiagnosticLog` 建構處改用 `JoinPath(DefaultSettingsDir(), L"logs")`，**重用既有根目錄來源**，不自己拼路徑；`.log.1` 由 `JoinPath` 自動跟著搬；舊檔留原地不搬不刪不讀）；`Write` 的**整個本體**（`EnsureDirectory`→大小檢查→輪替→開檔 append→寫入→關檔）包進 `std::lock_guard`（`mutable std::mutex write_mutex_`），修掉 UI 執行緒與 icon worker（`IconStore::WriteLog`）之間的「輪替與開檔交錯導致診斷行落進剛被搬走的檔」競賽，`DiagnosticLog` 明寫複製建構／賦值 `= delete`；因 **`EnsureDirectory` 只建一層**而 `logs` 在根目錄下一層，`Write` 內多呼叫一次 `EnsureDirectory` 傳父目錄——確認過根目錄在新安裝時於 icon worker 的第一次寫入前**不保證存在**（`SettingsStore` 建構只讀不建，worker 的 `IconStore::Open` 寫 `created` 事件早於任何 save）；設定頁加「Open log folder」按鈕（`OpenLogFolderButton`、`IDC_OPEN_LOG_FOLDER`、照既有 `ClearUsageButton`／`ResetSettingsButton` 形狀，`.rc` 因 320 寬單列塞不下 5 顆改為動作鍵一列＋Cancel/OK 第二列），行為是先 `EnsureDirectory(log_directory)` 再用 `ShellExecuteExW(lpVerb=open)` 開目錄、**絕不組命令列**、失敗走既有 notice 機制（新增 `OpenLogFolderFailedNotice`），按鈕不標 dirty、不參與 Apply/rollback；`log_directory` 以全域 `g_log_directory` 在 wWinMain 設一次，與設定頁共用同一變數。測試：`diagnostic_log_test` 新增子目錄（`<temp>\logs` 不存在時寫入自動建）、輪替仍留在 `logs\` 內（根目錄無任何 `.log`）、兩執行緒併發各 2000 行（約 136 位元組／行，總量 544 KB 落在 512 KiB 單次輪替與 1 MiB 二次輪替之間→**實際觀察到一次輪替**且總行數**恰為 4000**，每行 `\t` 分欄、`\n` 結尾、無交錯截斷）；`settings_editor_test` pin 住 `OpenLogFolderButton` 英文文字（既有「fresh editor is not dirty」斷言即守門員：editor 沒有為此新增任何 setter）。design-spec §FR-014 補入 item 提供之「記錄寫入須可由多執行緒安全呼叫…」句；§10.1 已寫 `logs\`（審計正確），不需改，亦無「舊版本檔案位置」段落故不補。sanity greps 全符合：`logs` 於 main.cpp 命中建構處＋共用變數、`lock_guard|write_mutex_` 於 Write 本體開頭一處、`= delete` 明寫、settings_dialog 僅 `ShellExecuteExW`、記錄呼叫端皆短字串常數、`SetTimer|Sleep|thread` 於 diagnostic_log.cpp 零命中。Release clean build 無新增警告、`ctest` 23/23 全綠、`ctest -R nimblerun_diagnostic_log_test|nimblerun_settings_ui_test` 皆過。**兩處必要調整**：(1) `settings_dialog.h` 的 `ShowSettingsDialog` 增加 `log_directory` 參數（按鈕需要目錄；`kSettingsMessage` 在 `WindowProc` 而 `log_directory` 原為 `wWinMain` 區域變數，故提為全域共用）；(2) `.rc` 底排按鈕由一列 4 顆改為兩列（動作鍵列＋Cancel/OK 列），對話框高度 366→384——這是「一列塞不下」的版面必然結果，非新增設計。未完成：6 條手動驗收屬人工操作（含「Open log folder」在從未寫過記錄的乾淨狀態開啟、按鈕不觸發設定已變更提示），Agent 不執行；`settings_dialog.cpp` 的 `ParseCountText` 與 startup rollback 未測缺口按 item 要求記入交接區、另案處理。

- 2026-08-06（NR-053 done）：空狀態（未打字）的格狀畫面依 §4.2 完整實作：**規則 2** 非釘選區改依 `usage_score` 排序、**規則 3** 資料不足時用 catalog 字母序補滿一頁。分數直接讀 `AppEntry::usage_score`（`StampRankingFields` 已貼好），**不查 `UsageStore`、不改 `UsageStore::Recent()`**（grep 確認它只有 `RefreshPanelSnapshot` 一個呼叫端，且「newest first、不填充」契約是對的，呈現順序屬 model）。排序 comparator 採**複製** search_engine 的 tie-break（分數高→名稱短→大小寫不敏感名稱→stable id，去掉 pinned 層），理由：共用需抽具名函式並改動 `SearchApps` 的比較子主體，item §1 明訂「若共用需改動 SearchApps 就不要共用」，故在 `panel_model.cpp` 檔內複製並以註解指名與 `search_engine.cpp:170-181` 同步；`DisplayNameKey` 同時鏡像 search 的 `NormalizedName` 回退規則。**容量常數用 `kIconCacheWorkingSetItems`（icon_cache.h，註解即「one full page of cells」）**，panel_layout 只有 `kGridColumns` 沒有列數常數，故未走欄×列，未製造第三個 24。填充排序選 **`std::partial_sort`**（`ponytail:` 註解：top-N 專用 stdlib 演算法，與全量 `sort` 同長度，只有空狀態才跑）。`std::stable_sort` 起點 `rows_.begin() + recent_start_`，釘選區（§FR-011）永不排序、`recent_start_` 不變（NR-046 拖曳／NR-041 標記安全）；填充落在 `recent_start_` 之後、視覺不區分、用 `std::unordered_set<std::wstring_view>` 排除已列出與已釘選 id。效能：`RefreshRows()` 空狀態 5,000 筆 timing 實測 **67 µs（0 ms）**，遠低於 50 ms 上限。測試：`panel_model_test` 新增規則 2（分數序、tie-break 長度＋大小寫不敏感、釘選區不被排序、`RecentStartIndex` 邊界不變）與規則 3（填滿一頁、不足不製造、空 catalog 兩形不當機、填充不重複（pin／recent 各一）、達容量不填充、打字後無填充）共 10 案例＋5,000 筆 timing block；`recent_usage_test` 新增 `TestRecentNeverPads` 守門員斷言 `Recent()` 未改變（newest-first、不填充）。design-spec §4.2 三條規則後補入 item 提供之原文段。sanity greps 全符合：`stable_sort` 起點為 `rows_.begin() + recent_start_`、`UsageStore|UsageScore` 於 panel_model.cpp **零命中**（欄位名 `usage_score` 帶底線，連 PowerShell 預設大小寫不敏感也不會誤配）、`git diff src/usage/` 與 `git diff src/search/` 皆空、無第三個 24（`= 24|24;` 只命中既有 `kIconCacheWorkingSetItems`、panel_layout 既有幾何與 `1024`/`kSearchFontDip`/`kDay` 等無關值）。Release clean build 無新增警告、`ctest` 23/23 全綠。**兩處必要調整**（皆因 item 自身驗收強制的行為改變，未改設計決策）：(1) `panel_model_test.cpp` 既有 `TestEmptyStateNoRecords` 原用非空 catalog 斷言「無紀錄→空狀態」，而規則 3 現在會把非空 catalog 填滿，該案例 fixture 改為空 catalog 以維持原意（新案例已另覆蓋空 catalog 兩形）; (2) `pin_store_test.cpp` 兩個既有 PanelModel 案例被規則 3 填滿行為推翻——`TestPanelModelPinnedFirst` 的 `rows.size()==2` 因填充變 3（改斷言前兩列順序＋不重複＋r2 為填充）、`TestPanelModelHidesAbsentPin` 的 `Rows().empty()` 因填充顯示 catalog 項目（改斷言 ghost pin 永不渲染）。未完成：6 條手動驗收屬人工視覺／操作驗證，Agent 不執行。

- 2026-08-06（NR-052 done）：Esc 清空的是**使用者看得到的 EDIT 輸入框**，空白字元組成的 query 維持格狀版面。改動五檔：`main.cpp` 的 `VK_ESCAPE` 分支（`Esc()` 回傳 true＝「已空，該隱藏面板」）於 false 時改呼叫 `SetWindowTextW(edit, L"")`，讓既有 `EN_UPDATE`→`SetQuery`→`InvalidateRect` 路徑（main.cpp:2181 一帶）把空 query 推進模型，**不在 `Esc()` 後另行 `SetQuery`**（一條資料流，EDIT 是輸入唯一真相、`query_` 是衍生值）；`panel_model.cpp` 的 `RefreshRows()` 版面判斷由 `query_.empty()` 改 `NormalizeName(query_).empty()`（§4.4 唯一正規化器，不寫第二套 IsBlank）；`panel_model.h` 的 `Columns()` 同步改同判準——item §3 驗收明寫「空白 query 要 `Columns() > 1`」，而 header 的 getter 是版面決定的**第二個**判定點（`query_.empty() ? grid_columns_ : 1`），不動它則空白 query 仍切單欄、新測試第一條就紅，屬 item 自身驗收強制、非逾越 scope；`panel_model_test.cpp` 新增 5 案例（單／多空白與 Tab 維持格狀且 rows 等於釘選＋常用、`" a "` 與 `"a"` 結果相同、`SetQuery(L"")` 與 `Reset()` rows 相同、Esc 兩段語意回歸沿用既有、空白 query 上 Esc 先回 false 清空再回 true 才隱藏）；`design-spec.md` §4.3 版面切換條目後補「『包含非空白字元』以 §4.4 的正規化結果判定…」句、§4.7 鍵盤表後補「清空搜尋欄指的是清空使用者可見的輸入框本身…」句。**CMake 不需變更**：`nimblerun_panel_model` 已 `PUBLIC nimblerun_search`（CMakeLists.txt:273-276）、`panel_model.cpp` 本已 include `search_engine.h`。`SetWindowTextW` 會送出 `WM_COMMAND/EN_UPDATE` 給父視窗（Win32 文件化行為），父 proc 的 `EN_UPDATE` 臂讀回文字並 `SetQuery`＋`InvalidateRect`，以讀程式碼確認、未下斷點或 `OutputDebugStringW`；EDIT 子視窗行為無法單元測試，由 6 條手動驗收覆蓋。**一處必要調整**：item §2 只列 `RefreshRows()`，但 `Columns()` 是同一判定、被 §3 驗收測試強制，故改 header（僅補 include `search_engine.h`＋getter 一行），交接區已載明。Release 建置無新增警告、`ctest` 23/23 全綠；`ctest -R panel_model` 因既有註冊名為 `nimblerun_list_vertical_slice_test`（執行檔才是 `nimblerun_panel_model_test`，NR-055 的清理範圍）回傳「No tests were found!!!」，改以直接執行 `build\tests\nimblerun_panel_model_test.exe` 驗證 exit 0。sanity greps：`RefreshRows` 用 `NormalizeName(query_).empty()`、無第二套 `IsBlank`（`iswspace` 僅 `search_engine.cpp:20` 正規化器本身）、`SetWindowTextW(edit, L"")` 在 `VK_ESCAPE` 分支內、`SetQuery` 僅 `EN_UPDATE` 一處、`git diff src/search/` 空。未完成：6 條手動驗收屬人工操作，Agent 不執行。

- 2026-08-06（NR-051 done）：COM 生命週期收成一個共用 header。新增 `src/win/com.h`（header-only，照 `src/storage/atomic_text_file.h` 的純 header 先例）：`ComGuard`（`own_ = SUCCEEDED(hr)`——`S_OK` 與 `S_FALSE`（本執行緒已初始化、計數 +1）都要配 `CoUninitialize`，`RPC_E_CHANGED_MODE` 仍只進 `usable_` 不增計數；default flag `COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE`，建構子帶參數故 `icon_worker.cpp` 日後可傳自己的旗標，本 item 不改它）＋ `ComRelease` deleter ＋ `ComPtr<T>` alias（= `std::unique_ptr<T, ComRelease>`）。**兩份 `ComGuard`（`start_menu_catalog.cpp:57-75` 與 `appsfolder_catalog.cpp:22-40`）逐字比對確認完全相同**（建構子、`own_ = hr == S_OK`、`usable_`、解構、private 兩旗標一字不差），故共用版本無需保留任何差異。刪除兩份逐字 `ComGuard` 與 `png_codec.cpp` 的 `ComRelease`，三檔改 `#include "win/com.h"`；`png_codec.cpp` 的 `std::unique_ptr<X, ComRelease>` 原樣保留（與 `ComPtr<X>` 同一型別，貫徹整檔不混用）。`ResolveShortcut`：三個 1024 wchar 緩衝區全部 `= {}` 零初始化，三個 Shell getter（`GetPath`／`GetArguments`／`GetWorkingDirectory`）判斷由 `SUCCEEDED(...)` 改 `== S_OK` 且全數加 `[0] != L'\0'` 防線（註解寫明 `SUCCEEDED(S_FALSE) == true` 與零初始化的理由）；`shell_link`／`persist`／`ShortcutIsWeb` 的 `item` 改 `ComPtr`（`IID_PPV_ARGS` 需 raw 指標中間變數再包 `ComPtr`，`&ptr.get()` 在 clang 對 rvalue 取址編不過——工具鏈實測的偏差），其餘檔案手動 Release 一律不動。**不需要 CMake 變更**：include 以 repo 根為基準（`#include "win/com.h"` → `src/win/com.h`），`nimblerun_catalog`／`nimblerun_icons` 的 `target_include_directories` 已覆蓋，純 header 無連結成本。**PIDL-only fixture 造得出來（用 item 的 fallback）**：實測 `CreateShortcut`（只會 `SetPath`）對**空字串目標**——`SetPath(L"")` 回 SUCCEEDED、`Save` 成功、reload 後 `GetPath` 回 **`S_FALSE`**（`GetArgs`/`GetWork`/`GetIDList` 回 S_OK）、且 `ShortcutIsWeb` 為 false（項目不被當網站丟掉）；另試了「不存在路徑」→ `GetPath` 回 `S_OK`（不觸發 S_FALSE）、控制台 CLSID `::{26EE0668-…}` → 也回 `S_FALSE` 但空字串最簡單直接。測試新增 `No Target.lnk` fixture，斷言：項目仍在結果（§FR-004）、`search_alias` 空、**連續兩次列舉 stable id 相同**、筆數 7→8（`arguments` 在 `AppEntry` 無欄位，依 item「若測試可觀察到」略過）。**建置與 CTest**：Release 建置無新增警告；`ctest` 23/23 全綠；`ctest -R "start_menu_catalog|appsfolder_catalog|png_codec"` 3/3。sanity greps 全符合：repo 內 `class ComGuard`／`struct ComRelease` 各只命中 `src/win/com.h` 一次、`own_ = SUCCEEDED(hr);`、三緩衝區全帶 `= {}`、`SUCCEEDED(shell_link->` 零命中、`git diff --name-only` 恰為 5 個預期路徑（新增 `src/win/com.h`）不含 `icon_worker.cpp`／`main.cpp`／`shell_icon_provider.cpp`、`stable_id.h`／`dedup.cpp` diff 為空。**未被自動化覆蓋**：`ComGuard` 的 `S_FALSE` 計數平衡無法在單元測試觀察（item 明令不加測試）；4 條手動驗收（handle 數不回昇、釘選存活、控制台項目捷徑顯示）為人工操作。**行為變更**：`arguments` 在 `S_FALSE` 時從「讀未初始化堆疊」改為恆空，理論上原本讀到非零垃圾的少數捷徑 stable id 會變——那是原本就不穩定的 id，屬修好（本機無法實測具體數量，因開發機 Start Menu 無 PIDL-only 捷徑）。未完成：無。
- 2026-08-06（NR-050 done）：`icons.cache` 的 header 是**磁碟上不受信任的輸入**，CRC 只證完整性不證合理性。`DecodeHeader` 新增 `payload_end` 界限檢查（`src/icons/icon_pack_format.cpp:129`，`kPayloadStart ≤ payload_end ≤ size`，全用既有常數），**採逐 slot 併入既有 slot 有效性判斷、而非選定後檢查**：item §3 明文要求「壞 slot A／好 slot B 仍選 B 回 Ok」，選定後檢查會把雙 header slot 的存在意義弄壞；檢查放在 `DecodeHeader` 這唯一入口（而非 `Flush` 等呼叫端），未來任何新增讀取者自動受保護，上界用實際檔案大小順帶擋掉截斷。回傳既有 `BothHeadersBad`（呼叫端的「重建空 pack」反應正是要的）、不新增列舉值、不換 pack 格式、`kPackSchemaVersion` 不動、不留 `.corrupt`。`GrowView` 的 unmap 區塊補 `view_size_ = 0`，讓「`view_` 為 null ⟺ `view_size_` 為 0」成對；`Flush` 的 GrowView 失敗路徑改為 `state_ = StoreState::Disabled`＋`grow-failed`＋`return false`、**不再 `ScanIndex()`**（檔案已被 `SetEndOfFile` 改過、mapping 已消失，disable 到下次啟動是正確且便宜的反應，§11）；`Flush` 入口補 `view_ == nullptr` 成本為零的防線（擋 `Compact` remap 失敗留下的 Ready＋null view 邊緣）。測試：`icon_pack_format_test` 新增 `TestMaliciousPayloadEnd`（huge／`kPayloadStart-1`／`0` 全 CRC 重算正確→`BothHeadersBad`、`kPayloadStart`／`== size`→`Ok`、壞 A 好 B→選 B 回 Ok）、`icon_store_test` 新增端到端（竄改兩 header slot 的 `payload_end` 為 `0x0000100000000000` 且重算 CRC → `Open` 回 Ready＋`recreated=true`、檔案 28736→28736→28768 bytes，TB 級 `SetEndOfFile` 從未發生；`Put`+`Flush` 不當機、檔案仍受 29760 上界約束）。Release 建置無新增警告、`ctest` 23/23 全綠、`ctest -R "icon_pack_format|icon_store"` 2/2；sanity greps 全符合（bounds 檢查命中、PackStatus 仍五值、`icon_pack_format.h` diff 為空、`view_=nullptr` 旁皆有 `view_size_=0`、WriteLog 全短事件名）。**兩處必要調整**：(1) `TestHeaderRoundTrip` 的 `payload_end=99999`／28736 bytes 案例在新檢查下為不合法值，改 `kPayloadStart+4` 並加長檔案（非正常路徑語意不變，否則既有 round-trip 紅燈）；(2) §1 檢查因逐 slot 形狀，`BothHeadersBad` 新案例兩個 slot 都設惡意值（只壞一個 slot 時選好 slot 是設計意圖）。未自動化：`GrowView` mapping 失敗（依 item 不加注入 seam，靠不變式）與 `Compact` remap 失敗（由 `Flush` guard 擋）；4 條手動驗收為人工操作。未完成：無。
- 2026-08-06（NR-049 done）：修 rebuild 執行緒的兩個 crash 級缺陷——直接讀全域 `g_settings`（use-after-free）與 detach 後無人 join（靜態解構競賽）。做法：`StartRebuild`（`main.cpp:945`）開執行緒前先 `JoinRebuildThreads()`（join 上一輪再開新一輪）並取 `const Settings settings_snapshot = g_settings;` 按值捕獲，lambda 捕獲清單只剩 `window, generation, source, settings_snapshot`，兩處 `g_settings` 改讀 snapshot（AppsFolder case 與 `EnumerateUserFolderCatalog`）；`worker.detach()` 改為 push 進檔案範圍 `std::vector<std::thread> g_rebuild_threads`（`main.cpp:221`，只由 UI 執行緒觸碰、無自身 lock）；`WM_DESTROY` 在停 icon worker 之後、拆除資源之前呼叫同一 `JoinRebuildThreads()`，並照 `kIconReadyMessage` 排空的形狀排空遲到的 `kRebuildDoneMessage`（含刪除 `RebuildResult*` payload）。不引入 mutex／atomic／旗標／執行緒池。自動化檢查：`catalog_refresh_test` 新增 §Scope 4 的 `Settings` 拷貝獨立性測試（拷貝後清空原物件 roots/extensions 並翻轉 flag，拷貝完全不變）；sanity greps 全符合（`g_settings` 在 StartRebuild 內僅剩 snapshot 拷貝那一處、`detach()` 零命中、`JoinRebuildThreads` 宣告 1＋定義 1＋呼叫 2＋`push_back` 1、`kRebuildDoneMessage` 排空存在、`std::mutex|std::atomic|shutting_down` 零命中）。Release 建置無新增警告、`ctest` 23/23 全綠。**必要調整**：`lifecycle_check.ps1` 的 tray Exit 5s 上界放寬為 30s——NR-049 的預期行為改變讓 `WM_DESTROY` 在冷啟動後立刻 Exit 時會被一輪 in-flight 掃描擋住（實測冷啟動最壞 21.5 s、暖機 0.3 s），測試實質斷言（正常結束、exit code 0）不變；§Scope 2 卡頓判斷為「暖機下 join 近 no-op、無卡頓，冷啟動首輪掃描可能擋 UI 執行緒達 20 s」，依指令不自行加取消機制，若產品要求再另開「可取消掃描」item。未完成：無。
- 2026-08-06（NR-048 done）：把 `tests/unit/search_engine_test.cpp` 的 20 條裸 `assert()` 全數改為 repo 標準 `Expect()` helper（`FAIL: <message>` 寫 stderr、累計 `g_failures`、`wmain` 結尾 `return g_failures == 0 ? 0 : 1`）；兩條 5000 筆 timing 的 `std::wprintf` 輸出行一字未改、50 ms 上限改以 `Expect` 檢查。照 §Scope 3 把 `prefix_results.size() == 2` 暫時改 `== 99` 建置重跑，ctest 紅燈且輸出含 `FAIL: trimmed prefix search returns Calendar and Calculator`，改回後全綠。**開啟斷言揭露既有 fixture bug**：主 catalog 與 alias_catalog 的 `AppEntry` 未填 `normalized_name`，而 `SearchApps` 依契約（design-spec §4.4、`catalog_refresh.cpp:121`、`search_engine.h`）只比對已正規化名稱，`"  CAL  "` 對原始 `"Calculator"/"Calendar"` 永不命中，首條斷言立刻失敗並因對空結果取 `[0]` 而 SegFault。判定為測試 fixture 缺陷、非 `src/search/` 行為錯誤（production 唯一呼叫端 `panel_model` 的 snapshot 一律由 `SetSnapshot` 預填，其他 search 測試如 `panel_model_test.cpp:29` 亦預填），故依 Acceptance「全綠」與 §Scope 3「改回即綠」修正 fixture（補 7 筆 `normalized_name`），**未改任何 `src/`、未修 production 行為、無需另開 item**。最終 Release 建置無新增警告、`ctest` 23/23 全綠；sanity greps 全數符合預期（無裸 `assert(`、無 `<cassert>`、`git diff --name-only` 只動測試檔、兩條 `NR-038:`/`NR-047:` 輸出行仍在）；timing 實測 583／189 µs 遠低於 50 ms 上限。偏差：§Scope 1 樣本註解含字面 `assert()` 會撞 `assert\(` grep，改寫為「assert macro」；NR-047 留下的 pragma 區塊依 Non-goals 保留。未完成事項：無。
- 2026-08-06（NR-048～NR-056 ready，全 repo 稽核後的修補批次）：backlog 清空後對整個 repo 做了四軸稽核（ponytail 過度設計、spec 對照、正確性／穩健性、測試與文件覆蓋），把發現收斂成 9 個 item。**排序原則是「先讓回歸網是真的，再修會當機的，再修使用者看得到的，最後修文件」**，因此 NR-048 排第一。逐項決策與「為什麼不那樣做」：**NR-048**——`search_engine_test.cpp` 是唯一用裸 `assert()` 的測試檔，而 `AGENTS.md §Validation` 指定的 `-DCMAKE_BUILD_TYPE=Release` 會帶 `-DNDEBUG`，所以**搜尋模組的全部斷言在專案規定的驗證組態下整批被編掉**，`ctest` 的那一格綠燈是假的（NR-047 交接區引用的「23/23 全綠」含這一格）。改測試檔而不是移除 `NDEBUG` 或 `#undef NDEBUG`：後兩者會讓這一個檔與其他 21 個用兩套失敗機制，而且下一個拿它當範本的人會把陷阱帶走。明文要求「故意弄壞一條斷言確認會紅」，否則本 item 無法證明自己修好了它宣稱修好的東西；若開啟斷言後有既有斷言真的失敗，那是產出物，記錄後停手，不准順手改 `src/search/` 讓它變綠。**NR-049**——`StartRebuild` 的 detached 執行緒直接讀全域 `g_settings`，設定對話框按 OK 時 `g_settings = reloaded` 會在掃描途中釋放 `catalog_roots` 與每個 root 字串（use-after-free），且執行緒永不 join，結束程式時 CRT 會在 worker 腳下解構全域。用**值拷貝**而非加鎖（重建要的是啟動當下的快照，不是最新設定；加鎖會讓使用者按 OK 時被整趟目錄掃描擋住），用**可 join 的 vector**而非 detach 加取消旗標（掃描依 §FR-003 有界，等它結束比正確實作合作式取消便宜）；照 icon worker 既有形狀補一段遲到訊息的 `PeekMessageW` 排空，否則每次重建中關閉都洩漏一個 `RebuildResult`。留下的契約是「背景工作的捕獲清單不得出現全域名稱」，用 grep 守。**NR-050**——`DecodeHeader` 對 `payload_end` 零界限檢查，一個 CRC 正確但值荒謬的 header 會讓 `Flush` 的 `SetEndOfFile` 把 `icons.cache` 撐到 TB 級塞爆磁碟（鏡像情況 `payload_end = 0` 則就地覆寫兩個 header slot 與整個 index）；`GrowView` 在 unmap 後若 mapping 失敗會留下 `view_ == nullptr` 但 `view_size_` 仍為舊值，下游 `DecodeEntry` 的 `size < kPayloadStart` 防線因此通過並解參考 null。檢查放在 `DecodeHeader` 這個**唯一入口**而非各呼叫端（未來新增的讀取者自動受保護），上界用**檔案大小**而非 `kMaxPackBytes`（順帶擋掉截斷），回傳**既有的** `BothHeadersBad` 而不新增列舉值（呼叫端的既有反應正是重建，不用改任何呼叫端），且明文要求「壞 slot A／好 slot B 仍選 B」的測試——雙 header slot 的存在意義不能被這個檢查弄壞。**決定不換掉 pack 格式**：改成「一 icon 一 PNG、NTFS 當索引」確實能刪約 1,800 行，但那是重寫已上線、§10.2 已規格化、三份測試覆蓋的子系統，屬產品決策而非修補，須使用者拍板。**NR-051**——`start_menu_catalog.cpp` 與 `appsfolder_catalog.cpp` 有兩份逐字相同的 `ComGuard`，兩份都寫 `own_ = hr == S_OK`，但 MSDN 要求每次成功的 `CoInitializeEx`（**含 `S_FALSE`**）都要配一次 `CoUninitialize`，於是巢狀呼叫下 apartment 永不拆除、每次 Start Menu 重建洩漏一次；同檔 `ResolveShortcut` 的三個 1024 wchar 緩衝區未初始化，而 `GetPath`／`GetArguments` 對 PIDL-only 捷徑回 `S_FALSE` 且 `SUCCEEDED(S_FALSE)` 為真，`assign` 會掃描殘留堆疊 bytes——那些垃圾進了 `search_alias` 與 §10.3 的 identity key，讓同一個捷徑在不同次掃描產生不同 stable id。**收成 `src/win/com.h` 是刪除不是新增抽象**（抽象已經存在，只是被複製了兩份且各帶同一個 bug）；只收 `ComGuard` 與 `ComRelease` 兩樣，明文拒絕長成 COM 包裝層；不全面改寫既有手動 `Release()`（無關 churn，且每改一處都是一次 double-release 機會）。核心斷言是「連續兩次列舉 stable id 相同」。**NR-052**——EDIT 的文字與 `PanelModel::query_` 是兩份狀態且會不一致：Esc 只清模型不清輸入框（畫面回到格狀但搜尋框還有殘字，下一鍵接在舊字後面），而版面切換用 `query_.empty()` 而非 §4.3 的「包含非空白字元」（打一個空白就掉出格狀、`SearchApps` 正規化後早退，使用者看到「No matching apps」）。修法是**讓 Esc 去改 EDIT，再由既有的 `EN_UPDATE` 把空 query 推進模型**——一條資料流向，而不是在 `Esc()` 之後另呼叫一次 `SetQuery`；版面判斷改用**唯一的** `NormalizeName`，不寫第二套 `IsBlank`（否則正規化規則日後改變時兩者會漂移）。留下的契約：程式碼要改查詢一律 `SetWindowTextW`，不得直接 `SetQuery`。**NR-053**——§4.2 三條規則只做對第一條：非釘選區依 `last_launch_utc` 排序而非使用分數（每天用十次的主力 App 輸給一小時前開過一次的冷門 App，§4.6 的分數機制在使用者最常看到的畫面上是關掉的），且完全沒有規則 3 的填充（全新安裝按下 `Alt+Space` 看到空格狀加「No matching apps」，這是產品的第一印象）。關鍵發現是 **`StampRankingFields` 已經把 `usage_score` 貼在每一筆 `AppEntry` 上**，所以空狀態排序不必再查 `UsageStore`；tie-break 明文要求與 `search_engine.cpp` 的 comparator 對齊（兩處漂移會讓同一批 App 在搜尋與空狀態呈現不同順序且無任何錯誤），排序起點必須是 `rows_.begin() + recent_start_`（§FR-011：釘選順序不因分數變動，且 NR-046 的拖曳範圍依賴這個邊界）。**不改 `usage.tsv` schema**：§4.6 缺的 7/30 日 bucket 需要遷移，與「空狀態根本沒用到分數」正交，另案。容量常數明文禁止製造第三個 24（重用 `kIconCacheWorkingSetItems` 或 `panel_layout`）。**NR-054**——三個小缺陷同一子系統一起修：記錄檔寫在根目錄而非 §10.1 的 `logs\`；`Write` 的「檢查大小→輪替→開檔 append→寫入」序列被 UI 執行緒與 icon worker 無序共呼叫，輪替與開檔之間的交錯會讓失敗當下的診斷行落進剛被搬走的檔案（§11 最需要記錄的那一刻）；§FR-014 的「開啟記錄資料夾」根本不存在。**搬移不遷移**（記錄是診斷產物不是使用者資料，替沒人回頭看的檔案寫遷移碼是付永久複雜度），鎖用程序內 `std::mutex` 且**涵蓋整個 `Write` 本體**（只鎖輪替那段沒用，競賽正是跨越輪替與開檔），設定頁只加一個按鈕不做記錄檢視器，開資料夾走 Shell 絕不組命令列。留下的契約：跨執行緒共用的物件自己負責同步，第三個寫入者不需要知道這件事。**NR-055**——`tests/CMakeLists.txt` 456 行裡有 22 份逐字相同的 25 行樣板，真正的自由度只有「來源檔」與「連結項」。代價不是行數是**漂移**：改一個 target 的警告選項現在意味著要在 22 個裡挑對的那一個，漏改不會報錯只會讓那個測試在不同組態下編譯（NR-047 被迫在測試檔頭塞 `#pragma clang diagnostic` 就是這個結構的後果）。用**清單加 `foreach`** 而非 CMake function／macro（function 會讓「這個 target 吃到哪些選項」變成跨檔追蹤），不合模式的 target 留在迴圈外並註明理由（寧可兩個例外，也不要把例外塞進參數讓清單長成迷你建置系統）。成功定義是**建置行為位元級不變**，故明文要求改動前後比對 `ctest -N` 與編譯命令列。**NR-056**——文件六處說謊：`testing.md` 還在教人驗證早已不存在的 Phase 0「fake app grid」（照著走不產生任何訊號）；`performance-baseline.md` 十列有八列 `Not measured`（含全部放行門檻），而 NR-047 已有真實 `SearchApps` 數字、`release_evidence.ps1` 也已在量 idle 資源；`release-evidence.md` 的工具版本是 `cmake | Usage`、`ninja | error`、`ctest | *****`，起因是腳本宣告了名為 `$args` 的參數而它是 PowerShell 自動變數，`& $name @args` 展開的是呼叫者的空陣列所以 `--version` 從未送達——一份存在意義就是可重現證據的文件，環境半邊是編造的；另有四處 spec 與碼不符（AppsFolder／UserFolder 的 identity 偏差、右鍵選單多兩項、面板可拖曳、匣選單 About 是空的 `return 0;`）。判定原則是**程式碼對就改文件、規格對就改碼**：identity 偏差與右鍵選單與面板拖曳都改規格（前者改碼會讓既有 `favorites.txt`／`usage.tsv` 全部失效），About 改碼（§4.10 要求它存在，而出貨選單裡點了沒反應的項目每次都被讀成 bug；用 `MessageBoxW`，版本從 `.rc` 的 `VS_VERSION_INFO` 取，**明文禁止在程式碼裡硬寫第二份版本號**）。順帶修「搜尋框內按壓被轉給 `HTCAPTION` 導致無法用滑鼠選取文字」。效能表格明文規定**只填真的量到的，量不到就維持 `Not measured` 並註明還缺什麼**——`Not measured` 是誠實狀態，`cmake | Usage` 不是，它看起來像資料。**全批次共同的不做事項**：搜尋防抖／增量收斂／背景搜尋執行緒（NR-047 實測 5,000 筆最壞路徑 204 µs、上限 50 ms，連續六份交接與現在的實測都反對）、`usage.tsv` schema 變更、pack 格式重寫、測試框架或共用測試支援程式庫（22 份各 8 行的 `Expect()` 比一個 library target 便宜）、以及任何 §3.2 的 out-of-scope 功能。稽核同時確認**乾淨的一面**：§3.2 的排除項目一項都沒有偷跑（無檔案內容搜尋、無網頁／計算機／命令執行、無外掛與網路路徑），啟動一律走 Shell 從不組命令列，FR-002／FR-006／FR-008／FR-009／FR-010／FR-014 的輪替與 §4.5 的五層排名加 tie-break 皆與規格相符。

- 2026-08-06（NR-047 ready）：搜尋除名稱外再比對「捷徑 resolved target 的檔名主體」與「AUMID 的 package family 部分」，讓中文捷徑名（`計算機.lnk`）能用 `calc` 找到。可行性來自兩個既有事實：`start_menu_catalog.cpp` 早已解析出 `link.target` 卻只餵進 stable id 後丟棄，`appsfolder_catalog.cpp` 的 `source_path` 早已是 AUMID——本 item 只是留下其中有用的一小段。決策：（1）**不做**使用者自訂 alias／nickname（使用者本 session 明確選擇只做 target 比對；alias 需要編輯 UI＋新的 `%LOCALAPPDATA%` 儲存＋app 缺席時的保留規則＋設定頁入口，成本是 target 比對的數倍，而它要解的痛已被解掉大部分）；（2）只取**檔名主體**，不比對完整路徑（`C:\Program Files\…` 會讓 `pro`／`file` 命中幾乎所有 App）；（3）AUMID 切到第一個 `_` 之前以丟掉每個 Store App 都相同的 publisher hash；（4）排名新增**單一最低層級** `MatchRank::Alias`（在 `Subsequence` 之下、`NoMatch` 之上），只在名稱完全不匹配時才查次要鍵，故任何名稱命中永遠優先，§4.5 名稱層級之間的順序與 tie-break comparator 一字未動，也刻意不按次要鍵的命中方式細分層級；（5）正規化沿用 NR-038 的單一入口 `SetSnapshot`，因空 alias 是合法值故改為無條件呼叫（`NormalizeName` 具冪等性，磁碟快取帶回的已正規化值再跑一次是 no-op）；（6）`catalog.cache` schema 1→2、`kFieldCount` 6→7、新欄位排在最後，且把「舊 schema」從 `PreserveCorrupt` 改為單純 `return false` 重建（舊版快取是這個 build 讀不懂的合法檔，不是損壞檔，例行 schema bump 不該替每個使用者產生 `.corrupt`），不寫遷移碼；（7）UserFolder 項目無次要鍵，因該列舉器刻意不在掃描時用 Shell COM 解析捷徑（其既有 `ponytail:` 註解已載明），本 item 不改。`AppEntry::search_alias` 必須**附加在 struct 最後**，否則 `search_engine_test.cpp` 逐欄位的 designated initializer 全部編不過。次要鍵不進 stable id（§10.3）也不進 `dedup.cpp` 的 `display_name` 比較（兩個不同 App 可能共用同一個 target stem，合併會少一筆）。不動 `src/app_host/`、`src/ui/`、CMake。測試分三處：`search_engine_test`（alias 命中、名稱 subsequence 仍勝 alias exact、空 alias 不受影響、既有 `CAL`／`3d` 斷言不變）、`start_menu_catalog_test`（新增 `小算盤.lnk` → `calc.exe` fixture 斷言 alias 為 `calc`，並確認 `Notepad.lnk`／`Notepad Copy.lnk` 仍共用 stable id）、`catalog_refresh_test`（`SetSnapshot` 正規化、快取含 escaping 的往返、手寫 `schema=1` 載入回 false 且不留 `.corrupt`）。同步更新 design-spec §4.4／§4.5（及 §10.1 若其載明快取 schema）。效能：新增的第二次 `Rank` 只在名稱完全不命中時才跑，故成本與結果集大小成反比（短查詢 `e` 幾乎不觸發、長查詢最多約 2× 掃描，且 alias 比 display name 短故是較便宜的那一次）；最壞路徑是「名稱全不命中且每筆都有 alias」，而 `search_engine_test` 既有的 5000 筆效能斷言用 `L"e"` 命中全部 5000 筆、`NoMatch` 一次都不發生，**完全沒量到這條路徑**——故在其旁**新增**一個 timing block（每筆給 alias、以 `L"zzqx"` 讓全部落到 alias 檢查、斷言結果為空以免 fixture 改名後靜默失去量測意義、同樣 50 ms 上限與 `wprintf`），不動 NR-038 那個原有 block。若該數字逼近上限，解法是既有 `ponytail:` 註解已寫的 incremental narrowing（同樣適用於 alias 層），不是把 alias 檢查加條件或做 per-query 快取。維護性：（a）明文「新增 catalog source 時只需在列舉器填 `search_alias`，其餘不動」為唯一擴充契約，並拒絕 `SearchKeys()` 存取器或 `vector<wstring> search_keys`（兩個 key 的排名語意不同，不是 list；vector 會在熱掃描裡加一次 per-entry 配置與一層內迴圈，去模擬沒人要求的通用性）；（b）alias 在 search 之外完全惰性（不進 stable id／dedup／UI／tie-break），故日後改變其推導方式只會影響「找不找得到」，不會影響合併、啟動或持久化身分，Agent checks 以 `Select-String` 機械化守住；（c）§5 對舊 schema 的修法是本 item 可重用的一半，日後加欄位就是 `kSchemaVersion`＋`kFieldCount`＋一行序列化＋一行解析，不寫遷移；（d）順手把 `search_engine_test.cpp` 那四筆逐欄位列滿的 designated initializer 縮掉（C++20 允許省略成員、不允許重排，被省略的成員都有 default member initializer），**移除**「新欄位必須附加在最後」這個陷阱本身，讓下一個加欄位的人不必再與該檔協調；此清理與 §1 同屬本 item 範圍，只落地 §1 等於把陷阱留著上膛。
- 2026-08-06（NR-047 done）：照 ready 決策實作，完成 10 檔修改。`AppEntry::search_alias` 附加在 struct 最後；Start Menu 取 `FileStem(link.target)`（`start_menu_catalog.cpp:151`，在 `launch_identity` 之後、identity block 之前，identity 一字未動）；AppsFolder 取 AUMID 第一個 `_` 之前（`appsfolder_catalog.cpp:78`）；正規化只留在 `SetSnapshot` 一處（無條件 `NormalizeName`，空 alias 合法）；`catalog.cache` schema 2／`kFieldCount` 7／新欄位序列化於 `source` 之後、解析 `fields[6]`（空欄合法）、舊 schema arm 改裸 `return false` 重建、無遷移碼；`MatchRank::Alias=5` 夾在 `Subsequence` 與 `NoMatch` 之間、`SearchApps` 三條件 cheapest-first fallback、tie-break comparator 未動。測試：`search_engine_test` 先做 §8 cleanup（省略式 designated init，`  CAL  `／`3d` 斷言原樣），新 alias/名稱 subsequence 勝 alias exact/空 alias/原樣比對四組斷言＋§7 最壞路徑 5000 筆 timing block（`L"zzqx"`、結果斷言為空）──兩條 timing：NR-038 `603 us`／NR-047 `204 us`；`start_menu_catalog_test` 新增 `小算盤.lnk`→`calc.exe` fixture（alias==`calc`、entry 數 6→7、Notepad／Copy 仍共用 stable id、bare `.exe` alias 空）；`catalog_refresh_test` 新增 SetSnapshot 正規化、含 escaping 的 cache 往返、手寫 `schema=1` 載入回 false 且無 `.corrupt` 三 case。design-spec §4.4（次要比對鍵 bullet）／§4.5（tier 6＋「任何名稱命中永遠優先於任何次要鍵命中」句）已更新；§10.1 只列檔名、未載明 schema，依 item 不加。`ctest` **23/23 全綠**、build 無新增警告；sanity greps 全符合（`git diff CMakeLists.txt tests/CMakeLists.txt src/app_host/ src/ui/` 為空、`dedup.cpp`／`stable_id.h` 無 `search_alias`、`NormalizeName` 只在 search_engine 定義＋SetSnapshot、`kSchemaVersion=2`／`kFieldCount=7`、`MatchRank` 只在 `search_engine.cpp`、`PreserveCorrupt` 不落在舊 schema arm）。**兩處必要調整**（設計決策零偏差）：(1) motivating entry（`計算機`＋alias `calc`）不放共享 fixture 而放獨立 `alias_catalog` block——`  CAL  ` 正規化為 `cal` 是 alias `calc` 的 NamePrefix，放共享 fixture 會被 fallback 撈成第三筆打破 §8 自己要求的 `  CAL  ` 斷言不變；(2) 省略式 designated init 在此工具鏈觸發 `-Wmissing-designated-field-initializers`（`-Wextra`），與「no new warnings」衝突且 item 禁動 `tests/CMakeLists.txt`，故在 `search_engine_test.cpp` 加 scoped pragma 抑制。5 條手動驗收為人工操作／視覺驗證，不在 Agent 範圍，交接區已註明。未完成：無。
- 2026-08-06（NR-046 done）：格狀狀態的釘選格可拖曳重排。`PinStore` 新增唯一入口 `ReorderPresent(order)`（接受新視覺順序的 stable ID；`slots`＝order 內 pin 的絕對索引升冪、`wanted`＝order 中已 pin 的 ID 去重，將 `wanted[k]` 的 record 搬到 `pins_[slots[k]]`，未列出的 pin 永不搬動故 `favorites.txt` 行號不變＝acceptance 9；回傳值以搬移前後 id 序比較）。`main.cpp`：新增 `g_dash_style`（由 D2D factory 建立、隨 factory 釋放，device-independent）＋拖曳狀態五個 global＋`PinnedRowCount()`／`DragPreviewOrder()`（`recent_start` 仍是唯一 pin/recent 邊界）；`DrawDecodedIcon` 加預設 `opacity=1.0f`；格狀迴圈改 `row = preview[i] : i`，`row==-1` 畫 `kSearchCornerRadiusDip` 圓角虛線落點後 `continue`，`rows[i]`/selected/hovered 全部改 `rows[row]`／比較 `row`（NR-041 標記、NR-045 數位框一併維持），拖曳中凍結 hover，迴圈後畫半透明 ghost（cache 命中 0.6f、miss 畫 `g_dim_brush`，不 `RequestVisibleIcon`）；`WndProc`：`WM_LBUTTONDOWN` 三臂（`cell<0`＝NR-039 原樣、格狀 pin 區＝`SetCapture` 記狀態不啟動、其餘按壓即啟動）、`WM_MOUSEMOVE` 拖曳臂（`SM_CXDRAG/CYDRAG` 門檻 promote、`CellAtPoint` 算 gap、整窗 invalidate 附 `ponytail:`）、新增 `WM_LBUTTONUP`（先拷貝狀態**再** `ReleaseCapture()`，因 `WM_CAPTURECHANGED` 會同步清 globals；未拖曳＝延遲啟動、`gap>=0 && gap!=row`＝`ReorderPresent`+`Save` 成功才 `SetPins`、其餘取消）與 `WM_CAPTURECHANGED`（唯一逃生門）；`ShowPanel` hover 重設旁重設拖曳狀態。spec §4.8 增兩條（拖曳重排＋放開啟動）／§FR-011 改兩條。未加常數／brush／format／target／timer；`ctest` 23/23 全綠、build 無警告；grep：`CellAtPoint` 5（定義＋MOUSEMOVE 2＋LBDOWN＋RBDOWN）、`RecentStartIndex` 3、`SetTimer` 仍 1。**一處必要修正**：item §2 測試 bullet 寫 `{d,a,c}` 重排後為 `[d,a,b,c]`（b「留在 index 2」），但 b 起始索引是 1、該結果違反 §1「未列出 pin 保留絕對索引」／header／acceptance 9「absent pin 保持原行號」／綁定約束，實作照 §1 得 `[d,b,a,c]`（b 停在 1），測試改斷言此結果。11 條手動驗收為人工視覺驗證，不在 Agent 範圍，交接區已註明。
- 2026-08-06（NR-045 done）：格狀狀態的數字快選框只在按住 `Alt` 時顯示，未按時 footer 以一句 `Hold Alt to show shortcuts` 取代 `Alt+1~N`＋`Launch` 群組；清單狀態完全不受影響。實作只動 `src/app_host/main.cpp` 與 `docs/design-spec.md` §4.9 一條：`footer_strings` 新增 `kHoldAltHint`；`DrawKeyBox` 旁新增檔案範圍 `bool AltHeld() { return GetKeyState(VK_MENU) < 0; }`（無 flag／timer／hook，每幀重讀鍵盤狀態故 Alt+Tab／失焦／隱藏皆無殘留）；grid cell 數字框外包 `if (AltHeld())`（幾何與繪製一字未改，清單列框未動）；footer 的 Alt 群組改分支——grid 且 `!AltHeld()` 時以既有 `draw_right_label` 畫 `kHoldAltHint` 並照 `right -= draw_right_label(...); hints_left = std::min(hints_left, right);` 折入 `hints_left`，其餘沿用原 `alt_label`＋`draw_key_box`＋`Launch` 組，兩臂皆持續餵 `hints_left`、`PgUp/PgDn/Scroll` 不動，未新增 format／brush／版面常數；`SearchEditProc` 加 `WM_SYSKEYDOWN`（`VK_MENU` 且自動重複位元未設）與 `WM_SYSKEYUP`／`WM_KEYUP`（共用 case）各一次 `InvalidateRect(GetParent(edit))` 後 `break`，NR-024 的 Alt+digit 與 Alt+Space 照走。spec §4.9 數位快選條改寫：清單常駐、格狀僅按 `Alt` 顯示、未按時以灰色說明句取代指引群組、`Alt` 仍只在 footer 說明一次。`git diff` 僅上述位置、`panel_layout.h` 為空；configure＋build 成功、無新增警告；`ctest` 全套件 23/23 通過；`VK_MENU` grep 恰三處（`AltHeld` 定義＋兩組 key 事件 guard）。九條手動驗收（含長按不閃爍、Alt 中啟動、Alt+Tab 無殘留、200% DPI）為人工視覺／操作驗證，依 `AGENTS.md` 交付規則不在 Agent 範圍，交接區已註明。未完成：無。

- 2026-08-06（NR-043 done）：按鍵提示框標籤統一改由新增的 `g_key_format` 繪製（`src/app_host/main.cpp`，只動此一檔）：宣告＋`CreateDeviceResources` 的「資源都在」判斷＋`FAILED` 檢查＋屬性設定（`SetWordWrapping(NO_WRAP)`／`SetTextAlignment(CENTER)`／`SetParagraphAlignment(CENTER)`，不設 trimming）＋結束時 `Release`，五處照既有 `g_grid_name_format` 模式。字型採 `Segoe UI` `SEMI_BOLD`、`kSmallFontDip`（不加粗不改面，`Segoe UI` 數字為 tabular 故單一位數置中後位置一致）；`DrawKeyBox` 內唯一一次 `DrawText` 改用 `g_key_format`、完整 `box_rect`、`g_dim_brush`（與框線同色，框的填色/框線/位置仍承擔狀態訊號，非顏色-only，符合 §NFR-006），垂直置中由 `DWRITE_PARAGRAPH_ALIGNMENT_CENTER` 取代 `kFooterTextInsetDip` 硬推，該常數保留供 footer 標籤與 path bar 使用；`g_small_format` 等既有四格式與三個 `DrawKeyBox` 呼叫點一字未改。footer `Alt+1~N` 壓住 `Scroll` 的 off-by-one 以最小改法修：`draw_right_label` 的回傳寬度同時推進 `right`（`right -= draw_right_label(...); hints_left = std::min(hints_left, right);`），`Scroll` 與 `Launch` 兩組同形，不重寫版面演算法、不量測快取、不動任何版面常數（`kFooterTextInsetDip`／框寬高／間距／圓角全未動）。`git diff` 僅上述七處；configure＋build 成功、無新增警告；`ctest` 全套件 23/23 通過。十條手動驗收（含第 6 條 `Alt+1~N` 是否溢出需否加大 `kFooterWideKeyBoxWidthDip`）為人工視覺驗證，依 `AGENTS.md` 交付規則不在 Agent 範圍，交接區已註明且未動用任何常數變更。未完成：無。
- 2026-08-06（NR-044 done）：面板改由 DWM 圓角。`CMakeLists.txt` 的 `NimbleRun` 連結清單加 `dwmapi`；`main.cpp` 在視窗建立後一次 `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND)`，由 DWM 合成階段連同 `WS_BORDER` 框線與系統陰影一起圓角，零每幀成本、非工作區框線不缺口；不使用 `SetWindowRgn`／layered window／mica（決策 2/3/§254）。工具鏈 LLVM-MinGW 22.1.8 的 `dwmapi.h` 已含實符號，採真實 SDK 標頭（不需 local-enum fallback）。Windows 10 不認 attribute 33：呼叫回 `E_INVALIDARG` 即忽略、面板維持直角，不做版本分支與替代方案（決策 4）。視窗樣式、`panel_layout`／`panel_palette`、`Render` 與 design-spec 皆未動；`git diff` 僅 `CMakeLists.txt` 一行＋`main.cpp` 一行 include＋一個 DWM 呼叫區塊。configure＋build 成功、連結無誤、無新增警告；`ctest` 全套件 23/23 通過。八條手動驗收（含 Windows 11 圓角目視與 Windows 10 不炸）為人工視覺驗證，不在 Agent 範圍；本機無 Windows 10 環境，交接區已註明「未在 Windows 10 驗證」並以程式碼複查代替。未完成：無。
- 2026-08-04：MVP／第一個垂直切片先採列表，顯示 Icon、名稱與有效路徑；matrix 延後為 NR-016。
- 2026-08-04：空白搜尋最多顯示 20 個最近執行 App，依最後執行時間排序，不以字母排序補位。
- 2026-08-04：不做拖曳排序。
- 2026-08-04：封裝 App 沒有有效路徑時隱藏「開啟檔案位置」。
- 2026-08-04：新增 UserFolder Catalog 來源；使用者可設定多個本機資料夾、各自的遞迴選項與受支援副檔名，掃描只在檔案變更事件、設定變更、啟動或手動 refresh 時進行，不在每次面板顯示或 App 啟動後完整重掃。
- 2026-08-04：Agent checks 不要求操作 App 視窗；人工驗證不列入本計畫。
- 2026-08-04：UI 文字採英文；中文只用於開發與規格文件。
- 2026-08-04（NR-001 done）：將 `-DCMAKE_TOOLCHAIN_FILE=...` 加引號為 `-D"CMAKE_TOOLCHAIN_FILE=..."`；PowerShell 對未加引號的 `.` 會切分參數。Clean Release build/CTest 驗證通過。
- 2026-08-04（NR-005 done）：Start Menu enumeration 符合更新後 §FR-004 與新增 non-goal（UserFolder 由 NR-019 負責）；fixture 測試通過。
- 2026-08-04（NR-004 補齊）：Spec v1.1 更新後補上 `catalog_roots`／`catalog_extensions` 的保存與驗證；同時修正 `Unescape` 吞掉未知 escape 反斜線的問題。
- 2026-08-05（NR-019 done）：`EnumerateUserFolderCatalog(settings)` 依設定列舉多個本機資料夾，allowlist 大小寫不敏感、recursive 不追蹤 reparse point、逐項目錯誤隔離；FNV-1a stable-id 抽成共享 `catalog/stable_id.h` 供三個來源共用；fixture 測試 7/7 全綠。
- 2026-08-05（NR-007 done）：新增純值 dedup 模組 `catalog/dedup.{h,cpp}` 與路徑 identity 正規化 `NormalizePathKey`（`stable_id.h`）；三個來源對正規化 identity key 取雜湊；dedup 依 stable ID 合併、依來源優先序取勝者，不明確（Start Menu 封裝 App 捷徑 vs AppsFolder）時保留兩者並計入診斷；新測試 9 例全綠，全套件 8/8。
- 2026-08-05（NR-008 done）：新增 `launch/shell_launch.{h,cpp}`（`nimblerun_launch` 庫），`LaunchEntry` 只接受 catalog 的 launch_identity（非空），單次 `ShellExecuteExW` 啟動，不拼接命令列、不取 process handle（無 `SEE_MASK_NOCLOSEPROCESS`），回傳 `LaunchResult{ok, error_code}`；假定 UI thread 已 STA COM init。測試用 temp 內自終結 `.cmd` fixture 驗證成功啟動並以 marker 佐證，空 identity 拒絕路徑也驗證；`ctest -R shell_launch` 與全套件 9/9 通過。
- 2026-08-05（NR-009 done）：新增純值 `usage/usage_store.{h,cpp}`（`nimblerun_usage` 庫）：`RecordLaunch(stable_id, injected UTC epoch)` 只在 caller 傳入成功路徑時被呼叫（失敗即不呼叫，狀態不變）；`Recent(cap=20)` 依 last-launch 由新到舊、同刻以 stable_id 升序當確定性 tie-breaker，無紀錄回傳空、不足不以字母補位。持久化 `usage.tsv`（`schema=1` 首行、UTF-8 TSV：escaped stable_id、total launches、last launch UTC epoch），沿用 settings 的 tmp＋flush＋atomic replace；損壞→改名 `usage.tsv.corrupt` 保留、較新 schema→原檔不動。將 settings_store 原本私有的 UTF-8／讀檔／escaping／atomic write／corrupt-preserve helpers 抽成共享 `storage/atomic_text_file.h`（header-only），settings_store 改用之（settings test 回歸綠）。新測試 `recent_usage_test`（11 案例：排序、上限 20、空狀態、tie-breaker 重載可重現、新啟動移首、失敗不更新、round-trip＋不存在於 catalog 的 id 存活、corrupt／malformed row／newer schema、atomic write failure 原檔保留）；`ctest -R recent_usage` 與全套件 10/10 通過。
- 2026-08-05（NR-010 done）：第一個列表垂直切片。新增純值 `app_host/panel_model.{h,cpp}`（`nimblerun_panel_model` 庫）：`SetQuery`（空→recent、非空→SearchApps）、`MoveSelection`（wrap）、`Activate`（回傳 launch identity）、`Esc`（先清空再隱藏）；main.cpp 啟動時以三來源＋dedup 建真實 catalog snapshot、載入 settings/usage，child EDIT（subclass）轉送鍵盤給 model，D2D 列表 render（tile placeholder，NR-012 換真實 icon），Enter/單擊只啟動選取項、成功更新 usage 並依設定隱藏、失敗保持面板顯示錯誤、右鍵僅有效路徑才「開啟檔案位置」。兩次 subagent 皆未產出，改由主 Agent 直接實作；`ctest -R list_vertical_slice` 與全套件 11/11 通過。
- 2026-08-05（NR-012 done）：新增 `icons/icon_cache.{h,cpp}`（`nimblerun_icons` 庫）：`IconKey`（stable_id＋size＋DPI，`Encode()` 為確定性單一字串鍵、DPI 四捨五入為整數）、`IconBitmap`（32bpp premultiplied BGRA，純值資料，UI/Shell 不擁有 COM）、抽象 `IconProvider`＋LRU `IconCache`（上限預設 64、Peek 不更動 recency、Resolve miss 時請 provider、失敗不進 cache）。真實 provider 為 `icons/shell_icon_provider.{h,cpp}`：`SHCreateItemFromParsingName`＋`IShellItemImageFactory::GetImage`（SIIGBF_ICONONLY|RESIZETOFIT）涵蓋檔案路徑與 AppsFolder parsing name，HBITMAP→BGRA 後補齊/premultiply alpha，任何失敗回傳空 bitmap。main.cpp 接上 NR-010 render：fixed tile（`kTileSize=30`）內 cache hit 畫真實 icon、miss 畫 fallback tile＋首字母，行幾何不變故無 reflow；fallback-first（第一幀 fallback，post `kIconRequestMessage` 到訊息佇列尾、離 input path，之後 `LoadVisibleIcons` 同步載入可見列（`VisibleRowCount` 只載入 viewport 內 rows）並 invalidate，失敗 key 記入 `g_requested_icon_keys` 不重試、每次 ShowPanel 清除以重試暫時性失敗）；`ponytail:` 註解說明採 bounded 同步可見集載入的取捨與升級路徑。新增 `icon_cache_test`（fake provider：hit／miss-then-insert／LRU eviction／reinsert 刷新 recency／failure 不 cache／size·dpi 分離 key／default cap）；`ctest -R icons` 1/1、全套件 12/12 通過。
- 2026-08-05（NR-013 done）：新增純值設定編輯模型 `settings/settings_editor.{h,cpp}`（`nimblerun_settings` 庫）：working copy＋集中式英文 string table＋typed validation setter＋dirty；`Apply(store, HotkeySwapper)` 先 swap hotkey（register-new-first、OS 拒絕即回滾）再 atomic save，任一失敗回滾 working copy／執行中 hotkey／已存設定。新增原生 modal `DialogBox` 設定頁 `app_host/settings_dialog.{h,cpp}`（模板 `resources/NimbleRun.rc`、ID `resources/resource.h`），tray「Settings」開啟；涵蓋 hotkey、recent 8–40、hide-after-launch、theme、user folders（Add/Remove/Include subfolders）、extension 勾選、clear usage、reset。`usage_store` 新增 `Clear()`（save 失敗還原 records）；main.cpp 啟動 hotkey 改由 `settings.hotkey` 解析、Apply 後即時更新 hide-after-launch。新測試 `nimblerun_settings_ui_test`（12 case：validation、persist round-trip、save-failure/OS-reject rollback、reset 只寫 settings.ini、clear usage 不動 settings、string keys 集中）；`ctest -R settings_ui` 1/1、全套件 13/13 通過。
- 2026-08-05（NR-015 done）：新增純值 `ui/panel_layout.{h,cpp}` 與 `ui/panel_palette.{h,cpp}`（`nimblerun_ui` 庫，無 HWND/COM）：DIP 常數（640×432、row 48、tile 30、list 16..624／top 60、search box、font 16/14/11），`LayoutForDpi(dpi)` 把 DIP 依 dpi/96 轉成 physical px（`ClampWindowSize` 保留 32px 邊距），`ResolveColors(Theme, system_dark, high_contrast, SystemColors)` 回傳 0xRRGGBB 純值面板色（light/dark 自訂色＋high contrast 注入系統色，選取列另有獨立 `selected_border`）。OS 讀取留在 main.cpp：深色偵測用 `HKCU\...\Themes\Personalize\AppsUseLightTheme` registry（不用 winrt），high contrast 用 `SPI_GETHIGHCONTRAST`，系統色 `GetSysColor`→0xRRGGBB。main.cpp 接上：Render 改用 DIP 幾何＋每幀 ResolveColors（色變則重建 device resources）、選取列加 1–2px 邊框（非顏色訊號）、hit-test/icon key/VisibleRowCount 用 `LayoutForDpi(GetDpiForWindow)`、ShowPanel 以 cursor monitor 的 `GetDpiForMonitor` 做 DIP 尺寸＋work-area clamp、`WM_DPICHANGED` 採 suggested rect 並重排 search EDIT、主題於每次 ShowPanel 重載（next-launch 語意）。accessibility 採 model-level：`PanelModel::AccessibleNameFor(index)`／`SelectedAccessibleName()`（回傳 display_name，無選取回空），host 未實作 WM_GETOBJECT/IAccessible（文件化為後續最小 IAccessible）。`panel_model_test` 不動；新測試 `nimblerun_dpi_theme_accessibility_test`（10 case：96/144/192 layout bounds、monotonic、clamp、light≠dark、system 跟隨 OS、HC 覆寫、border≠fill、theme 不觸 AppEntry、per-row＋selected accessible name）；`ctest -R dpi_theme_accessibility` 1/1、全套件 14/14 通過。
- 2026-08-05（NR-011 done）：新增純值 refresh coordinator `catalog/catalog_refresh.{h,cpp}`：per-source dirty／500ms debounce 合併、`MarkSourceFullRescan`（overflow 立即到期）、`BeginGeneration(sources)` 記 generation 與待收來源、`ApplySourceResult/Failure` 僅在該 generation 全來源回報後 atomic 重建 merged（不顯示半成品）、stale generation 忽略、單一來源失敗保留舊 entries。新增 `catalog/catalog_cache.{h,cpp}`（`catalog.cache` schema=1、tmp＋atomic replace、corrupt 改名保留、newer schema 原檔不動、載入後 dedup）與 `app_host/catalog_watcher.{h,cpp}`（每 root 一背景 thread 的 `ReadDirectoryChangesW`，user-folder 依 recursive flag 設 `bWatchSubtree`，overflow 以 full-rescan marker 回報，`CancelIoEx` 乾淨關閉）。main.cpp：啟動先載 cache 立即顯示、再背景 full rebuild；watcher 事件 500ms debounce；`Ctrl+R`／tray Refresh 全來源重建、成功 launch 不觸發；AppsFolder 於面板顯示且距上次成功 >10 分鐘才背景重列舉；settings 套用後重啟 watcher＋重建。`PanelModel` 改持 catalog pointer＋`SetCatalog`/`SetRecent` 以支援 snapshot swap。兩次 subagent 皆未產出，主 Agent 直接實作；新測試 `nimblerun_catalog_refresh_test`（10 case：debounce 合併、overflow 立即 full rescan、stale generation 不覆寫、失敗保留舊 snapshot、單一來源失敗隔離、AppsFolder 10 分鐘、無部分 snapshot、cache round-trip／corrupt→rebuild／newer schema）；`ctest -R catalog_refresh` 1/1、全套件 15/15 通過。
- 2026-08-05（NR-014 done）：新增 HKCU Run 集中封裝 `settings/startup_option.{h,cpp}`（§FR-012 擇一：Run value，非 Startup folder）。Injectable `StartupOptionRegistry{base=HKCU, subkey}` seam，測試指向 `HKCU\Software\NimbleRunTest\<pid>` 絕不碰真實 Run key；全模組只透過 `base` 存取，無 HKLM 路徑（by construction 只影響目前使用者）。`GetStartupStatus`（Disabled/Enabled/EnabledMoved/UnknownError，REG_SZ 值與 `GetModuleFileNameW` 大小寫不敏感比對）、`SetStartupEnabled`（enable 寫 REG_SZ "NimbleRun" 指向目前 EXE，disable 只 `RegDeleteValueW` 刪該 value、不動 key 與其他 entry、absent 為 no-op）。設定頁加 "Launch at startup" checkbox（新 ID `IDC_AUTO_START`，Launcher group 加一行並下移 theme，dialog 300×350）；`SettingsEditor::SetAutoStart` 依既有 setter 模式（round-trip settings.ini）。Apply 流程與 hotkey rollback 完全獨立：先寫 Run entry、persist 成功才保留，Apply 失敗以純 registry 呼叫回滾（不經 hotkey swapper）；auto_start=true 於 OK 時固定重寫以修復移動 EXE；fresh 狀態不建立任何 entry。新測試 `nimblerun_startup_option_test`（10 case：fresh Disabled、enable 建立指向 module path 的 value、disable 只刪自己 value 且 OtherApp 存活、absent disable 為 no-op、per-user 只有 HKCU、moved 偵測、re-create 重寫目前路徑、editor round-trip 不觸 swapper、hotkey reject 時整份回滾、string keys 集中）；`ctest -R startup` 1/1、全套件 16/16 通過。
- 2026-08-05（NR-018 done）：新增純值 `pins/pin_store.{h,cpp}`（`nimblerun_pins` 庫）：`favorites.txt` 為版本化 UTF-8 TSV（首行 `schema=1`，其後每行 `<escaped stable_id>\t<last_seen_utc epoch>`，行序即 pin order；§10.2 名稱／§10.4 versioned 首行／FR-011 last_seen 的取捨已在 header 註解文件化）；`Pin`（重 pin 冪等、只刷新 last_seen、不換位）、`Unpin`、`IsPinned`、`OrderedPins`、`Reconcile`（present 刷新 last_seen；absent 超過 30 天丟棄；空 catalog 一律不動 pin，避免第一次掃描失敗誤刪）；沿用 tmp＋flush＋atomic replace、corrupt 改名保留、newer schema 原檔不動。PanelModel 新增 `SetPins`：空白查詢改為 pinned（依 catalog snapshot 解析、absent 的 pin 不顯示但紀錄保留）→ recent（已 pin 者跳過，dedup 符合 AC-002）。main.cpp：WM_RBUTTONDOWN 改為 TrackPopupMenu context menu（依 pin 狀態顯示 Pin／Unpin，有效路徑才加「Open file location」，集中式字串表）；pin/unpin 後寫 store、刷新 model＋invalidate；ShowPanel 與 snapshot swap 時 reload＋reconcile pins；menu modal loop 期間抑制 WM_KILLFOCUS 隱藏。新測試 `nimblerun_pinning_test`（13 case，含 round-trip、pin order、30 天過期、空 catalog 不刪 pin、panel model pinned 優先＋不重複）；`ctest -R pinning` 1/1、全套件 17/17 通過。
- 2026-08-05（NR-017 done）：新增 `diagnostics/diagnostic_log.{h,cpp}`（`nimblerun_diagnostics` 庫）：bounded 輪替 log（512 KiB cap、最多 2 份、tab/newline 消毒、寫入失敗不 throw），只記 stage＋error code＋短 detail，不含搜尋文字／個人路徑／command line。新增 `tests/release/release_evidence.ps1` 產出可重複 evidence（tool 版本、條件、命令＋exit code、build/ctest 輸出、process smoke＋短 soak、idle 量測、與 blocking threshold 比較，超過即 exit 1）。首次執行發現 idle thread count 14 > 8 blocking threshold：app 自有 3 執行緒（main＋2 Programs watcher）其餘為 OS 基礎設施，記為 known issue 不誤報。main.cpp 在 hotkey/launch/open-location 失敗時寫診斷 log。全套件 18/18 通過；`docs/release-evidence.md` 產出。
- 2026-08-05（NR-016 done）：啟用自 deferred（依賴 NR-010/012/015 皆完成）。新增純值 `app_host/matrix_model.{h,cpp}`（`nimblerun_panel_model` 庫）包住 PanelModel，固定 columns 的確定性 2D cursor（Left/Right 列內與 wrap、Up/Down 列間與 wrap、Enter 只啟動 cursor cell、Esc 兩階段、query 變更重設 cursor）；`Activate()` 回傳 cursor cell 自己的 launch_identity。`ui/panel_layout` 新增 cell 常數（`kCellWidthDip=112`、`kCellHeightDip=82`、`kIconSizeDip=40`）與 `GridColumns()`（640 DIP 寬 → 5 欄）。main.cpp render 改 matrix 呈現（固定 cell 幾何、icon 置中、選取 cell 邊框＋填色雙訊號），鍵盤/click/context menu 轉送到 matrix model；tooltip 未實作（與列表切片一致皆 model-level）。subagent 只產出 header，主 Agent 完成實作；新測試 `nimblerun_matrix_test`（11 case：GridRows 確定性、四方向移動與 wrap 含短列、Enter 只啟動選取 cell、空不啟動、query 重設 cursor、Esc 兩階段、不變 identity 資料、columns clamp）；`ctest -R matrix` 1/1、全套件 19/19 通過。
- 2026-08-05（NR-016 superseded）：面板呈現改回單欄垂直清單（icon＋名稱＋來源路徑），matrix 取消。決策細節：全狀態共用同一份清單版面（不保留兩套渲染與導航）；列高沿用 48 DIP、icon 30 DIP；packaged App 第二行顯示 `Windows app` 而非 Shell parsing name；長文字尾端省略號不換行；`↑`／`↓` 環繞，`PgUp`／`PgDn` 與滾輪以可見列數翻頁且夾在頭尾不環繞、翻頁後選取落在新的第一可見列；`←`／`→`／`Home`／`End` 交還搜尋欄；單擊即啟動；面板底部固定 footer 只放 `Scroll` ＋ `PgUp`／`PgDn` 按鍵指引（無網路、不放更新提示）；啟動失敗改為單次 MessageBox ＋ 背景自動觸發一次 Catalog refresh（已在進行則合併），面板保持顯示。已同步更新 `design-spec.md` §4.1／§4.2／§4.3／§4.7／§4.8／§4.9／§11／§12.3／§15／§19。拆為 NR-020（清單取代 matrix）、NR-021（翻頁與 footer）、NR-022（啟動失敗對話框）；NR-016 文件與完成紀錄保留作為決策軌跡。
- 2026-08-05（NR-020 done）：移除 matrix（`matrix_model.{h,cpp}`、`matrix_model_test.cpp`、`nimblerun_matrix_test` 目標、`kCellWidthDip`/`kCellHeightDip`/`kIconSizeDip` 與 `GridColumns()`），清單取代網格。`PanelModel` 新增純值 viewport 狀態：`SetViewportRows`（clamp ≥1）、`FirstVisibleRow`／`ViewportRows`、`SelectRow`（click），`first_visible_` 於 `RefreshRows` 重設為 0，`MoveSelection` 環繞後以最小位移把選取帶回可見範圍。main.cpp Render 改單欄清單（圖示 30 DIP 垂直置中、名稱 14／第二行 11 各佔半列、AppsFolder 顯示集中字串 `Windows app`、`selected_fill`＋`selected_border` 雙訊號、空狀態 `Building app catalog…`／`No matching apps`、名稱與小字格式 `NO_WRAP`＋CHARACTER 省略號 `SetTrimming`）；`kFooterTopDip=400` 保留 footer 空間、launch error 移到其上；EDIT 子類化只攔 `↑`／`↓`／Enter／Esc／Ctrl+R，`←`／`→`／Home／End 交還文字編輯；`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN` hit-test 改 `(y-list_top)/row_height + FirstVisibleRow()`，單擊選取後立即啟動。viewport 於 ShowPanel／`WM_SIZE`／`WM_DPICHANGED` 依 client rect＋DPI 更新。`panel_model_test` 新增 7 case（viewport clamp、少於可見列、下／上移出範圍各移一列、環繞跳尾端不越界、SetQuery／Reset／SetPins 重設、大 viewport 不負）；`ctest -R list_vertical_slice` 1/1、全套件 18/18 通過（移除 matrix 後）。
- 2026-08-05（NR-021 planned→）依賴完成，待排程：翻頁導航與 footer 按鍵指引。
- 2026-08-05（NR-021 done）：`PanelModel` 新增純值 `ScrollBy(delta_rows)`（`first_visible_ += delta`，夾在 `[0, max(0, RowCount()-viewport)]`、不環繞、夾住後選取設為新的第一可見列、空清單 no-op），為 PgUp/PgDn 與滾輪唯一捲動入口。EDIT 子類化攔 `VK_PRIOR`／`VK_NEXT`（±viewport 呼叫 ScrollBy 並 invalidate）；`WM_MOUSEWHEEL` 以 `SPI_GETWHEELSCROLLLINES` 讀系統一次捲動列數（`WHEEL_PAGESCROLL`→可見列數、讀取失敗退回 3）、乘 `WHEEL_DELTA` 方向倍數後呼叫 ScrollBy，靠 `DefWindowProc` 的 parent 轉送收到（單行 EDIT 不吞 wheel）。footer band 400~432 DIP：上緣 1 DIP dim 分隔線、右對齊 `Scroll` ＋ 兩個圓角按鍵方塊 `PgUp`/`PgDn`（框線 dim＋內填 card、`kSmallFontDip` text 色），指引字串集中於 `footer_strings`，`panel_layout.h` 新增 band 幾何常數；不加狀態/版本/更新文字。`panel_model_test` 新增 6 case（翻頁前進且選取跟隨、尾端/開頭夾住、列數少於 viewport 不捲、空清單 no-op、到底再回不環繞）；`ctest -R list_vertical_slice` 1/1、全套件 18/18 通過、build 無新增 warning。
- 2026-08-05（NR-022 done）：啟動失敗從面板底部紅字改為單次 `MessageBoxW`（`MB_OK|MB_ICONWARNING`、owner 面板 HWND、標題 `NimbleRun`、含 App 名稱與來自 shell_launch 錯誤碼的簡短英文原因），並在失敗時於背景觸發一次 Catalog refresh。移除 `g_last_launch_error`、其渲染區塊與 `g_error_brush`（palette 的 `error` 顏色欄位保留，為 `PanelColors` 聚合值的一員）；`OpenFileLocation` 失敗改走同一 `ShowErrorDialog`（僅記錄診斷，不觸發 refresh）。純值決策 `LaunchFailureRefreshGate`（`catalog_refresh.{h,cpp}`）：`OnLaunchAttempt(succeeded, refresh_in_progress)` 啟動失敗且無 rebuild 進行中才回傳 true、已在進行則合併、成功路徑重置不觸發、`OnRefreshComplete()` 在世代完成後放行下一次失敗觸發；`CatalogRefreshCoordinator` 新增 `IsRebuildInProgress()`。main.cpp 失敗分支沿用 Ctrl+R 同一個 `StartRebuild` 全來源入口（不另寫一條），`g_dialog_active` flag 比照 NR-018 抑制 `WM_KILLFOCUS` 自動隱藏，對話框關閉後 `SetFocus` 回搜尋 EDIT，面板保持顯示、不執行 hide-after-launch；`kRebuildDoneMessage` 世代完成時呼叫 `OnRefreshComplete`。`catalog_refresh_test` 新增 4 case（無 rebuild 觸發一次、rebuild 進行中合併、連續兩次失敗只觸發一次、成功不觸發）；`ctest -R catalog_refresh|shell_launch` 2/2、全套件 18/18 通過、build 無新增 warning。
- 2026-08-05（NR-023 ready）：搜尋欄改為與面板一致的圓角輸入框。決策細節：字型取系統 message font（`SystemParametersInfoForDpi` 的 `lfMessageFont`）只覆寫字級 24 DIP，不硬寫 `Segoe UI`（Win10 22H2 沒有 Segoe UI Variable，硬寫會讓兩個 OS 長相不一致，且違反 §4.9「使用系統字型」）；沿用原生 EDIT 以保留 caret／選取／IME／剪貼簿的系統行為，圓角框與 1 DIP 邊框由 D2D 畫在面板上、EDIT 內縮 12／6 DIP 藏住直角，`WM_CTLCOLOREDIT` 供色；**不做** placeholder／cue banner（參考截圖沒有此元素，且 `EM_SETCUEBANNER` 需在 manifest 掛 comctl32 v6，會連帶改變 `NimbleRun.rc` 對話框外觀）；`PanelColors` 新增 `input_fill`／`input_border` 兩欄而非重用 `card`＋`dim`，因為高對比模式下 `card == window` 會讓輸入框整個消失，違反 §NFR-006；明暗方向維持本專案語意（淺色＝白框在灰面板上），不照抄截圖的灰框白底，否則與我們的 `background=0xF3F3F3` 糊在一起。搜尋框高 48 DIP（16~64）使 `kListTopDip` 變 72，為維持截圖的 8 列可見，面板加高至 `kFooterTopDip=456`／`kPanelHeightDip=488`（§4.9 本就寫「高度依內容調整」）。此三值取代 NR-020 文件中的 400／432／7 列，NR-020／NR-021 文件不回頭修改，覆寫指示寫在 NR-023 內。已同步更新 `design-spec.md` §4.9。
- 2026-08-05（NR-023 done）：搜尋欄改為圓角輸入框。`panel_layout` 改 `kPanelHeightDip=488`／`kFooterTopDip=456`／`kListTopDip=72`／`kSearchBottomDip=64`，新增 `kSearchCornerRadiusDip=6`／`kSearchTextInsetDip=12`／`kSearchEditInsetYDip=6`／`kSearchFontDip=24`；`LayoutPx` 新增 `search_edit_*` 四欄與 `search_font_height`（負字高）。`PanelColors` 加 `input_fill`／`input_border`（light 白框灰邊、dark 深灰框淺灰邊、HC 用 system.window/window_text 實心可見）。main.cpp：`Render` 在清單前以 DIP 畫圓角填色框＋1 DIP 邊框（沿用 NR-015 線寬算法）、色變時重建 GDI brush；`UpdateSearchFont` 取 `SystemParametersInfoForDpi` 的 message font 只覆寫字高（失敗退回 `SystemParametersInfoW`），EDIT 建立後與 `WM_DPICHANGED` 各呼叫一次；`RepositionSearchEdit` 改用 `search_edit_*`，建立時的固定座標改為建立後立即重新定位；`WM_CTLCOLOREDIT` 供 `text`／`input_fill` 色與快取 brush（`RgbToColorRef` 3 行 helper）；`WM_DESTROY` 釋放 HFONT/HBRUSH；EDIT 不加 `WS_BORDER`／`WS_EX_CLIENTEDGE`。caret 決定：無法實際執行深色 UI，採保守修正——`WM_SETFOCUS` 走預設流程後 `CreateCaret((HBITMAP)1,0,0)`＋`ShowCaret`、`WM_KILLFOCUS` 後 `DestroyCaret`（solid caret 對背景取反色，明暗皆可見，詳見 NR-023 交接區）。`ui_palette_layout_test` 新增 5 case（16~64 框幾何與 8 列、200% 全翻倍、EDIT 矩形三 DPI 內含、light/dark/HC 輸入框色）；`ctest` 全套件 18/18 通過、build 無新增 warning、repo 已無 608/28 硬寫 EDIT 座標。
- 2026-08-05（NR-024 done）：新增 `Alt`＋數字直接啟動可見列。純值 `ui/quick_select.h`（header-only、不含 `windows.h`）：`kQuickSelectDigits=L"1234567890"`、`kQuickSelectSlotCount=10`、`QuickSelectSlotForKey`（`'1'`→0…`'9'`→8、`'0'`→9、其餘 -1）、`QuickSelectLabelForSlot`（長度 1 靜態字串，越界 nullptr）。`PanelModel::RowForVisibleSlot(slot)` const 純值：`slot<0 || slot>=viewport` 或 `first_visible_+slot>=RowCount()` → -1，否則 `first_visible_+slot`，不改變狀態。`SearchEditProc` 處理 `WM_SYSKEYDOWN`（Alt 位元 + Ctrl 未按 + model 存在，digit → `RowForVisibleSlot` → `SelectRow` 後沿用 `ActivateRow` 同一 usage/hide-after-launch/NR-022 路徑；無對應列仍 `return 0` 不嗶聲）與 `WM_SYSCHAR`（吞 10 個 digit 的系統嗶聲）；`Alt+Space` 等未綁定組合照舊走 `CallWindowProcW`。Render：`text_right` 由 `kListRightDip-8` 改為 `-kRowHintReserveDip`（=36，無條件預留不跳動），列迴圈對 `slot=i-first` 畫 20 DIP 窄方塊；footer 的 `draw_key_box` lambda 抽成檔案範圍 `DrawKeyBox`（footer 與列內共用唯一一份按鍵方塊繪製）；`panel_layout` 新增 `kRowKeyBoxWidthDip`/`kRowKeyRightInsetDip`/`kRowKeyGapDip`/`kRowHintReserveDip`/`kFooterWideKeyBoxWidthDip`；footer_strings 加 `Launch` 與格式片語 `Alt+1~`，`Scroll` 組左方新增 `Launch` ＋ 寬 56 DIP 方塊（內容 `Alt+1~` 接 `QuickSelectLabelForSlot(min(ViewportRows(),10)-1)`，label 量測沿用右對齊 lambda）。`panel_model_test` 新增 6 case、`ui_palette_layout_test` 新增 4 case；`ctest -R list_vertical_slice|dpi_theme_accessibility` 2/2、全套件 18/18 通過、build 無新增 warning。未完成：無。
- 2026-08-05（NR-028 ready）：AppsFolder 項目啟動失敗的根因確認為 launch identity 存錯，非資料品質問題。查證方式為在開發機列舉全部 313 筆 AppsFolder 子項目並實測 `SHCreateItemFromParsingName`：目前實作直接保存的 Shell parsing name 僅 39/313 可解析（僅 parsing name 恰為真實絕對路徑者），加上 `shell:AppsFolder\` 前綴後 313/313 可解析——即 87% 的「Windows app」項目按下去必定失敗。同時實測 `SIGDN_DESKTOPABSOLUTEPARSING` 與 `SIGDN_PARENTRELATIVEPARSING` 在 313 筆上回傳完全相同字串（differ=0），因此不需改取值 SIGDN，`stable_id` 的來源字串不變，**既有 pin 與使用紀錄零遷移**（此性質寫入 §10.3 作為約束）。決策細節：**不做可啟動性偵測**（`ShellExecute` 前無可靠 dry-run，且會把修好前綴後其實能開的 `.url`／`.chm` 誤殺，是在錯誤前提上蓋補丁）；`appsfolder_catalog` 補上與 `start_menu_catalog` 對齊的非 App 過濾（前者原本完全沒有過濾，這是 `.chm`／`.url` 只以「Windows app」身分漏出的原因），且**不為它加設定開關**——使用者要的是 app drawer 不是檔案瀏覽器；判準由黑名單改**白名單**（`.exe .com .bat .cmd .lnk .appref-ms .msc`），因為黑名單語意是「除了列舉到的以外都算程式」，遇到未預期文件型別會靜默漏出，代價是 `.ps1`／`.vbs` 被排除（已知並接受）；判準抽成共用純值模組 `catalog/app_filter.{h,cpp}` 供兩個列舉器呼叫，不留第二份副檔名清單；判準**不套用於 user folder 來源**，否則使用者手動加進 `settings.ini` 的副檔名（如 `.msi`）會被無聲擋掉；**AUMID 不得做副檔名判斷**——`Microsoft.WindowsCalculator_8wekyb3d8bbwe!App` 天真取最後一個點會被誤判成副檔名，可靠的路徑／AUMID 分界是「字串是否含反斜線」，此點列為迴歸測試；新設定 `include_windows_apps` 預設 **true**（關閉會使封裝 App 完全消失，它們沒有 Start Menu 捷徑可代替），關閉時整個跳過 AppsFolder 列舉而非在項目上加旗標或查詢時過濾，設定變更走既有 `MarkSourceFullRescan`；安裝檔（`.msi` 等）不特別處理，使用者需要時自行加入 `catalog_extensions`——已查證該清單讀檔時接受任意副檔名且 `SettingsEditor` 不會清掉手加值，但對話框只有五個寫死勾選框、無自由輸入欄，故目前只能手改 `settings.ini`，此限制記錄於 NR-028 而不修。設定對話框重排為 `Catalog sources` group（`Include Windows apps` → user folders → 子標題 `Extensions to scan in user folders:`），理由是新勾選框與 user folders 同屬「catalog 從哪來」這個維度，而現況 `Extensions to scan` 作為獨立頂層 group 會被誤讀為全域設定；一併修好底部按鈕列（`OK` 與 `Cancel` 原本不同 y、`IDC_STATUS` 與 `Cancel` 矩形重疊）。`.rc` 只在本 item 改一次：日後若實作 handoff 預留的 NR-027（移除 `recent_count`），只需從已整理好的對話框刪兩個控制項。編號用 NR-028 是因為 NR-025～NR-027 已預留給搜尋對齊評分／結果上限／最近清單固定 20，但那三份文件尚未撰寫；NR-028 雖編號較大，優先度最高。本輪同步更新 `design-spec.md` 新增 §2.6 術語（釘住 Shell parsing name／launch identity／identity key／program-like item／AUMID 的邊界，此 bug 的成因是詞彙未釘死而非邏輯寫錯）與 §FR-004a，並修訂 §FR-004／§FR-006／§FR-010／§FR-013／§10.3。決策由使用者逐題確認（Q1=c、Q2=c、Q3=a、Q4=a、Q5=不改安裝檔）；未問直接決定者：`stable_id` 維持不變、`app_filter` 模組化與其不套用於 user folder、`include_windows_apps` 預設值與生效方式、術語表寫進 design-spec 而非另開領域模型文件（避免第二個真實來源）。

- 2026-08-05（NR-028 done）：新增純值模組 `src/catalog/app_filter.{h,cpp}`（加入 `nimblerun_catalog` 庫、不含 `windows.h`）：`IsProgramLikeTarget` 依序「空→false → 無 `\`／`/`→AUMID→true → URL scheme（`file:` 除外）→false → `unins*`→false → 白名單副檔名 `.exe .com .bat .cmd .lnk .appref-ms .msc`（大小寫不敏感）→true，否則 false」；`start_menu_catalog` 的 `ToLower/FileName/FileStem/Extension/IsUrlTarget` 搬入本模組並由 header 導出，原處副本刪除、`LooksLikeNonAppTarget` 刪除改呼叫 `!IsProgramLikeTarget`（`link.target` 空時不呼叫判準、保留項目；`ShortcutIsWeb`/PIDL/SIGDN_URL web 判斷維持原樣）。`BuildAppsFolderEntry`：新增 `!IsProgramLikeTarget(parsing_name)` 過濾（caller 維持略過並計入 `failed_items`）、`launch_identity` 改 `L"shell:AppsFolder\\"+parsing_name`、`source_path` 維持 parsing_name、`stable_id` 維持 `HashStableId(NormalizePathKey(parsing_name))` 一字不改。新設定 `include_windows_apps`（預設 true、存 settings.ini、`SettingsEditor::SetIncludeWindowsApps`）；main.cpp worker 的 AppsFolder case 在關閉時不呼叫 `EnumerateAppsFolderCatalog()` 並回報空 entries（走既有 `ApplySourceResult` 清空路徑）、ShowPanel 的 10 分鐘到期排程加 `include_windows_apps` guard，設定變更沿用既有全來源 `StartRebuild`。設定對話框重排為 `Catalog sources` group（`Include Windows apps` 勾選框 → `User folders:` → `Extensions to scan in user folders:` 標籤＋五勾選框），`IDC_FOLDERS_GROUP`→`IDC_CATALOG_SOURCES_GROUP`、`IDC_EXTENSIONS_GROUP`→`IDC_EXTENSIONS_LABEL`、新增 `IDC_INCLUDE_WINDOWS_APPS`/`IDC_FOLDERS_LABEL`；四顆按鈕同 y（OK 最右）、`IDC_STATUS` 移按鈕下方；dialog 300×358→320×366；無 comctl32 v6。新測試 `nimblerun_app_filter_test`（AUMID 含 3 個防誤判迴歸、白名單命中/未命中含大小寫、URL scheme 與 `file:///` 走路徑規則、`unins*`、空字串）；`appsfolder_catalog_test` 新增 launch identity 前綴、`.chm` 過濾、零遷移（寫死期望 hash `445ac7f22c5f914c`）；`settings_store_test` 新增 round-trip＋舊格式無 key 預設 true。實測本機 AppsFolder 313→252 筆（61 筆非程式項被濾除）。`ctest` 全套件 19/19 通過、clean build 無 warning。

- 2026-08-05（NR-029 ready）：空白查詢狀態改為 icon grid，搜尋狀態維持單欄清單。動機是清單一頁只有 8 格，容不下 `recent_count` 預設的 20 個常用項目，釘選功能因此發揮不出來；grid 一頁 24 格可一次看完。此決策**推翻 NR-020「全狀態共用同一份清單版面、不切換版面」**那一條，但回到 §1／§3.1 原本就寫的「空白時呈現常用 App 圖示網格」意圖；NR-016 仍維持 `superseded`，只恢復意圖不恢復其實作。逐題確認：Q1=接受第 0→1 個字元的版面跳動並改 spec（跳動只發生一次，且此刻視覺焦點在搜尋欄）；Q2=路徑列放進既有 footer band 左半（NR-021 本就「左側留白」，不吃掉任何一格 cell，截斷重用既有 `CreateEllipsisTrimmingSign`）而非新增獨立列或加高面板；Q3=hover 只覆蓋 path bar 與淡填色、**不移動選取**（`Enter` 的目標永遠是看得見的邊框，滑鼠掃過不得偷改鍵盤狀態）；Q4=6 欄 × 4 列、cell 101×96 DIP、icon 40 DIP、一頁 24 格（再密下去換到的是名稱辨識度，而 grid 的識別線索只剩 icon＋名稱）；Q5=滾輪一個 grid 列、`PgUp`／`PgDn` 一整頁（兩種手勢粒度不同才有意義）；Q6=`Alt+1~0` 落在可見前 10 格並在格子右上角畫徽章，`quick_select.h` 與 footer 指引皆不改。未問直接決定者：**不復活 `MatrixModel`**，改在 `PanelModel` 加 `SetGridColumns`／`Columns()`（query 非空時回傳 1），清單狀態即「欄數＝1」，兩態共用同一份 viewport／捲動／選取程式碼，唯一新增邏輯是 `first_visible_` 夾邊界後向下對齊 `Columns()` 倍數以免出現半列；`←`／`→` 只在 grid 狀態攔截（此時搜尋欄為空，插入點移動本無作用），清單狀態的 NR-020 驗收條件不受影響；grid 不加 pin／recent 分隔線或分組標題（順序本身即區隔）；hover 只存在於 grid 狀態且為視窗層純視覺狀態，僅在命中格改變時 invalidate，不新增 timer；`kCellWidthDip`／`kCellHeightDip`／`kIconSizeDip` 沿用 NR-016 被刪除的原名加回。已同步更新 `design-spec.md` §4.2／§4.3／§4.7／§4.8／§4.9／§12.3／AC-002／新增 AC-002b／AC-003 措辭／§19.1。

- 2026-08-05（圖示效能規劃，NR-030～NR-037）：icon 取得改為背景 worker ＋ 磁碟持久化 decoded bitmap。使用者逐題確認的決策如下。**採磁碟持久化**（而非只做記憶體 LRU ＋ 預熱），代價是必須改 §FR-009 與 §9 職責表第 645 行明寫的「不：永久保存全部 bitmap」，由 NR-030 一次改完。**不讀 explorer 的 `iconcache_*.db`**（無公開 API、格式隨 build 變動、explorer 持有檔案鎖）；系統 icon cache 幫我們省的只有「找到圖」，`SHCreateItemFromParsingName`＋`GetImage` 的建 COM／載 DLL／抽資源／縮放／解碼上傳每顆仍是 1–10 ms，24 顆 50–200 ms，這才是要買掉的成本（讀自家 4 KiB PNG 約數十–數百 µs，差兩個數量級）。**不引入 SQLite 或任何 DB**（§10.2 明文禁止，且 DB 讀 blob 的磁碟往返不會比讀小檔少）。尺寸階梯定為 **48／96／256**，取最小的 ≥ 需要的實體像素：40 是非標準尺寸（Shell 標準只有 16／32／48／256），`GetImage(40)` 本身就是 48 縮到 40，存 40 等於存資訊更少的同一張圖；96 雖非原生資源尺寸（由 256 縮得），保留它是因為 150%（多數筆電出廠預設）下 40 DIP grid 需 60 px，只有 {48,256} 會讓那台機器每顆都存 256px（約 5 倍位元組與解碼），而三層與兩層在程式上只差一個陣列元素。**快取鍵移除 DPI 與畫面尺寸**（`stable_id + variant`）：D2D 降尺寸是免費的 GPU 取樣，讓磁碟層不知道畫面長什麼樣，改版面或換螢幕都不使既有快取失效；DPI 變更視為罕見事件，允許整批重抓，換取簡單好維護。**不新增 pre-resized 的 grid 專用 cache**：預先縮好一份 40×40 之後畫圖仍是一次 `DrawBitmap`，省下的是零，卻要付第二套失效規則，且只留 40×40 會在 DPI 一變時失去來源。LRU 上限由寫死 64 改為推導 `釘選數 + recent_count + 24`（前兩項是預熱集，第三項是一次搜尋結果的工作集，少了它使用者打一次搜尋就把預熱好的釘選項目擠出去）。**grid 圖示維持 40 DIP**，不改 48 DIP：只有 100% 機器能享受 1:1，150% 機器改與不改都得縮放，不值得動 §4.3 的版面常數。持久化採**單一 pack 檔** `icons.cache`（非每顆一個檔）＋ mmap 讀 ＋ append 寫，記憶體常駐接近零（payload 不進 heap，OS 只 page in 實際碰到的部分），寫入時機為面板隱藏／idle／結束三處——只在關閉時寫不夠，tray 程式會被登出或工作管理員終結，`WM_ENDSESSION` 不保證跑完。**毀損處理是格式設計的一部分**：雙份 header slot（交替提交、`generation` 較大者為新）＋ 每筆 index entry 自帶 CRC32 ＋ 每筆 payload 自帶 CRC32，使復原粒度為一筆而非整檔（這消掉了單檔相對於 per-file 的唯一實質劣勢）；magic／雙 header 皆壞則刪檔重建、較新 schema 則不動原檔並停用磁碟層、單筆壞只丟該筆；毀損**不保留 `.corrupt` 副本**（與使用者資料的既有慣例刻意不同，因為這是可完全重建的快取）。§10.2 的「不得原地覆寫、須 `.tmp`＋replace」由 NR-030 界定為針對使用者資料，可重建快取允許就地 append，compaction 仍走 `.tmp`＋replace。執行緒模型**不用 lock**：`ID2D1Bitmap` 只能在擁有 device context 的 thread 建立，加鎖只會買到一個可以死的地方，改為單一擁有權（UI 獨佔 LRU 與 D2D，worker 獨佔 Shell COM／WIC／檔案）＋ `PostMessage` 傳純值，唯一同步原語是請求 deque 的 mutex/condvar；固定**一條** worker（§9 第 658 行禁止每圖一 thread），可見請求 `push_front`、預熱 `push_back`，結果晚到不丟棄（那就是預熱），不實作取消。使用者授權**待機工作集放寬到 60 MiB**（§NFR-001 連帶 Private Bytes 50、面板顯示 75、執行緒 5、新增 `icons.cache` ≤ 32 MiB）。未問直接決定者：worker 自行 `CoInitializeEx` STA、PNG 而非 raw BGRA（96px 36 KiB → 3–8 KiB）、premultiply 轉換交給 `IWICFormatConverter` 不自行寫迴圈、預熱上限固定為一頁 24 格以維持 §FR-009 第 391 行「Catalog 不預解碼所有圖示」的相容性、`stable_id` 一字不改故 pin 與使用紀錄零遷移。切分為 8 個 item：NR-030（只改文件）、NR-031（variant 鍵與繪製時降尺寸）、NR-032（worker thread）、NR-033（pack 格式與毀損分類，純值）、NR-034（WIC PNG codec）、NR-035（檔案端 store）、NR-036（接線與 flush 時機）、NR-037（預熱一頁）；NR-033／NR-034 可與 NR-031／NR-032 平行。
- 2026-08-05（`AGENTS.md` 新增 Work item authoring rules）：把「敏捷切分、每個 item 0.5～2 天、自足到低階 agent 可接手、把 spec／development guide／AGENTS 約束抄進 item 內、明列必讀檔案與 scope／non-goals／acceptance／Agent checks、覆寫決策寫在新 item 不回頭改舊文件」寫成六條英文規則常駐於 `AGENTS.md`，避免每次規劃時重述。
- 2026-08-05（NR-029 done）：空白查詢改為 6×4 icon grid，搜尋狀態維持單欄清單，不復活 `MatrixModel`。`PanelModel` 新增 `SetGridColumns`（clamp ≥1）與 `Columns()`（query 空→`grid_columns_`、非空→1），清單狀態即「欄數＝1」；`ClampFirstVisible` 夾在 `[0, max(0, RowCount()-ViewportRows()*Columns())]` 後向下取整 `Columns()` 倍數、`EnsureSelectionVisible` 以整列（`Columns()` 個項目）為單位位移、`ScrollBy` 語意不變內部乘 `Columns()`、`RowForVisibleSlot` 上界改 `ViewportRows()*Columns()`（grid 下 Alt+1~0 對前 10 格），`MoveSelection` 一字不改。`panel_layout` 加回 `kCellWidthDip=101`／`kCellHeightDip=96`／`kIconSizeDip=40`／`kGridColumns=6` 與置中用的 `kGridLeftDip`；`UpdateViewportRows` 依 `Columns()` 選列高（48 list row／96 grid cell，結果區 72~456 DIP → 4 列），`ShowPanel` 在 `Reset()` 後才計算 viewport、`EN_UPDATE` 版面切換時重算並清 hover。main.cpp Render：`Columns()>1` 走 grid 分支（每格 icon 40 DIP 置中上半、名稱居中單行 `NO_WRAP`＋CHARACTER 省略號、選取 `selected_fill`＋`selected_border`、hover 只用 `card` 級淡填色不畫邊框、前 10 格右上角共用 `DrawKeyBox` 畫 NR-024 數字、空狀態沿用 `Building app catalog…`／`No matching apps`），icon 請求尺寸 40 DIP 物理像素；footer 左半新增 path bar（hover 優先於選取、AppsFolder 顯示集中字串 `Windows app`、`kSmallFontDip`＋dim＋`NO_WRAP`＋省略號、右界為指引區左緣減 `kFooterHintGapDip`，只在 grid 狀態繪製），右側指引與分隔線不變。輸入：`VK_LEFT`／`VK_RIGHT` 只在 `Columns()>1` 攔截、`VK_UP`／`VK_DOWN` 改 `∓Columns()`、`VK_HOME`／`VK_END` 維持交還；`WM_LBUTTONDOWN`／`WM_RBUTTONDOWN` hit-test 改 `FirstVisibleRow()+(row*Columns())+col`（超出 `RowCount()` 視為未命中），新增 `WM_MOUSEMOVE`（命中格改變才 invalidate＋`TrackMouseEvent(TME_LEAVE)`）與 `WM_MOUSELEAVE`（清 hover），版面切換清 hover，無新 timer。`PanelColors` 新增 `hover_fill`（light/dark＝card、HC＝system.highlight，HC 下靠 `selected_border` 區分 hover／selected）。`panel_model_test` 新增 7 case、`ui_palette_layout_test` 新增 2 case；`ctest` 全套件 19/19 通過、clean build 無 warning、repo 無 `MatrixModel`。未完成：無（視覺與三主題外觀屬人工驗證）。
- 2026-08-05（NR-030 done）：`design-spec.md` 新增「decoded 圖示磁碟持久化」並放寬待機資源預算。§FR-009 快取鍵改為 stable ID ＋ 標準尺寸 variant（48／96／256 實體像素），**不含 DPI 與版面尺寸**、取用取最小 ≥ 需求者並由 renderer 降尺寸；記憶體 LRU 上限改由 `釘選項目數 + recent_count 設定值 + 一頁格數（24）` 推導；decoded 圖示持久化於本機單一 `icons.cache` pack 檔（可完全重建的加速器，任何毀損降級運作）；取得圖示一律在背景 worker，UI thread 不等待 Shell／磁碟／解碼。§9 職責表 `icon_cache` 改為「背景取得、記憶體 LRU、標準尺寸 variant key、可重建的本機 pack 快取」，持久化範圍僅限曾顯示過的項目。§10.1 新增 `icons.cache` 目錄行；§10.2 新增 pack 檔格式條目並界定 append 例外只涵蓋可重建快取（使用者資料原子寫入規則不變）；§10.4 補快取降級對使用者不可見（不顯示錯誤提示）。§NFR-001 待機工作集 20→60 MiB、Private Bytes 15→50 MiB、面板顯示後 35→75 MiB、執行緒數 4→5（新增常駐 icon worker，§9 不為每圖示建 thread 不變），新增 `icons.cache` ≤32 MiB／>48 MiB 磁碟預算與 mmap 不常駐說明。§19.1 補「圖示取得與快取讀寫一律在背景 worker，UI thread 只接收純值結果」。`docs/performance-baseline.md` 同步更新四項數字並加 `icons.cache` 磁碟行；`docs/testing.md` 僅引用未複製數字，不需改。未動任何 `src/`、`tests/`。
- 2026-08-05（NR-031 done）：快取鍵收斂為 `stable ID + variant`，renderer 繪製時降尺寸，LRU 上限改為推導值（覆寫 NR-012 的 `IconKey`（stable_id＋size＋dpi）與 `kDefaultMaxItems=64`，`stable_id` 一字不改零遷移）。`icon_cache.h` 新增 `kIconVariants[]={48,96,256}`、`IconVariantForPixels`（range-for＋回傳最後一個元素，`<=0`→48）、`kIconCacheWorkingSetItems=24`、`IconCacheCapacityFor(pinned,recent)=pinned+recent+24`、`IconCache::SetMaxItems`（上限 0 視為 1、縮小從 LRU 尾端淘汰、不影響剩餘項目相對 recency）；`IconKey` 移除 `size`／`dpi`、`Encode()` 為 `stable_id+'|'+variant`，`#include <cmath>` 移除。`shell_icon_provider` 取得尺寸改 `key.variant`（取得方式不變）。main.cpp：`IconKeyFor(entry, needed_px)` 內部取 variant，needed_px 由既有 `LayoutForDpi()` 幾何給（grid `kIconSizeDip`、清單 `layout.tile_size`，不在 render 迴圈乘 dpi/96）；`DrawDecodedIcon` 本就用 `DrawBitmap(bitmap, tile, 1.0f, LINEAR)`，bitmap 尺寸為 variant、dest 為 DIP tile，兩者不再相等為預期；`ShowPanel`／設定 Apply／pin／unpin 各呼叫一次 `SetMaxItems(IconCacheCapacityFor(pins.size(), settings.recent_count))`；`LoadVisibleIcons()` 仍留在 UI thread（NR-012 的 `ponytail:` 註解保留並補一行指向 NR-032，未搬 thread）。`icon_cache_test` 重寫 helper 為 `Key(id, variant)`，新增 `IconVariantForPixels` 邊界表、30/40px 同鍵與 40/60px 異鍵、capacity 三值＋單調性、`SetMaxItems` 縮小／零上限／放大；`ctest -R icons` 1/1、全套件 19/19 通過、build 無新增 warning。
- 2026-08-05（NR-033 done）：新增純值 `icons/icon_pack_format.{h,cpp}`（加入 `nimblerun_icons` 庫、不含 `windows.h`），定義 `icons.cache` 位元組佈局：雙份 32-byte header slot（交替提交、generation 較大者為新、CRC 涵蓋 0..23）＋ 512 個 56-byte index entry（每筆自帶 entry CRC 涵蓋 0..51，flags bit0＝使用中）＋ 從 `kPayloadStart=28736` 起 append 的 payload 區；逐位元組 little-endian 讀寫（無 `#pragma pack`／無結構體 reinterpret_cast／無整結構 memcpy），`static_assert` 固定 64／28736／等式。`Crc32` 為 IEEE 802.3 reflected（poly 0xEDB88320，自行建表，已知向量空=0、`"123456789"`=0xCBF43926）。`DecodeHeader` 選取規則：短於 28736→`Absent`、皆無 magic→`BadMagic`、magic 在但 CRC 皆壞→`BothHeadersBad`、取 CRC 有效且 generation 較大者、schema>1→`NewerSchema`（out 仍填）、index_capacity≠512 視為 `BothHeadersBad`；`DecodeEntry` 回傳 `Free`／`CrcMismatch`／`OutOfBounds`（offset<28736、offset+len>payload_end 以減法防溢出、len=0xFFFFFFFF 一併涵蓋）／`BadVariant`（variant 依 `kIconVariants`）；`VerifyPayload` 只驗 payload CRC，與 entry CRC 彼此獨立。空槽判定依 in-use flag **先於** CRC（全零槽的 CRC 非零，CRC-first 會把正常空槽誤判為毀損）；撕裂寫入清掉 flag 時降級為 `Free`，該筆損失、鄰槽不受影響。`MakeEmptyPack()` 回傳長度恰 28736（slot A gen 1、slot B gen 0、512 空槽、payload_end=28736）供 NR-035 建新檔；`ParseStableIdHash`（16 hex、大小寫皆可，與 `HashStableId` 實測互通）；`MakeSourceStamp`（time XOR size×0x9E3779B97F4A7C15）。新測試 `icon_pack_format_test` 18 組（空檔/round-trip/毀損分類逐項/CRC 向量/hex 解析/fuzz 400 組固定 seed）；`ctest -R icon_pack_format` 1/1、全套件 20/20 通過、clean build 無 warning。未完成：無（檔案 I/O 屬 NR-035）。
- 2026-08-05（NR-035 done）：新增檔案端 `icons/icon_store.{h,cpp}`（加入 `nimblerun_icons` 庫，並把 `nimblerun_diagnostics` 設為其 PUBLIC 連結）。`IconStorePaths{pack}` 可注入（空路徑→Disabled），建構子第二參數 `max_bytes` 供測試覆寫、預設 `kMaxPackBytes=32MiB`。**讀取一律 mmap**（`CreateFileW(GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ)`＋`CreateFileMappingW(PAGE_READWRITE)`＋`MapViewOfFile`，`Lookup` 把位元組複製出 mapping，呼叫端不持 view 指標；`file_size_` 與 `view_size_` 分開追蹤，dead_bytes 以實際檔大小計算以涵蓋已提交 header 外的尾端垃圾）。`Open()` 依 `PackStatus` 分類：`Absent`/不存在→`CreateEmptyPack()`（先寫 `.tmp`＋flush＋`MoveFileEx(REPLACE|WRITE_THROUGH)`）並 `icon_pack_created`；`Ok`→`ScanIndex` 收集有效槽、毀損槽計入 `dropped_entries`；`BadMagic`/`BothHeadersBad`→刪檔重建、`recreated=true`、`icon_pack_recreated`；`NewerSchema`→不動原檔、`Disabled`、`icon_pack_newer_schema`；開檔/mapping 失敗→`Disabled`。**不保留 `.corrupt` 副本**（header 註解說明：這是可完全重建的快取，保留副本只堆積無用位元組，與使用者資料的慣例刻意不同）。`Lookup` 過期規則：`source_stamp!=0` 比對、`==0` 走 30 天 TTL（沿用 `pins/pin_store.h` 的 `kPinRetentionSeconds`，單一常數來源），再 `VerifyPayload`（entry 與 payload CRC 各自獨立）。`Put` 只緩衝於記憶體（`pending_` 以 `stable_id|variant` 鍵）。`Flush`：先 `PruneCorruptPayloads`（payload CRC 壞的槽回收供下次複用）→ slot 分配（**`FindFreeSlot` 必須跳過本輪已分配的 `touched` 槽，否則多筆新鍵會全撞同一槽互相覆蓋——實測抓到的 bug**；無槽時依 LRU 逐出非釘選、再逐出釘選）→ 依結果計算 live payload、超出 `max_bytes_` 時逐出（釘選豁免但仍有硬上限）→ 一次以 64 KiB 粒度 `GrowView` 擴檔→**防撕裂寫入順序**（payload→entry→最後 header 寫入另一 slot、generation+1，註解固定此順序；`FlushViewOfFile` 失敗→`ScanIndex` 恢復＋`ReadOnly`）→ 重算 stats→dead > live/2 時 `Compact`。`Compact` 把有效 entry 重排寫 `.tmp`＋replace；**必須先 `Unmap` 再 `MoveFileEx`**（mapping 未開 `FILE_SHARE_DELETE`，先 replace 會 ERROR_SHARING_VIOLATION——實測抓到的 bug），失敗刪 `.tmp`、重新 `MapFile`＋`ScanIndex` 恢復原檔、原檔不被修改。實作中也修掉兩個 `ScanIndex` 問題：`stats_ = StoreStats{}` 清掉了 `recreated` 旗標（改為先保存）、dead_bytes 改用 `file_size_`（`GrowView` 的 64 KiB 對齊會讓 `view_size_` 高估）。新測試 `tests/unit/icon_store_test.cpp`（CTest 目標 `nimblerun_icon_store_test`，`%TEMP%\NimbleRunTest\<pid>`，payload 合成位元組、時間/source_stamp 全注入，**每個 section 先 `WriteFileBytes(MakeEmptyPack())` 重置**以免上一 section 的 schema-2 或舊 entry 污染）：全新 Open/round-trip/同 id 不同 variant 兩筆/過期/TTL 29/31 天/Lookup 複本在 remap 後仍有效/撕裂復原（手工 craft payload+entry 但 header 未提交）/單筆 entry 毀損不影響鄰筆（`FindEntry` helper 依 hash 定位 slot，不假設 slot 順序）/payload 毀損→Lookup 空＋下次 Flush 回收槽/整檔壞 magic→recreated 且無 .corrupt/較新 schema→Disabled 且檔案位元組完全不變/淘汰（max_bytes=80）/釘選豁免/512 筆上限/compaction 檔案不隨覆寫成長＋無 .tmp 殘留/compaction 失敗（.tmp 佔用為目錄）保留原檔/`FILE_SHARE_NONE` 鎖檔→Disabled/隨機 fuzz 50 組。測試 helper 讀檔需 `FILE_SHARE_READ|WRITE`（PAGE_READWRITE mapping 存在時只開 READ 會 sharing violation，純測試端問題，生產路徑無外部併開）。`ctest -R icon_store` 1/1、全套件 23/23 通過、clean build 無新增 warning、grep `icon_store.cpp` 無 `HWND`/`InvalidateRect`/`MessageBox`。未完成：無（worker/renderer 接線屬 NR-036）。**後續 flakiness 修復（重跑 50 次全綠）**：`MoveFileEx` 的 atomic replace 會被即時掃描等外部短暫鎖檔以 ERROR_ACCESS_DENIED 偶發打斷（重複跑測試抓到的），原本失敗路徑的 `ScanIndex` 會把「只在記憶體執行、未寫磁碟」的淘汰 entry 復活；修正為（1）`Compact` 建檔時**不再改寫 `entries_by_slot_`**、失敗路徑只 `MapFile` 不 `ScanIndex`，in-memory index（含淘汰）在失敗後保持正確；（2）`ReplaceFileWithRetry` 對 atomic replace 做 5 次×10 ms 的 bounded retry，`CreateEmptyPack` 與 `Compact` 共用。
- 2026-08-05（NR-034 done）：新增 WIC PNG codec `icons/png_codec.{h,cpp}`（加入 `nimblerun_icons` 庫，同處新增 `windowscodecs` 連結）：`EncodeIconPng` 以 PBGRA `CreateBitmapFromMemory` 建 bitmap → `IWICFormatConverter` 轉 straight BGRA → PNG encoder（`GUID_ContainerFormatPng`，不設 property），`CreateStreamOnHGlobal` 記憶體串流、`Stat`/`Seek`/`Read` 讀回；`DecodeIconPng` 取 frame 0 → `IWICFormatConverter` 轉回 PBGRA → `CopyPixels`，`expected_size>0` 尺寸不符拒絕。premultiply↔straight 全程由 `IWICFormatConverter` 處理（不自行乘除）；拒絕條件依文件（Empty/0/超 1024/`pixels.size()` 不符/null/size=0）；COM 指標以 `std::unique_ptr`＋匿名 `ComRelease` deleter RAII（沿用既有釋放風格、未引入新 smart pointer 模板）；factory 每次 `CoCreateInstance` 不單例（`ponytail:` 註解記錄 worker thread-local 升級路徑）；模組不自行 `CoInitializeEx`。**實測發現**：WIC 的 PNG decoder 不驗證 chunk CRC——中段單一位元翻轉或截斷到 IDAT 中段都會「成功」解出垃圾，故為履行「corrupt input 回傳空」的契約，`DecodeIconPng` 在解碼前以既有 `icon_pack_format::Crc32`（實測與 WIC 產出的 PNG 逐 chunk 零失配，證實與 PNG 同為 IEEE 802.3 標準 CRC-32）做 `PngChunkCrcsValid` 逐 chunk 驗證（簽章＋長度越界＋CRC），pack 層 NR-033 的每 payload CRC 因此是第二層防禦。新測試 `tests/unit/png_codec_test.cpp`（CTest 目標 `nimblerun_png_codec_test`，自行 `CoInitializeEx(COINIT_APARTMENTTHREADED)`／`CoUninitialize`）：round-trip 48/96/256（不透明/全透明逐像素精確、半透明每通道 ±1 附註解、a==0 像素保持全零、四角獨特色）、PNG 簽章 `89 50 4E 47 0D 0A 1A 0A`、96×96 <20 KiB、全部失敗路徑回傳空不崩潰（空 bitmap/尺寸不符/2048/null/size=0/8-byte 截斷/中段翻轉/隨機 4KiB）、`expected_size=96` 拒絕 48×48 而 0 接受。`ctest -R png_codec` 1/1、全套件 21/21 通過、clean build 無新增 warning。未完成：無（worker/renderer 接線屬 NR-036）。
- 2026-08-05（NR-036 done）：把 NR-035 的 `IconStore` 與 NR-034 的 PNG codec 接到 NR-032 的 worker，形成三層取圖：記憶體 LRU → 磁碟 pack → Shell。`icon_worker.{h,cpp}`：建構子新增 `IconStore* store = nullptr`（nullptr＝沿用 NR-032 無磁碟行為）；佇列元素擴充為 tagged `IconTask`（`IconTaskKind{Load,Flush}`，單一佇列，flush 一律 `push_back`）；新增 `PostFlush(pinned_ids, now_utc)`；`Run()` 於 `CoInitializeEx` 後呼叫一次 `store->Open()`（不在 UI thread 開檔）；Load 依取圖順序：`Lookup` 命中→`DecodeIconPng(expected_size=variant)`（解碼失敗視同 miss 走 Shell）→回傳；miss／過期／毀損→`ShellIconProvider::Load` 成功→`EncodeIconPng`→`Put`（僅緩衝）→回傳，失敗→回傳空 bitmap 不寫入 store；`source_stamp` 在 worker 以 `GetFileAttributesExW` 取 last-write-time＋size 經 `MakeSourceStamp`，`shell:AppsFolder\` 前綴或無法 stat 者為 0（store 走 TTL）；`now_utc` 以 `std::time` 在 worker 取得（`PostFlush` 的 now 由 UI 傳入）。flush 三時機：面板隱藏後（UI 單點）、idle（佇列排空且 `pending_puts_ > 0` 在 `wait` 前 flush）、結束前（`Stop()` 內、`kStopFlushMaxPending=64` 上限，超過丟棄不寫，`ponytail:` 註解記錄「只寫最近使用部分」需 store API、量測有需要再補）。`main.cpp`：新增唯一隱藏匯流函式 `HidePanel`（Esc 第二階段／`WM_KILLFOCUS`／hide-after-launch／hotkey toggle 四個呼叫點改走它），於 `ShowWindow(SW_HIDE)` 後呼叫一次 `g_icon_worker->PostFlush(pins, now)`，pins 沿用 `g_pins->OrderedPins()` 不重新讀檔；`wWinMain` 在 worker 前建立 `IconStore`（`DefaultSettingsDir()/L"icons.cache"`、`kMaxPackBytes`、注入 `&diag`），生命週期長於 worker；`WM_DESTROY` 先 `Stop()`（含最後 flush）再釋放 D2D 與 store；診斷沿用 `IconStore` 既有 `icon-store` 事件（created／recreated／entries-dropped／newer-schema／flush-failed），無新增 log 呼叫。不做預熱（NR-037）、不改 variant 階梯／IconKey／LRU 容量公式／版面／輸入處理、未新增 thread／timer／可取消 I/O、UI 不顯示快取狀態。新測試 `tests/unit/icon_worker_test.cpp` 5 例（沿用 message-only window＋fake provider＋`%TEMP%\NimbleRunTest\<pid>`）：第一輪 provider 被呼叫 N 次、`PostFlush` 後 `Stop()`；第二輪同臨時路徑新建 store＋worker，provider 呼叫 0 次且位元組與第一輪相同；`source_stamp` 改變（`SetFileTime` +1hr）後第二輪重新走 provider；預寫 `schema_version=2` 檔時兩輪都走 provider 且檔案位元組不變；空 bitmap 不寫入 store（第二輪仍走 provider）；`Stop()` 在佇列非空且有待寫資料（worker 已進入延遲 provider）時 <2s 完成且最終 flush 落盤。fake provider 回傳 `key.variant × key.variant` 全不透明、每鍵獨特色，使 PNG round-trip 位元組精確可比；測試入口改 `NR-032/NR-036 icon worker check PASSED`。Agent checks：`ctest -R "icons|icon_worker|icon_store|png_codec"` 4/4、全套件 23/23 通過、clean build 無新增 warning、grep `main.cpp` 無 `Lookup`/`Put`/`Open`（唯一 `PostFlush` 是 worker 方法，非 IconStore）。冷啟動兩次第一幀即真實圖示屬人工驗證，未實測（見 NR-036 交接區，底層機制已由測試驗證）。未完成：無。
- 2026-08-05（NR-037 done）：面板隱藏後在背景預熱「下一次開窗一定會顯示的那一頁」——空白查詢第一頁（釘選＋常用，一頁 24 格）。`PanelModel` 新增純值 `EmptyStatePrewarmIds(max_items) const`：查詢非空或 `max_items==0` 回傳空；否則取 `rows_` 前 `min(max_items, rows_.size())` 筆 `stable_id`，重用 `RefreshRows` 的 pinned→recent 合併結果（repo 內無第二份合併邏輯），註解固定上限界線＝§4.3 一頁 24 格、§FR-009 禁止預解碼全 Catalog。`main.cpp` 在 `HidePanel` 內 `PostFlush` **之後**呼叫 `PrewarmEmptyStatePage`：variant 取 `kIconSizeDip` 於 `GetDpiForWindow` DPI 的實體像素經 `IconVariantForPixels`（下次開窗必為 grid 狀態，清單 variant 同階梯不需另預熱）；stable ID 由 `g_refresh->Snapshot()` 解析（查不到跳過，§FR-011 absent pin）；`Peek` 命中／`g_pending_icon_keys`／`g_requested_icon_keys` 三種鍵一律跳過（避免每次隱藏重送 24 筆）；以 `visible=false` `Post`（worker 既有 push_front/push_back 分流，無新優先度層級）。硬約束皆守：不新增 thread/timer/輪詢、預熱結果不 `InvalidateRect`（沿用 kIconReadyMessage 的 `IsWindowVisible` 判斷）、不改 LRU 容量公式／variant 階梯／store 格式／flush 時機、無新設定或 UI 字串、flush 先於預熱（NR-036 時機 1 不變）。新測試 `panel_model_test` 7 case（3 釘選＋5 常用→8 個且順序與 rows_ 一致、釘選 40→恰 24、`max_items=0`→空、查詢非空→空、空 catalog→空、const 不變 SelectionIndex/FirstVisibleRow/rows_、absent pin 濾除且回傳 ID 全部可在 catalog 找到）；`icon_worker_test` 1 case（3 個 `visible=false`＋1 個 `visible=true`，後者以結果到達順序證明排在第一個未處理預熱請求之前）。`ctest -R "list_vertical_slice|icon"` 5/5、全套件 23/23 通過、build 無新增 warning。未完成：無。「第二次開窗第一幀無 fallback 閃動」屬人工驗證未實測（底層機制：記憶體 LRU→磁碟 pack→Shell 三層已由測試驗證）；隱藏期間無 `InvalidateRect` 亦屬人工驗證。
- 2026-08-05（NR-038 done）：名稱正規化移到「每個 snapshot 一次」，排序改在 8-byte 索引上進行；`normalized_name` 由 dead field 變為發布 snapshot 必填（覆寫 NR-007／NR-011 的隱含假設，指示寫在 NR-038 內不回頭改舊文件）。`Normalize()` 提升為公開 `search_engine::NormalizeName`（CollapseWhitespace＋LCMAP_LOWERCASE 原封不動，query 每次按鍵與 catalog 共用同一函式不會漂移）；`CatalogRefreshCoordinator::SetSnapshot` 成為全專案唯一填寫點（空值才填、磁碟 cache 與測試自備值不被覆寫），`RebuildMerged` 改走 `SetSnapshot`，CMake 補 `nimblerun_catalog PUBLIC nimblerun_search`；`SearchApps` 迴圈內不再呼叫正規化，`RankedEntry` 改 `{MatchRank, uint32 index}`、比較子經 `catalog[index]` 取值、鍵序原樣，排序後才複製命中項，簽章與回傳型別不變、`panel_model.cpp:65` 零改動。此語意改變使「預填的 `normalized_name` 逐字採用」，既有 `panel_model_test`／`ui_palette_layout_test` 的 fixture 原本預填 raw display_name（非正規化）在 Release 下實測失敗，其 `Entry` helper 改為預填 `NormalizeName(name)`（對舊 `Normalize` 冪等、行為與順序不變），其餘 case 未動。新增測試：`search_engine_test` NormalizeName 三值、預填被採用（Zebra→note 命中／zeb 不命中）、5,000 筆計時（實測 633 µs = 0 ms、matched 5000）；`catalog_refresh_test` generation 填寫／直接 SetSnapshot 填寫／預填不被覆寫。`ctest -R search` 1/1、`ctest -R catalog` 4/4、全套件 23/23 通過、build 無新增 warning、repo 已無 `Normalize(` 呼叫。未完成：無（`icon_store_test` 曾並行 flaky 一次，單獨重跑通過，與本 item 無關）。
- 2026-08-05（NR-039 done）：面板任何位置都不能拖動，改為「搜尋輸入欄＋圓角框外框＋所有未壓到 App 項目的空白處」皆為拖曳把手，拖曳只影響當次顯示、不記位置（design-spec §127 置中不變）。只改 `src/app_host/main.cpp` 兩處、無新增檔案／timer／設定／`SetWindowPos` 呼叫點：`SearchEditProc`（:1516）新增 `WM_LBUTTONDOWN` case（:1535）——`DragDetect(edit, screen)`（先 `ClientToScreen`，消費至系統拖曳門檻）TRUE→`ReleaseCapture()`＋`SendMessageW(GetParent(edit), WM_NCLBUTTONDOWN, HTCAPTION, 0)` 交系統移動迴圈；FALSE→`EM_CHARFROMPOS`（client 座標）→`EM_SETSEL(index,index)`→`SetFocus(edit)`；不呼叫 `CallWindowProcW`（預設會 SetCapture 與 DragDetect 相衝）；代價已接受：EDIT 上按住拖曳選取文字失效，雙擊／Shift+方向／Ctrl+A 不受影響。父視窗 `WM_LBUTTONDOWN`（:1865）改 `cell < 0` 即 `ReleaseCapture()`＋`SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0)`（`lParam` 傳 0：`WM_NCLBUTTONDOWN` 的 lParam 是螢幕座標而 `WM_LBUTTONDOWN` 給 client 座標，預設處理自取系統游標位置），`cell >= 0` 的 `SelectRow`＋`ActivateRow` 一字不改。不碰 `WM_LBUTTONDBLCLK`／`WM_RBUTTONDOWN`／`WM_MOUSEMOVE` hover／`ShowPanel`／`RepositionSearchEdit`／`panel_layout.h`。Release build 無新增 warning；全套件 CTest 23/23 全綠（`icon_store_test` 並行時一次 flaky「oldest evicted」，單獨重跑與第二次全套皆通過，與 NR-038 交接區已知記錄一致）。六條手動驗收以 PowerShell＋user32 P/Invoke 送真實滑鼠／鍵盤事件並以 `GetWindowRect`／`EM_GETSEL`／usage.tsv 量測，逐條實測通過：拖輸入欄位移 (175,105)（略小於施作量因 DragDetect 門檻）；點 notepad 字元 3 → `EM_GETSEL=3..3`、再輸入 X 得 `notXepad`；圓角框細邊／grid 左留白／項目下方空白三處拖曳皆移動且可見；點 cell(0,0) 依 hide-after-launch 隱藏、程序數 419→420、usage 該項 count 1→2；拖曳期間 `IsWindowVisible` 全程 True（WM_KILLFOCUS 未觸發）、放開後打字／`VK_DOWN`／`Alt+1` 皆正常；拖到 (490,172) 隱藏再顯示回到工作區中央 (640,272)。驗證環境備註：本機 Alt+Space 被 FastStone Editor 占用（RegisterHotKey error 1409），測試暫改 `settings.ini` hotkey=Ctrl+Alt+Space 並於完成後刪除還原（原始無 settings.ini）。未完成：無（「拖曳結束後 hover 高亮延遲」為已知可接受之外觀瑕疵，未實測）。
- 2026-08-06（NR-042 done）：搜尋 caret 被面板重繪吃掉的根因修復——父視窗建立時加 `WS_CLIPCHILDREN`，D2D present 不再覆蓋子 EDIT 的像素。只改 `src/app_host/main.cpp` 的 `CreateWindowExW` 樣式列（實作時位於 `:2166`）由 `WS_POPUP | WS_BORDER` 改為 `WS_POPUP | WS_BORDER | WS_CLIPCHILDREN` 並加 Scope 指定的註解；不改 caret 建立參數（NR-023）、不加 `RDW_ALLCHILDREN`、不改成局部失效（NR-038 交接區所述路線仍不觸發）、旗標加在樣式列不加在 `WNDCLASSEX::style`。未新增測試（改動是 GDI/D2D present 行為，單元測試只能斷言 style bit，屬於把 diff 抄一遍）。Release build 無新增 warning、全套件 CTest 23/23 全綠；九條視覺人工驗收（caret 持續可見、移動、退格、貼上／全選、外框、版面回歸、高 DPI、雙主題、跨螢幕）依 AGENTS.md 屬人工驗證，Agent 未操作視窗。未完成：無。
- 2026-08-06（NR-041 done）：釘選狀態視覺記號。grid 格左上角實心圓點、list 列最左緣 3 DIP 直條，皆以 `g_selected_border_brush` 繪製、形狀（非顏色）傳達狀態（§NFR-006）、畫在選取邊框之後不被蓋住；純繪製改動，只改 `src/app_host/main.cpp` 兩處，不新增筆刷/text format/D2D 資源，版面與 palette 逐位元組不變。Release build 無新增 warning、全套件 CTest 23/23 全綠；視覺人工驗證屬追蹤表外。未完成：無。
- 2026-08-05（NR-040 done）：右鍵選單補上 Properties 與 Remove from recent。`UsageStore` 新增純值 `Forget(stable_id)`（找到 erase 回 true，找不到／空字串回 false，不呼叫 Save，與 RecordLaunch 一致）；`PanelModel` 新增 `RecentStartIndex()`（私有 `recent_start_ = -1`；`RefreshRows` 空查詢 pinned 迴圈後設 `rows_.size()`、非空查詢兩分支設 -1），讓主機端分辨該 row 是否位於 recent 區。main.cpp：`ShowItemProperties` 走 `IsPathIdentity` 守門 → `ShellExecuteExW`（`lpVerb=L"properties"`、`SEE_MASK_INVOKEIDLIST|SEE_MASK_NOASYNC`）失敗記診斷＋`ShowErrorDialog`；選單在 Pin/Unpin 後依 `RecentStartIndex` 只在 recent 區 row 顯示 Remove from recent，分隔線＋Open file location＋Properties 只對 path identity 顯示；分派 `kCmdProperties`／`kCmdForgetRecent`（Forget→Save 成功才 `RefreshPanelSnapshot`＋invalidate，Save 失敗不動 view）。新字串集中在 `context_menu_strings`／`dialog_strings`。決策照項：Properties 與 Open file location 同守門條件；Remove from recent 只在 recent 區出現；不做確認對話框／清空全部；不新增焦點例外旗標（Properties 開啟後面板隨前景自然隱藏）。新測試 5 case（usage：既有 id→true 且不復現、不存在→false 完全不變、空字串→false、Forget+Save 重載消失其餘完好、Forget 後重啟動以 count==1 重現；panel model 4 case：3 pin＋5 recent→3、全 pin→rows 數、非空查詢→-1、無 pin→0）。`ctest -R "recent_usage|list_vertical"` 2/2、全套件 23/23 通過、build 無新增 warning。七條手動驗收以 PowerShell＋user32 P/Invoke 逐條實測通過：filtered 與 grid 的 Properties 皆開啟 Shell 真實內容對話框（#32770「PowerShell 7 (x64) - 內容」）；Remove from recent 使該記錄從 usage.tsv 消失、格遞補、面板不隱藏且再顯示仍在 recent 外；pinned 格選單無 Remove from recent；UWP（搜尋 store）選單僅 Pin、無 Open/Properties；Pin/Unpin 未回歸；Properties 對話框取得前景後面板依 WM_KILLFOCUS 隱藏。驗證期間寫入的 usage.tsv／favorites.txt 已還原成實作前內容。未完成：無。
