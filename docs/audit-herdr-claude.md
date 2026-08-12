# NimbleRun 稽核報告（read-only，三軸：ponytail／重要問題／主要流程）

- 稽核日期：2026-08-12
- 稽核者：Claude（ponytail mode: full）
- 稽核基準：`HEAD = 1ea6fff`（NR-177~180 close the tickets）＋工作樹未提交變更
- 範圍：`src/**`（27.8k 行含 tests）、`docs/design-spec.md`、`docs/development.md`、`AGENTS.md`、`docs/work-items.md` 與 177 份 item 文件
- 本報告未修改任何其他檔案。

閱讀說明：這個 repo 已經過 16 輪稽核、180 個 work item，低垂的果實早被摘完。以下所有發現都經過原始碼確認，並標注是否與既有 item 的決策衝突。凡是既有文件已明確否決／接受的方向，我標為「已知決策」而不假裝是新發現。

---

## 0. 摘要排序（依嚴重度）

| # | 軸 | 檔案:行 | 問題 | 嚴重度 |
|---|---|---|---|---|
| I-1 | 重要 | `src/ui/cell_tooltip.cpp:10-15,140`（HEAD 版） | HEAD 送出的 `TTM_SETTIPTEXTW`(WM_USER+52) 實為 `TTM_NEWTOOLRECTW`，把字串緩衝當 `TOOLINFOW*` 解讀 → tooltip 永遠空白＋越界讀 | **CRITICAL** |
| I-2 | 重要 | `src/resources/NimbleRun.manifest`（HEAD 版）／全 repo 無 `InitCommonControlsEx` | HEAD 無 comctl32 v6 相依也不呼叫 `InitCommonControlsEx`，`TOOLTIPS_CLASS` 建立不保證成功、外觀非 §4.9 要求的系統主題 | **CRITICAL** |
| I-3 | 重要 | `src/app_host/rebuild_pipeline.cpp:97` | `Start()` 呼叫 `Shutdown()`（預設 `INFINITE`）→ UI thread 無界 join 列舉 worker，Ctrl+R／設定套用／watcher 事件都可凍結面板 | HIGH |
| I-4 | 重要 | `src/icons/icon_worker.cpp:96` | `IconWorker::Stop()` 無 timeout join；worker 卡在 Shell 圖示擷取即無限延遲關機，違反 §9.4「等待有界」 | HIGH |
| I-5 | 流程 | `src/launch/shell_launch.cpp:8` × `src/catalog/catalog_cache.cpp:166` | 冷啟動 cache snapshot 的每一列 `launch_verified=false`，rebuild 完成前按 Enter 一律失敗，且錯誤訊息誤導為「App 已被移除」 | HIGH |
| I-5b | 重要 | `src/app_host/main.cpp:2054-2065` | `ShowInfoBalloon` 對 `wstring_view::data()` 呼叫 `wcsncpy`，view 不保證 NUL 結尾 | MEDIUM |
| I-6 | 流程 | `src/app_host/panel_model.cpp:196` | `ScrollBy` 把選取強制設為 `first_visible_`：滾輪捲動會偷偷搬動鍵盤選取，違反 §4.8「Enter 永遠啟動具備選取邊框的那一格」的使用者預期 | MEDIUM |
| I-7 | 重要 | `src/app_host/catalog_watcher.cpp:108,150` | 監看 root 永久失效後 `Sleep(1000)` 重試迴圈永不退避（1 Hz 打磁碟）。**已知決策（NR-074）**，僅記錄殘留風險 | LOW |
| P-1 | ponytail | `src/app_host/main.cpp:1517` | `target_size` 宣告後未使用（build 唯一 warning，NR-180 交接區已記錄未修） | 刪 1 行 |
| P-2 | ponytail | `src/app_host/main.cpp:2078-2080`＋`:1272` 前置宣告 | `ShowLoadIssueNotice` 是 `ShowInfoBalloon` 的一行純轉呼叫 | 刪 4 行 |
| P-3 | ponytail | `src/app_host/main.cpp:1380` | `StartRebuild(HWND, …)` 第一參數自 NR-132 起未使用，卻仍在 5 處傳 `window` | 刪參數 |
| P-4 | ponytail | `src/app_host/main.cpp:2901-2909` | `WM_KILLFOCUS` 自動隱藏分支經 NR-085 後已由 `WM_ACTIVATE` 取代，註解自陳「dead on the two most common paths」 | 合併成一條路徑 |
| P-5 | ponytail | `src/app_host/main.cpp:731-733` vs `:750-753` | `UpdateTooltipTimer` 前兩行與 `HideCellTooltip` 完全相同 | 重用既有函式 |
| P-6 | ponytail | `src/ui/cell_tooltip.h:69` | `CellTooltip::IsVisible()` 零呼叫者（NR-180 建議 API 的殘留） | 刪 1 行 |
| P-7 | ponytail | `src/app_host/panel_model.h:17-20` | `PanelAction::identity` 只有測試在讀；生產路徑 `ActivateRow` 重新取 row | 縮成 `bool` |
| P-8 | ponytail | `src/catalog/app_filter.h:19-27` | header 內的匿名 namespace 包住已經 `inline` 的 `FileName` | 刪 2 行 |
| P-9 | ponytail | `src/app_host/main.cpp:337-339` | `MonotonicMs()` 與 `rebuild_pipeline.cpp:13` 的 `NowMs()` 是同一個 one-liner 的兩份 | 二選一 |
| H-1 | 衛生 | `docs/testing.md:9`、`docs/release-evidence.md:8` | 都寫「31 checks」，實際 32（NR-180 新增 `nimblerun_cell_tooltip_test`） | 更新兩處 |
| H-2 | 衛生 | 工作樹 | NR-180 標 `done`，但其驗收所需的兩項修正（I-1／I-2）仍未提交 | 提交或改狀態 |

