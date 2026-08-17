# NR-190 — Optional English input mode on panel show

- Phase: 5 release gate
- Depends on: NR-004、NR-010、NR-013
- Source: docs/design-spec.md §4.1、§4.9、§4.10、§FR-002、§FR-013、§NFR-006、§9.3、§10.1、§10.2、§10.4

## Goal

新增一個預設關閉的設定，讓使用者選擇 NimbleRun 主視窗顯示時是否把搜尋框的輸入狀態切換為 IME 的英文／英數模式。

啟用後，主視窗只有在「隱藏 → 可見」的轉換中，且搜尋框已取得焦點後才執行一次切換。Hotkey、tray 左鍵點擊與 tray menu 的 Open 必須共用這個行為。切換只作用於目前搜尋框的輸入模式，不切換 Windows 鍵盤配置、不改變其他程式的輸入狀態。

## 已確認的產品決策

- 設定名稱與英文 UI label：Switch input to English on show。
- C++ 設定欄位與 settings.ini key：english_input_on_show。
- 預設值為 false；Reset settings 也回到 false。
- 舊版 settings.ini 缺少此 key 時，載入 false；schema 維持 1，不做 migration。
- 設定關閉時不得呼叫輸入模式 API。
- 設定開啟時，只處理真正的 hidden→visible；面板已可見時再次收到 tray／show 訊息不得重複切換。
- 不以程式啟動時的 Windows 顯示語言作為持久化 gate。當下焦點輸入環境才是判斷依據；英文系統或沒有可用 IME context 時，切換請求應安全 no-op。
- 優先使用 Windows TSF 的標準 conversion mode，無法使用時以 IMM32 fallback；不呼叫輸入法廠商私有 API。這是對標準 TSF／IMM32 輸入法的 best-effort 相容，不承諾所有第三方私有輸入法都能被強制控制。

## Binding constraints

以下規則是本 item 的硬性約束，實作 agent 不得只依賴外部文件自行推導：

- design-spec §4.1 的既有行為是「面板出現後搜尋框取得焦點」；本 item 只能在這個既有流程上增加選配行為。
- design-spec §4.9 已規定「搜尋輸入沿用原生 EDIT 控制項（caret、選取、IME、剪貼簿為系統行為）」；本 item 使用 Windows 原生輸入 API，不替換 EDIT、不自行繪製輸入法。
- design-spec §FR-002 規定全域快捷鍵使用 RegisterHotKey 與 MOD_NOREPEAT；不得為此功能加入 low-level keyboard hook。
- design-spec §FR-013 的設定由既有 settings.ini 保存；新增 key 必須沿用 schema=1、既有 default、parse、atomic save 路徑。
- design-spec §10.2 規定 settings.ini 是少量 key/value 格式；使用既有 SettingsStore，不引入 SQLite 或新的 serialization format。
- design-spec §NFR-006 規定內部字串使用 UTF-16、檔案交換格式使用 UTF-8；NimbleRun UI label 必須是英文。
- AGENTS.md 規定：「Read the relevant design-spec section and trace existing callers before changing shared code。」
- AGENTS.md 規定：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions。」
- AGENTS.md 規定：「Use the C++ standard library or Win32 native APIs before adding dependencies。」
- AGENTS.md 規定：「New non-trivial logic needs one focused runnable test or self-check。」
- AGENTS.md 規定不得加入網路、telemetry、third-party runtime dependency、service、driver 或 administrator requirement；使用者資料寫入必須維持 temporary file 加 atomic replacement。
- docs/development.md 的 module boundary 是 ui 擁有 HWND、focus、input、DPI、rendering，storage 擁有 settings；核心 value type 應維持不持有 HWND 或 Shell COM。
- docs/development.md 的 change workflow 要求：讀規格、確認最窄 module boundary、重用既有型別與 helper、補 focused test、執行 build／test／manual checks、同步文件；沒有 measured need 不得新增 dependency、background loop、framework 或 abstraction。

## 必讀與追蹤範圍

實作前必須讀完並追蹤下列檔案：

