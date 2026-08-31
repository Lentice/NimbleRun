# NR-200 — hidden→visible 顯示前完成 TSF 暖機

Phase 5 · Release gate · Depends on: NR-190（done）, NR-199（done）

## 目的

修正使用者在 NimbleRun 執行中把 english_input_on_show 從 false 套用成
true 後，下一次 hidden→visible 顯示可能錯過 TSF focus-tracking activation
的時序缺口。ShowPanel 目前在 SetWindowPos(...SWP_SHOWWINDOW) 與
SetForegroundWindow 之後才重新讀 settings；若此路徑先讓視窗／子控制項取得
focus，之後才暖機就太晚。

本 item 覆寫 NR-199 的 non-goal：

> 不處理 NR-198 決策 4 的「Apply 從 false 改 true 且尚未重啟」情境。

新的證據是：NR-199 已將 TSF activation 改為 UI thread 的程序生命期持有狀態，
而 ShowPanel 的 live settings reload 仍發生在 show/activation 呼叫之後；因此
可用既有 WarmUpInputMode()，只調整 reload、決策與 focus 的順序，不增加新的
TSF implementation。

## Binding constraints

AGENTS.md：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

docs/development.md：

> ui | HWND, focus, input, DPI, rendering | Folder scanning

> Core value types should remain copyable and testable without HWND or Shell COM ownership.

> Add or update one focused test for non-trivial logic.

docs/design-spec.md §4.9：

> 啟用「Switch input to English on show」時，面板 hidden→visible 且搜尋框取得焦點後，以原生 TSF … 嘗試切換目前 IME 為英文／英數模式；TSF 不可用時回退 IMM32 … 切換為 best-effort。

docs/design-spec.md §9.2：

> UI thread：Win32 message loop、HWND、Direct2D、輸入、Catalog snapshot 交換。

## 先讀與追蹤

- docs/design-spec.md §4.9、§9.1–9.2、§13 AC-001。
- docs/development.md 的 Architecture rules 與 Change workflow。
- src/app_host/main.cpp：ShowPanel；wWinMain 的既有暖機呼叫；所有 ShowPanel callers。
- src/ui/input_mode.h、src/ui/input_mode.cpp：ShouldSetEnglishInputMode、
  WarmUpInputMode、SetEnglishInputMode。
- tests/unit/input_mode_test.cpp：現有 transition predicate 與 failure-safe checks。
- docs/testing.md §English input mode manual verification。
- docs/work-items/NR-199-tsf-activation-lifetime.md：不可改動的歷史決策與
  activation thread-affinity 備註。

## Scope

1. 在 ShowPanel 通過 monitor 查詢後、任何 SWP_SHOWWINDOW 或
   SetForegroundWindow 前重新讀取 live settings，保留目前的 theme、
   hide-after-launch、recent-count 與 english_input_on_show 投影行為。
2. 以目前 was_visible 與最新 english_input_on_show 計算一次
   ShouldSetEnglishInputMode 結果；該區域只在 UI thread 執行，不重新讀取
   g_settings，避免 nested window message 造成暖機與套用決策分歧。
3. 若該結果為 true，先呼叫既有 WarmUpInputMode()，再顯示／啟用視窗，最後
   SetFocus(g_search_edit) 後呼叫既有 SetEnglishInputMode。
4. 沿用既有 hidden→visible 閘門：面板已可見的 re-show 不切換輸入模式；設定
   關閉或 search EDIT 不存在時不呼叫輸入模式操作。
5. 更新 docs/testing.md，加入「不重啟，Apply false→true，再 hide→show」
   的手動驗證步驟。
6. 保留現有 tests/unit/input_mode_test.cpp 的 focused checks；若為順序決策
   增加可測的純 predicate，必須先證明現有 ShouldSetEnglishInputMode 不足，
   不得為測試製造只包一行的 wrapper。

## Non-goals

- 不重寫 input_mode 的 TSF／IMM32 implementation；NR-199 的 activation
  lifetime、雙路徑 fallback 與 FULLSHAPE 修正維持不變。
- 不新增 thread、timer、低階鍵盤 hook、vendor-private IME call 或 settings
  schema 欄位。
- 不把所有 main.cpp 狀態集中成 PanelHost，不展開整個 launcher window
  module。
- 不讓另一條 thread 呼叫 ActivatedThreadMgr()；既有快取仍限定 UI thread。
- 不修改 NR-199 文件；本 item 是覆寫的歷史記錄。
- 不保證非標準第三方 IME 一定接受切換；失敗仍是安全 no-op。

## Acceptance

1. english_input_on_show=false 時，ShowPanel 不呼叫 WarmUpInputMode、TSF 或
   IMM32；既有 startup gate 仍成立。
2. live settings 從 false 改為 true 且不重啟後，下一次真正
   hidden→visible show 在任何可能取得 focus 的 show/activation 呼叫前完成
   WarmUpInputMode()，再於 SetFocus 後嘗試 SetEnglishInputMode()。
3. 面板已可見時再次收到 show request，不重複套用英文輸入模式。
4. transition predicate、null/invalid HWND failure-safe checks 通過；不新增
   compiler warning。
5. Release x64 build 通過 focused input/lifecycle checks 與完整 CTest。
6. docs/testing.md 的 live-toggle 手動步驟在至少一個 TSF CJK IME 與一個
   IMM32 IME 完成；應確認每輪只影響 NimbleRun search EDIT，不改 keyboard
   layout 或其他程式。

## Agent checks

~~~powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "input_mode|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
~~~

若要做 live-toggle 手動檢查，先確認沒有殘留 NimbleRun 行程，並依
docs/testing.md 的 English input mode matrix 執行；不要以 Debug build 判定
release gate。

## 交接區

### 落地結果

2026-08-31：

- `ShowPanel` 在任何 `SWP_SHOWWINDOW`／`SetForegroundWindow` 前重新讀取 live
  settings，並以 UI thread 內的一次性 transition 決策控制暖機與套用。
- hidden→visible 且設定啟用時，先呼叫 `WarmUpInputMode()`，再顯示／啟用視窗，
  `SetFocus` 後才呼叫 `SetEnglishInputMode()`；可見面板 re-show 與關閉設定仍
  不呼叫輸入模式操作。
- Release configure/build 成功；`ctest --test-dir build -R
  "input_mode|lifecycle" --output-on-failure` 2/2 通過；完整 CTest 33/33
  通過（`nimblerun_startup_option_test` 依環境 skipped）。
- `git diff --check` 無輸出。真機 TSF／IMM32 live-toggle 手動矩陣尚未在本次
  session 執行，需依 `docs/testing.md` 完成。
