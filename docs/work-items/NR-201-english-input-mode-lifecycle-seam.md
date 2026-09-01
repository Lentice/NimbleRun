# NR-201 — English input mode lifecycle seam

Phase 5 · Release gate · Depends on: NR-200

- Source: `docs/design-spec.md` §4.9、§FR-013、§NFR-006、§9.2；`docs/development.md` 的
  `ui` module boundary；`CONTEXT.md` 的 English input mode 定義
- Origin: 2026-09-01 architecture review；近期 b043ec1、31c1e13、437e4a1、7ac560f
  連續修正 input-mode scope、首次顯示暖機、TSF activation lifetime 與 live-toggle readiness
- Priority: **MEDIUM**——NR-198～NR-200 已修好已知行為；本 item 收回仍由 caller 持有的
  lifecycle ordering，避免下一次修補再次散落在 `main.cpp`

## Goal

深化既有 `src/ui/input_mode` module，讓它成為 English input mode 顯示轉換的單一
test surface。現在 `ShowPanel` 必須自己知道三件事的順序：hidden→visible predicate、
顯示／啟用前的 TSF warm-up、`SetFocus` 後的 native mode apply。這些知識跨過 seam，且
最近五次修補都落在這個順序或其生命期上。

本 item 只改 module 形狀與 caller 契約；使用者看得到的切換規則維持不變：設定開啟且
真正 hidden→visible 時，搜尋框取得 focus 後 best-effort 切換為英文／英數模式。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant design-spec section and trace existing callers before changing shared code.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Use the C++ standard library or Win32 native APIs before adding dependencies.

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

> Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.

`docs/development.md`：

> `ui` | HWND, focus, input, DPI, rendering | Folder scanning

> Core value types should remain copyable and testable without HWND or Shell COM ownership.

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`docs/design-spec.md` §4.9：

> 啟用「Switch input to English on show」時，面板 hidden→visible 且搜尋框取得焦點後，
> 以原生 TSF … 嘗試切換目前 IME 為英文／英數模式；TSF 不可用時回退 IMM32 …
> 切換為 best-effort。

`docs/work-items/NR-190-input-english-on-panel-show.md`：

> 設定關閉時不得呼叫輸入模式 API。

> 不加入 low-level keyboard hook、背景執行緒、輪詢或 vendor-private IME API。

## Files to read and trace first

- `CONTEXT.md`：English input mode 的 domain 定義。
- `docs/design-spec.md` §4.9、§FR-013、§NFR-006、§9.2。
- `docs/development.md` 的 Architecture rules 與 Change workflow。
- `docs/work-items.md` 的使用方式、Agent 交付規則與「已否決的方向」。
- `docs/work-items/NR-190-input-english-on-panel-show.md`：原始產品決策與手動矩陣。
- `docs/work-items/NR-198-tsf-warmup-first-show.md`、`NR-199-tsf-activation-lifetime.md`、
  `NR-200-input-mode-show-transition.md`：不可改寫的歷史決策與已完成修補。
- `src/ui/input_mode.h`、`src/ui/input_mode.cpp`：現有 TSF／IMM32 implementation、
  activation lifetime 與 failure-safe guard。
- `src/app_host/main.cpp`：`ShowPanel` 的 `was_visible`、settings live reload、
  `SetWindowPos`／`SetForegroundWindow`、`SetFocus(g_search_edit)` 與 input-mode calls；
  `wWinMain` 的 startup warm-up；所有 `WarmUpInputMode`、`ShouldSetEnglishInputMode`、
  `SetEnglishInputMode` caller。
- `tests/unit/input_mode_test.cpp`：現有 predicate、invalid HWND 與 no-COM checks。
- `CMakeLists.txt`、`tests/CMakeLists.txt`：既有 `nimblerun_ui` 與 input-mode test target。

## Scope

### 1. 收斂顯示轉換契約

在既有 `ui/input_mode` module 內加入一個最小、可消費一次的 transition value。實作
可採既有專案的普通 copyable value pattern；預期 public shape 為：