- AGENTS.md
- CONTEXT.md
- docs/development.md
- docs/design-spec.md 的 §4.1、§4.9、§4.10、§FR-002、§FR-013、§NFR-006、§9.3、§10.1、§10.2、§10.4
- docs/testing.md
- docs/work-items.md 的使用方式、Agent 交付規則與「已否決的方向」
- docs/work-items/NR-004-settings-store.md
- docs/work-items/NR-010-list-vertical-slice.md
- docs/work-items/NR-013-settings-ui.md
- docs/work-items/NR-002-single-instance-tray.md
- docs/work-items/NR-003-global-hotkey.md
- src/settings/settings_store.h、src/settings/settings_store.cpp
- src/settings/settings_editor.h、src/settings/settings_editor.cpp
- src/app_host/settings_dialog.cpp
- src/app_host/main.cpp
- src/ui/ 目前的 CMake source 與 module style
- src/win/com.h
- src/resources/resource.h、src/resources/NimbleRun.rc
- CMakeLists.txt、tests/CMakeLists.txt
- tests/unit/settings_store_test.cpp
- tests/unit/settings_editor_test.cpp
- tests/integration/lifecycle_check.ps1

必須 trace 的既有 caller／入口：

- src/app_host/main.cpp 的 ShowPanel（目前約在 1950 行）：定位、顯示、設定 live reload、清 query、SetFocus(g_search_edit)。
- WindowProc 的 g_show_panel_message 分支：tray 左鍵、tray menu Open、second instance wake-up 都應通過此分支。
- WindowProc 的 WM_HOTKEY 分支：可見時 HidePanel，不可見時 ShowPanel。
- DispatchTrayCommand 的 kCmdOpen 分支與 kTrayCallbackMessage 的 WM_LBUTTONUP 分支。
- WindowProc 的 kSettingsMessage 分支：SettingsDialog Apply 後重載 g_settings，確保新選項不必重啟即可影響下一次顯示。
- wWinMain 的 SettingsStore Load、g_settings 初始化與 g_hide_after_launch 初始化。

## Scope

### 1. 規格與持久化設定

更新 docs/design-spec.md：

- 在 §4.1／§4.9 描述「啟用設定後，hidden→visible 的搜尋框 focus 完成後切換為英文／英數輸入模式」。
- 在 §FR-013 加入此設定，標明預設關閉。
- 在 §10.2 或設定格式的既有說明中列出 english_input_on_show=true/false，並明確記錄缺少 key 的 backward-compatible default false。
- 不新增啟動時系統語言旗標，不把 keyboard layout 切換列入產品行為。

更新 docs/testing.md：

- 保留既有 hotkey／tray smoke test。
- 加入設定關閉、設定開啟、hidden→visible、面板已可見重複 show、Microsoft New Phonetic 與另一個可用 IME 的手動驗收步驟。
- 手動驗收需明確區分「IME 英文／英數模式」與「Windows keyboard layout」。

### 2. SettingsStore 與 SettingsEditor

修改既有 settings 模組，不建立第二套設定抽象：

- 在 Settings 加入 bool english_input_on_show = false。
- DefaultSettings() 必須產生 false。
- SettingsStore::Load 解析 english_input_on_show；合法值 true／false 套用，缺少或非法值保持 false；仍遵守既有 schema=1、unknown-key、corrupt/newer-schema 與 atomic-save 行為。
- SettingsStore::Save 寫入 english_input_on_show。
- SettingsEditor 加入 SetEnglishInputOnShow(bool enabled)，沿用既有 dirty tracking、Apply、rollback、ResetToDefaults。
- 在 SettingsString 加入 EnglishInputOnShowLabel，SettingsStringText 回傳 Switch input to English on show。
- 擴充既有 settings_store_test.cpp 與 settings_editor_test.cpp：default false、missing-key false、invalid value false、round-trip true/false、Reset false、設定模型與 UI string key。

### 3. Settings dialog

修改既有設定對話框：

- resource.h 新增下一個控制項 ID，預期為 IDC_ENGLISH_INPUT_ON_SHOW = 2032；若實際檔案已有佔用，使用下一個未使用 ID，不重排既有 ID。
- NimbleRun.rc 在既有 LAUNCHER group 內加入英文 checkbox；維持既有 440x338 對話框與可讀版面，只有必要時調整同 group 控制項位置。
- settings_dialog.cpp 的 InitLabels、Populate 與 WM_COMMAND checkbox 分支接上新 string key、SettingsEditor setter 與 Settings 欄位。
- Reset／Cancel／Save 行為沿用既有 SettingsEditor semantics；預設 checkbox 必須未勾選。
- UI 所有新文字維持英文，不新增中文 UI。