---

## 1. Ponytail／過度設計軸

先講結論：**這個 repo 沒有典型的過度設計問題**。我特別檢查了容易長出贅肉的地方，結果如下：

- 沒有單一實作的 interface。唯一的抽象注入點（`IconProvider`、`RebuildPipeline::ThreadFactory`、`IconStore::IconStorePaths`）都是為了讓 Shell／thread／檔案系統可以在單元測試裡替換，各自都有對應測試在用，全部合格。
- 沒有重新發明 stdlib。`atomic_text_file.h` 的 `SplitLines`／`Trim`／`ParseInt64` 是 C++20 沒有 `std::from_chars` for wchar_t 的合理補位；`ReadAllBytes` 帶 16 MiB 上限與明確 error-out，不是 `std::filesystem` 能取代的。
- `docs/work-items.md` §已否決的方向 已經明確擋掉六條「抽象化衝動」（icons 疊層重寫、泛用 versioned persistence、`PanelHost` struct、`SearchKeys` 抽象等），而且每條都附了刪除測試與量測依據。這是我見過少數把 YAGNI 制度化的 repo。

因此以下只有小型死碼與零成本收斂，沒有結構性建議。

### P-1 `Render()` 內宣告後未使用的 `target_size`
`src/app_host/main.cpp:1517`
```cpp
const D2D1_SIZE_F target_size = g_render_target->GetSize();   // ← 無人使用
```
同一個名字在 `:1786`（`if (g_model)` 區塊之外）重新宣告，那一份才是 footer 幾何真正用的。`:1517` 這行是 NR-133 搬動 slot 幾何時留下的殘骸，是目前 build 的唯一 warning（NR-180 交接區也記錄了它，但沒人修）。
- **刪什麼**：`:1517` 整行。
- **換成什麼**：什麼都不用。`:1786` 已經在正確的 scope 算了一次。

### P-2 `ShowLoadIssueNotice` 是純轉呼叫
`src/app_host/main.cpp:2078-2080`（＋`:1272` 的前置宣告）
```cpp
void ShowLoadIssueNotice(HWND window, const std::wstring& text) {
    ShowInfoBalloon(window, text);
}
```
`ShowHotkeyConflictNotice` 的存在有理由（它綁死了一段固定文案），這一個沒有：兩個呼叫點（`:1334`、`:3374`）都自己算好了 `text`。
- **刪什麼**：函式本體、前置宣告，兩個呼叫點改叫 `ShowInfoBalloon`。
- **換成什麼**：`ShowInfoBalloon(window, text)`。

### P-3 `StartRebuild` 的死參數
`src/app_host/main.cpp:1380`
```cpp
void StartRebuild(HWND, std::vector<nimblerun::CatalogSource> sources) {   // 參數已無名字
```
NR-132 把 thread 建立搬進 `RebuildPipeline` 之後，HWND 就不需要了，但 5 個呼叫點（`:1074`、`:2015`、`:2541`、`:2595`、`:3382`）還在傳 `window`，前置宣告 `:978` 也還帶著它。一個沒有名字的參數就是在告訴讀者「這裡有東西沒清乾淨」。
- **刪什麼**：參數與 5 處實參。
- **換成什麼**：`void StartRebuild(std::vector<CatalogSource> sources)`。

### P-4 `WM_KILLFOCUS` 的自動隱藏分支已被 `WM_ACTIVATE` 取代
`src/app_host/main.cpp:2901-2909`，對照 `:2910-2923` 的註解：

> NR-085: ShowPanel puts focus on the search EDIT …, so the panel itself never holds keyboard focus and the WM_KILLFOCUS path above only fires for the EDIT — which does not tell its parent. "Click outside to hide" was therefore **dead on the two most common paths**.

也就是說 NR-085 已經確認 `WM_KILLFOCUS` 到不了面板 HWND，`WM_ACTIVATE(WA_INACTIVE)` 才是唯一有效路徑，但舊分支仍原封不動留著，兩者用同一組 modal flag 做同樣的事。兩個入口做同一件事就是兩個要同步維護的隱藏規則。
- **刪什麼**：`WM_KILLFOCUS` 的 `HidePanel` 分支（保留 `return 0`，或直接落到 `WM_ACTIVATE` 共用的 lambda）。
- **換成什麼**：只留 `WM_ACTIVATE(WA_INACTIVE)` 一條隱藏路徑；若擔心某個 shell 情境只送 `WM_KILLFOCUS`，就把兩個 case 合併標籤共用同一段程式，而不是兩份拷貝。

### P-5 `UpdateTooltipTimer` 手抄了 `HideCellTooltip`
`src/app_host/main.cpp:731-733`
```cpp
void UpdateTooltipTimer(HWND window) {
    g_cell_tooltip.Hide();
    KillTimer(window, kTooltipTimerId);      // ← 這兩行就是 HideCellTooltip(:750-753)
```
`HideCellTooltip` 的註解自己寫著「every tooltip hide point funnels through here」，結果第一個呼叫者就沒走它。
- **刪什麼**：那兩行。
- **換成什麼**：`HideCellTooltip(window);`。

