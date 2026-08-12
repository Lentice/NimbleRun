# NimbleRun 唯讀稽核報告

- 稽核日期：2026-08-12
- 目標：`E:\GitHub\NimbleRun`
- 依據：`AGENTS.md`、`docs/design-spec.md`、`docs/development.md`、`docs/work-items.md`、全部 `src/`、`tests/` 與相關文件
- 目前 HEAD：`1ea6fff`（`main`，與 `origin/main` 相同）
- 稽核方式：唯讀檢視、`rg`、`git status`、`git diff --check`、`git log`、既有 `build` 的 `ctest -N`

本報告沒有修改既有檔案；本檔本身是依使用者要求新增的報告。

## 結論摘要

目前程式已經是可用的多來源 App Drawer 垂直切片，不是 `AGENTS.md` 與 `README.md` 所描述的 Phase 0 fake-grid probe。主要風險集中在生命週期與訊息驅動的邊界：重建替換及 worker 停止可能無限等待，明確 Refresh 沒有套用規格要求的重掃節流；此外，提交版 tooltip 仍有已知訊息常數錯誤，而工作樹的修補尚未提交。

未發現生產程式使用 `CreateProcess` 啟動 App、網路／telemetry、把使用者資料寫到 EXE 旁，或以 busy loop 取代 message loop。資料解析有大小上限，持久資料一般使用 temp + flush + replace；主要缺口是部分寫入失敗被靜默忽略。

## 1. Ponytail／過度設計稽核

以下是實際可刪除或收斂的複雜度。每項都列出位置、應刪內容與最小替代形狀。測試用 callback／factory seam（例如 `RebuildPipeline` 的 `ThreadFactory`）沒有列為問題，因為它們有實際測試消費者。

| 優先 | 位置 | 應刪／收斂 | 替代 | 理由 |
|---|---|---|---|---|
| 中 | `src/app_host/panel_model.h:17-20`、`src/app_host/panel_model.cpp:243-250`、`src/app_host/main.cpp:2463-2466` | 刪 `PanelAction::identity`，並刪 `PanelAction` 這個只有 `launch` 的包裝 | `PanelModel::Activate()` 直接回傳 `bool`；host 仍以 selection index 呼叫既有的 `ActivateRow` | 生產呼叫者只讀 `action.launch`，從不使用 `action.identity`；identity 重新從 model selection 取得，形成死欄位與重複介面。 |
| 低 | `src/ui/cell_tooltip.h:69` | 刪 `CellTooltip::IsVisible()` | 保留內部 `visible_`，若未來真的有消費者再加回查詢 | `rg` 顯示沒有任何生產或測試呼叫點；目前只是預留 API。 |
| 低 | `src/ui/cell_tooltip.h:58`、`src/ui/cell_tooltip.cpp:88-115` | 刪 `EnsureCreated` 的第二個 `tooltip_owner` 參數，並可將方法收為內部實作 | `EnsureCreated(HWND panel)`，owner 固定使用 `panel` | 全 repo 唯一呼叫是 `Show()`，且永遠傳同一個 `panel`；第二個參數是未使用的未來彈性。 |
| 低 | `src/catalog/app_filter.h:19-27` | 刪 header 內的 anonymous namespace | 讓 `FileName` 成為 namespace scope 的 `inline` 函式 | `inline` 已足以處理多個 translation unit；目前 anonymous namespace 會在每個 translation unit 產生獨立內部副本。 |
| 低 | `src/app_host/main.cpp:1272`、`:2078-2080` | 刪 `ShowLoadIssueNotice` forward declaration 與一層轉呼叫 | 兩個呼叫點直接呼叫既有的 `ShowInfoBalloon` | wrapper 沒有策略、驗證或狀態，只是三行轉發。 |
| 低 | `src/app_host/main.cpp:731-733` | 刪 `UpdateTooltipTimer` 內重複的 `Hide`／`KillTimer` | 改呼叫既有 `HideCellTooltip(window)` | `HideCellTooltip` 在 `:750-753` 已是相同的集中 hide path；保留兩份容易日後只改一份。 |
| 低 | `src/app_host/main.cpp:978`、`:1380` 與所有 `StartRebuild` call site | 刪 `StartRebuild` 的未命名 `HWND` 參數及每個傳入的 `window` | `StartRebuild(std::vector<CatalogSource>)` | 參數完全沒有讀取；重建本身透過 pipeline 的 callback 回 UI。 |
| 低 | `src/app_host/main.cpp:337-339`、`src/app_host/rebuild_pipeline.cpp:13-15` | 刪其中一個只包 `GetTickCount64()` 的 helper | 直接使用現有 helper 或在唯一呼叫點使用 `static_cast<std::int64_t>(GetTickCount64())` | `MonotonicMs` 與 `NowMs` 是同一個 native clock 的兩份命名包裝。 |
| 低 | `src/app_host/main.cpp:1517` | 刪未使用的 `const D2D1_SIZE_F target_size` | 不需要替代；footer 已在 `:1786` 取得自己的 `target_size` | 這是實際 unused local，不提供任何行為。 |