```cpp
class EnglishInputModeTransition {
public:
    static EnglishInputModeTransition Prepare(bool enabled, bool was_visible);
    bool WillApply() const noexcept;
    bool Apply(HWND edit);  // consumes the transition; inactive/repeated calls are false
};
```

具體命名可依現有 style 微調，但不得讓 host 再自行串接 predicate、warm-up、native
apply 三個獨立操作。`Prepare` 只在 `enabled && !was_visible` 時取得／維持既有 TSF
activation readiness；`Apply` 只在 focus 已完成後執行既有 TSF→IMM32 best-effort path，
且同一 transition 最多消費一次。`WillApply` 只表達純值狀態，供 focused test 使用。

保留既有 `ActivatedThreadMgr`、TSF compartment、IMM32 fallback、FULLSHAPE 清除、
null／invalid HWND guard 與 UI-thread affinity；不要另造 IME adapter、vendor API 或
第二套 COM lifetime。startup 的既有 warm-up gate 只有在證明 transition 已完全涵蓋它
之後才可移除；若保留，必須維持設定關閉時零 TSF／IMM 呼叫。

### 2. 改寫 ShowPanel caller

- 在 `ShowPanel` 取 `was_visible` 並完成 live settings reload 後，建立一個 transition；
  建立點必須早於任何 `SetWindowPos(...SWP_SHOWWINDOW)`、`SetForegroundWindow` 或可能
  觸發 focus 的呼叫。
- `SetFocus(g_search_edit)` 成功後只呼叫 transition 的 `Apply(g_search_edit)`；host 不再
  直接呼叫 `ShouldSetEnglishInputMode` 或 `SetEnglishInputMode`。
- 保持 hotkey、tray click、tray menu Open、second-instance wake-up 共用同一 `ShowPanel`。
- 不改 `was_visible` 閘門、live settings reload、empty-query reset、catalog refresh、
  icon prewarm、hover／tooltip／drag reset 或 hide-after-launch。

### 3. focused test

更新 `tests/unit/input_mode_test.cpp`，透過 transition interface 覆蓋：

- disabled、already-visible 與 hidden→visible 的 `WillApply` truth table；
- hidden→visible transition 在沒有 COM 初始化時仍安全建立；
- null／invalid HWND 的 `Apply` failure-safe；
- 一次 `Apply` 後 transition 不會再次執行（以 `WillApply`／回傳值驗證）；
- 既有 TSF conversion constant、重複 warm-up 不崩潰的 checks 保持有效。

測試不得依賴真實 IME provider；真機 TSF／IMM32 matrix 仍依 `docs/testing.md` 手動驗收。

## Non-goals

- 不改 English input mode 的產品行為、settings.ini schema、UI label 或 design-spec 規則。
- 不改 TSF／IMM32 conversion semantics、activation lifetime、thread affinity 或 provider
  相容性範圍；不得重新引入 NR-198／NR-199 已否決的 immediate Deactivate。
- 不把 `main.cpp` 全域收成 `PanelHost`；該方向已在 `docs/work-items.md` 明確否決。
- 不建立泛用 IME abstraction、第三方 dependency、low-level keyboard hook、background
  thread、polling 或 timer。
- 不把 Settings、HWND、Shell COM pointer 放進可持久化或 catalog value type。
- 不重做 `PanelAccessibilityProvider`、cell tooltip、panel renderer、catalog rebuild 或
  snapshot assembler。
- 不自動恢復使用者原本的中文模式，不切換 Windows keyboard layout／input locale。

## Acceptance

1. `main.cpp` 不再直接呼叫 `ShouldSetEnglishInputMode` 或 `SetEnglishInputMode`；
   ShowPanel 只建立 transition 並在 `SetFocus(g_search_edit)` 後消費它。
2. Transition 建立點早於任何 show／activation call；`Apply` 僅能成功消費一次，且在
   disabled、already-visible、null／invalid HWND 或 native failure 時安全 no-op。
3. `english_input_on_show=false` 時，startup 與 ShowPanel 都不呼叫 TSF／IMM API；
   `true` 且 hidden→visible 時，warm-up 仍早於 show，native apply 仍晚於 search EDIT focus。
