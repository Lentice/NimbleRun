# NR-089 — 設定頁快速鍵改為唯讀顯示＋按鍵擷取小對話框

Phase 4 · Depends on: NR-088

- Source: `docs/design-spec.md` §4.1、§FR-002；`src/app_host/settings_dialog.cpp`
  的既有自由輸入欄位
- Origin: 2026-08-08 使用者需求討論（grilling 逐輪確認，見下方 Decisions 逐條
  對應的問答），詳見 [hotkey-override-research.md](../hotkey-override-research.md)
- 依賴 NR-088：需要它把 `ParseHotkey` 對 Win 鍵的處理從硬拒絕降級為可解析，
  以及它新增的 `TryRegisterHotkey(HWND, const HotkeyBinding&)` 探測函式
  （`src/app_host/hotkey.h`／`.cpp`）。**開始本 item 前先確認 NR-088 已
  `done`**，本 item 不重做它的範圍。

## Why

現況：`settings_dialog.cpp` 的快速鍵欄位是一個純自由輸入的 Edit control
（`IDC_HOTKEY_EDIT`，`settings_dialog.cpp:99,161-166`），使用者要自己打
`Ctrl+Alt+E` 這種文字，容易打錯格式、也不知道有哪些鍵可用。

使用者要求改成：

1. 快速鍵欄位改成唯讀顯示（不能打字），字體略大、加粗。
2. 旁邊加一個「Change」按鈕，按下彈出一個小對話框。
3. 小對話框讓使用者直接按下實體按鍵來設定，畫面即時顯示目前偵測到的組合。
4. 只接受「修飾鍵組合」：Ctrl／Alt／Shift／Win，允許同時多個修飾鍵
   （例如 `Ctrl+Alt+E`）。從使用者開始按下（key down）直到所有相關修飾鍵
   全部放開（key up）才視為「這次擷取結束、組合確定」，並且要考慮放開順序
   不固定（例如先放 Ctrl 再放 Alt，或反過來）。
5. 若擷取到的組合與系統或其他程式衝突，只在對話框下方顯示提示訊息，不阻擋；
   使用者按下「確認」鍵後就套用新快速鍵，衝突與否由使用者自行處理。
6. 小對話框有「確認」與「取消」兩個按鈕。

技術上的關鍵坑（已用 GitHub 上 Wox／Flow Launcher 的既有 issue 交叉確認，
兩者都踩過同樣的洞後改用低階鍵盤 hook）：

- **Alt 不會產生 `WM_KEYDOWN`**，而是 `WM_SYSKEYDOWN`／`WM_SYSKEYUP`，一般
  對話框的 `WM_COMMAND`／`IsDialogMessage` 鍵盤前處理不會把它們原封不動交給
  應用程式邏輯，且會被系統選單語意（Alt 進入選單模式）打斷。
- **Win 鍵**在一般視窗訊息迴圈裡預設會觸發 Start Menu，若不攔截，使用者一
  按 Win 對話框就失焦、Start Menu 蓋版，擷取邏輯完全看不到後續按鍵。
- Flow Launcher 的 issue #129（key-down vs key-up 觸發時機）與 Wox 的
  issue #2445（Win 鍵綁定會變成 `Win + LWin` 且一按下就誤觸發）都是同一類
  「用普通視窗訊息處理修飾鍵不夠、需要更底層攔截」的坑。

因此本 item 的擷取邏輯**不能**只靠對話框的 `WM_KEYDOWN`／`WM_KEYUP`，而是
在小對話框開啟期間安裝一個**只在對話框存活期間生效、對話框關閉立刻移除**的
低階鍵盤 hook（`WH_KEYBOARD_LL`），用來：

- 同時可靠攔截 Ctrl／Alt／Shift／Win／一般鍵的 down／up，不受 Alt 走
  `WM_SYSKEYDOWN`、對話框鍵盤導覽（Tab/Enter 被 `IsDialogMessage` 吃掉）
  影響。
