# NR-039 — Drag the panel by its search box and empty chrome

- Status: `done`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.7／§4.8／§4.9／§127（面板顯示位置）

## Goal

面板目前**任何位置都不能拖動**，於是它有時正好蓋住使用者需要看的視窗內容。本 item 讓面板可以用滑鼠左鍵拖到別處：搜尋輸入欄本身、輸入欄的圓角外框，以及面板上所有沒有壓到 App 項目的空白處，全部成為拖曳把手。

拖曳只影響**當次顯示**。下一次按快捷鍵顯示時，面板依 design-spec §127 回到游標所在螢幕工作區正中央——本 item **不**新增任何位置持久化。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.7／§4.8／§4.9 與第 127 行、`docs/work-items.md`、`docs/work-items/NR-023-search-box-visuals.md`（若存在）、本文件。

## 現況事實（已查證，不需重新推導）

- 搜尋輸入欄是**真的 Win32 `EDIT` 子視窗**：`src/app_host/main.cpp:2058` 建立，樣式 `WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL`，**無邊框**（圓角框由 D2D 在父視窗畫，見 `main.cpp:884`）。
- 該 `EDIT` 已被 subclass：`SearchEditProc`（`main.cpp:1447`），目前處理 `WM_SETFOCUS`／`WM_KILLFOCUS`（NR-023 自繪 caret）、`WM_SYSKEYDOWN`／`WM_SYSCHAR`（NR-024 Alt+數字）、`WM_KEYDOWN`（NR-020 方向鍵／Enter／Esc 路由）。**目前沒有任何滑鼠訊息的 case**。
- `EDIT` 幾何由 `RepositionSearchEdit`（`main.cpp` 約 :1400-1442 區段）依 DPI 設定；版面常數在 `src/ui/panel_layout.h:54-55`（左右內縮 `kSearchTextInsetDip = 12.0f`、上下內縮 `kSearchEditInsetYDip = 6.0f`），所以圓角框只有一圈細邊露在 `EDIT` 之外。
- 父視窗 `WM_LBUTTONDOWN`（`main.cpp:1813`）目前只做一件事：`CellAtPoint()` 命中項目就選取並啟動，`cell < 0` 時直接 `return 0` 吃掉。
- `CellAtPoint()`（`main.cpp:449`）回傳 `-1` 的四種情況：`y < layout.list_top`（搜尋欄整條 ＋ 其外框）、清單狀態 `x < layout.list_left`、格狀狀態 `col` 超出欄數、`index >= Rows().size()`（項目不足時的下方空白與 footer path bar 一帶）。
- 面板上**沒有其他可點目標**：無捲軸、無按鈕，footer path bar 只是繪製。右鍵選單走 `WM_RBUTTONDOWN`（`:1824`），與本 item 無關。
- 視窗樣式為 `WS_POPUP | WS_BORDER`（`main.cpp:1985`），**無標題列**，所以預設沒有任何拖曳區。
- `ShowPanel()`（`main.cpp:1276-1299`）每次顯示都以 `MonitorFromPoint(cursor, ...)` 重算置中並 `SetWindowPos(..., SWP_SHOWWINDOW)`；因此拖到螢幕外也會在下次顯示自動回正，**不需要任何 clamp 邏輯**。
- 父視窗有 `WM_KILLFOCUS`（`main.cpp:1882`）會隱藏面板；`WM_MOUSEMOVE`（`:1775`）維護 `g_grid_hover_index` hover 高亮。

## 使用者已確認的決策（不要重新設計）