### 4. 跨 IME 的 native input helper

在既有 ui module 新增最小 helper：

- src/ui/input_mode.h／src/ui/input_mode.cpp。
- 提供可測的 transition predicate：ShouldSetEnglishInputMode(bool enabled, bool was_visible)；結果只有 enabled=true 且 was_visible=false 時為 true。
- 提供 best-effort native operation：SetEnglishInputMode(HWND edit)。
- SetEnglishInputMode 只接受目前搜尋框 HWND；nullptr、無效 HWND、沒有可用 IME context 或 native API 失敗時回傳 false，不拋例外、不阻擋面板顯示、不修改 settings。
- 先用 TSF 的 thread-manager keyboard compartments 設定 alphanumeric conversion mode（TF_CONVERSIONMODE_ALPHANUMERIC）；TSF 不可用時再用 IMM32 的 ImmGetContext、ImmSetOpenStatus／ImmSetConversionStatus，並正確釋放 HIMC。
- 重用 src/win/com.h 的既有 COM RAII；helper 不自行 CoInitializeEx／CoUninitialize，不持有 Shell COM，不加入 vendor-specific API。
- 不使用 LoadKeyboardLayout、ActivateKeyboardLayout 或其他改變鍵盤配置的 API。
- 將 helper 納入 nimblerun_ui；只加入 Windows native link library（實際需要的 imm32／既有 ole32／uuid linkage），不得加入第三方 dependency。

### 5. ShowPanel 接線

修改 src/app_host/main.cpp：

- 在 ShowPanel 一開始、第一次 SetWindowPos 前保存 const bool was_visible = IsWindowVisible(window) != FALSE；不可在視窗已顯示後才取值。
- 保持既有 shared ShowPanel 路徑，不在 hotkey、tray click、tray menu 各自複製輸入處理。
- settings live reload 時一併取得 english_input_on_show，包含 SettingsDialog Apply 後的下一次顯示。
- 既有 SetFocus(g_search_edit) 成功後，呼叫 ShouldSetEnglishInputMode(g_settings.english_input_on_show, was_visible)；為 true 才呼叫 SetEnglishInputMode(g_search_edit)。
- 不要在 panel hide、每次按鍵、WM_SETFOCUS、timer 或 background worker 再次執行。
- 保持既有空 query、row refresh、focus、tray、second-instance wake-up 與 hide 行為不變。

## Non-goals

- 不切換 Windows keyboard layout、input locale 或其他程式的輸入法。
- 不在程式啟動時儲存或快取「英文系統」判斷，也不以顯示語言取代目前 focus context。
- 不在面板隱藏時恢復使用者原本的中文模式。
- 不在搜尋框每次輸入、每次 focus 或固定 timer 中強制英文。
- 不加入 low-level keyboard hook、背景執行緒、輪詢或 vendor-private IME API。
- 不做中文拼音／注音／同義詞展開；本 item 只處理輸入模式。
- 不保證不遵守公開 TSF／IMM32 行為的第三方輸入法一定能切換；這類失敗必須 graceful no-op。
- 不改變 hotkey 註冊規則、catalog、search ranking、launch、usage、tray command semantics 或資料檔 schema version。

## Acceptance

