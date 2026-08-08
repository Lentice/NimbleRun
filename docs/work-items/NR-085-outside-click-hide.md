# NR-085 — 搜尋框持有焦點時，點擊面板外不會自動隱藏面板

Phase 3 · Depends on: —（無依賴，可與 NR-084、NR-086 平行）

- Source: `docs/design-spec.md` §4.8（「點擊面板外，面板自動隱藏」）
- Origin: 2026-08-08 第六次全 repo 稽核（ShowPanel 焦點歸屬與 WM_KILLFOCUS 收件方比對）

## Why

面板唯一的「點擊外部自動隱藏」機制是 `WindowProc` 的 `WM_KILLFOCUS`
（`src/app_host/main.cpp:2920-2928`）。但 `WM_KILLFOCUS` **只送給失去鍵盤
焦點的那一個視窗**，而 `ShowPanel` 把焦點放在搜尋框這個 EDIT 子視窗上
（`main.cpp:1979` `SetFocus(g_search_edit)`），面板本身從未持有焦點。

於是最常見的操作序列**不會隱藏面板**：

1. 使用者按 `Alt+Space` → 面板顯示、焦點在搜尋框（EDIT 持有焦點，面板只是
   啟用中）。
2. 使用者輸入幾個字（焦點仍在 EDIT）。
3. 使用者點擊面板外的任何視窗 → 焦點從 EDIT 移走 → `WM_KILLFOCUS` 送給
   **EDIT**，其 subclass（`main.cpp:2330-2334`）只 `DestroyCaret`，不通知
   父視窗；面板收到的是 `WM_ACTIVATE(WA_INACTIVE)`（去啟用），但 `WindowProc`
   完全沒有 `WM_ACTIVATE` 分支 → 沒有 `HidePanel` 被呼叫 → 面板停留在
   TOPMOST，蓋在使用者剛點的那個視窗上面。

`WM_KILLFOCUS` handler 的 `GetFocus() != g_search_edit` 檢查假設「面板失去
焦點時焦點不在搜尋框」，這只在**面板自己**持有焦點時才成立（例如使用者先
點過面板背景）。ShowPanel 從把焦點放進 EDIT 的那一刻起，該假設就反了。
§12.3 的「點擊外部自動隱藏」UI 測試因此只對「面板自身有焦點」的子集成立。

這是 §4.8 的直接違反，且是高頻路徑：顯示→輸入→點別處是 launcher 最常見的
關閉手勢。影響：面板賴在螢幕上不關，遮住使用者正在互動的視窗。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **修在 `WindowProc` 新增 `WM_ACTIVATE` 分支**：`w_param == WA_INACTIVE`
   且既有的 `g_context_menu_active`／`g_dialog_active` 兩旗標皆為 false 時
   `HidePanel(window)`。理由：`WA_INACTIVE` 是「面板被去啟用」的單一事實
   來源，涵蓋「EDIT 持有焦點、使用者點到別處」的全部路徑，而兩個旗標已
   精確覆蓋「彈出選單／錯誤對話框把啟用帶走」的合法例外（它們正是為
   WM_KILLFOCUS 的同一類問題而設）。
2. **保留既有 `WM_KILLFOCUS` handler 不動**：它仍負責「面板自身持有焦點時
   被移走」的互補路徑（如 `Alt+Tab`），兩者並存不衝突。
3. **不改 EDIT subclass 的 `WM_KILLFOCUS`**：在那裡攔截需要複製 context-menu
   /dialog 旗標判斷，而且 EDIT 不知道 app 層的 modal 狀態；去啟用語意屬於
   host（`WindowProc`）不屬於輸入子視窗。
4. **驗證「點開設定對話框會讓面板隱藏」是可接受行為**：`DialogBox`（owner
   為面板）開啟時面板被去啟用 → 本修法會 `HidePanel`。設定對話框是獨立
   視窗，owner 被隱藏不影響 modal 生命週期；使用者從面板右鍵開設定時，
   面板退開正是預期行為，與 NR-060 的「面板空白處右鍵」語意一致。
5. **不加單元測試**：行為在 Win32 訊息層，`panel_model`／既有測試庫不碰
   HWND（NR-060 先例「不為測試點發明抽象」）；以 sanity grep ＋手動驗收
   覆蓋。既有的 `lifecycle_check.ps1` 不操作滑鼠，無法自動化此案例。

## Binding constraints — quoted, do not go looking for them

design-spec §4.8：

> - 點擊面板外，面板自動隱藏。

design-spec §4.1（面板顯示焦點）：