Ponytail 淨效果約可刪或收斂 35–55 行、0 個 dependency；不需要新增 abstraction。`SearchApps` 的每次 O(catalog) scan、D2D 每格建立 bitmap、icons 分層與 `PanelHost` 沒有列入：前兩者已有量測／設計決策，後兩者在 `docs/work-items.md` 的否決方向中已有具體理由。

## 2. 重要問題：正確性、資源、執行緒、安全與規格

### 2.1 高優先問題

#### H1 — UI thread 在重建替換時使用無界 join

- 位置：`src/app_host/rebuild_pipeline.cpp:96-98`；契約在 `src/app_host/rebuild_pipeline.h:80-85`。
- 問題：`RebuildPipeline::Start()` 先呼叫無參數 `Shutdown()`，即 `INFINITE`。它在 UI thread 執行，而舊 worker 可能仍卡在 Shell／COM／目錄列舉。觸發點包括 `Ctrl+R`／tray Refresh、settings apply、launch failure refresh，以及 show 時的 AppsFolder refresh。
- 影響：面板會在開始下一個背景 rebuild 前卡住；這違反 `docs/design-spec.md §FR-008、§9.4` 的「背景掃描、不阻塞 UI」及有界 shutdown 要求。現有 shutdown 只有 `WM_DESTROY` 路徑使用 5 秒 timeout，不能保護 rebuild replacement。
- 最小方向：不要在 UI thread 以無界 `Shutdown()` 等待舊 generation；沿用現有 cancellation、generation 與 handoff 設計，將 replacement 變成可合併／有界等待的路徑，並確保 detached worker 的 owner lifetime 契約仍成立。

#### H2 — icon worker 停止仍是無界 join

- 位置：`src/icons/icon_worker.cpp:87-100`；呼叫點為 `src/app_host/main.cpp:3000-3008`。
- 問題：`IconWorker::Stop()` 設 stop flag、通知 condition variable 後直接 `thread_.join()`。worker 可能在 `IconProvider::Load`、Shell extension、WIC decode 或 `IconStore::Flush` 內；目前沒有 timeout。
- 影響：`WM_DESTROY` 可能無限卡住，與 `docs/design-spec.md §9.4` 明確要求「等待有界，超時即繼續退出」相反。`kStopFlushMaxPending` 只限制 pending count，不限制 Shell／I/O 的時間。
- 最小方向：沿用 rebuild 的 bounded wait／安全 process-exit lifetime 策略，或把可取消邊界放在 worker 所有可能長時間阻塞的操作前；不要用 `TerminateThread`。

#### H3 — 明確 Refresh 繞過重掃節流

- 位置：`src/app_host/rebuild_pipeline.cpp:68-72`、`:96-98`；`src/app_host/main.cpp:2537-2545`。
- 問題：`RebuildReason::Explicit` 無條件直接 `Start()`，不經 `AcceptRebuildStart()`。`Ctrl+R` 與 tray／空白處的 `Refresh Apps` 都會投遞 `kRefreshMessage`；同 user process 也可偽造 `WM_APP` 訊息。
- 影響：快速重複 Refresh 可反覆啟動三來源完整掃描，並與 H1 疊加成 UI stall。這直接違反 `docs/design-spec.md §NFR-004` 對「可被無限重複驅動的重掃描路徑限流」的要求。
- 最小方向：讓 explicit full rebuild 使用既有的 per-source start gate／合併 pending intent；被節流時只記錄一次 pending refresh，不要再新增另一套 timer 或 queue abstraction。

### 2.2 中優先問題

#### M1 — 提交版 tooltip 仍使用錯誤的訊息常數；修補只在未提交工作樹