- 新安裝、DefaultSettings、Reset settings 與缺少 key 的既有 settings.ini 都是 disabled／unchecked。
- english_input_on_show=true 能保存、重載並在 Settings dialog 顯示勾選；false 能保存並顯示未勾選；invalid value 不會把預設變成 enabled。
- 關閉設定時，hotkey／tray click／tray menu Open 顯示面板的輸入內容與目前版本完全相同，且不執行輸入模式切換。
- 開啟設定時，三個入口都在 hidden→visible 顯示後先讓搜尋框取得焦點，再嘗試切換為英文／英數模式。
- 面板已可見時再次收到 show 請求不會重複切換；先手動切回中文，保持可見期間不會被下一個訊息改回英文；隱藏後再次顯示才會重新執行一次。
- Microsoft New Phonetic 在中文模式下，以 hotkey、tray click、tray menu Open 各開啟一次，搜尋框輸入 Latin letters 時直接得到英文／英數文字；面板保持可見時手動切回中文仍可輸入中文。
- 至少另一個本機可用的標準 Windows TSF／IMM32 IME 完成相同手動驗收；若某第三方 IME 不支援公開模式切換，面板仍正常顯示且不 crash。
- 英文系統／無 active IME context 時，啟用設定不會造成 layout 改變、錯誤對話框或 panel show failure。
- Existing settings、hotkey、tray、search、lifecycle tests 保持通過；新增邏輯有 focused runnable test／self-check，且 Release build 不依賴被 NDEBUG 移除的 assert。
- 文件明確記錄此設定、預設值、TSF／IMM32 best-effort 邊界與手動驗收方式。

## Agent checks

先依 AGENTS.md 的 Release 流程執行：

~~~powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
~~~

再執行 focused checks：

~~~powershell
ctest --test-dir build -R "settings|input_mode|lifecycle" --output-on-failure
~~~

最後執行完整 suite：

~~~powershell
ctest --test-dir build --output-on-failure
~~~

Focused runnable coverage 必須包含：

- nimblerun_settings_test：default／missing／invalid／round-trip。
- nimblerun_settings_ui_test：editor setter、Reset、string key。
- 新增的 nimblerun_input_mode_test：ShouldSetEnglishInputMode truth table 與 nullptr／無 context failure-safe；測試不得以 assert 作唯一驗證。
- nimblerun_lifecycle_check：既有 single-instance／tray lifecycle 不回歸。

實際 IME provider 的 TSF／IMM32 行為不應硬綁 CI runner；依 docs/testing.md 的手動 smoke matrix 驗收。若 Agent 能在可控 Windows 桌面執行手動矩陣，應把 provider、目前模式、入口、結果與失敗原因記入交接區。

## Non-goal boundaries for implementation agent

- 不要順手抽出 PanelHost、泛用 settings policy、泛用 IME abstraction 或新的 persistence framework。
- 不要把 Settings value 變成持有 HWND／COM pointer 的物件。
- 不要把 input helper 的失敗轉成阻擋式錯誤提示；這是 optional convenience。
- 不要因為 TSF header／provider 差異引入第三方套件；先以現有 LLVM-MinGW／Windows SDK headers 與 native APIs 完成。

## Handoff requirements

交接時必須記錄：

- 修改檔案與新增的 native link libraries。
- settings.ini key、default、舊檔行為、是否維持 schema=1。
- ShowPanel 的 hidden→visible 判斷與所有實際入口 trace。
- TSF 成功／fallback IMM32／無 context failure-safe 的實際結果。
- settings_store、settings_editor、input_mode、lifecycle、full CTest 命令與結果。
- Microsoft New Phonetic 與另一個 IME 的手動驗收結果；不支援的 provider 要明確列出。
- 未完成或未能在 Agent 環境驗證的項目，不得寫成已通過。

## 交接區

實作（2026-08-17，NR-190 done，Agent 環境無互動桌面所以未執行人工 IME 矩陣）── 依 item 的「實際 IME provider 的 TSF／IMM32 行為不應硬綁 CI runner；手動驗收依 docs/testing.md 的 smoke matrix」原則，把可驗證邊界與未驗證項目分開記錄。

### 修改／新增檔案與 native link

