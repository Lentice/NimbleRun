# NR-060 — Right-clicking the panel's empty area offers Refresh Apps / Settings / About

Phase 3 · Depends on: NR-013, NR-018

- Source: `docs/design-spec.md` §4.8（滑鼠操作）／§4.10（通知區選單）／FR-013（設定）
- Origin: 2026-08-07 使用者需求——面板開著時想進設定，只能先把面板關掉、
  再去托盤圖示按右鍵。入口存在但在錯的地方。

## Why

Settings 已經完整存在（NR-013）：`ShowSettingsDialog()` 是 modal 對話框，
由托盤選單的 `kCmdSettings` 經 `PostMessageW(window, kSettingsMessage, 0, 0)`
觸發。**本 item 不新增任何功能，只補一個觸發點。**

目前面板上右鍵只有命中格子才有反應（`main.cpp:2509` 的 `WM_RBUTTONDOWN`，
`CellAtPoint(...) < 0` 時 `return 0` 直接吞掉）。使用者在面板空白處按右鍵
沒有任何回饋——不是「這裡沒有選單」，而是看起來像沒收到點擊。而使用者當下
想做的事（改設定、重新整理）都在一個他必須先關閉面板才能到達的選單裡。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 與使用者逐題確認）：

1. **搜尋輸入框維持系統預設的右鍵選單。** 使用者最初要求「包含輸入框」，
   確認代價後改為不含。理由：design-spec §4.9 明文「搜尋輸入沿用原生 EDIT
   （caret、選取、IME、剪貼簿為系統行為）」，攔下 `WM_CONTEXTMENU` 就等於
   拿掉 Paste——而把查詢字串貼進搜尋框是真實且常見的動作。
   **`SearchEditProc` 在本 item 內完全不動。**
2. **選單內容固定三項：`Refresh Apps` / `Settings` / `About`。**
   不放 `Exit`（面板上誤點的代價是整個程式結束），不放 `Open NimbleRun`
   （面板已經開著）。
3. **不抽共用的選單建構 helper。** 三行 `AppendMenuW` 直接寫在面板的
   `WM_RBUTTONDOWN` 分支裡，沿用既有的 `kCmdRefresh` / `kCmdSettings` /
   `kCmdAbout` 常數，命令分派直接呼叫既有的 `DispatchTrayCommand(window, command)`。
   共用的是**分派**（真正有狀態、會變動的部分），不是三行 AppendMenu。
4. **「空白處」就是 `CellAtPoint(...) < 0`。** 不新增幾何判定，涵蓋 footer、
   格子間空隙、結果區下方。與既有的面板拖曳共用同一個「非項目」概念。
5. **命中格子時不附加 Settings。** 項目選單維持項目語意（§4.8 列舉的四項）。
6. **選 Settings 不預先隱藏面板。** 與托盤路徑行為完全一致：`PostMessage`
   給既有的 `kSettingsMessage` 處理，其餘由 NR-013 已經寫好的流程負責。
7. **不加單元測試。** 見 Acceptance 的說明。

## Binding constraints — quoted, do not go looking for them

design-spec §4.8：

> - 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、自常用清單移除、內容（交由 Shell 的 properties verb 顯示）。
> - 點擊面板外，面板自動隱藏。

design-spec §4.9：

> - 搜尋輸入沿用原生 EDIT 控制項（caret、選取、IME、剪貼簿為系統行為），文字左內距 12 DIP、字級 24 DIP。

design-spec §4.10：

