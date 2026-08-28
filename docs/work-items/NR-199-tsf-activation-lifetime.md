# NR-199 — 持有 TSF activation 以修復英文輸入切換間歇失效

Phase 5 · Release gate · Depends on: NR-190（done）, NR-198（done）

- Source: 使用者回報「settings 有自動切換 IME to eng 的功能，但有很多次都沒順利切換到 eng」——NR-198 上線後症狀從「第一次一定失效」變成「頻繁但隨機失效」
- Origin: NR-190 導入、NR-198 未能修正的既有缺陷
- **本 item 覆寫 NR-198 的核心機制假設**，見下方 §對 NR-198 的覆寫

## 根因

`ITfThreadMgr::Activate` 是**每執行緒的 reference count**，不是一次性初始化。NR-190／NR-198 的所有路徑都把它與緊接著的 `Deactivate` 配成一對：

- `TryTsf()`：`Activate` → 寫 compartment → `Deactivate`（在同一個運算式內）
- `WarmUpInputMode()`：`Activate` → 立刻 `Deactivate`

當本行程是 UI 執行緒上唯一持有 activation 的人，refcount 歸零會拆掉整條執行緒的 TSF client state，包含：

1. `WarmUpInputMode` 剛裝好的 focus-tracking hook——**NR-198 的修法因此是安慰劑**；
2. `TryTsf` 剛寫進 `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION` 的值。

「有時有效」的條件是「這條執行緒上剛好還有別人撐著 activation」。使用者手動切過一次輸入法之後常常就有——這正是 NR-198 原始回報所描述的觸發條件，當時被誤讀成「hook 只需要裝一次」。

疊加的兩個次要缺陷：

- `SetEnglishInputMode` 用 `if (TryTsf()) return true;` 短路。`ITfCompartment::SetValue` 回 `S_OK` 只代表 compartment 現在存著這個值，**不代表焦點 TIP 觀察到並改了自己的 conversion mode**。把這個 `S_OK` 當成成功，等於讓對微軟注音等 CJK IME 實際有效的 IMM32 路徑永遠不會執行。
- `TryImm` 只清 `IME_CMODE_NATIVE`，保留 `IME_CMODE_FULLSHAPE`，會輸出全形 ａｂｃ——對使用者而言就是「沒切成功」。

## 對 NR-198 的覆寫

NR-198 §Goal 主張「TSF 只在該執行緒**第一次**呼叫 `ITfThreadMgr::Activate` 時才安裝 focus hook」，並據此把修法定為「一次性 `Activate`/`Deactivate`（丟棄結果）」。

該主張的後半段不成立：hook 的生命期綁在 activation refcount 上，`Deactivate` 會把它拆掉。NR-198 的決策 1（「只做 `CoCreateInstance` → `Activate` → `Deactivate`」）與其 §Scope 第 2 點在此被覆寫為「取得一次 activation 並持有到行程結束」。

NR-198 的呼叫點（決策 2）、設定閘門（決策 3）、範圍外的 Apply-toggle 情境（決策 4）與依賴邊界（決策 5）**維持不變**。NR-198 的文件依規則不修改，本節即為歷史軌跡上的覆寫紀錄。

## 已確認的產品決策

1. `src/ui/input_mode.cpp` 新增檔案內部的 `ActivatedThreadMgr()`：首次呼叫時 `CoCreateInstance(CLSID_TF_ThreadMgr)` → `Activate`，成功則把 `ITfThreadMgr*` 與 `TfClientId` 存進 namespace-scope 變數並回傳；之後直接回傳快取值。
2. 該 thread manager **刻意永不 `Release`、永不 `Deactivate`**。這是 UI 執行緒狀態，必須活過任何 static 解構與 `CoUninitialize` 的順序；用 static 智慧指標持有反而會製造解構順序風險。
3. 失敗**不快取**：取得失敗只會在本來就無法運作的機器上多付一次失敗的 `CoCreateInstance`，換來「沒有隱藏的永久毒化狀態」。
4. `TryTsf()` 改用 `ActivatedThreadMgr()`，不再自行 `Activate`/`Deactivate`；`IsThreadFocus` 閘門與 compartment 寫入邏輯不變。
5. `WarmUpInputMode()` 的實作縮為呼叫 `ActivatedThreadMgr()`。對外語意（best-effort、無回傳值、不碰 settings、不阻塞）不變。
6. `SetEnglishInputMode()` 改為兩條路徑都跑：`const bool tsf_ok = TryTsf(); const bool imm_ok = TryImm(edit); return tsf_ok || imm_ok;`。TSF 已成功時 IMM 是 no-op。
7. `TryImm()` 清除的位元從 `~IME_CMODE_NATIVE` 改為 `~(IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE)`。
8. 不改 `ShowPanel` 的呼叫順序、不改 `ShouldSetEnglishInputMode` 的 `was_visible` 閘門、不改 `wWinMain` 的暖機呼叫點與其設定閘門。
9. 不新增設定欄位、不改 settings.ini schema、不引入背景執行緒／timer／第三方依賴。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Use the C++ standard library or Win32 native APIs before adding dependencies.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

`docs/work-items/NR-190-input-english-on-panel-show.md`：

> 設定關閉時不得呼叫輸入模式 API。

> 不加入 low-level keyboard hook、背景執行緒、輪詢或 vendor-private IME API。

## Files to read and trace first

