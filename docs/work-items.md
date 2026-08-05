# NimbleRun Work Items

這是 NimbleRun 的實作追蹤總覽。產品行為唯一以 [design-spec.md](design-spec.md) 為準；本頁與 item 文件只同步拆分、依賴與驗收證據。若 Spec 後續變更，先更新 Spec，再調整受影響的 item。

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
| `superseded` | 曾完成，但產品決策改變後由另一個 item 取代；文件與完成紀錄保留作為決策軌跡 |

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
| NR-024 | Alt+digit quick select with per-row key hints | 3 | `ready` | NR-020, NR-021 | [NR-024](work-items/NR-024-quick-select-digits.md) |

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
```

可平行處理的前提是依賴已完成且寫入的資料／訊息邊界穩定；不要為了平行而複製同一份邏輯。

## 計畫決策紀錄

- 2026-08-04：`design-spec.md` 是唯一原始完整 Spec；本次新增 UserFolder 來源後，同步調整受影響的 item 與依賴。
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
- 2026-08-05（NR-024 ready）：新增 `Alt`＋數字直接啟動可見列。決策細節：鍵序固定為 `1 2 3 4 5 6 7 8 9 0` 並**依可見列順序指派前 N 個**，而不是死綁 10 鍵——可見 8 列時只用 `Alt+1`~`Alt+8`，避免出現永遠無效的死鍵，也不必為了湊 10 鍵去改剛定案的面板高度（NR-023 的 488 DIP／8 列）；日後若可見列變 9／10，第 9／10 列自動吃 `9`／`0`，無須改碼。按下即**直接啟動**（等同對該列按 Enter，沿用 `ActivateRow` 既有的 usage／hide-after-launch／NR-022 失敗對話框路徑），不做「只選取、再按 Enter」的兩段式——spec §4.8 的滑鼠單擊已定「指定某一列即啟動」，數字鍵是同一種動作，兩段式省不到操作。列內指引只印**數字本身**（約 20 DIP 窄方塊），修飾鍵 `Alt` 改由 footer 的 `Launch` `Alt+1~8` 說明一次：`Alt+` 對所有列都相同，重複 8 次是純冗餘，且每列 44 DIP 的方塊會吃掉 App 名稱寬度。此決策**覆寫 NR-021 的「footer 為固定內容」**（footer 新增的 `Alt+1~N` 隨可見列數組出），§4.9 本就定義 footer 是「當下適用的按鍵指引」；NR-020／NR-021 文件不回頭修改，覆寫指示寫在 NR-024 內。指引常駐而非「按住 Alt 才顯示」（後者需追蹤按鍵狀態且會閃爍），且列文字寬度無條件預留 36 DIP，有無指引都不跳動。鍵序對應抽成純值 header `src/ui/quick_select.h`（不含 `windows.h`，主鍵區數字 VK 值等於 ASCII）、列對應為 `PanelModel::RowForVisibleSlot(slot)` const 純值函式，兩者皆可不建視窗測試；`Alt` 組合走 `WM_SYSKEYDOWN` 並額外吞掉對應的 `WM_SYSCHAR` 以免系統嗶聲，不註冊新全域 hotkey、不裝鍵盤 hook。不支援數字鍵盤、不提供設定開關。已同步更新 `design-spec.md` §4.7、§4.9。