- 位置（HEAD）：`src/ui/cell_tooltip.cpp` 在 `HEAD` 的前 10 行及 `Show()` 約 `:135-136`。
- 問題：提交版把 `WM_USER + 52` 自訂為 `TTM_SETTIPTEXTW`，再把字串指標直接傳給 tooltip。Windows SDK 的 `WM_USER + 52` 是 `TTM_NEWTOOLRECTW`，不是文字更新訊息，也不存在該名稱的 SDK 文字 API；因此 tooltip text update path 的 payload 型別不對，tooltip 可能空白或讀取錯誤資料。
- 工作樹狀態：目前未提交的 `src/ui/cell_tooltip.cpp` diff 已改為 `TTM_UPDATETIPTEXTW (WM_USER + 57)` 並傳完整 `TOOLINFOW`，這是正確方向，但尚未進入 HEAD。`git status` 顯示它是 modified，不可視為已交付修補。
- 最小方向：保留工作樹的 `TOOLINFOW` 修補並提交；為 message constant／native tooltip path 保留一個 focused runtime 或 message-shape self-check。

#### M2 — 找不到 `InitCommonControlsEx`，tooltip 建立失敗也完全靜默

- 位置：`src/ui/cell_tooltip.cpp:88-115`；全 repo 沒有 `InitCommonControlsEx`／`InitCommonControls` 呼叫；`src/resources/NimbleRun.manifest` 目前只有未提交的 Common Controls v6 dependency diff。
- 問題：程式依賴 `TOOLTIPS_CLASS`，但沒有顯式初始化 common-control classes；`CreateWindowExW` 失敗時 `EnsureCreated` 直接 return，沒有 log 或 fallback。
- 影響：在乾淨執行環境中 tooltip 可能整條 flow 都沒有可觀測結果；即使 v6 manifest 已載入，也沒有把初始化契約寫在程式內。這是 tooltip flow 的 runtime 風險，不是單元 geometry test 能覆蓋的問題。
- 最小方向：在建立第一個 common control 前用 `InitCommonControlsEx` 初始化所需 class，並在建立失敗時留下 bounded diagnostic；不要只依賴 manifest。

#### M3 — 沒有對應列的 Alt+digit 被吞掉，與規格「未綁定」相反

- 位置：`src/app_host/main.cpp:2387-2401`、`:2412-2420`。
- 問題：`Alt+1..0` 只要是 digit 就 `return 0`；即使 `RowForVisibleSlot(slot)` 回傳 `-1`，仍被視為「bound digit」。`WM_SYSCHAR` 又把所有十個 digit 一律吞掉。
- 影響：在少於 10 個可見項目、空白 footer 或列表尾端按下沒有對應項目的數字時，按鍵不會依 §4.7 的「沒有對應項目則不綁定」交給預設處理。
- 最小方向：只有 `row >= 0` 並完成啟動時才吞掉 `WM_SYSKEYDOWN`／`WM_SYSCHAR`；無 row 時 fall through。

#### M4 — cold-start cache row 可顯示、可搜尋，但直到重建驗證前不能啟動

- 位置：`src/catalog/catalog_cache.cpp:161-166`、`src/launch/shell_launch.cpp:7-10`、`src/app_host/main.cpp:1064-1085`。
- 問題：cache load 一律將 `launch_verified=false`，launch boundary 拒絕它。啟動時 cache 會立即發布，完整 source rebuild 則在背景進行。
- 影響：若 user 在第一次完整 rebuild 完成前按 Enter／點擊一個剛從有效 cache 顯示的 App，會走 launch failure，顯示「The app entry is invalid.」，而非啟動該 App。這與「valid cache 立即提供舊結果」的體驗有張力。
- 判定：這是 NR-113 明確選擇的安全 gate，不是本次新發現的未防護執行風險；它確實使 persistence／restart flow 暫時不完整。若要改，必須先重新決定 cache trust model，不應只移除 `launch_verified` 檢查。

#### M5 — 重要持久化寫入失敗被忽略，跨 restart 可能靜默遺失狀態

