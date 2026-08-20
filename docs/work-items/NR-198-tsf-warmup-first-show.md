# NR-198 — Warm up TSF before the first hidden→visible show

Phase 5 · Release gate · Depends on: NR-190（done）

- Source: 使用者回報「Switch input to English on show 第一次執行／第一次按熱鍵沒作用，之後（含手動切換過一次輸入法之後）才會起作用」，經追蹤程式碼確認為 TSF 一次性初始化時機問題
- Origin: NR-190 落地後才浮現的既有邏輯缺陷（timing bug），非新功能
- Priority: **HIGH**——這是設定開啟後使用者第一個動作就會撞到的失效，且無任何錯誤提示，使用者只會覺得「這個設定沒用」

## Goal

`SetEnglishInputMode`（`src/ui/input_mode.cpp`）在 UI 執行緒上第一次真正生效之前必須先失敗一次，因為 TSF（Text Services Framework）只在該執行緒**第一次**呼叫 `ITfThreadMgr::Activate` 時才安裝它用來追蹤 focus 的 hook。目前程式碼裡，第一次 `Activate` 剛好發生在 `ShowPanel`（`src/app_host/main.cpp`）呼叫 `SetFocus(g_search_edit)` **之後**——也就是這次 focus 切換已經錯過了 hook 安裝的時間點，於是 `IsThreadFocus()` 讀到舊狀態、`TryTsf()` 回傳 false，且該次 IMM32 fallback 同樣落在「這條執行緒第一次真正處理輸入焦點」的冷啟動窗口內一併落空。

修法：在啟動階段、任何真正的 `SetFocus` 之前，先在 UI 執行緒上做一次一次性的 TSF `Activate`/`Deactivate`（丟棄結果），讓 hook 提前裝好。之後（包含使用者手動切換過一次語言的情境）就不再需要靠巧合暖機。

## 已確認的產品決策

1. 新增 `nimblerun::WarmUpInputMode()`（`src/ui/input_mode.h`／`.cpp`），簽章 `void WarmUpInputMode()`：只做 `CoCreateInstance(CLSID_TF_ThreadMgr)` → `Activate` → `Deactivate`，任何一步失敗就靜默返回，不拋例外、不回傳值、不碰 settings。
2. 呼叫點在 `wWinMain`，緊接在 `g_settings = settings;` 之後（settings 已載入、COM 已由既有 `ComGuard` 初始化、早於任何視窗建立與訊息迴圈）。
3. **只有 `g_settings.english_input_on_show == true` 才呼叫**，維持 NR-190 既有的「設定關閉時不得呼叫輸入模式 API」約束；不新增獨立的「TSF 是否已暖機」設定或旗標。
4. `SettingsDialog` Apply 把此設定從 false 改成 true 且尚未重啟的情境**不在本 item 範圍**：`ShowPanel` 既有的 live-reload 早於下一次 `SetFocus`，且下一次熱鍵/tray 顯示本來就會呼叫 `TryTsf()`（該次仍可能因為尚未暖機而落空一次，但這是既有的「Apply 後下一次顯示才生效」語意的邊緣情況，不是本 item 要修的「應用程式冷啟動後第一次」問題，且沒有既有機制可以在 Apply 當下重跑 wWinMain 一次性初始化）。若要修，需要另開 item。
5. 不引入背景執行緒、不引入 timer、不呼叫 `LoadKeyboardLayout`/`ActivateKeyboardLayout`、不新增第三方依賴——沿用 NR-190 已用的 `msctf.h`/`ComPtr`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

`docs/work-items/NR-190-input-english-on-panel-show.md` 的既有產品決策：

> 設定關閉時不得呼叫輸入模式 API。

> 不加入 low-level keyboard hook、背景執行緒、輪詢或 vendor-private IME API。

## Files to read and trace first

- `src/ui/input_mode.h`／`src/ui/input_mode.cpp`：`TryTsf()`（`CoCreateInstance`→`Activate`→`IsThreadFocus`→...→`Deactivate`）、`SetEnglishInputMode()`、既有 `ComPtr`（`src/win/com.h`）RAII 用法。
- `src/app_host/main.cpp` 的 `ShowPanel`（約 `:1962-2075`）：`SetFocus(g_search_edit)` 與其後的 `ShouldSetEnglishInputMode`/`SetEnglishInputMode` 呼叫順序——本 item 不改這裡的順序或邏輯，只確保 TSF 在此之前已暖機過。
- `src/app_host/main.cpp` 的 `wWinMain`：`ComGuard` 建立處（約 `:3179-3183`）、settings 載入與 `g_settings = settings;`（約 `:3238-3248`）、後續視窗建立（`:3390` 附近的 `g_search_edit`）與訊息迴圈（`:3475`）——確認新呼叫點在 COM 已可用、且早於任何真正的 `SetFocus`。
- `tests/unit/input_mode_test.cpp`：既有 `TestSetEnglishInputModeFailureSafe` 的 pattern，新增測試沿用同一沒有 CoInitialize 的環境。

## Scope

1. `src/ui/input_mode.h`：新增 `void WarmUpInputMode();` 宣告。
2. `src/ui/input_mode.cpp`：實作 `WarmUpInputMode()`，重用既有 `ComPtr<ITfThreadMgr>`；失敗一律靜默返回。
3. `src/app_host/main.cpp`：在 `wWinMain` 的 `g_settings = settings;` 之後，`if (g_settings.english_input_on_show) { nimblerun::WarmUpInputMode(); }`。
4. `tests/unit/input_mode_test.cpp`：新增 `TestWarmUpInputModeDoesNotCrash`，呼叫 `WarmUpInputMode()` 一次，確認在沒有 `CoInitializeEx` 的測試行程中仍是安全的 no-op（不崩潰、不掛起）。