> - 面板出現在目前游標所在螢幕的工作區中央。搜尋欄取得鍵盤焦點；欄位為空。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2920-2928` — `WindowProc` 的 `WM_KILLFOCUS`
  （唯一既有的外部點擊隱藏路徑，**不改**）。
- `src/app_host/main.cpp:1894-1987` — `ShowPanel`：`:1979` `SetFocus(g_search_edit)`
  證明焦點歸 EDIT 所有；`:1937-1939` `Reset()`、`:1944` `RefreshPanelSnapshot()`。
- `src/app_host/main.cpp:2284-2478` — `SearchEditProc`：`:2330-2334`
  `WM_KILLFOCUS` 只 `DestroyCaret`，不通知父視窗（**不改**）。
- `src/app_host/main.cpp:182-191` — `g_context_menu_active`／`g_dialog_active`
  旗標與用途註解。
- `src/app_host/main.cpp:854-864` — `HidePanel`：冪等（`SW_HIDE`＋flush＋prewarm），
  重複呼叫安全。
- `src/app_host/settings_dialog.cpp:402-407` — `DialogBoxParamW`（modal、owner
  為面板），本修法會讓面板在開設定時隱藏（Decisions §4）。

## Scope

### 1. 在 `WindowProc` 新增 `WM_ACTIVATE` 分支

`src/app_host/main.cpp`，放在 `WM_KILLFOCUS` 附近：

```cpp
case WM_ACTIVATE:
    // NR-085: ShowPanel 把焦點放在搜尋 EDIT（SetFocus(g_search_edit)），面板
    // 本身從未持有鍵盤焦點，所以「點擊外部 → WM_KILLFOCUS」只送給 EDIT、
    // 面板收不到——§4.8「點擊面板外，面板自動隱藏」在「顯示後直接點別處」
    // 與「輸入後點別處」兩個高頻路徑上完全失效。WM_ACTIVATE(WA_INACTIVE) 是
    // 面板被去啟用的單一事實來源，涵蓋 EDIT 持有焦點的所有外部點擊。既有
    // 的兩個 modal 旗標與 WM_KILLFOCUS 一樣保護：選單／錯誤對話框把啟用
    // 帶走時不隱藏。
    if (w_param == WA_INACTIVE && !g_context_menu_active && !g_dialog_active) {
        HidePanel(window);
    }
    return 0;
```

`HidePanel` 冪等（重複呼叫安全），且既有 `WM_KILLFOCUS` 路徑繼續存在，
`Alt+Tab` 與「面板自身持焦時點外部」的舊行為不變。

### 2. 更新 spec？

design-spec §4.8 已含「點擊面板外，面板自動隱藏」。本 item 是讓該句名副其實
的修補，不另加條文。

## Non-goals

- **不改 `SearchEditProc` 的 `WM_KILLFOCUS`**（Decisions §3）。
- **不刪既有 `WM_KILLFOCUS` handler**（Decisions §2）。
- **不加單元測試**（Decisions §5）；不改 `lifecycle_check.ps1`。
- **不處理「點擊外部但面板已是 deactivated 的殘留 hover／拖曳狀態」**：
  `HidePanel` 後續的 `ShowPanel` 已重設 hover/drag（`main.cpp:1957-1964`），
  無殘留可清。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，本 item 不新增測試）。
2. sanity greps 通過（見 Agent checks）。

Manual（Release build，逐條打勾）：

1. 按 `Alt+Space` 顯示面板，**不輸入**，直接點面板外的另一個視窗：面板隱藏。
2. 按 `Alt+Space`，輸入幾個字（焦點在搜尋框），點面板外：面板隱藏。
3. 點面板背景讓面板自身取得焦點後，點面板外：面板隱藏（既有行為回歸）。
4. 格狀狀態在一個 App 上按右鍵開出項目選單，選擇期間面板不隱藏；
   選單關閉後點外部，面板隱藏（回歸）。
5. 從面板空白處右鍵 → Settings：設定對話框開啟，面板隱藏（Decisions §4）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# WM_ACTIVATE 分支存在且只有一處：
Select-String -Path src/app_host/main.cpp -Pattern 'WM_ACTIVATE'
# expect: case WM_ACTIVATE 1 處（本 item 新增）

# 既有 WM_KILLFOCUS 仍在、EDIT subclass 的 WM_KILLFOCUS 未動：
Select-String -Path src/app_host/main.cpp -Pattern 'case WM_KILLFOCUS'
# expect: 兩處（WindowProc 原樣 + SearchEditProc 原樣）

git diff --name-only
# expect: 僅 src/app_host/main.cpp（及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

（實作者填寫：改動位置、插入的 WM_ACTIVATE 區塊、建置與 CTest 結果、
5 條手動驗收結果、sanity greps、偏差、未完成事項。）

- 改動位置：`src/app_host/main.cpp` `WindowProc` 內新增 `case WM_ACTIVATE`
  （建議放在 `WM_KILLFOCUS` case 之前或之後）。
- 建置與 CTest：Release 建置無新增警告；`ctest --test-dir build
  --output-on-failure` 全綠。
- 手動驗收：本工作區不操作視窗，5 條手動驗收（含「輸入後點外部隱藏」主
  案例）未實跑；由訊息流讀碼確認（焦點歸屬 EDIT → 去啟用送 WM_ACTIVATE）。
- sanity greps：`WM_ACTIVATE` 1 處；`case WM_KILLFOCUS` 2 處未動。
- 偏差：實作已存在於既有本地 commit `7e0f385`；本次 opencode job 只做 clean-worktree
  驗證，沒有新增 patch。手動 GUI 驗收未執行。
- 未完成：無。