- 位置：`src/app_host/main.cpp:1092-1098` 的 `g_usage->Save()`；`src/app_host/main.cpp:1374-1377` 的 `SaveCatalogCache()`；`src/catalog/catalog_cache.cpp:81-87` 的 `AtomicWriteUtf8Text()` 回傳值被丟棄。
- 問題：usage launch 成功後在記憶體更新，但 Save 失敗不 log、不 retry、不通知；cache writer 也沒有回傳成功／失敗給 host。pins／settings 的大部分 caller 有檢查 Save，這兩條路徑沒有。
- 影響：磁碟滿、權限或 AV lock 時，本次使用紀錄可在下次 restart 消失；cache failure 雖可 rebuild，但診斷不可見。atomic replace 本身是正確的，缺的是 failure propagation。
- 最小方向：usage 至少沿用現有 `g_diag` 記錄一次失敗；cache 讓既有 writer 回傳 bool 或以既有 pipeline completion callback 記錄失敗。不要改成 in-place write。

#### M6 — directory watcher 也使用無界 join

- 位置：`src/app_host/catalog_watcher.cpp:234-243`。
- 問題：`Stop()` 先呼叫 `CancelIoEx`，但隨後直接 `thread.join()`，沒有與 rebuild 相同的 timeout。
- 影響：通常 `ReadDirectoryChangesW` 會被取消而快速退出，但任何取消／Shell filesystem edge case 都可能讓關閉不再滿足有界等待。這是 H2 的同類生命週期風險，嚴重度較低因為 OS I/O 有明確 cancel path。
- 最小方向：採用 bounded wait；timeout 時讓 watch state 以安全 shared lifetime 存活，不能先關閉仍被 worker 使用的 directory handle。

### 2.3 低優先但是真實的邊界缺口

#### L1 — 搜尋輸入超過 1023 字元會靜默截斷

- 位置：`src/app_host/main.cpp:2648-2652`。
- 問題：每次 `EN_UPDATE` 固定讀入 `wchar_t buffer[1024]`，沒有 `EM_LIMITTEXT`，也沒有用 `GetWindowTextLengthW` 動態取得完整內容。
- 影響：這不是記憶體安全漏洞，但長貼上／IME 輸入會在 UI 與 `PanelModel` 中被靜默截斷，搜尋結果可能與使用者看到的字串不一致。
- 最小方向：若要有產品上限，明確以 `EM_LIMITTEXT` 設定並文件化；否則使用 native length API 取得完整文字。

#### L2 — `wstring_view::data()` 被當成 NUL-terminated 字串

- 位置：`src/app_host/main.cpp:2054-2064`。
- 問題：`ShowInfoBalloon(HWND, std::wstring_view)` 對 `text.data()` 呼叫 `wcsncpy`。`wstring_view` 不保證後面有 NUL；目前兩個 caller 傳的是 literal 或 `std::wstring`，所以現有路徑通常安全，但 helper contract 不安全。
- 最小方向：用明確長度的 `std::min` copy 並手動補 NUL，或把此僅供本檔使用的 helper 參數改為 `const std::wstring&`。

#### L3 — 空白查詢的 grid 顯示正確，但 prewarm 走錯分支

- 位置：`src/app_host/panel_model.cpp:210-215`。
- 問題：`RefreshRows()` 用 `NormalizeName(query_).empty()` 判定空白查詢，但 `EmptyStatePrewarmEntries()` 仍用 `!query_.empty()`。輸入一個或多個空格時畫面仍是 grid，卻不會 prewarm 下一頁圖示。
- 影響：不影響 catalog 或 launch correctness，只會使這個 transient state 使用 fallback icon；是兩個已存在判定規則漂移。
- 最小方向：重用 `NormalizeName(query_).empty()`，不再新增 `IsBlank` helper。

### 2.4 已核對、目前沒有發現違反的規則