- 新增 `src/ui/input_mode.{h,cpp}`（`ShouldSetEnglishInputMode`／`SetEnglishInputMode`，併入 `nimblerun_ui` 庫）。
- 修改：`src/settings/settings_store.{h,cpp}`（Settings 欄位＋Load/Save）、`src/settings/settings_editor.{h,cpp}`（`SetEnglishInputOnShow`、`EnglishInputOnShowLabel` string key）、`src/resources/resource.{h,.rc}`（`IDC_ENGLISH_INPUT_ON_SHOW=2032`）、`src/app_host/settings_dialog.cpp`（InitLabels／Populate／WM_COMMAND checkbox 分支）、`src/app_host/main.cpp`（ShowPanel 接線＋live reload）、`CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/unit/settings_store_test.cpp`、`tests/unit/settings_editor_test.cpp`。
- 新增測試 `tests/unit/input_mode_test.cpp`（CTest name `nimblerun_input_mode_test`）。
- 文件：`docs/design-spec.md`（§4.1、§4.9、§FR-013、§10.2）、`docs/testing.md`（English input mode manual verification 六步）、`AGENTS.md`（CTest 32→33）。
- **新增 native link**（`nimblerun_ui` 的 PUBLIC link）：`imm32`（IMM32 fallback）、`ole32`＋`uuid`（`CoCreateInstance(CLSID_TF_ThreadMgr)` 與 `IID_ITfThreadMgr/ITfDocumentMgr/ITfContext/ITfCompartmentMgr/ITfCompartment`、`CLSID_TF_ThreadMgr` 的定義在 `libuuid.a`）。無第三方依賴、無 `msctf` import lib 需求（本 helper 只用 COM vtable 方法）。

### settings.ini

- Key：`english_input_on_show`，值 `true`／`false`；**default false**；Reset settings 回 false。
- 舊檔缺少 key → Load false（新增 `TestEnglishInputOnShowMissingKey`）；非法值 → false（`banana` 測試）。
- Save 一律寫出該行；schema 維持 **1**、無 migration；unknown-key 既有行為不變。

### ShowPanel 接線與入口 trace

- `ShowPanel`（`main.cpp:1951`）開頭、第一次 `SetWindowPos` 前保存 `const bool was_visible = IsWindowVisible(window) != FALSE;`（`:1954`）。
- settings live reload（ShowPanel 內 `:1967-2003`）新增 `g_settings.english_input_on_show = current.english_input_on_show;`（`:2002`）；`kSettingsMessage` 的 Apply 後 `g_settings = reloaded;`（整份複製）已自動涵蓋新欄位 → 不必重啟即可影響下一次顯示。
- `SetFocus(g_search_edit)`（`:2056`）後以 `ShouldSetEnglishInputMode(g_settings.english_input_on_show, was_visible)` gate，為 true 才呼叫 `SetEnglishInputMode(g_search_edit)`（`:2062-2064`）。不在此路徑之外（hide／按鍵／WM_SETFOCUS／timer／worker）呼叫。
- 實際入口 trace（四個入口全部匯入同一個 `ShowPanel`）：
  1. **Hotkey**：`WM_HOTKEY`（`:2689`）→ 不可見時直接 `ShowPanel(window)`。
  2. **Tray 左鍵**：`kTrayCallbackMessage` + `WM_LBUTTONUP` → `PostMessageW(window, g_show_panel_message, 0, 0)`（`:2582`）→ WindowProc `g_show_panel_message` 分支 → `ShowPanel`。
  3. **Tray menu Open**：`DispatchTrayCommand` `kCmdOpen` → `PostMessageW(window, g_show_panel_message, 0, 0)`（`:2129`）→ 同上。
  4. **Second instance wake-up**：`wWinMain`（`:3151`）→ `PostMessageW(existing, g_show_panel_message, 0, 0)` → 同上。
- 已驗證：每個入口都只經過共享 ShowPanel；`was_visible` 在第一次 SetWindowPos 前取值，抵達後續 SetFocus 點時仍代表「顯示前狀態」。

### TSF／IMM32 實際結果（誠實記錄）