4. Hotkey、tray click、tray menu Open 與 second-instance wake-up 的行為維持一致；面板
   已可見時不重複切換，設定 live-toggle 的下一次 hidden→visible 仍有效。
5. `nimblerun_input_mode_test` 以非 `assert` 的 focused checks 通過，涵蓋上述 transition
   狀態與 failure-safe cases。
6. Release build 無新增 warning；`ctest --test-dir build --output-on-failure` 全部通過。
7. 手動 English input mode matrix 的 provider 結果不在 Agent 自動測試中臆測；無桌面／IME
   時於交接區明確記為未驗證。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```powershell
ctest --test-dir build -R "input_mode|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ShouldSetEnglishInputMode|SetEnglishInputMode" src/app_host/main.cpp
# expect: zero hits; input_mode.cpp remains the only native implementation.
rg -n "WarmUpInputMode" src/app_host/main.cpp
# expect: only the intentional startup gate, unless the implementation proves the transition
# fully replaces it and records that removal in the handoff.
```

## Handoff requirements

Record:

- final transition name and public shape, with the prepare/apply call-site ordering;
- whether startup warm-up remains, and why its setting gate is still correct if it does;
- proof that repeated Apply cannot re-run native input operations;
- updated focused test cases and their actual result;
- Release build, focused CTest, full CTest and `git diff --check` output;
- manual TSF／IMM32 provider matrix result, or an explicit unverified note.

## 交接區

### 落地結果（2026-09-01）

- 新增 `nimblerun::EnglishInputModeTransition`：`Prepare(bool enabled,
  bool was_visible)` 捕捉 hidden→visible 並在任何 show／activation call 前沿用既有
  `WarmUpInputMode()`；`WillApply()` 回報純值狀態；`Apply(HWND edit)` 在 focus 後消費
  transition 並呼叫既有 TSF→IMM32 best-effort path。
- `ShowPanel` 在 live settings reload 後建立 transition，早於所有
  `SetWindowPos`／`SetForegroundWindow`；`SetFocus(g_search_edit)` 後只呼叫
  `input_mode_transition.Apply(g_search_edit)`。Hotkey、tray、Open 與 second-instance
  wake-up 仍共用 `ShowPanel`。
- 移除 `wWinMain` 的重複 startup warm-up：transition 已涵蓋第一次真正顯示前的暖機，
  且設定關閉時 `Prepare` 不進入任何 TSF／IMM path。`WarmUpInputMode` 的既有持有式
  activation lifetime 未改動。
- `Apply` 先將 transition 標為已消費，再進入 native path；因此 null／invalid HWND、
  native failure 與重複呼叫都不會再次執行輸入模式操作。

### 驗證證據

- `cmake -S . -B build-nr201 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：通過。
- `cmake --build build-nr201`：通過，118/118；無新增 warning。
- 原 `build` 的同一 Release build 在最後覆寫 `build/NimbleRun.exe` 時因既有
  `build/NimbleRun.exe` 行程持有檔案而被 Windows 拒絕；已用未鎖定的 `build-nr201`
  完成相同 Release build，未終止外部行程。
- `ctest --test-dir build-nr201 -R "input_mode|lifecycle" --output-on-failure`：
  `nimblerun_input_mode_test` 通過；`nimblerun_lifecycle_check` 因既有外部 NimbleRun
  行程而無法取得測試視窗，失敗於等待 `first instance main window`。
- `ctest --test-dir build-nr201 --output-on-failure`：32 通過、
  `nimblerun_startup_option_test` 依環境跳過、`nimblerun_lifecycle_check` 同上環境性失敗。
- `rg -n "ShouldSetEnglishInputMode|SetEnglishInputMode" src/app_host/main.cpp`：零命中。
- `rg -n "WarmUpInputMode" src/app_host/main.cpp`：零命中；暖機責任已由 transition
  seam 完整取代。
- `git diff --check`：無輸出。

### 手動驗證

TSF／IMM32 provider matrix 尚未在本次 Agent session 執行；需依 `docs/testing.md` 的
English input mode matrix，在沒有殘留 NimbleRun 行程的桌面環境補做。