### P-6 `CellTooltip::IsVisible()` 無呼叫者
`src/ui/cell_tooltip.h:69`。這是 NR-180 item 文件裡「建議 API」的原文照抄，`visible_` 只被 `Show`／`Hide` 內部用。
- **刪什麼**：`IsVisible()`（`visible_` 成員保留，`Hide()` 需要它）。

### P-7 `PanelAction::identity` 只服務自己的測試
`src/app_host/panel_model.h:17-20` / `panel_model.cpp:249`。生產路徑 `main.cpp:2462-2467` 只讀 `action.launch`，之後 `ActivateRow(SelectionIndex())` 自己重新取 `rows_[index]`（而且必須這樣做，因為它還要看 `IsMissingPin`）。唯一讀 `identity` 的是 `tests/unit/panel_model_test.cpp:122`。
- **刪什麼**：`identity` 欄位與該測試斷言。
- **換成什麼**：`Activate()` 回傳 `bool`（或直接 `bool CanLaunch() const`）。這同時消掉每次 Enter 的一次 `std::wstring` 複製。

### P-8 header 內的匿名 namespace
`src/catalog/app_filter.h:19-27` 把已經是 `inline` 的 `FileName` 包進 `namespace { }`。在 header 裡這樣寫會讓每個 TU 各持一份 internal-linkage 副本（現在只是無謂，未來若有人取它的位址就是 ODR 陷阱）。
- **刪什麼**：`namespace {` 與 `}` 兩行。
- **換成什麼**：什麼都不用，`inline` 已經給了正確語意。

### P-9 兩份 `GetTickCount64` one-liner
`src/app_host/main.cpp:337-339` 的 `MonotonicMs()` 與 `src/app_host/rebuild_pipeline.cpp:13-15` 的 `NowMs()` 是逐字相同的實作，服務同一個 `CatalogRefreshCoordinator` 的時間軸。NR-127／NR-154 已經做過同型的收斂（`kSchemaPrefix`、`ParseInt`），這一對漏了。
- **刪什麼**：其中一份。
- **換成什麼**：放到 `catalog_refresh.h`（時間軸的擁有者）並讓兩邊共用；`main.cpp` 只有一個呼叫點（`:2014`）。

### 其他檢查過但**判定不動**的地方
- `dedup.cpp` 的 same-name bucket、`icon_store` 的雙檔頭與 per-entry CRC32：都由 spec §10.2／§FR-007 明文要求，不是自行加碼。
- `panel_accessibility.cpp`（566 行 UIA provider）：§NFR-006 要求「所有 App item 提供可存取名稱」，owner-drawn 面板沒有便宜替代品。
- `main.cpp` 3454 行：已於 2026-08-10 依刪除測試否決 `PanelHost` 收斂，證據充分，我同意該結論，不重開。

---

## 2. 重要問題軸（正確性／資源／執行緒／安全／規則遵循）

### I-1（CRITICAL）HEAD 的 cell tooltip 送錯訊息 → 空白 tooltip ＋ 越界讀
`src/ui/cell_tooltip.cpp`（HEAD 版本）
```cpp
#define TTM_SETTIPTEXTW (WM_USER + 52)      // 不存在的訊息
...
SendMessageW(window_, TTM_SETTIPTEXTW, 0, reinterpret_cast<LPARAM>(name_.c_str()));
```
Windows SDK 裡沒有 `TTM_SETTIPTEXTW`。`WM_USER + 52` 是 **`TTM_NEWTOOLRECTW`**，它的 `lParam` 必須是 `TOOLINFOW*`。實際發生的事：

1. comctl32 會把 `name_.c_str()`（一個 wchar_t 緩衝）當成 `TOOLINFOW` 讀，先讀 `cbSize`（=名稱前 4 個 wchar 的位元組）再往後讀 ~72 bytes。短名稱（SSO，緩衝只有 16 wchar）→ **越界讀**。因為 comctl32 會先驗 `cbSize` 而多半直接丟棄，所以沒炸；但這純粹是運氣。
2. tooltip 文字從未被更新。`EnsureCreated` 的 `TTM_ADDTOOL` 是在 `name_` 還是空字串時送出的（`Show()` 先 `EnsureCreated`，之後才 `name_ = name`），而 comctl32 在 add 時就把文字複製走了 → **tooltip 永遠顯示空字串**。

也就是說 NR-178/179/180 三個 item 的產品目標（hover 顯示完整名稱）在 HEAD 上是完全失效的。

- **修法**：工作樹的未提交 diff 已經是正確修法——改用 `TTM_UPDATETIPTEXTW`（`WM_USER + 57`）並在送出前填好 `TOOLINFOW`。這份修正必須提交。
- **殘留（工作樹版仍在）**：`Show()` 的 `if (!window_ || tool_owner_ != panel) EnsureCreated(panel, panel);` 在 `window_` 已存在但 `tool_owner_` 不同時，`EnsureCreated` 的第一行 `if (window_) return;` 會直接返回，tool 不會為新 HWND 重新註冊。目前只有一個面板 HWND，所以無害；若要留這個分支，`EnsureCreated` 就該處理 owner 變更，否則把條件簡化成 `if (!window_)`，別留一個看起來會做事、實際不做事的判斷。
- **殘留**：`CellTooltip` 沒有解構子，`window_` 在面板 `WM_DESTROY` 後成為 dangling HWND。目前 `HideCellTooltip` 在 `WM_DESTROY` 內、視窗銷毀前呼叫，之後沒有人再碰它，所以不會出事；但一行 `window_ = nullptr` 比一段「為什麼這沒事」的推理便宜。