- 使用者資料根目錄由 `src/settings/settings_store.cpp:122-150` 從 `FOLDERID_LocalAppData` 建立為 `%LOCALAPPDATA%\NimbleRun`；production 的 settings、pins、usage、catalog cache、icon cache、logs 都從此 root 衍生。
- 文字使用者資料由 `src/storage/atomic_text_file.h:402-441` 走 `.tmp`、`FlushFileBuffers`、`MoveFileExW(...REPLACE_EXISTING | WRITE_THROUGH)`。pins、usage、settings caller 也使用同一 writer。`icons.cache` compaction 另有 temp + replace。
- App launch 只在 `src/launch/shell_launch.cpp:12-25` 使用 `ShellExecuteExW`；沒有從搜尋字串組 command line，也沒有 `CreateProcess` launch path。
- 沒有 production 網路／telemetry API；`docs/design-spec.md` 中的 URL 只在參考資料。
- message loop 使用 `MsgWaitForMultipleObjectsEx` + `GetMessageW`（`src/app_host/main.cpp:3386-3417`）；watcher 使用 blocking `ReadDirectoryChangesW`，錯誤 retry 的 `Sleep(1000)` 是 persistent error backoff，不是 idle busy loop。
- `AppEntry` 是普通可 copy value，UI model 不持有 Shell COM pointer；COM 只在 enumerator／icon worker 所屬 thread 使用。
- storage parser 有 16 MiB read、1M lines、20k cache／pin／usage row 等上限；settings 對 local absolute path、extension、root 數與 hotkey 長度也有 validation。
- production UI strings 經 `main.cpp`、`settings_dialog.cpp` 的 English constants／literal 提供；未發現把 Traditional Chinese 文件文字帶入 UI 的路徑。

## 3. 端到端使用流程

| 流程 | 主要 code path | 狀態 | 稽核結果 |
|---|---|---|---|
| 顯示／隱藏：Alt+Space、tray Open、outside click | `src/app_host/hotkey.cpp:35-80`；`src/app_host/main.cpp:2639-2647`、`:1923-2027`、`:2901-2923` | 大致完成 | hotkey 會 toggle、`ShowPanel` 清空 query、設定 focus 到 EDIT、依游標 monitor 置中；`WM_ACTIVATE(WA_INACTIVE)` 補上 child EDIT focus 時的 outside-click hide。若 show 同時觸發 stale AppsFolder rebuild，會落入 H1 的無界等待；tooltip 另受 M1/M2 影響。 |
| 打字／正規化／切換 grid-list | `src/app_host/main.cpp:2648-2668`；`src/app_host/panel_model.cpp:46-122`；`src/search/search_engine.cpp:93-188` | 大致完成 | `EN_UPDATE` → `SetQuery` → `NormalizeName` → empty grid 或 non-empty single-column list；空白查詢規則已統一，Esc 也會清實際 EDIT。輸入超過 1023 字元會截斷（L1），空白查詢的圖示 prewarm 漂移（L3）。 |
| Rank results | `src/search/search_engine.cpp:23-78`、`:93-188`；`src/app_host/snapshot_assembler.cpp:33-65` | 完成 | 五層 name rank、alias fallback、pin、usage、display length、normalized name、stable id tie-break 都有實作；`StampRankingFields()` 在 snapshot／usage／pin change 後重貼 derived fields。沒有看到以 command/search text 直接 launch 的路徑。 |
| 選取／Enter／滑鼠點擊／launch | `src/app_host/main.cpp:2462-2467`、`ActivateRow` `:1048-1103`；`src/launch/shell_launch.cpp:7-25` | 新鮮 verified catalog 完成；cold cache 部分完成 | Enter、滑鼠及 Alt+digit 最終共用 `ActivateRow`，成功後記 usage、依設定 hide；失敗保留面板並觸發一次 refresh + dialog。Shell boundary 正確，但 cache row 在重建完成前被 NR-113 gate 拒絕（M4），usage Save failure 又被忽略（M5）。 |
| Catalog startup rebuild／watcher／debounce／AppsFolder refresh | `src/app_host/main.cpp:3382-3383`；`src/app_host/rebuild_pipeline.cpp:68-290`；`src/app_host/catalog_watcher.cpp` | 架構完成，生命週期不完整 | generation、source failure isolation、stale result rejection、500ms debounce、AppsFolder 10分鐘 on-demand 與 event-driven wait 都有；但 explicit refresh 不節流（H3），開始下一輪時無界 join（H1），shutdown watcher join 無界（M6）。 |
| persistence across restarts | `src/app_host/main.cpp:3168-3200`、`:3255-3299`；`src/settings`、`src/pins`、`src/usage`、`src/catalog/catalog_cache`、`src/storage` | 大致完成 | startup 先 load settings/usage/cache，cache 可先顯示，pins／usage 於 assembler reconcile；資料位置與 atomic writer 正確。cache row 的 launch verification 是刻意安全 trade-off；launch usage 與 catalog cache 的 Save 結果沒有回報（M5）。 |
| quick-select numbers | `src/ui/quick_select.h:8-33`；`src/app_host/main.cpp:2382-2418` | 部分完成 | `1 2 … 9 0` 對應可見 slot 0–9，grid/list mapping 與 footer label 都有。無對應 row 時仍 return 0／吞 `WM_SYSCHAR`，違反 spec 的未綁定語意（M3）。 |
| tooltip：截斷判定、150ms、下／上方、離開／按鍵／scroll 隱藏 | `src/app_host/main.cpp:728-783`、`:2559-2567`；`src/ui/cell_tooltip.cpp`；`src/ui/cell_tooltip_test.cpp` | HEAD broken；工作樹修補未提交 | geometry 與純值測試覆蓋 below-first、flip、clamp；host 只對 truncated grid name arm 150ms timer，也在 pointer leave、mouse down、drag、page、hide 時 hide。提交版 text message payload 錯誤（M1），且沒有 common-control init（M2），所以實際 native tooltip flow 尚未能視為完成。 |