- 在擷取進行中吞掉 Win 鍵的預設行為（回傳非 0 阻止系統處理該次按鍵事件），
  避免 Start Menu 彈出打斷擷取；hook 移除後，Win 鍵恢復正常系統行為。

這不算是違反 `docs/design-spec.md:299`「不安裝低階鍵盤 hook，也不攔截任何
輸入」——那句話描述的是**背景待機時**偵測全域快捷鍵的機制選擇（用
`RegisterHotKey`，不用 hook 常駐監聽所有按鍵），跟「使用者主動點開一個小
對話框、以此對話框為生命週期範圍安裝／移除的擷取用 hook」是不同情境，
背景待機路徑完全不受影響。**本 item 需要在 design-spec §FR-002 或鄰近位置
補一句澄清**，避免未來讀者誤以為兩者矛盾（見 Scope §5）。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08，逐輪確認記錄）：

1. **修飾鍵判定時機＝「非修飾主鍵按下當下，相關修飾鍵仍按住」**：不是「按過
   就算」。例如使用者依序按 Ctrl → Alt → E，只有 E 被按下那一刻 Ctrl 與 Alt
   都還在按住狀態，才算組成 `Ctrl+Alt+E`。
2. **不允許純修飾鍵組合**：必須至少一個非修飾主鍵（字母／數字／功能鍵／
   `ParseHotkey` 既有 `kNamedKeys` 清單內的鍵，見 NR-088 的
   `settings_editor.cpp:37-44`）。純修飾鍵（例如只按 `Ctrl+Alt` 就放開）
   不構成合法組合，`RegisterHotKey` 本身也不支援。
3. **擷取中按到不合法輸入（例如滑鼠事件、或放開時發現沒有主鍵）**：即時在
   對話框下方顯示錯誤提示文字，允許使用者立刻重新開始按，不需要清空整個
   對話框重新開啟。
4. **WIN 鍵政策**：擷取 UI 顯示 WIN（依賴 NR-088 讓 `ParseHotkey` 能解析出
   `MOD_WIN`）；套用時比照其他「衝突」情況——只顯示警告訊息、仍允許套用。
   不恢復 `ParseHotkey` 對 Win 鍵的硬拒絕。
5. **衝突偵測機制＝兩者並用**：
   - `ParseHotkey` 既有的 shell-reserved 靜態清單（NR-086，
     `settings_editor.cpp:242-248`）先擋 `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc`
     ——這三個維持**硬拒絕**（NR-088 Decisions §2 已明定不降級），擷取對話框
     偵測到這三個時視同「不合法輸入」（比照 Decisions §3 的錯誤提示，不能
     被確認套用，因為 `ParseHotkey` 本身就會回傳 `false`，沒有合法
     `HotkeyBinding` 可套用）。
   - 其餘所有能被 `ParseHotkey` 解析出 `HotkeyBinding` 的組合（含 Win 鍵、
     含目前正被別的程式註冊住的組合），用 NR-088 的
     `TryRegisterHotkey(HWND, const HotkeyBinding&)` 即時探測，若探測失敗
     則在下方顯示衝突警告，但**不阻擋確認鍵**（Decisions §4／Why）。
6. **小對話框實作方式＝沿用現有 native Win32 dialog resource 做法**：新增
   一個 `.rc` dialog template，用 `DialogBoxParamW` 開啟，比照
   `settings_dialog.cpp:365,402` 的既有模式（`ShowSettingsDialog` 呼叫
   `DialogBoxParamW`）。不引入 Direct2D 自訂彈窗或第三方 UI 元件。
7. **單獨按 Esc（無修飾鍵）＝取消對話框**，比照 Cancel 按鈕。只有當 Esc
   搭配修飾鍵按下（例如 `Ctrl+Esc`）時才視為「嘗試組成的快速鍵一部分」，
   交給 `ParseHotkey` 判斷（會被 NR-086 既有規則擋下，走 Decisions §5 的
   「不合法輸入」提示路徑）。