1. **保留原生 `EDIT`，不改自繪輸入欄。** 曾考慮把輸入欄改成 D2D 自繪以便攔截滑鼠，已否決：`EDIT` 的 `WM_LBUTTONDOWN` 本來就在我們的 subclass 手上，換掉控件要自己重寫 caret 命中測試、選取、Shift+方向、剪貼簿、Undo 與 **IME 組字**，且會連帶砸掉 NR-023／NR-024／NR-020 的既有行為。
2. **拖曳判定用位移門檻，不是「按下就拖」。** 按下不動後放開 → 把 caret 放到點擊處；按住並移動超過系統拖曳門檻 → 拖視窗。已接受的代價：`EDIT` 上「按住拖曳選取文字」會失效（雙擊選字、Shift+方向、Ctrl+A 不受影響）。
3. **可拖區為 `cell < 0` 的全部區域**，不只外框。理由：與「只拖外框」相比程式碼更少（不必另算外框矩形），手感也一致。
4. **不記住拖曳後的位置。** 不改 design-spec §127、不新增設定項、不寫任何持久化。
5. **沒有自動化測試，改以具名手動驗收步驟。** 本 item 的邏輯全是 Win32 訊息膠水（`DragDetect`／`WM_NCLBUTTONDOWN`／`EM_CHARFROMPOS`），需要真 HWND 與真滑鼠輸入；`CellAtPoint` 本身就因為要 HWND 而沒有單元測試。為此抽純函式再測會測到「點在不在項目上」——那是既有行為，拖曳壞掉時不會失敗，等於假的檢查。故本 item 的 Agent 檢查是「建置 ＋ 既有 CTest 全綠（確認沒弄壞任何東西）」，加上 Acceptance 的五條手動步驟逐條打勾。

## 硬約束

- 最小可行改動：**只改 `src/app_host/main.cpp`，兩處**。不新增檔案、不新增模組、不新增 helper class、不新增設定項。
- 不新增 timer、不新增輪詢；拖曳用 Win32 內建的移動迴圈，不自己寫 `WM_MOUSEMOVE` 追蹤狀態機。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限。
- 不寫入任何檔案（本 item 無持久化）。
- App UI 文字一律英文（本 item 不新增 UI 字串）。
- 不改 design-spec；面板預設位置仍是置中。

## Scope

### 1. 父視窗：`cell < 0` 即為拖曳把手（`main.cpp:1813`）

把 `WM_LBUTTONDOWN` 改成：

```cpp
case WM_LBUTTONDOWN: {
    // NR-020/NR-029: a single click selects and launches the row (list) or
    // cell (grid) under the cursor (design-spec §4.8).
    const int cell = CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
    if (cell < 0) {
        // NR-039: nothing under the cursor -> the panel itself is the drag
        // handle, so it can be moved off whatever it happens to be covering.
        // WM_NCLBUTTONDOWN/HTCAPTION hands the window to the shell's own move
        // loop; DefWindowProc takes the live cursor position from the system,
        // so lParam is unused here (client coords would be wrong anyway --
        // WM_NCLBUTTONDOWN's lParam is in screen coordinates).
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    g_model->SelectRow(static_cast<std::size_t>(cell));
    ActivateRow(static_cast<std::size_t>(cell), window);
    return 0;
}
```

- 注意 `lParam` 傳 `0`：`WM_NCLBUTTONDOWN` 的 `lParam` 是**螢幕**座標，而 `WM_LBUTTONDOWN` 給的是 client 座標，直接轉手會是錯的；預設處理本來就從系統取當下游標位置。
- 原本 `cell >= 0` 的兩行行為一字不改。

### 2. `EDIT`：位移門檻決定「拖視窗」或「放 caret」（`SearchEditProc`，`main.cpp:1447`）

在 `switch` 內新增一個 case（放在 `WM_KILLFOCUS` 之後、`WM_SYSKEYDOWN` 之前即可）：

```cpp
case WM_LBUTTONDOWN: {
    // NR-039: the search box doubles as the panel's drag handle. DragDetect
    // blocks until the user either moves past the system drag threshold
    // (SM_CXDRAG/SM_CYDRAG -> TRUE) or releases without dragging (FALSE), and
    // it consumes the mouse messages either way -- so the plain-click path has
    // to place the caret itself. Cost accepted in NR-039: drag-selecting text
    // with the mouse is gone; double-click, Shift+arrows and Ctrl+A are not.
    const POINT client{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    POINT screen = client;
    ClientToScreen(edit, &screen);  // DragDetect takes screen coordinates
    if (DragDetect(edit, screen)) {
        ReleaseCapture();
        SendMessageW(GetParent(edit), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    const LRESULT hit = SendMessageW(edit, EM_CHARFROMPOS, 0,
                                     MAKELPARAM(client.x, client.y));
    const int index = static_cast<int>(LOWORD(hit));
    SendMessageW(edit, EM_SETSEL, index, index);
    SetFocus(edit);
    return 0;
}
```