## 4. Repo hygiene、commit 與文件漂移

### Git 狀態

`git status --short --branch`：

```text
## main...origin/main
 M src/resources/NimbleRun.manifest
 M src/ui/cell_tooltip.cpp
?? docs/audit-herdr-claude.md
```

`git diff --check` 沒有 whitespace error。工作樹已有另一份未追蹤的 `docs/audit-herdr-claude.md`；本次報告不修改它。兩個 modified source files 不是本次稽核造成的：manifest 加入 Common Controls v6，tooltip source 包含 M1 的修補方向。

最近 commit 為：

```text
1ea6fff (HEAD -> main, origin/main, origin/HEAD) NR-177~180: close the tickets
558e1bb NR-176: close the ticket
18cf921 NR-176: grid footer shows the full Alt+0~9 quick-select range
5cf19f5 docs: 第十六次稽核第 2~5 輪決策紀錄與第 5 輪 clean review
```

### 工作項目拓撲

唯讀 script 核對結果：

- `docs/work-items.md` overview rows：177。
- `docs/work-items/NR-*.md` 檔案：177。
- tracker IDs 與檔名 `NR-###` 集合：完全相同，沒有 missing／orphan。
- item 文件中含 `Status:`／`狀態:` header：0；符合「status 只在 tracker table」規則。
- 已閱讀 `docs/work-items.md` 的「已否決的方向」；本報告沒有把已否決的 `PanelHost`、icons 全面重寫或測試 framework 重構重新包裝成新 item。

### 確認的文件漂移

1. `docs/testing.md:10` 仍寫「currently 31 checks」；既有 build 的 `ctest --test-dir build -N` 實際列出 32 項，新增的是 `nimblerun_cell_tooltip_test`（`tests/CMakeLists.txt:191-201`）。
2. `docs/release-evidence.md:8` 仍寫 `Total Tests: 31`，文件產生時間為 2026-08-10，commit 是 `dcd0a6b...`，都不是目前 HEAD `1ea6fff`。它應被標成歷史 evidence 或重新產生，不能當 current release evidence。
3. `AGENTS.md:47-55` 仍稱 repo 是 Phase 0 foundation、rendering probe、English fake app grid；`README.md:14-15` 也稱 current executable 是 probe。實際 source 已有真實 catalog、icons、settings、persistence、watcher 與 Phase 5 release gate。這會誤導 cold-start agent 的 scope 判斷。
4. `docs/work-items/NR-176-grid-footer-quick-select-range.md` 的 handoff 仍記錄 `main.cpp:1410` 的 unused `target_size` 與 31 tests；目前 unused local 是 `src/app_host/main.cpp:1517`，live registration 是 32。歷史 handoff 可保留，但若它代表目前狀態就已過時。

## 5. 建議處理順序

1. 先修 H1/H2，讓「重建 replacement」與「關閉」都真正有界；這兩個問題可能把正確的 UI flow 變成不可用的 hung process。
2. 將 H3 的 explicit refresh 接到既有節流／pending intent，避免 same-user `WM_APP` flood 變成無限完整掃描。
3. 提交並驗證 tooltip 工作樹修補，同時補 common-control initialization；再修 M3 的未綁定 Alt digit 行為。
4. 補 Save failure logging／propagation，然後同步 31→32、commit 與 Phase baseline 文件。
5. 最後收斂 Ponytail 小項目；它們不應排在 worker lifetime 與 user-visible flow 之前。