8. **快速鍵欄位的唯讀顯示**：原本的 `IDC_HOTKEY_EDIT` 改成唯讀（例如加上
   `ES_READONLY` style，或直接換成 Static control——擇一即可，取決於
   `.rc` 修改的最小幅度），字體比對話框預設字體略大且加粗。對話框整體是
   native Win32 dialog（非 Direct2D），字體差異用該 control 的
   `WM_SETFONT`／自訂 `HFONT`（`CreateFontW` 加大 `lfHeight`、
   `lfWeight = FW_BOLD`）達成，不需要 DirectWrite（那是主視窗面板
   `main.cpp` 的 `g_text_format` 等物件在用，設定對話框從未用過 D2D，見
   NR-088 探索階段的既有結論）。
9. **設定存檔／還原／套用後生效都要驗證，不只是 UI 擷取邏輯本身**：使用者
   明確要求「注意各種快速鍵 save/restore setting file 是否正常，以及之後
   套用是否能正常使用」。這代表擷取到的新組合（含 Win 鍵組合）走完整既有
   `SettingsEditor::SetHotkey` → `SettingsEditor::Apply` →
   `GlobalHotkey::Swap` → `SettingsStore` 寫檔路徑，本 item 的驗收必須覆蓋
   「寫檔格式正確」「重啟讀回一致」「新快速鍵能被 `RegisterHotKey` 實際
   觸發面板」三段，不能只驗 UI 層擷取到的字串對不對。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md:135`（§4.1）與 `:296-302`（§FR-002）：已由 NR-088 明確
覆寫「Windows 鍵…一律拒絕」這句；本 item 承接 NR-088 完成後的行為，不重複
覆寫，但**新增一句澄清**低階鍵盤 hook 的適用範圍（Scope §5）。

`AGENTS.md`：

- Keep the idle path event-driven: no busy loops and no high-frequency
  timers.（本 item 的 hook 只在對話框開啟期間存在，對話框關閉立即
  `UnhookWindowsHookEx`，不影響待機路徑。）
- Do not overwrite user data in place. Use temporary files and atomic
  replacement for persistent writes.（既有 `SettingsStore` 的 atomic
  replace 機制不變，本 item 不改寫入格式。）
- UI strings are English and should be centralized when more than one
  screen needs them.（新對話框的所有文案走
  `SettingsString`／`SettingsStringText`，`settings_editor.h`／`.cpp`
  既有機制，不要在 `settings_dialog.cpp` 內寫字面字串常數。）
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/settings_dialog.cpp:80-320`（`Populate`、
  `SettingsDialogProc` 的 `WM_INITDIALOG`／`WM_COMMAND`／`IDOK` 分支）——
  `IDC_HOTKEY_EDIT` 目前的讀寫方式（`:99`、`:161-166`），本 item 的主要
  修改起點。
- `src/settings/settings_editor.h`／`.cpp` — `SettingsEditor::SetHotkey`、
  `HotkeyBinding`、`ParseHotkey`、`FormatHotkey`（`:255+`，把
  `HotkeyBinding` 轉回顯示字串，擷取對話框顯示目前組合要用它）、
  `SettingsString`／`SettingsStringText`（新增對話框文案要走這裡：標題、
  提示文字、衝突警告、Confirm/Cancel 按鈕字）。
- `src/app_host/hotkey.h`／`.cpp`（NR-088 完成後的版本）—
  `TryRegisterHotkey` 簽章與行為，本 item 的衝突偵測直接呼叫它。
- Win32 低階鍵盤 hook 的既有專案內先例：目前專案內**沒有**任何 hook 相關
  程式碼（`Grep -r "SetWindowsHookEx"` 預期落空）——本 item 是專案內第一個
  用到 `WH_KEYBOARD_LL` 的地方，沒有既有型態可抄，需要自行對照 Win32
  `KBDLLHOOKSTRUCT`／`LowLevelKeyboardProc` 文件寫（不新增第三方依賴，
  純 Win32 API，符合 `AGENTS.md`「Use the C++ standard library or Win32
  native APIs before adding dependencies」）。
- `docs/design-spec.md:135`、`:296-302`（本 item 要在 §FR-002 旁補充低階
  鍵盤 hook 的適用範圍澄清，見 Scope §5）。