- `DragDetect(HWND, POINT)` 的 `POINT` 必須是**螢幕**座標（Win32 文件明訂），故先 `ClientToScreen`。
- `EM_CHARFROMPOS` 的 `lParam` 是 **client** 座標；單行 `EDIT` 回傳值 `LOWORD` 為字元索引、`HIWORD` 為列索引（單行恆為 0）。以 `EM_SETSEL(index, index)` 造成零長度選取，即「把 caret 放在那裡」。
- 拖曳送往 `GetParent(edit)`，不是 `edit` 自己——要移動的是面板。
- **不要**呼叫 `CallWindowProcW(g_search_original_proc, ...)`：預設處理會自己 `SetCapture` 並進入選取追蹤，與 `DragDetect` 相衝。
- 不動既有的 `WM_SETFOCUS`／`WM_KILLFOCUS`／caret 建立邏輯；`SetFocus(edit)` 只是保險（`EDIT` 通常已有焦點），它會走既有的 `WM_SETFOCUS` 路徑重建 caret。

### 3. 不做的接線

不碰 `WM_LBUTTONDBLCLK`（雙擊選字維持預設）、`WM_RBUTTONDOWN`、`WM_MOUSEMOVE` hover、`ShowPanel()`、`RepositionSearchEdit()`、`panel_layout.h`、任何設定或持久化路徑。

## Non-goals

- 不記住拖曳後的位置、不新增設定項、不改 design-spec §127 的置中行為。
- 不寫任何邊界 clamp（拖出螢幕外由下次顯示的置中自動修正）。
- 不改成自繪輸入欄、不動 IME 相關行為。
- 不為此加 timer、不自寫滑鼠追蹤迴圈、不用 `SetWindowPos` 手動搬視窗。
- 不改視窗樣式（維持 `WS_POPUP | WS_BORDER`）、不加標題列、不加尺寸調整。
- 不改 `CellAtPoint()`、不改版面常數、不改點擊啟動項目的行為。
- 不動 icon lane（NR-032～NR-037）與 NR-038 的任何檔案。

## Acceptance

自動部分：

- 只有 `src/app_host/main.cpp` 被修改；無新增檔案、無新增設定項、無新增 timer、無 `SetWindowPos` 新呼叫點。
- 建置無新增警告；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。
- repo 內搜尋不到為本 item 新增的持久化寫入或 `panel_layout.h` 改動。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **拖輸入欄可移動視窗**：按快捷鍵顯示面板，在搜尋輸入欄**中央**按住左鍵並移動 → 整個面板隨游標移動，放開後停在新位置。
2. **點擊仍放 caret**：先輸入 `notepad`，在文字中間某個字元上**按一下不移動** → caret 出現在該字元位置（不是跳到字尾），且面板沒有移動。接著輸入一個字元，確認它插在 caret 處。
3. **拖空白處可移動視窗**：在圓角框的細邊、清單／格狀區的左右留白、以及項目不足時下方的空白區各拖一次 → 面板都會移動。
4. **點項目仍然啟動**：在任一 App 項目上按一下 → 該項目被選取並啟動（NR-020／NR-029 行為未變）。
5. **拖曳過程面板不消失**：拖曳全程面板不因焦點變化而隱藏（父視窗 `WM_KILLFOCUS` 於 `main.cpp:1882` 會隱藏面板，須確認移動迴圈不觸發它）；放開後仍可繼續打字、方向鍵仍能移動選取、Alt+數字仍能啟動。
6. **位置不被記住**：拖到角落後按 Esc 隱藏，再按快捷鍵顯示 → 面板回到目前螢幕工作區正中央（design-spec §127）。

已知且可接受的外觀瑕疵（不必修）：拖曳結束後游標若正好落在某個格子上，hover 高亮可能要等下一次滑鼠移動才更新。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的六條手動步驟。**不要**只憑編譯成功就回報完成——本 item 唯一的行為驗證是那六條。

## 交接區

