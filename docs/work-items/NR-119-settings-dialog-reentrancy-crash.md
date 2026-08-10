# NR-119 — 設定對話框重入會覆寫 `g_dialog`，外層對話框後續互動 null deref crash

Phase 4 · Settings UI · Depends on: NR-013, NR-089（皆 done）

- Source: `docs/design-spec.md` §4.10、§11（「不得因重入崩潰」族）；`AGENTS.md` §Safety boundaries
- Origin: 2026-08-10 第十三次全 repo 稽核（四軸平行子 agent audit，正確性軸）；已由主 Agent 重讀
  `settings_dialog.cpp`／`main.cpp` 訊息路徑驗證
- Priority: **CRITICAL**（任何同桌面 process 或使用者操作都可觸發的確定性 null deref，殺死常駐 tray 程式）

## Why

`g_dialog` 是 `settings_dialog.cpp:47` 的檔案範圍全域 `DialogContext`。`ShowSettingsDialog`
（`:578-621`）在建構 `DialogContext` 時把 **stack 上的 `SettingsEditor editor` 位址**寫進
`g_dialog`（`:606-613`），`DialogBoxParamW` 結束後立刻 `g_dialog = DialogContext{}`（`:619`）。
Dialog proc 在 `:365`（Change）、`:398`（OK）、`:425`（capture 對話框）、`:431-540` 等多處直接解參考
`g_dialog.editor`。

重入路徑（兩段 modal loop 都成立）：

1. 設定對話框開啟（`DialogBoxParamW` modal loop 在跑）。
2. 使用者右鍵 tray icon → `kTrayCallbackMessage`（WM_APP+1，`main.cpp:2924-2929`）→
   `ShowTrayMenu` → `TrackPopupMenu` 又是 modal loop → 點「Settings」→
   `PostMessageW(window, kSettingsMessage, 0, 0)`（`main.cpp:2485`）。
3. `kSettingsMessage` handler（`main.cpp:3058-3064`）**沒有任何 re-entrancy 守門**，直接呼叫
   `ShowSettingsDialog` → 巢狀第二層 `DialogBox`。
4. 內層關閉時 `:619` 把 `g_dialog` 清空——這清掉的是**外層**還在用的 context。
5. 使用者再碰外層對話框任何控制項（OK／Change／任一 checkbox）→ `g_dialog.editor` 為 nullptr → 當場 crash。

`g_dialog_active`（`main.cpp:200`，`:901-956`）只在 `ShowErrorDialog`／`ShowAboutDialog`／context menu
的 MessageBox 前後設為 true，**不涵蓋 `ShowSettingsDialog`**，且它的語意是「面板不要因 KILLFOCUS 隱藏」，
不是 re-entrancy 守門。第二條同族路徑：tray → Settings 的巢狀呼叫中也含 hotkey capture 對話框
（`ShowHotkeyCaptureDialog` 同樣吃 `g_dialog.editor`，`settings_dialog.cpp:425`）——守門放在
`ShowSettingsDialog` 入口即可一併覆蓋。

## Decisions already made — do not reopen

1. 沿用 NR-060／NR-089 的「對話框生命週期與 HWND 綁定、不加測試抽象」先例：本 item 不加測試 seam；
   以 sanity grep ＋ lifecycle check 覆蓋。
2. 不做「把 context 搬進 `GWLP_USERDATA`」的較大重構——re-entrancy guard 一行即可封閉 crash，
   搬移是選修而非必要；若實作者選擇搬移必須維持 `:606-613` 的 snapshot 語意。
3. `WM_HOTKEY`／`g_show_panel_message` 在對話框開啟期間把面板叫到 modal 對話框上方（焦點竊取）
   是**同族但不同**的問題（不 crash），不在本 item 範圍。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> 任何對話框或選單的開啟期間，程式不得因重入而崩潰或進入不一致狀態。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

- `src/app_host/settings_dialog.cpp` — `g_dialog`（`:47`）、`ShowSettingsDialog`（`:578-621`，
  設定 `:613`／清空 `:619`）、proc 內所有 `g_dialog.editor->` 解參考（`:365/:398/:425/:431-540`）。
- `src/app_host/main.cpp` — `kTrayCallbackMessage`（`:2924-2929`）、`ShowTrayMenu` 的
  `PostMessageW(kSettingsMessage)`（`:2485`）、`kSettingsMessage` handler（`:3058-3064`）。