### I-2（CRITICAL）`TOOLTIPS_CLASS` 依賴未提交的 manifest，且從未呼叫 `InitCommonControlsEx`
- 全 repo grep 不到任何 `InitCommonControls` / `InitCommonControlsEx`（唯一 comctl32 命中是 `CMakeLists.txt:389` 的 link 清單）。
- HEAD 的 `src/resources/NimbleRun.manifest` **沒有** `Microsoft.Windows.Common-Controls` v6 相依；那段是工作樹未提交的變更。

後果：HEAD 載入的是 comctl32 v5.82，而 v5 的 common control window class 只有在呼叫 `InitCommonControls*` 之後才註冊 → `CreateWindowExW(TOOLTIPS_CLASS, …)` 很可能直接失敗，`CellTooltip::EnsureCreated` 靜默返回，tooltip 一次都不會出現。加上 v6 才有的 Win11 圓角主題外觀，design-spec §4.9（NR-180 剛改寫的那條）在 HEAD 上不可能成立。

- **修法**：提交 manifest 變更；並在 `wWinMain` 建立視窗前呼叫一次
  `INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);`
  即使有 v6 manifest，MS 文件仍要求顯式初始化；這是 3 行，換掉「靠隱式註冊」這個假設。

### I-3（HIGH）UI thread 在 `Start()` 裡無界 join 列舉 worker
`src/app_host/rebuild_pipeline.cpp:96-97`
```cpp
void RebuildPipeline::Start(std::vector<CatalogSource> sources) {
    Shutdown();                      // ← 預設 timeout_ms = INFINITE
```
`Shutdown(INFINITE)` 會跳過整段 `WaitForMultipleObjects` 分支，直接走 `if (finished)` → `worker.join()`。而 `Start()` 全部從 UI thread 進來：`kRefreshMessage`（Ctrl+R 與 tray Refresh）、`kSettingsMessage`（套用設定）、`ShowPanel` 的 AppsFolder on-demand、啟動時的第一次 rebuild、launch 失敗後的 refresh。

`cancel_` 會先被設起，而三個列舉器都會檢查它，所以多數情況下 join 很快。但 cancel flag 不能中斷**已經進去的 Shell 呼叫**：`IShellLinkW::Resolve` 對一個掛在斷線網路磁碟上的 `.lnk`、或 `FOLDERID_AppsFolder` 的 `BindToObject` 遇到壞掉的 shell extension，都可能卡數秒到無限。期間 UI thread 停在 window proc 裡：面板不重繪、不吃鍵盤、hotkey 訊息堆積。

這與 spec 的兩條規則直接衝突：§FR-009「UI thread 不得等待 Shell」的精神，以及 §9.4「worker join 應有可控取消路徑。等待有界」。NR-123 已經替**關機**路徑做了有界等待（`kJoinTimeoutMs = 5000`），但這條 in-session 路徑被漏掉了，而且 header `:80-85` 的註解把「Start() 用 INFINITE，永不 detach」寫成刻意設計。

- **修法**：`Start()` 改呼叫 `Shutdown(kJoinTimeoutMs)`，逾時就沿用 NR-123 的 detach 語意（此時**不能**重設 `cancel_`，否則被 detach 的舊 worker 會繼續跑完並嘗試 post 舊 generation——generation 檢查會擋掉內容，但 `cancel_` 的生命週期需要重新想清楚：目前 `Shutdown` 的 detach 分支根本不碰 `cancel_`，留在 `true`，於是**下一次 rebuild 的新 worker 一開工就自我取消**。這是 detach 路徑目前只在 process 結束時走才沒被發現的第二個 bug）。
- 最小可行版本：`Start()` 用有界 wait；逾時時把 `cancel_` 換成 per-generation 的 `shared_ptr<atomic<bool>>`，新 generation 拿新旗標，舊 worker 繼續看舊旗標。這樣 detach 與「新 worker 不被舊旗標毒到」兩件事一次解決。

### I-4（HIGH）`IconWorker::Stop()` 無 timeout
`src/icons/icon_worker.cpp:87-100`
```cpp
cv_.notify_all();
thread_.join();          // 無界
```
`Stop()` 只從 `WM_DESTROY`（`main.cpp:3006`）與解構子呼叫。worker 的迴圈體會呼叫 `provider_.Load()`（Shell icon 擷取，第三方 shell extension 可任意慢）與 `store_->Flush()`（磁碟）。這兩者都無法被 `stop_` 中斷 → 「結束」被按下後 process 可能無限期不退，違反 §9.4「關閉不得因等待 Shell extension 無限卡住…超時即繼續退出」。

- **修法**：與 NR-123 同型：`Stop(DWORD timeout_ms)`，用 `WaitForSingleObject(thread_.native_handle(), timeout)`，逾時 `detach()` 並讓 `main.cpp` 走現有的「刻意洩漏、不銷毀」模式（`g_rebuild_shutdown_timed_out` 已經是這個形狀，直接照抄）。注意 detach 後 `g_icon_handoffs.Clear()` 與 `IconStore` 的生命週期需要同樣的處理。