- 新 `.rc` 資源：找現有 `settings_dialog.cpp` 對應的 `.rc` 檔案（grep
  `IDC_HOTKEY_EDIT` 或 `IDD_SETTINGS` 定位檔名），比照既有 dialog template
  的寫法新增一個新的 `IDD_HOTKEY_CAPTURE` template 與對應的
  `resource.h` ID 定義區塊。

## Scope

1. **`settings_dialog.cpp` 快速鍵欄位唯讀化＋字體**：
   - `.rc` 內 `IDC_HOTKEY_EDIT` 加 `ES_READONLY`（或改為 Static text
     control，擇一，取最小改動幅度且不破壞既有 `GetDlgItemTextW`／
     `SetDlgItemTextW` 呼叫相容性——若改 Static 需同步檢查
     `settings_dialog.cpp:161` 的 `GetDlgItemTextW` 呼叫是否要跟著調整
     讀取方式）。
   - 在 `WM_INITDIALOG` 建立一個略大、`FW_BOLD` 的 `HFONT`（相對於對話框
     預設字型放大，例如 +2pt），透過 `WM_SETFONT` 套用到
     `IDC_HOTKEY_EDIT`，並在 `WM_DESTROY`／dialog 結束時
     `DeleteObject` 釋放，避免 GDI 物件洩漏。
2. **新增「Change」按鈕**：`.rc` 內在快速鍵欄位旁新增一個按鈕控制項
   （新 ID，例如 `IDC_HOTKEY_CHANGE`），`SettingsDialogProc` 的
   `WM_COMMAND` 新增對應分支，呼叫下方第 3 點的新對話框函式。
3. **新增按鍵擷取小對話框**（新 `.rc` dialog template + 新的
   `DialogBoxParamW` 呼叫，比照 `ShowSettingsDialog` 既有模式）：
   - 顯示區：唯讀文字，即時顯示目前偵測到的組合（用
     `FormatHotkey`／既有格式，例如 `Ctrl+Alt+E`），初始為空或提示文字
     （走 `SettingsString`）。
   - 下方訊息區：顯示 Decisions §3（不合法輸入）或 Decisions §5（衝突
     警告）的提示文字，兩者共用同一個訊息區塊，同一時間只顯示一則。
   - Confirm／Cancel 兩個按鈕（`IDOK`／`IDCANCEL`）。Confirm 按鈕只有在
     目前已擷取到一個合法組合（`ParseHotkey` 能解析出
     `HotkeyBinding`，即便有衝突警告）時才可用／有效；沒有合法組合時按
     Confirm 視同無動作或維持提示。
   - 對話框開啟（`WM_INITDIALOG`）時安裝 `WH_KEYBOARD_LL`；關閉
     （`IDOK`／`IDCANCEL`／`WM_CLOSE`／Esc 快捷路徑，Decisions §7）時在
     同一個結束路徑上 `UnhookWindowsHookEx`，確保沒有任何提前 return
     漏移除 hook 的分支。
   - Hook callback 邏輯（核心狀態機，對應 Decisions §1／§2／§7）：
     - 追蹤目前按住的修飾鍵集合（Ctrl/Alt/Shift/Win 各自的 down/up，
       用 `KBDLLHOOKSTRUCT::vkCode` 判斷是左右鍵中的哪個修飾鍵，並各自
       記錄，因為 up 順序不固定，需要一個「目前哪些修飾鍵仍按住」的
       集合而不是單一 flag）。
     - 收到非修飾鍵 down 時：若目前修飾鍵集合非空，記錄「候選組合＝目前
       修飾鍵集合 + 這個非修飾鍵」，更新顯示區為即時預覽；若修飾鍵集合
       為空，走 Decisions §2／§3 的不合法提示路徑。
     - 收到非修飾鍵 down、但同時它本身是修飾鍵按下前已經放開的殘留狀態
       時（例如 Alt+Tab 系統吃掉某次 up 事件的邊界情況），以「目前
       hook 觀察到的 down/up 事件序列」為準，不用 `GetAsyncKeyState`
       這種會受系統狀態污染的旁路查詢——全部狀態由 hook 收到的事件自行
       維護。
     - 當「候選組合」存在，且所有相關修飾鍵陸續全部收到 up 事件（不論
       順序）之後，才視為「這次擷取結束、組合確定」：呼叫
       `ParseHotkey`／或直接用內部累積的 `HotkeyBinding` 資料，若落入
       shell-reserved 清單（Decisions §5 第一點）顯示不合法提示並允許
       重新開始；否則呼叫 `TryRegisterHotkey` 探測，依結果顯示或不顯示
       衝突警告，但保留這個組合為「目前可被 Confirm 套用」的候選。
     - 按住 Win 鍵期間，hook callback 回傳非 0（吞掉事件）以抑制 Start
       Menu，直到這次擷取序列結束或對話框關閉。
   - Confirm 邏輯：把候選 `HotkeyBinding` 格式化成文字（`FormatHotkey`），
     呼叫 `g_dialog.editor->SetHotkey(...)`（沿用既有 working-copy 機制，
     跟主設定對話框的 `IDOK` 共用同一個 `SettingsEditor`），關閉小對話框
     並回到主設定對話框，主對話框的唯讀欄位刷新成新值（`Populate` 或等效
     局部更新）。**此時只是寫入 working copy，真正落盤與
     `GlobalHotkey::Swap` 仍在主設定對話框按下它自己的 OK 時才發生**——
     不改變現有「主對話框 OK 才真正套用並存檔」的既有流程
     （`settings_dialog.cpp:158-211`），小對話框的 Confirm 只是把值放進
     working copy，這點與 Decisions §9 的「走完整既有套用路徑」一致。