- `src/app_host/settings_dialog.h` — `ShowSettingsDialog` 簽名。

## Scope

1. `ShowSettingsDialog` 入口加 re-entrancy guard：已有對話框在跑時直接回傳 false（不顯示、不做事）。
   建議形狀（與既有 `g_dialog` 語意相容，例）：
   ```cpp
   // NR-119: kSettingsMessage can arrive while the settings dialog's own modal
   // loop is running (tray -> Settings re-entry). A nested ShowSettingsDialog
   // would overwrite g_dialog and leave the outer dialog with a null editor.
   if (g_dialog.editor != nullptr) {
       return false;
   }
   ```
2. 確認 guard 的語意：`kSettingsMessage` handler 在 `applied == false` 時仍會 reload `g_settings`
   （`main.cpp:3069-3072`）——reload 讀同一份 `settings.ini`，值不變，無副作用；若實作者想更乾淨，
   可讓 handler 在 `ShowSettingsDialog` 回 false 時跳過 reload，但這非必要。
3. 若熱鍵 capture 對話框（`ShowHotkeyCaptureDialog`）有獨立入口（不經 `ShowSettingsDialog`），
   依同一形狀加同一守門；grep 確認入口數量後再決定。
4. Sanity grep 釘住：`ShowSettingsDialog(` 呼叫點只有 tray handler 一處（`main.cpp:3064`）；
   `g_dialog.editor` 的所有解參考都在 `DialogBoxParamW` 之後仍持有有效 context 的同一執行緒內。

## Non-goals

- 不改 `WM_HOTKEY`／ShowPanel 在對話框開啟期間的行為（焦點竊取另案）。
- 不做 `GWLP_USERDATA` 搬移以外的介面變更；不改 `DialogContext` 成員；不加 mutex（單一 UI 執行緒）。
- 不新增 balloon／訊息通知。

## Acceptance

1. 設定對話框開啟期間，tray → Settings（含 capture 對話框開啟時）不再 crash、不再開出第二個對話框。
2. 正常單一開啟、OK／Cancel、capture、套用後 rebuild 的行為與現況完全相同。
3. Release build 無新增 warning；完整 CTest 26/26 與既有 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ShowSettingsDialog|g_dialog\.editor" src
# expect: 1 個呼叫點（tray handler）；guard 在 ShowSettingsDialog 入口；
# 無新增 g_dialog 寫入點（仍只有 :613/:619 兩處）。
git diff --name-only
# expect: 只動 settings_dialog.{h,cpp}（＋本 item 文件與 tracker）。
```

## Handoff

實作者需記錄 guard 形狀、tray handler 在 guard 擋下時的行為、capture 對話框入口是否需第二個守門、
sanity grep 結果與 lifecycle 證據。

### 交接區（2026-08-10，實作完成）

- **Guard 形狀**：`ShowSettingsDialog` 入口（`settings_dialog.cpp:581-586`）加一行
  `if (g_dialog.editor != nullptr) { return false; }`，附 NR-119 註解說明重入路徑。不做
  `GWLP_USERDATA` 搬移（Decisions §2 的選修未採）。
- **tray handler 在 guard 擋下時的行為**：`main.cpp:3064` 的 `kSettingsMessage` handler 收到
  `false` 時照常往下執行 `applied == false` 分支重新讀取 `settings.ini`（值不變、無副作用，
  與 Decisions §2 的允許一致），不開第二個對話框、不 crash。
- **capture 對話框入口**：grep 確認 `ShowHotkeyCaptureDialog` 只有一個呼叫點
  （`settings_dialog.cpp:425`），位於設定對話框 proc 內部——已被 `ShowSettingsDialog` 入口的
  守門涵蓋，**不需要第二個守門**（Decisions §3 的分支條件不成立）。
- **Sanity grep**：`ShowSettingsDialog(` 呼叫點僅 tray handler 一處（`main.cpp:3064`）；
  `g_dialog.editor` 解參考全部位於 `DialogBoxParamW` modal loop 內的同一 UI 執行緒 proc 中；
  `g_dialog` 寫入點仍只有原 `:613`（設定）與 `:619`（清空）兩處，本 item 未新增。
- **Lifecycle 證據**：Release build 94/94 完成、無新增 warning；`ctest` 26/26 passed
  （含 `nimblerun_message_loop_test`、settings 相關測試與 lifecycle 類測試）。