### I-5（HIGH／流程）冷啟動期間所有 cache 列都不可啟動，且錯誤訊息誤導
`src/catalog/catalog_cache.cpp:166` 把每一列設成 `launch_verified = false`；`src/launch/shell_launch.cpp:8-10` 對 `!launch_verified` 直接回 `{false, ERROR_INVALID_PARAMETER}`；`main.cpp:901-902` 把 `ERROR_INVALID_PARAMETER` 映射成 `"The app entry is invalid."`。

使用者實際看到的：開機後立刻按 Alt+Space（spec §FR-008 的整個賣點就是「立即提供舊結果」）、按 Enter → 面板不隱藏、跳出 `Failed to launch "Firefox". The app entry is invalid.`，同時觸發一次多餘的全來源 rebuild。等背景 rebuild 完成（Start Menu 走完整棵樹，實測 item 記錄可達數秒）之後同一個 App 才能啟動。

NR-113 的安全立場（cache 不是真實來源，不該驅動 Shell）是對的，但目前的落地讓「顯示得到、按不動、還罵你的 entry 壞了」成為冷啟動的預設體驗。

- **修法（三個選項，都比現況好）**：
  1. 啟動路徑保持不變，但在 `ActivateRow` 對 `!entry.launch_verified` 走專屬分支：訊息改成「Still preparing apps — try again in a moment.」，並且**不**觸發 launch-failure refresh（背景 rebuild 本來就在跑）。這是最小改動，且把誤導訊息換成真話。
  2. 讓 `!launch_verified` 的列進入待啟動佇列，rebuild 完成（`OnGenerationCompleteRefresh`）後若 stable_id 出現在新 snapshot 就自動啟動一次。行為最好，但引入新狀態。
  3. 重新檢視 NR-113：cache 位於 `%LOCALAPPDATA%\NimbleRun`，與 `favorites.txt`／`usage.tsv` 同一信任邊界，而後兩者的內容是**直接**參與啟動決策的（pin 的 stable_id 決定要啟哪一列）。把 cache 單獨降級成不可啟動，威脅模型上並不比其他 store 更嚴格。要重開必須依 AGENTS.md 在新 item 內寫出覆寫聲明。

  我建議選 1（今天就能做、不動安全邊界、直接消掉誤導訊息），並把 2/3 記進 `docs/work-items.md` §候選。

### I-5b（MEDIUM）`ShowInfoBalloon` 對 `string_view` 做 `wcsncpy`
`src/app_host/main.cpp:2054-2065`
```cpp
void ShowInfoBalloon(HWND window, std::wstring_view text) {
    ...
    wcsncpy(nid.szInfo, text.data(), sizeof(nid.szInfo)/sizeof(wchar_t) - 1);
```
`wstring_view::data()` 沒有 NUL 結尾保證。目前兩個呼叫點傳的分別是 `std::wstring` 與字面字串，都恰好有結尾，所以不會炸；但簽名承諾的是 view，下一個呼叫者傳 `substr()` 就是讀越界。
- **修法**：參數改 `const std::wstring&`（兩個呼叫點都已經是 `wstring` 或可隱式轉），或用 `text.copy(nid.szInfo, n)` 後手動補 `\0`。

### I-6（MEDIUM／流程）滾輪捲動會搬動鍵盤選取
`src/app_host/panel_model.cpp:188-197`
```cpp
void PanelModel::ScrollBy(int delta_rows) {
    ...
    selected_ = first_visible_;      // ← 選取被強制拉到頁首
}
```
`ScrollBy` 同時服務 `PgUp/PgDn`（`main.cpp:2454,2459`）與 `WM_MOUSEWHEEL`（`:2698`）。對 PgUp/PgDn 這行為說得過去（鍵盤翻頁本來就要帶著選取走）。對**滑鼠滾輪**則違反 §4.8 的核心區隔：

> hover 只改變 path bar 內容與該格的淡填色，**不改變選取**… `Enter` 永遠啟動具備選取邊框的那一格。

滾一下滾輪、再按 Enter，啟動的是新頁的第一格，不是使用者原本選著的那一格。spec 沒有明文寫「滾輪不得改選取」，但它明文寫了 Enter 的契約，而滾輪讓選取無聲移動就是在破壞那個契約。

- **修法**：把「選取跟隨」變成參數而不是隱含行為：`ScrollBy(int delta_rows, bool move_selection)`，鍵盤路徑傳 `true`、滾輪傳 `false`。滾輪路徑之後選取可能離開可見範圍，這正是 §4.8 允許的（Enter 仍然啟動那一格；`EnsureSelectionVisible` 不該在此被呼叫）。若希望滾動後 Enter 不會啟動看不見的東西，則另一個一致的選擇是滾輪也不動選取但把選取框留在原地並在下次方向鍵時 `EnsureSelectionVisible`——兩種都可以，但**不能**是現在這樣「靜默改變 Enter 目標」。

### I-7（LOW／已知決策）失效監看 root 的 1 Hz 重試迴圈
`src/app_host/catalog_watcher.cpp:104-109` 與 `:146-151`：`ReadDirectoryChangesW` 或 `GetOverlappedResult` 持續失敗時，`reported` 旗標確保只 post 一次 full-rescan（NR-074 的修正），但 `Sleep(1000); continue;` 的迴圈本身**永不退避**。把自訂資料夾設在外接碟上再拔掉碟，NimbleRun 就會每秒對 driver 發一次 `ReadDirectoryChangesW`，直到程序結束。