## Non-goals

- 不改變 `ShowPanel`、`ShouldSetEnglishInputMode`、`TryTsf`、`TryImm` 的既有邏輯或呼叫順序。
- 不處理「Settings dialog Apply 把設定從 false 改成 true 之後、下一次顯示前」這個更窄的邊緣情境（見上方決策 4）。
- 不新增設定欄位、不新增 persistence key、不改變 settings.ini schema。
- 不對真實 IME 做互動驗證的自動化（沿用 NR-190 的手動 smoke matrix 邊界）；本 item 只修「暖機時機」，不重新驗證 TSF/IMM32 provider 相容性。
- 不加入任何背景執行緒、輪詢或 timer。

## Acceptance

1. `english_input_on_show=false`（預設）時，`wWinMain` 完全不呼叫 `WarmUpInputMode()`／任何 TSF API，行為與修改前一致。
2. `english_input_on_show=true` 時，`WarmUpInputMode()` 在任何真正的 `SetFocus(g_search_edit)` 之前於 UI 執行緒上執行一次。
3. `nimblerun_input_mode_test` 通過，涵蓋 `WarmUpInputMode()` 在無 COM 初始化環境下的安全 no-op。
4. Release build 無新增 warning；完整 CTest 通過（34/34，因新增一個 assertion 而非新 CTest target）。
5. 手動驗收（不在 Agent 範圍，記錄於交接區）：全新啟動程式、開啟設定、Apply 前手動重啟一次、按下熱鍵——第一次即應直接呈現英文／英數輸入，無需先手動切換一次語言。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "input_mode|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

## Handoff requirements

交接時記錄：

- `WarmUpInputMode()` 的定義位置與呼叫點（含行號）。
- 呼叫點是否確實在 settings 已載入、COM 已初始化、且早於第一次 `SetFocus` 之後。
- Agent checks 的完整命令與結果。
- 手動驗收（全新啟動＋設定開啟＋第一次熱鍵）的結果，若無法在 Agent 環境執行需誠實記錄為未驗證。

## 交接區

實作（2026-08-20）── Agent 環境無互動桌面，手動驗收未執行，未寫成已通過。

### 修改檔案

- `src/ui/input_mode.h`：新增 `void WarmUpInputMode();` 宣告。
- `src/ui/input_mode.cpp`：新增 `WarmUpInputMode()` 實作，重用既有 `ComPtr<ITfThreadMgr>`；`CoCreateInstance`/`Activate` 任一失敗即靜默返回，`Activate` 成功必配對 `Deactivate`。
- `src/app_host/main.cpp`：`wWinMain` 的 `g_settings = settings;` 之後（`:3246` 後）新增：
  ```cpp
  if (g_settings.english_input_on_show) {
      nimblerun::WarmUpInputMode();
  }
  ```
  此處早於 `RegisterMainWindow`／`CreateWindowExW`／`g_search_edit` 建立與訊息迴圈，且晚於 `ComGuard` 初始化與 settings 載入。
- `tests/unit/input_mode_test.cpp`：新增 `TestWarmUpInputModeDoesNotCrash`，在未呼叫 `CoInitializeEx` 的測試行程中呼叫一次 `WarmUpInputMode()`，確認安全 no-op。

### 呼叫點驗證

- 確認 `g_settings.english_input_on_show` 在此處已是 `SettingsStore::Load` 的結果（`:3242-3246`），不是 `DefaultSettings()` 的殘留值。
- 確認此呼叫點與 `ShowPanel`（`main.cpp` 內 `SetFocus(g_search_edit)` 前後的既有邏輯）之間沒有任何其他 `SetFocus` 呼叫路徑：`g_search_edit` 本身要到 `:3390` 附近才建立，因此不可能有更早的真實 focus 事件搶在 `WarmUpInputMode()` 之前發生。
- 未新增第二個呼叫點；`SettingsDialog` Apply 把設定從 false 改成 true 且未重啟的情境維持原本「下一次顯示才生效」語意，不受本 item 影響（見文件決策 4，留給後續 item 視需要處理）。

### Agent checks

- `cmake --build build` → 第一次因 `build/NimbleRun.exe` 正在執行（PID 被使用者手動啟動測試）鎖住，link 失敗（`Permission denied`）；經使用者確認後 `taskkill` 結束該行程，重新 `cmake --build build` → exit 0，無新增 warning。
- `ctest --test-dir build -R "input_mode|lifecycle" --output-on-failure` → **2/2 Passed**（`nimblerun_input_mode_test`、`nimblerun_lifecycle_check`）。
- `ctest --test-dir build --output-on-failure` → **33/33 Passed**（39.46s）；CTest 註冊數維持 33（本 item 沒有新增 CTest target，只在既有 `nimblerun_input_mode_test` 加一條 assertion）。

### 未完成 / 未驗證

- 手動驗收（全新啟動＋設定開啟＋第一次熱鍵直接得到英文輸入）**未在本次 Agent 環境執行**（無互動桌面、無可控 IME），依 item 定義列為未驗證，不寫成已通過。需要有桌面環境的人工驗收才能關閉此 item 的第 5 條 Acceptance。
- `docs/design-spec.md`、`docs/testing.md` 未因本 item 修改：本 item 修的是既有已記錄行為的一個時序 bug，不改變任何已文件化的使用者可見規格或驗收步驟。