- `src/ui/input_mode.cpp`／`src/ui/input_mode.h`：`TryTsf()`、`TryImm()`、`SetEnglishInputMode()`、`WarmUpInputMode()`、`src/win/com.h` 的 `ComPtr`／`ComGuard`。
- `src/app_host/main.cpp`：`ShowPanel` 的 `SetFocus(g_search_edit)` → `ShouldSetEnglishInputMode` → `SetEnglishInputMode`（約 `:2279-2288`）；`wWinMain` 的暖機呼叫（約 `:3499-3504`）。本 item 兩處都不改。
- `tests/unit/input_mode_test.cpp`：既有四個 case 的 pattern。

## Scope

1. `src/ui/input_mode.cpp`：決策 1–7。
2. `src/ui/input_mode.h`：更新 `SetEnglishInputMode`／`WarmUpInputMode` 的註解契約——前者改述為「兩條路徑都跑，任一成功即回 true」，後者記下 activation 是持有而非拋棄。
3. `tests/unit/input_mode_test.cpp`：擴充 `TestWarmUpInputModeDoesNotCrash`，重複呼叫 `WarmUpInputMode()` 並跨越一次 `SetEnglishInputMode(nullptr)`，覆蓋「取得失敗不快取、不重複 Release」的路徑——該處若有 double-Release 或毒化快取，測試行程會崩潰。

## Non-goals

- 不處理 NR-198 決策 4 的「Apply 從 false 改 true 且尚未重啟」情境。修好 activation 生命期之後，這個洞的成本已降到「在 `ShowPanel` 的 live-reload 之後補一次 `WarmUpInputMode()`」，但仍需另開 item。
- 不處理 `SetForegroundWindow`／`SetFocus` 與 `IsThreadFocus` 之間的時序競態。`TryTsf` 失敗現在必定落到 IMM32，已有實質 fallback。
- 不改 `was_visible` 閘門。「面板已顯示時再按熱鍵不重複切換」是 NR-190 的刻意設計。
- 不對 compartment 寫入做讀回驗證或重試。
- 不加入診斷事件或 log。

## Acceptance

1. `english_input_on_show=false` 時完全不呼叫任何 TSF／IMM API，行為與修改前一致。
2. `SetEnglishInputMode` 對 null／invalid HWND 仍是回 false 的安全 no-op。
3. TSF 路徑成功時 IMM32 路徑仍會執行，且整體回傳 true。
4. Release build 無新增 warning；完整 CTest 33/33 通過。
5. 真機驗收：微軟注音（TSF-only TIP）環境下，連續 8 次「強制設回中文 → Alt+Space 顯示面板 → 讀面板 focus 視窗的 conversion mode → Esc」，每次 `IME_CMODE_NATIVE` 位元皆為 0。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "input_mode|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

注意：`nimblerun_lifecycle_check` 會因為任何殘留的 NimbleRun 行程（包含手動測試或探針留下的改名副本）而失敗，訊息為「Main window is owned by process N」。跑 CTest 前先確認沒有殘留行程。

## 交接區

### 落地結果（2026-08-28）

決策 1–9 全數實作，`cmake --build build` 無新增 warning，`ctest --test-dir build` **33/33 通過**。

### 真機量測 — 跨行程 IME 探針

環境：Windows 11 Pro 26200，zh-Hant-TW，輸入法為微軟注音（TSF-only TIP，`0404:{B115690A-EA02-48D5-A231-E3578D2FDF80}{B2F9C502-1742-11D4-9790-0080C882687E}`），`hotkey=Alt+Space`，`english_input_on_show=true`。

方法：從外部行程用 `ImmGetDefaultIMEWnd(hwnd)` + `SendMessageTimeout(WM_IME_CONTROL, IMC_GETCONVERSIONMODE)` 讀取目標視窗的 conversion mode（`IMC_SETCONVERSIONMODE` 設定）。這是唯一能跨行程讀 IME 狀態的文件化途徑；`ImmGetConversionStatus` 的 `HIMC` 是行程私有的，跨行程無效。每輪：把共用 IME 模式強制設回 `IME_CMODE_NATIVE`(1) → 送 Alt+Space → 用 `GetGUIThreadInfo` 取得面板執行緒的 focus HWND → 讀 conversion mode → 送 Esc。

修正後結果，8 輪全部 `before=1, after=0`：

| n | before | after | english |
|---:|---:|---:|---|
| 1–8 | 1 | 0 | True |

`RESULT NEW : pass=8 fail=0 of 8`

### 未完成的證據 — 下一位請補

**修正前的基準線沒有跑。** 上述 8/8 證明新版在微軟注音上穩定，但**沒有證明這支探針抓得到舊版的失效**——缺少對照組，8/8 不能單獨當作「修好了」的證據。要補：用 `git stash` 隔離 `src/ui/input_mode.*` 後建置一份舊版執行檔，對它跑同一支探針，預期應出現 `after` 帶有 `IME_CMODE_NATIVE` 位元的失敗輪次。探針腳本本身是 session-scoped 的，未入庫；若要重複使用請一併補進 `tests/integration/`。

### 給後續 item 的備註

- 探針方法（`ImmGetDefaultIMEWnd` + `WM_IME_CONTROL`）值得沉澱成 `docs/testing.md` 的手動 smoke matrix 工具，讓 NR-190 系列從「純人工目視」升級成可重複量測。
- `ActivatedThreadMgr()` 只在 UI 執行緒上呼叫。若未來有第二條執行緒需要切輸入模式，那個快取必須改成 thread-local——目前沒有這種呼叫者，也不該有。