`AGENTS.md`「Keep the idle path event-driven: no busy loops and no high-frequency timers」與 §NFR-002「禁止固定小於 60 秒的 timer」照字面讀都被違反。NR-074 的交接區明確決定保留 `Sleep(1000)`（「錯誤期間已無訊息，1 秒一次空迴圈不構成…」），所以我**不主張這是新 bug**，只指出：同一份程式在 `pending_notify` 的重送路徑上已經實作了 1s→30s 的指數退避（`:120-127`），失敗重試路徑用同一套退避幾乎零成本，而且會讓「拔掉外接碟」這個真實情境從 1 Hz 降到 0.033 Hz。若同意，開一個新 item 並在其中寫出對 NR-074 的覆寫。

### 檢查過且**合格**的重要項目（供交叉比對）
- **使用者資料位置**：`DefaultSettingsDir()`（`settings_store.cpp:122`）唯一來源 `FOLDERID_LocalAppData` + `\NimbleRun`；`g_user_data_directory` 為空時所有持久化與 cache 全程關閉（`persistence_available`）。無任何 exe-relative 寫入。**符合**。
- **原子寫入**：`AtomicWriteUtf8Text`（`atomic_text_file.h:405`）走 `.tmp` → `WriteFile` → `FlushFileBuffers` → `MoveFileExW(REPLACE|WRITE_THROUGH)`，失敗刪 tmp 且不動原檔；三個 user-data store 全部經過它。`icons.cache` 的 append 例外由 §10.2 明文允許，compaction 走 `.tmp`+replace。**符合**。
- **不覆寫較新 schema**：`write_protected_` 三個 store 一致（NR-096），cache 端 `SetCacheWritesDisabled`。**符合**。
- **啟動只走 Shell**：`shell_launch.cpp` 只有 `ShellExecuteExW`，`fMask = 0`（無 `SEE_MASK_NOCLOSEPROCESS`，無 handle 可洩漏），全 repo 無 `CreateProcess`。搜尋輸入從未進入 `lpFile`（只過濾既有 catalog）。**符合 §FR-010／NFR-004**。
- **不受信輸入邊界**：`WM_SIZE`（NR-171）改讀 `GetClientRect`；`WM_DPICHANGED`（NR-149）不解 lParam；WM_APP 一律走 `HandoffRegistry` token 而非裸指標（NR-077/151）；`ParseUint64` 拒負號（NR-070）；`UsageScore` 用比較而非相減避免 int64 溢位；store 有 byte cap／line cap／row cap（NR-121/122/141）。這一軸做得比多數產品程式碼嚴謹。**符合**。
- **無網路／遙測**：無 socket、無 WinHTTP／WinINet、無 `URLDownload`；唯一的 URL 是文件裡的參考連結。**符合**。
- **UI 文字英文**：三個字串命名空間集中管理（`context_menu_strings`／`list_strings`／`footer_strings`／`dialog_strings`），無中文字串進入 UI。**符合 §NFR-006**。
- **執行緒**：`HandoffRegistry` 全方法上鎖；`IconWorker` 的 queue 與 dropped-keys 有明確鎖序（`mutex_` → `g_icon_dropped_keys_mutex`，worker 端不持 `mutex_` 呼叫，無反轉）；`DiagnosticLog::Write` 全體序列化且 `noexcept`＋catch-all（NR-174）。**除 I-3／I-4 的 join 無界問題外合格**。

---

## 3. 主要使用者流程軸

逐條走 spec 的流程，標記完整度。

### 3.1 hotkey 顯示／隱藏（§4.1、FR-002、AC-001）
**完整。** `WM_HOTKEY`（`main.cpp:2639`）→ `IsWindowVisible` 決定 toggle。`ShowPanel`（`:1923`）序列正確：先把隱藏視窗停到游標所在螢幕 work area 原點（NR-103，讓 DPI 變更在此觸發）→ `ClampWindowSize` 依該螢幕 DPI 算尺寸 → 置中 → `SetForegroundWindow` → 重載 theme/hide_after_launch/recent_count → `Reset()` 清 query → `RefreshPanelSnapshot()` → 清 hover/tooltip/drag/dropped-icon → 清空 EDIT 並 `SetFocus` → `UpdateViewportRows` → invalidate。註冊失敗走一次性 tray balloon 且不裝低階 hook（`:3356-3365`），符合 FR-002。`Alt+Tab/Alt+Esc/Ctrl+Esc` 硬拒在 parse 端（NR-086）。

### 3.2 輸入／即時過濾／版面切換（§4.3、§4.4、AC-002b、AC-003）
**完整。** `EN_UPDATE`（`:2649`）是唯一的「打字 → query」路徑，Esc 也刻意繞回它（NR-052），所以不會出現「已回格狀但輸入框仍有殘字」。切換點唯一：`PanelModel::RefreshRows` 只看 `NormalizeName(query_).empty()`（`panel_model.cpp:53`），純空白字元維持格狀，符合 §4.3 第三條。切換後 `first_visible_ = 0`、`selected_ = 0`，符合 AC-002b「可見範圍回到頂端」。
**小缺口**：`GetWindowTextW(g_search_edit, buffer, 1024)`（`:2650-2651`）而 EDIT 未設 `EM_LIMITTEXT`。貼上超過 1023 字元會被靜默截斷（不會溢位，buffer 安全）。設一次 `EM_LIMITTEXT` 讓截斷發生在使用者看得到的地方會更誠實。