> - 開啟 NimbleRun。
> - 重新整理 App。
> - 設定。
> - 關於：顯示一個包含產品名稱與版本號的訊息框，按確認後關閉，不影響面板與執行中狀態。
> - 結束。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- UI strings are English and should be centralized when more than one screen
  needs them.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2509-2589` — 面板的 `WM_RBUTTONDOWN`。**本 item 只改
  開頭那幾行**（`CellAtPoint(...) < 0` 的早退），其餘項目選單原樣不動。
  特別注意既有的三件事，新分支要照抄：
  `SetForegroundWindow(window)`、`g_context_menu_active = true/false` 包住
  `TrackPopupMenu`、之後 `PostMessageW(window, WM_NULL, 0, 0)` 與 `DestroyMenu`。
- `src/app_host/main.cpp:1897-1918` — `ShowTrayMenu()`。三項的字面字串與
  `TrackPopupMenu` 旗標（`TPM_RIGHTBUTTON | TPM_RETURNCMD`）照它寫。
- `src/app_host/main.cpp:1875-1895` — `DispatchTrayCommand()`。新分支的命令
  處理就是呼叫它一次，不要自己 `PostMessage`。
- `src/app_host/main.cpp:2237` 一帶 — `kSettingsMessage` 的處理（NR-013）。
  **只讀不改**，確認它不假設呼叫來源是托盤。
- `src/app_host/main.cpp:2590-2598` — `WM_KILLFOCUS`。`g_context_menu_active`
  就是讓面板在選單彈出期間不被隱藏的守衛；新分支不設它，面板會在選單出現的
  瞬間消失。
- `CellAtPoint` 的定義（`grep` `int CellAtPoint`）——確認 < 0 就是「沒命中任何
  格／列」，不含其他語意。

## Scope

### 1. 空白處右鍵建立選單

`WM_RBUTTONDOWN` 分支開頭，把 `cell < 0` 的早退換成面板選單。大致形狀
（以現場程式碼為準；`g_model`/`g_pins` 的空指標檢查維持在項目路徑上）：

```cpp
    case WM_RBUTTONDOWN: {
        const int cell = CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        if (cell < 0) {
            // NR-060: right-clicking the panel's empty area (gaps, footer, the
            // area below the grid) offers the app-level commands. Settings
            // already exists behind the tray menu; before this the user had to
            // dismiss the panel to reach it. The search EDIT is deliberately not
            // covered -- design-spec §4.9 keeps its native clipboard menu.
            const HMENU menu = CreatePopupMenu();
            if (!menu) {
                return 0;
            }
            AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh Apps");
            AppendMenuW(menu, MF_STRING, kCmdSettings, L"Settings");
            AppendMenuW(menu, MF_STRING, kCmdAbout, L"About");

            POINT cursor{};
            GetCursorPos(&cursor);
            SetForegroundWindow(window);
            g_context_menu_active = true;
            const UINT command = static_cast<UINT>(TrackPopupMenu(
                menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, 0, window, nullptr));
            g_context_menu_active = false;
            PostMessageW(window, WM_NULL, 0, 0);
            DestroyMenu(menu);
            DispatchTrayCommand(window, command);
            return 0;
        }
        if (!g_model || !g_pins) {
            return 0;
        }
        ...既有項目選單原樣...
```

**注意順序**：現況是先檢查 `g_model`/`g_pins` 再算 `cell`。空白處選單不需要
model 與 pins（Refresh/Settings/About 都不看它們），所以 `cell` 的計算要移到
空指標檢查之前。**先確認 `CellAtPoint` 本身不解參考 `g_model`**——若它會，
就把空指標檢查留在最前面，空白處選單也一併受它保護，並在交接區說明。

`DispatchTrayCommand(window, 0)`（使用者按 Esc 或點別處關掉選單）走 `default:`
什麼都不做，不需要額外的 `if (command)`。

### 2. 更新 spec

`docs/design-spec.md` §4.8，在既有的右鍵選單條目之後補一句：

> 面板空白處（格子之間、footer、結果區下方）的右鍵選單提供與通知區選單相同的
> 「重新整理 App／設定／關於」三項；搜尋輸入框內的右鍵維持系統剪貼簿選單（§4.9）。

§4.10 標題下補一句：

> 其中「重新整理 App／設定／關於」三項與面板空白處的右鍵選單共用同一組命令
> 與同一條分派路徑（§4.8）。

不要動 §4.9，該條文在本 item 之後仍然成立。

## How this stays maintainable

**一個命令，一條分派路徑。** 新入口只負責「顯示三個名字」，按下去之後走的是
和托盤一模一樣的 `DispatchTrayCommand`。日後 Settings 的開啟流程再變（例如
改成非 modal），只有一個地方要改，兩個入口自動跟上。這也是本 item 刻意
**不**抽選單建構 helper 的原因：會漂移的是行為，不是三行字串。

## Non-goals

- **攔截搜尋框的右鍵**（Decisions §1）。若日後真要做，必須自己補齊
  Cut/Copy/Paste/Select All，並在新 item 中記錄對 §4.9 的 override。
- **在面板選單加 `Exit` 或 `Open NimbleRun`**。
- **改 Settings 對話框的內容、版面或 `ShowSettingsDialog` 的簽章。**
- **改項目右鍵選單（§4.8 四項）。**
- **把三項 UI 字串搬進 `context_menu_strings`**。目前只有這兩個選單用它們，
  而托盤那份是既有寫法；為此動 NR-018 的字串表不划算。若日後出現第三個
  使用者，那時再集中。
- **鍵盤的 `VK_APPS` / Shift+F10 開選單。** 未在規格中。
- **選單項目的啟用／停用狀態**（例如重建進行中把 Refresh 變灰）。托盤那份
  也沒有，兩邊要一致地簡單。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠（既有 23 項，本 item 不新增測試）。

**為什麼不加測試**：本 item 全部落在 Win32 訊息路徑上——`WM_RBUTTONDOWN` 的
分派、`TrackPopupMenu` 的 modal loop、`PostMessage`，沒有一項可在單元測試中
執行。唯一的純邏輯 `CellAtPoint` 未被改動且已有既有覆蓋。**不要為了製造測試點
而發明 `PanelContextMenuKind(cell)` 之類的 enum 或抽象**（AGENTS.md：Prefer the
smallest working change）。行為由下方手動驗收覆蓋。

Manual（Release build，逐條打勾）：

1. `Alt+Space` 開面板，在**格子之間的空隙**按右鍵：出現三項選單，面板不隱藏。
2. 在 **footer** 與**結果區下方**按右鍵：同樣出現選單。
3. 選 `Settings`：設定對話框開啟，內容與行為與從托盤進入時相同；關閉後面板
   狀態正常。
4. 選 `Refresh Apps`：與 `Ctrl+R` 效果相同。選 `About`：訊息框出現。
5. 按 `Esc` 或點選單外關掉選單：什麼都沒發生，**面板仍在**。
6. 在**某個格子上**按右鍵：仍是原本的 Pin/Unpin、Open file location、
   Properties 等項目選單，**沒有** Settings。
7. 在**搜尋輸入框**內按右鍵：仍是系統的 Undo/Cut/Copy/Paste/Select All，
   Paste 可用。
8. 面板拖曳（空白處按住**左鍵**移動視窗）行為未受影響。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 新選單走既有分派，沒有自己 PostMessage 到 kSettingsMessage：
Select-String -Path src/app_host/main.cpp -Pattern 'DispatchTrayCommand'
# expect: 3 處——定義、ShowTrayMenu 內、新的 WM_RBUTTONDOWN 空白處分支

Select-String -Path src/app_host/main.cpp -Pattern 'kSettingsMessage'
# expect: 常數定義、DispatchTrayCommand 內的 PostMessage、WM_APP 訊息處理，共 3 處（未增加）

# 搜尋框未被動到：
Select-String -Path src/app_host/main.cpp -Pattern 'WM_CONTEXTMENU'
# expect: no match

# 選單期間面板不隱藏的守衛有被設：
Select-String -Path src/app_host/main.cpp -Pattern 'g_context_menu_active'
# expect: 5 處（宣告、既有項目選單 2、新分支 2）

# 沒有新增抽象：
Select-String -Path src/app_host/main.cpp -Pattern 'PanelContextMenuKind|AppendCommonCommands'
# expect: no match

# 改動範圍：
git diff --name-only
# expect: src/app_host/main.cpp、docs/design-spec.md
```

## 交接區

**修改的位置**：只動了 `src/app_host/main.cpp` 的 `WM_RBUTTONDOWN` 分支
（原 `main.cpp:2509` 一帶）與 `docs/design-spec.md` §4.8／§4.10，完全依規格
文字落地，未動 `SearchEditProc`、未加 `WM_CONTEXTMENU`、未抽任何選單建構
helper。

**`CellAtPoint` 是否解參考 `g_model`**：會——`CellAtPoint`（`main.cpp:535`）
第一行就是 `if (!g_model) { return -1; }`，也就是說它自己已經對空指標安全，
`g_model` 為 null 時必回傳 `-1`。因此把 `cell` 的計算移到 `g_model`/`g_pins`
空指標檢查之前是安全的：空白處分支（`cell < 0`）不需要、也不會解參考
`g_model`/`g_pins`；只有命中格子的既有路徑（`cell >= 0`）才會走到後面的
`if (!g_model || !g_pins) return 0;`，維持原本的保護語意不變。

**建置結果**：
```
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Configure 與 build 皆成功，無新增警告（`main.cpp.obj` 編譯與連結乾淨過）。

**CTest 結果**：`ctest --test-dir build --output-on-failure` 第一次跑
23 項中 22 項通過，`nimblerun_lifecycle_check`（整合測試，會實際啟動
app 並模擬托盤 Exit）逾時失敗（"First instance did not exit after tray
Exit command"）；單獨重跑該測試與再次跑全套皆 100% 通過（23/23）。判斷
為既有的時間敏感型整合測試偶發性 flaky，與本次改動的程式碼路徑
（面板 `WM_RBUTTONDOWN`）無關——本次改動完全未觸碰 tray Exit 的流程。

**8 條手動驗收**：本次工作在無互動式 Windows GUI 環境的 agent 下執行，
無法操作 `Alt+Space` 開面板、按右鍵、點選單項目等互動流程，因此
Acceptance 的 8 條 Manual 驗收**全數未執行**，留給人類在 Release build
上逐條打勾：
1. 格子間空隙右鍵出現三項選單、面板不隱藏——未驗證。
2. footer／結果區下方右鍵同樣出現選單——未驗證。
3. 選 Settings 開啟設定對話框，行為與托盤路徑一致——未驗證。
4. 選 Refresh Apps／About 效果正確——未驗證。
5. Esc／點選單外關閉選單後面板仍在——未驗證。
6. 格子上右鍵仍是原本項目選單、無 Settings——未驗證。
7. 搜尋輸入框右鍵仍是系統剪貼簿選單、Paste 可用——未驗證。
8. 面板拖曳（左鍵）行為未受影響——未驗證。

**Sanity greps**（全部符合 item 的 expect）：
- `DispatchTrayCommand`：3 處（定義 `1875`、`ShowTrayMenu` 內 `1917`、新的
  `WM_RBUTTONDOWN` 空白處分支 `2538`）。
- `kSettingsMessage`：3 處（常數定義 `64`、`DispatchTrayCommand` 內的
  `PostMessage` `1884`、`WM_APP` 訊息處理 `2237`），未增加。
- `WM_CONTEXTMENU`：no match。
- `g_context_menu_active`：5 處（宣告 `175`、既有項目選單 2 處
  `2532`/`2535`、新分支 2 處 `2575`/`2578`）。
- `PanelContextMenuKind|AppendCommonCommands`：no match，未新增抽象。
- `git diff --name-only`：本次改動只涉及 `src/app_host/main.cpp` 與
  `docs/design-spec.md`；命令輸出還列出了在本 item 開工前工作目錄中
  已存在、與本 item 無關的其他未提交改動（多份其他 work-items 文件、
  `panel_model.cpp/.h`、`panel_model_test.cpp`、`AGENTS.md`），非本 item
  所致。

**偏差／未完成事項**：無程式邏輯偏差。上述 8 條 Manual 驗收待人類在互動式
環境下驗證後再視為完成。