4. **`SettingsString` 新增項目**：擷取對話框標題、提示文字（例如 "Press
   the key combination"）、不合法輸入訊息、衝突警告訊息、（若沿用既有
   `OkButton`／`CancelButton` 則不必新增，否則新增對應鍵）。
5. **`docs/design-spec.md` 澄清補充**：在 §FR-002（`:296-302`）後面加一句
   澄清「低階鍵盤 hook 僅限使用者主動開啟的快速鍵擷取小對話框、以該對話框
   生命週期為範圍安裝與移除；背景待機與一般快捷鍵偵測仍完全依賴
   `RegisterHotKey`，不受影響」，避免與既有『不安裝低階鍵盤 hook』一句
   產生誤讀衝突。這是文件澄清、不是產品行為變更，維持既有段落結構，只加
   一句。

## Non-goals

- 不重做 NR-088 的範圍（`ParseHotkey` 的 Win 鍵解析、`TryRegisterHotkey`
  本身的實作）——本 item 只消費它。
- 不支援滑鼠按鍵或滾輪作為快速鍵的一部分（`RegisterHotKey` 支援的是鍵盤
  虛擬鍵碼，滑鼠事件超出範圍，且使用者的敘述只提到「按鍵」）。
- 不做「同時支援打字輸入與擷取」的雙模式：唯讀欄位就是唯讀，不保留舊的
  自由輸入路徑作為 fallback。
- 不新增快速鍵「歷史記錄」或「多組可切換快速鍵」——維持現況單一全域快速鍵
  的產品範圍，只換輸入方式。
- 不在小對話框內加入「立即測試/預覽面板彈出」之類的額外互動——擷取到組合
  只顯示文字與衝突訊息，不做任何額外的視覺回饋機制。
- 不處理低階鍵盤 hook 在極端情境下的效能／相容性邊界（例如與其他也裝了
  低階鍵盤 hook 的協力軟體互動）——這類問題等使用者實際回報再開新 item，
  比照 `docs/design-spec.md:784` 既有「下次使用者叫出時再試，不高頻
  retry」的處理風格：先求正確運作，不預先為未發生的相容性問題設計方案。

## Acceptance

Automated：