### 3.3 排名（§4.5、§4.6）
**完整且與 spec 逐條對應。** `search_engine.cpp` 的 6 層（Exact/NamePrefix/WordPrefix/Substring/Subsequence/Alias）與同分四級 tie-break（is_pinned → usage_score → 名稱長度 → 正規化名稱 → stable_id）完全吻合 §4.5，alias 命中一律歸最低層且不細分（§4.5 第二段）。`UsageScore`（`usage_store.cpp:222`）的 8/4/1/0 與 1,000,000 clamp 吻合 §4.6。`is_pinned`／`usage_score` 由 `StampRankingFields`（`snapshot_assembler.cpp:44`）在每次 snapshot 重建與每次 store 變更後統一蓋章，沒有第二個來源。
空白狀態的常用區依「最後啟動時間降冪、stable_id 升冪」（`UsageStore::Recent`），且 `RefreshRows` 刻意不再排序（NR-071 覆寫 NR-053），吻合 §4.2 規則 2。

### 3.4 選取／啟動（§4.7、§4.8、AC-004、AC-005）
**完整，除 I-5 與 I-6。** 四個啟動入口（Enter、click、Alt+digit、pinned cell 的 mouse-up）全部收斂到 `ActivateRow`（`:1052`），所以 `IsMissingPin` 的不可啟動守門、launch-failure 的一次性 refresh gate、usage 記錄、hide-after-launch 都只有一份。釘選格改為放開左鍵才啟動（與拖曳共存，§4.8），常用格與清單列仍在按下時啟動——與 spec 逐字相符。
- I-5：冷啟動 cache 列不可啟動＋訊息誤導。
- I-6：滾輪偷改 Enter 目標。

### 3.5 Catalog 重建（§FR-008、AC-007、AC-013）
**完整，除 I-3。** generation 機制正確：`BeginGeneration` 遞增世代並快照事件時間戳（NR-065），舊 worker 的結果被 `IsActiveGenerationSource` 擋掉；單一來源失敗保留該來源舊結果（`ApplySourceFailure` 只標 received，不清 `source_entries_`）；整批替換只在 `GenerationComplete` 時發生（`RebuildMerged`）。500 ms debounce（`kDebounceMs`）＋ per-source 1 s 啟動節流（NR-147）＋ buffer overflow → full rescan（`bytes_returned == 0`）都在。AppsFolder on-demand 10 分鐘門檻在 `ShouldRefreshAppsFolder`，且刻意在 rebuild 進行中回 false（NR-081，避免 {AppsFolder} 世代把整份全掃結果洗掉）——這個坑挖得很深，處理得對。
`StartWatchers`（`:1388`）在換 watch 表與換 roots 之間 drain 掉舊表的 `kWatchChangedMessage`（NR-156），避免用新表 index 去解舊訊息。

### 3.6 重啟後的持久化（Phase 4 完成定義、AC-010）
**完整。** pins（`favorites.txt` schema=2，含 display_name 供缺失標示）、usage（`usage.tsv` schema=1，byte-identical 重存）、settings（`settings.ini` schema=1）三者皆 atomic write、皆有 corrupt 隔離（`.corrupt`）與 newer-schema 唯讀保護，且 `ShowPanel` 每次重載 pins，所以外部手改 `favorites.txt` 也會被反映。`PinStore::Reconcile` 對空 catalog 早退（不因一次失敗掃描刪 pin）、30 天保留、`last_seen_utc` 用比較而非相減（NR-070）。缺席 pin 以 placeholder 列（`IsMissingPin`）保位置、只提供 Unpin，吻合 §FR-011。

### 3.7 數字快選（§4.7、§4.9）
**完整。** `QuickSelectSlotForKey`（1→slot0 … 0→slot9）＋ `RowForVisibleSlot(slot) = first_visible_ + slot` 且以 `ViewportRows()*Columns()` 為界，吻合「依序指派給當前可見項目的前 10 個」。`WM_SYSCHAR` 吞掉 10 個綁定鍵的系統 beep，未綁定的（Alt+Space）保留預設處理。footer 在格狀固定顯示 `Alt+0~9`（NR-176 決策）、清單依可見列數組出，格狀的 per-cell 方塊只在按住 Alt 時畫（NR-045），且是不佔空間的 overlay。
**極小瑕疵**：拖曳釘選格進行中，格子畫的是 `preview[]` 排列後的 row，而 Alt+digit 走的是未排列的 `first + slot`。也就是拖曳中按 Alt+digit 會啟動「那個位置原本的」App 而不是眼前看到的。拖曳是短暫且需要按住左鍵的手勢，同時按 Alt+digit 屬病態操作，列為觀察不建議開 item。