- **可驗證（已做）**：LLVM-MinGW 的 `msctf.h` 提供 `ITfThreadMgr/ITfDocumentMgr/ITfContext/ITfCompartmentMgr/ITfCompartment` 與 `CLSID_TF_ThreadMgr`＋各 `IID_*`，且定義在 `libuuid.a`（已以 `llvm-nm` 驗證）；`GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`（`{CCF05DD8-4A87-11D7-A6E2-00065B84435C}`）與 `TF_CONVERSIONMODE_ALPHANUMERIC`（0x80）在 mingw headers **不存在**（MS SDK 放在 msimtf.h／ctffunc.h），已在 `input_mode.cpp` 依 Windows SDK 的 documented 值以 local constexpr 宣告，未引入第三方 header。
- TSF 路徑：`CoCreateInstance(CLSID_TF_ThreadMgr)` → `Activate` → `GetFocus`（無 focused document 即 false，交由 IMM fallback）→ `GetBase` → `QI ITfCompartmentMgr` → `GetCompartment(kGUID)` → `SetValue(client_id, VT_I4=0x80)`；**編譯／連結通過**，但**未對真實 IME 執行互動驗證**（本次 Agent 環境無可控桌面）。COM 由 wWinMain 的既有 `ComGuard`（STA）提供，helper 不自行 CoInitializeEx/CoUninitialize（`main.cpp:3145`）。
- IMM32 fallback：`ImmGetContext` → `ImmSetOpenStatus(TRUE)` → `ImmGetConversionStatus` → 清 `IME_CMODE_NATIVE` → `ImmSetConversionStatus` → `ImmReleaseContext`；**未對真實 IMM IME 執行互動驗證**。
- failure-safe：`nullptr`／無效 HWND（`IsWindow` 為 FALSE）在呼叫任何 TSF/IMM/COM 之前就回傳 false——此路徑**已由 `nimblerun_input_mode_test` 以 Expect 驗證**（Release build，非 assert）；無 context／native 失敗一律回傳 false、不拋例外、不阻擋面板、不改 settings。
- **未驗證（未寫成已通過）**：Microsoft New Phonetic 與另一個 IME 的中文模式→英文切換、candidate-mode 逆向、visible 期間重複 show 的真實 IME 行為、英文系統無 active context 的實機行為。這些是 `docs/testing.md` 登錄的手動 matrix（off／on／hidden→visible／repeated show／雙 IME／無 context 六項），須在 release gate 的桌面環境完成，屬完成 item 前的一項人工作業。

### 建置與 CTest 證據

LLVM-MinGW + Ninja Release x64（`cmake/llvm-mingw.cmake`），沿用既有 `build` 目錄：

- `cmake --build build` → exit 0，無 warning（`input_mode.cpp` 單獨重編亦無 warning）。
- `ctest --test-dir build -R "settings|input_mode|lifecycle" --output-on-failure` → **4/4 Passed**（`nimblerun_settings_test`、`nimblerun_settings_ui_test`、`nimblerun_input_mode_test`、`nimblerun_lifecycle_check`）。
- `ctest --test-dir build --output-on-failure` → **33/33 Passed**（33.16s）。
- CTest 註冊數由 32 增至 **33**（新增 `nimblerun_input_mode_test`，排在 `nimblerun_pin_drag_state_test` 之後、`lifecycle` 之前，因 tests/CMakeLists.txt 的 TAIL sublist 由 `10 17` 調成 `10 18`）。`docs/release-evidence.md` 由 `tests/release/release_evidence.ps1` 重新產生（live count），未手改。

### 決策與偏差

- `settings_store.h` 的 `Settings` struct 直接加 `english_input_on_show = false`（與 `hide_after_launch`/`include_windows_apps` 同型），沒有新增第二套設定抽象。
- `.rc` 的 LAUNCHER group 加入 checkbox（232,84,190,12），Theme label/combo 下移 8px（100/98）；維持 440x338 與同 group 既有控制項可讀；`IDC_ENGLISH_INPUT_ON_SHOW=2032` 是資源檔下一個未用 ID。
- IMM32 以「清 `IME_CMODE_NATIVE`」切到英數（`ImmSetConversionStatus`、保持 open status），這是 standard IMM 切「英文／英數」的實作位元；`TF_CONVERSIONMODE_ALPHANUMERIC` 直接用於 TSF compartment 值。切換不碰 keyboard layout（無 LoadKeyboardLayout/ActivateKeyboardLayout）。
- `ShouldSetEnglishInputMode` 是純轉移 predicate（enabled && !was_visible），放在 `ui/input_mode.h` 維持可測。
- 未做：tray menu／hotkey 各自複製輸入處理（共用 ShowPanel）；未在 hide／按鍵／WM_SETFOCUS／timer／worker 呼叫；未加任何 keyboard hook、背景執行緒或第三方套件。

**Commit（main agent 執行）**：`NR-190: optional English input mode on panel show`，含本文件交接區與 `docs/work-items.md` 狀態 `ready`→`done`。
