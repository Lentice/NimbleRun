# NR-117 — Message loop 必須正確處理 GetMessageW error，不得 dispatch 未定義 MSG

Phase 3 · UI lifecycle · Depends on: NR-115

- Source: `docs/design-spec.md` §NFR-002、§NFR-003、§11
- Origin: 2026-08-09 第十一次 post-implementation audit；檢查 NR-115 新增的 `MsgWaitForMultipleObjectsEx`／`GetMessageW` loop
- Priority: IMPORTANT（message retrieval error 會被誤當成正常 message，可能把未定義 `MSG` 送進 Win32 dispatch）

## Why

NR-115 把原本的

```cpp
while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
}
```

改成 `src/app_host/main.cpp:3807-3811` 的：

```cpp
if (!GetMessageW(&message, nullptr, 0, 0)) {
    break;
}
TranslateMessage(&message);
DispatchMessageW(&message);
```

Win32 `GetMessageW` 有三種結果：正值代表取得 message、`0` 代表 `WM_QUIT`、`-1` 代表取得
message 失敗。C++ 中 `!(-1)` 為 false，所以目前的分支只處理 `0`，遇到 `-1` 會繼續使用
未定義／無效的 `MSG` 呼叫 `TranslateMessage` 與 `DispatchMessageW`。這是 message loop 核心
生命週期的新回歸，可能造成錯誤 dispatch、錯誤 HWND 存取或 process 不穩定；它與 NR-115 的
wake-up event 修正是不同責任。

## Override / decisions already made — do not reopen

1. 本 item 擴充 NR-115 的 message-loop implementation，不重開其 manual-reset event、可靠 wake-up、
   UI-owned drain、token registry、WM_DESTROY 或 no-timer 決策；completed item 文件不得修改。
2. `GetMessageW` 回傳 `-1` 與 `0` 都不得進入 `TranslateMessage`／`DispatchMessageW`；只有正值才 dispatch。
3. 採最小 branch 修正，避免新增 message-loop abstraction、timer、worker 或第二套 event path。
4. 若需要 test seam，僅抽出最小的 return-value decision（或同等純值 self-check）；不複製整個 Win32 message loop，
   不用假測試宣稱已覆蓋 OS queue failure。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-002：

> 主執行緒阻塞於 `GetMessage`／`MsgWaitForMultipleObjectsEx`，沒有工作時不使用 busy loop。

`docs/design-spec.md` §NFR-003：

> 任一損壞捷徑不得造成崩潰或卡住整體掃描。

`docs/design-spec.md` §11：

> Worker 發生例外：捕捉邊界、記錄並丟棄該次結果；UI 不崩潰。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — NR-115 的 `g_rebuild_failure_event`、`MsgWaitForMultipleObjectsEx`、
  `GetMessageW`、`TranslateMessage`、`DispatchMessageW`、`WM_DESTROY` 與 exit code。
- `tests/integration/lifecycle_check.ps1` — 真實 exe 的 message-loop／shutdown smoke。
- `tests/unit/catalog_refresh_test.cpp` — 不要把此 UI return-value 修正誤塞進 coordinator。
- `docs/work-items/NR-115-rebuild-failure-wakeup-reliability.md` — 保留已完成的 event-driven wake-up scope，
  不編輯其文件。

## Scope

1. 修正 `GetMessageW` return-value 分流：`-1` 與 `0` 都安全離開 loop／走明確錯誤或 shutdown 路徑，
   正值才翻譯與 dispatch；不得讀取未成功填入的 `MSG`。
2. 保留 NR-115 的 event wait、manual-reset reset race handling、`WM_QUIT` shutdown、worker join、
   event close 與既有 return code semantics；只在必要處補 deterministic error handling。
3. 新增一個 focused runnable check，證明 `-1`、`0`、正值三種結果的 dispatch decision 正確；若無安全
   OS injection，使用最小純值 seam／self-check，並以 lifecycle check 覆蓋真實 message loop 不回歸。

## Non-goals

- 不改 `MsgWaitForMultipleObjectsEx` wait handle、rebuild failure queue、generation completion 或 catalog snapshot。
- 不新增 polling、timer、thread、message retry、third-party dependency 或 UI 功能。
- 不把 `GetMessageW` error 當成 `WM_QUIT` 以外的新產品狀態；只確保不 dispatch 無效資料並留下可診斷的安全結果。

## Acceptance

1. `GetMessageW == -1` 時不會呼叫 `TranslateMessage`／`DispatchMessageW`；`GetMessageW == 0` 維持
   `WM_QUIT` shutdown；只有 `> 0` 才 dispatch。
2. NR-115 failure-event path、正常 message dispatch、WM_DESTROY／worker join 與 lifecycle check 不回歸。
3. Focused return-value test/self-check、Release build、完整 CTest 通過且無新增 warning。
4. Diff 僅包含 message-loop/error handling 與 focused test（必要的 item Handoff／tracker status），不帶入相鄰票券變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "lifecycle|catalog_refresh" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -C 8 "GetMessageW|TranslateMessage|DispatchMessageW|MsgWaitForMultipleObjectsEx|WM_QUIT" src/app_host/main.cpp tests
git diff --name-only
# expect: main.cpp 與最小 focused test；不改 catalog、icon、launch 或 NR-115 completed item 文件。
```

## Handoff

實作者需記錄三種 `GetMessageW` return value 的測試／decision、error path 的 exit／diagnostic 行為、
event wait 與 WM_QUIT lifecycle、Release build／CTest／lifecycle 結果，以及未能安全注入的 OS-only queue error path。