### 3.8 tooltip 行為（§4.8、§4.9）
**在 HEAD 上是壞的（I-1、I-2）；工作樹修好了文字更新但尚未提交。** 觸發邏輯本身正確且乾淨：150 ms 一次性 timer 只在 hover 到「名稱確實被 DWrite 量測判定為截斷」的格子時 arm（`UpdateTooltipTimer`），`WM_TIMER` 再驗一次才顯示；八個隱藏點齊全（HidePanel／WM_MOUSELEAVE／左右鍵按下／滾輪／PgUp/PgDn／EN_UPDATE 版面切換／拖曳／ShowPanel／WM_DESTROY）。無常駐 timer，符合 §NFR-002。`ComputeTooltipGeometryDip` 的下方優先＋最後一列翻上＋水平 clamp 有 6 個純函式測試覆蓋。
問題純粹在 Win32 訊息層，也正好是 NR-180 決定「原生封裝屬視窗層、不測試」的那一塊——這個政策的代價在這裡具體化了：唯一沒有測試的 30 行，就是唯一壞掉的 30 行。

---

## 4. Repo 衛生

### H-1 測試數量在兩份文件裡都過期
- `docs/testing.md:9`：「currently 31 checks」
- `docs/release-evidence.md:8`：「CTest count: Total Tests: 31」（連同 43-59 行的逐項輸出）

NR-180 新增了 `nimblerun_cell_tooltip_test`（`tests/CMakeLists.txt:201`），其交接區自己記錄「32/32 Passed」。NR-104 就是為了「release evidence 的測試數」而開的 item，這次又漂了。
- **修法**：`docs/testing.md` 改 32；`docs/release-evidence.md` 由 `tests/release/release_evidence.ps1` 重新產生（該 script 本來就會重寫這份報告），不要手改。

### H-2 未提交變更，且 NR-180 已標 `done`
```
 M src/resources/NimbleRun.manifest      （新增 Common-Controls v6 相依）
 M src/ui/cell_tooltip.cpp               （TTM_SETTIPTEXTW → TTM_UPDATETIPTEXTW）
```
這兩個檔案就是 I-1／I-2 的修正。目前狀態：tracker 的 NR-180 是 `done`，但它的驗收條件（「原生外觀 Win11 圓角／Win10 方角」「tooltip 顯示該格完整顯示名稱」）在 HEAD 上不可能通過——通過條件只存在於未提交的工作樹。這正是 AGENTS.md「Anything a later session needs must live in the repository」要防的情況：現在若換一個 agent 接手，它會 clone 到一個 tooltip 壞掉的 HEAD，並讀到一份說它已完成的 tracker。
- **修法**：提交這兩個檔案（訊息建議 `NR-180: use TTM_UPDATETIPTEXTW and require comctl32 v6`），並把 `InitCommonControlsEx`（I-2）與 manifest 一起處理。若不打算立刻提交，NR-180 的 tracker 狀態應退回 `in_progress`。

### H-3 tracker 與 item 檔案的一致性：**通過**
- `docs/work-items/` 有 177 個 NR-*.md，tracker 的 Item 總覽也有 177 列，逐一交叉比對**零缺漏、零孤兒**。
- 編號空缺只有 NR-025/026/027 三個，屬刻意（NR-020 用清單取代 icon matrix 後那三個被 supersede，NR-016 也標 `superseded` 保留為決策軌跡）。
- 沒有任何 item 文件在檔頭自宣告狀態（2026-08-07 清理後的規則被遵守）。
- §已否決的方向 有 8 條，每條都附依據與「要重開需要什麼新證據」。

這一塊的紀律遠高於一般專案，唯一的漏洞是 H-2 那種「實作完成但沒進 repo」。

### H-4 其他文件檢查
- `docs/design-spec.md` §4.8/§4.9 的 tooltip 兩處已隨 NR-180 更新（在 HEAD 內），與程式碼意圖一致（與 HEAD 的實際行為不一致，見 I-1）。
- `docs/adr/0002-cell-tooltip-native.md` 存在，正確覆寫 0001，兩份都保留為歷史紀錄——符合 AGENTS.md 的「不編輯已完成 item／ADR」。
- `AGENTS.md`「Current baseline」一節仍寫著「repository currently contains the Phase 0 foundation … Direct2D/DirectWrite rendering probe with an English fake app grid」。實際已到 Phase 3/4/5（真 catalog、icon store、settings dialog、release evidence）。**這是全 repo 最誤導的一段文件**：它是每個 agent 進來讀的第一份檔案，卻告訴它這裡只有一個 probe。建議改寫成當前實際基準，或直接刪掉這一節（`docs/roadmap.md` 已經是 phase 狀態的正確來源）。

---

## 5. 建議處理順序

1. **提交 I-1／I-2 的修正並補 `InitCommonControlsEx`** — HEAD 目前有一個功能完全失效的已宣告完成特性，加上一處越界讀。
2. **I-3、I-4：兩處無界 join** — 一個凍結面板、一個卡住結束；NR-123 已經有現成的模式可以照抄。修 I-3 時務必一併處理 detach 分支把 `cancel_` 留在 `true` 的問題。
3. **I-5：冷啟動不可啟動列的訊息與行為** — 使用者第一個動作就會撞上；選項 1 的改動只有幾行。
4. **I-6、I-5b、H-1、H-4** — 一輪小修可以清完。
5. **P-1~P-9** — 全部是刪除或收斂，合計約 40 行淨減少，零行為變更，既有 32 個測試即回歸網。建議合成一個 item（延續 NR-128／NR-145／NR-153 的 dead-code cleanup lane），並依 AGENTS.md 在搬移時把 NR 編號註解一起帶走。
6. **I-7** — 若決定改，需在新 item 內寫出對 NR-074 的覆寫聲明。