- Start: 2026-08-05（實作日）。依「必讀」讀完 AGENTS.md、development.md、work-items.md、design-spec §4.7／§4.8／§4.9 與 §4.1 流程（面板置中 §127）、NR-023；trace `src/app_host/main.cpp` 的 `CellAtPoint`（:450）、`ShowPanel`（:1306）、`RepositionSearchEdit`（:1469）、`SearchEditProc`（:1516）、父視窗 `WM_LBUTTONDOWN`／`WM_MOUSEMOVE`／`WM_RBUTTONDOWN`／`WM_KILLFOCUS`（:1865／:1836／:1876／:1934）、EDIT 建立處（:2139），以及 `src/ui/panel_layout.h:54-55`。環境事實：本機預設熱鍵 Alt+Space 已被 FastStone Editor 占用（`hotkey-register error 1409`），面板改以「暫建 `settings.ini`（`hotkey=Ctrl+Alt+Space`）＋重啟＋真實熱鍵」顯示，驗證後已刪除該檔還原原始狀態（原本無 settings.ini）。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/main.cpp` 的 `CellAtPoint`（:449）、`ShowPanel`（:1276）、`RepositionSearchEdit`（:1400 區段）、`SearchEditProc`（:1447）、父視窗 `WM_LBUTTONDOWN`／`WM_MOUSEMOVE`／`WM_RBUTTONDOWN`／`WM_KILLFOCUS`（:1775／:1813／:1824／:1882）、`EDIT` 建立處（:2058），以及 `src/ui/panel_layout.h:54-55`。實作 Scope 1、2，明確不要動 Scope 3 所列的任何路徑。回報修改的行號、建置與 CTest 結果、六條手動驗收逐條的實測結果與未完成事項。
- Result: 完成。只改 `src/app_host/main.cpp` 兩處：`SearchEditProc` 新增 `WM_LBUTTONDOWN` case（:1535，`DragDetect`→TRUE 拖曳／FALSE 走 `EM_CHARFROMPOS`＋`EM_SETSEL`＋`SetFocus`），父視窗 `WM_LBUTTONDOWN` 改 `cell < 0` 即 `ReleaseCapture()`＋`SendMessageW(WM_NCLBUTTONDOWN, HTCAPTION)`（:1889，`cell >= 0` 兩行一字不改）。未動 `WM_LBUTTONDBLCLK`／`WM_RBUTTONDOWN`／`WM_MOUSEMOVE`／`ShowPanel`／`RepositionSearchEdit`／`panel_layout.h`，未新增 timer／`SetWindowPos` 呼叫點／設定或持久化。建置（LLVM-MinGW Ninja Release）無新增 warning；全套件 CTest 23/23 全綠。六條手動驗收（以 PowerShell＋user32 P/Invoke 送真實滑鼠／鍵盤事件、`GetWindowRect`／`EM_GETSEL`／usage.tsv 量測）逐條實測通過：
  1. 拖輸入欄可移動視窗 — 通過：EDIT 中央按住拖曳，面板位移 (175,105) 且全程可見（位移略小於施作量，因 `DragDetect` 先消耗系統拖曳門檻距離）。
  2. 點擊仍放 caret — 通過：輸入 `notepad` 後點擊字元索引 3 位置，`EM_GETSEL=3..3`（非跳字尾 7），面板未移動；再輸入 `X` 插入成 `notXepad`（插在 caret 處）。
  3. 拖空白處可移動視窗 — 通過：圓角框細邊（client 20,40）位移 (120,60)、grid 左留白（client 8,200）位移 (100,50)、項目下方空白（client 300,400，當時僅 2 筆 recent 佔 1 列）位移 (100,50)，三處面板皆移動且保持可見。
  4. 點項目仍然啟動 — 通過：點 cell(0,0)，面板依 hide-after-launch 隱藏、程序數 419→420、`usage.tsv` 該項 count 1→2 且 last_launch 更新。
  5. 拖曳過程面板不消失 — 通過：拖曳期間輪詢 `IsWindowVisible` 保持 True（`WM_KILLFOCUS` 未觸發）；放開後可打字（輸入 `x` 成功）、`VK_DOWN` 正常（面板存活無崩潰）、`Alt+1` 仍啟動（面板依設定隱藏）。
  6. 位置不被記住 — 通過：拖到 (490,172) 後隱藏，再顯示回到工作區正中央 (640,272)，與置中 rect 完全一致。
  「拖曳結束後 hover 高亮可能延遲更新」之已知外觀瑕疵未實測（純視覺，需人工確認），其餘無未完成事項。驗證環境備註：本機 Alt+Space 被其他程式占用，測試暫改用 Ctrl+Alt+Space（temp settings.ini，已還原）；不影響本 item 的行為結論（拖曳／caret／啟動全經真實滑鼠與熱鍵路徑驗證）。