1. `ctest --test-dir build --output-on-failure` 全綠。
2. 新增針對擷取狀態機的 self-check（不需要真的模擬 OS 層級鍵盤事件；把
   hook callback 的核心判斷邏輯（修飾鍵集合追蹤、「主鍵 down 時修飾鍵是否
   仍按住」、「全部 up 才確定」）抽成一個不依賴 `HHOOK`／HWND 的純函式或
   小型狀態機類別，可以直接餵合成的 `(vkCode, isDown)` 事件序列做單元測試）
   涵蓋：
   - `Ctrl down → Alt down → E down → Ctrl up → Alt up`：確定為
     `Ctrl+Alt+E`。
   - 同上但放開順序改成 `Alt up → Ctrl up`：仍確定為 `Ctrl+Alt+E`（驗證
     Decisions §1 的「放開順序不固定」）。
   - `Ctrl down → Ctrl up`（沒有主鍵）：不產生合法組合。
   - `E down`（沒有任何修飾鍵）：不產生合法組合，走不合法提示路徑。
3. 新增至少一項「設定存檔／讀回一致」的測試或 self-check：把一個含
   `MOD_WIN` 的 `HotkeyBinding` 走 `SettingsEditor::SetHotkey` →
   `SettingsStore` 寫檔 → 重新 `Load` 讀回，確認格式往返一致（呼應
   Decisions §9）。

Manual（因涉及實際鍵盤輸入與低階 hook，無法完全自動化，需在 Release build
人工驗證，記錄於交接區）：

4. 實機開啟設定頁，快速鍵欄位確認唯讀（無法點擊輸入游標打字）、字體比其他
   欄位略大且加粗。
5. 點擊 Change，實際按下 `Ctrl+Alt+E` 等組合，確認畫面即時顯示、Win 鍵組合
   也能正常顯示且不會誤彈出 Start Menu。
6. 按下 Confirm 套用一個刻意製造衝突的組合（例如先用另一支測試程式
   `RegisterHotKey` 占用同一組合），確認畫面顯示衝突警告但 Confirm 仍可
   套用；套用後確認全域快速鍵確實能喚出主面板（若真的衝突則確認既有
   `GlobalHotkey::Swap`／`HotkeyRejectedNotice` 行為介入，不會讓程式進入
   壞狀態）。
7. 重啟程式，確認新快速鍵從 `settings.ini`（或既有設定檔）正確還原並持續
   可用。
8. 在擷取對話框內單獨按 Esc，確認對話框直接取消、不套用任何變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path src/app_host/hotkey.h,src/app_host/hotkey.cpp -Pattern 'TryRegisterHotkey'
# expect: 命中（NR-088 已完成的前置依賴）

Select-String -Path src/app_host/settings_dialog.cpp -Pattern 'WH_KEYBOARD_LL|SetWindowsHookEx'
# expect: 至少一處，且同檔案內能找到對稱的 UnhookWindowsHookEx

git diff --name-only
# expect: src/app_host/settings_dialog.cpp、對應 .rc 與 resource.h、
#         src/settings/settings_editor.h/.cpp（新增 SettingsString 項目）、
#         docs/design-spec.md（§FR-002 澄清句）、對應測試檔（及本 item
#         文件與 docs/work-items.md 追蹤表格）
```

## 交接區

實作已存在於既有本地 commit `ec7608b`：設定欄位唯讀化與 Change 按鈕、native
`IDD_HOTKEY_CAPTURE`、`WH_KEYBOARD_LL` 擷取狀態機、衝突警告與 working-copy
套用流程均已完成；`hotkey_capture_test` 覆蓋左右放開順序、純修飾鍵、無修飾主鍵
與 Win／shell-reserved 案例，settings roundtrip 也已覆蓋。Release build 成功，
完整 CTest **24/24 通過**，hook install/uninstall sanity grep 符合。本次 opencode
job 只做 clean-worktree 驗證，沒有新增 patch。

手動驗證步驟 4–8 未執行（需實體鍵盤／GUI）；另有既存 stale comment 仍寫 Win key
rejected，屬文件瑕疵，未擴大本 ticket scope。未完成事項：無。
