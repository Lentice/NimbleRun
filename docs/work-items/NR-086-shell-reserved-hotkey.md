# NR-086 — 熱鍵解析接受 shell 保留組合（Alt+Tab／Alt+Esc／Ctrl+Esc），會劫持 Windows 工作切換

Phase 3 · Depends on: —（無依賴，可與 NR-084、NR-085 平行）

- Source: `docs/design-spec.md` §4.1（「MVP 不覆寫 Windows 或其他程式的系統快捷鍵」）／§FR-002
- Origin: 2026-08-08 第六次全 repo 稽核（ParseHotkey 接受集合與「OS 接受但 shell 保留」組合比對）

## Why

`ParseHotkey`（`src/settings/settings_editor.cpp:177-236`）只拒絕兩種輸入：
格式錯誤（`< 2` 段、無修飾鍵、未知鍵名）與 Windows 鍵組合（`:200-205`）。
`Alt+Tab`、`Alt+Esc`、`Ctrl+Esc` 這類 **shell 保留組合**可以正常通過解析
（`Tab`／`Esc` 都在 `kNamedKeys`，`:37-44`），接著 `GlobalHotkey::Swap`
（`src/app_host/hotkey.cpp:34-62`）把組合交給 `RegisterHotKey`——而
`RegisterHotKey` 對這些組合**不會失敗**（它們不是 SAS 保留組合；SAS 層只
保留 `Ctrl+Alt+Del` 等，`Alt+Tab` 是 shell 用自己的鍵盤處理實作的，註冊
照樣成功）。結果：

- 使用者在設定頁輸入 `Alt+Tab` → 儲存成功 → NimbleRun 註冊成功 →
  `Alt+Tab` 不再切換工作視窗，改為顯示／隱藏 NimbleRun 面板——**Windows
  最基本的視窗切換被劫持**，且使用者不註銷／手動改回之前無法自救
  （NimbleRun 是常駐程式，`Alt+Tab` 被吞掉連工作管理員都不好開）。
- `Alt+Esc` 同形（原本是「以視窗堆疊順序循環」）。
- `Ctrl+Esc` 同形（原本是「開啟開始功能表」）。

這直接違反 design-spec §4.1：

> MVP 不覆寫 Windows 或其他程式的系統快捷鍵。包含 Windows 鍵、已被註冊或
> 被作業系統保留的組合，註冊失敗時一律拒絕並提醒使用者

「被作業系統保留」在 `RegisterHotKey` 層不會回報失敗（Alt+Tab 這類由 shell
消費的組合），所以「註冊失敗時一律拒絕」這條防線對它們**形同虛設**——唯一
能攔下它們的地方是解析端。`Win` 鍵組合已在解析端拒絕（`:200-205` 先例），
本 item 只是把同一道防線補到 shell 保留組合上。

覆寫與既有決策的關係：NR-003（global hotkey）的範圍是「註冊失敗的拒絕與
提醒」與「swap 語意」，沒有涵蓋「OS 接受但 shell 保留」的組合——那時
`ParseHotkey` 也還不存在（它是 NR-013 產物）。本 item 是補 NR-003/NR-013
都未覆蓋的缺口，不是重開任一已否決方向。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **只擋 `Alt+Tab`、`Alt+Esc`、`Ctrl+Esc` 三個**。這是 shell 層級的
   視窗管理／開始功能表組合，被劫持會直接破壞使用者切換應用程式的能力。
   `Alt+F4`、`Ctrl+Alt+Del`、`Win+L` 不擋：`Ctrl+Alt+Del` 是 SAS 保留、
   `RegisterHotKey` 本來就會失敗（走既有拒絕路徑）；`Alt+F4` 是應用程式
   層慣例而非 shell 保留（一個明確設定成 `Alt+F4` 的 launcher 熱鍵是可
   接受的個人選擇，且 `Alt+F4` 只關前景視窗，不阻斷切換）；`Win` 組合
   已在 `:200-205` 拒絕。
2. **修在 `ParseHotkey`**（單一解析入口）：最後組出 `(modifiers, virtual_key)`
   後，若 `modifiers`（去掉 `MOD_NOREPEAT` 位元後）與 `virtual_key` 命中
   三組保留組合之一 → 回傳 false（走既有「格式錯誤」的拒絕路徑：設定頁
   顯示 `HotkeyRejectedNotice`、保留舊值、`Populate` 回填）。**不改
   `RegisterBinding`／`Swap`／`hotkey.cpp`**——OS 層面這些組合本來就會
   成功，擋在解析端才是對的。
3. **不放寬「設定頁可提供 `Ctrl+Alt+Space` 作為建議值」**：那只是設定頁
   提示文案，與本 item 無關，不動。
4. **加純值測試**：`settings_editor_test`（或 `hotkey_test`）直接測
   `ParseHotkey` 對 `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 回 false、對
   `Alt+Space`／`Ctrl+Alt+Space`／`Ctrl+Shift+Esc`（非保留）回 true。
   不需新 seam。

## Binding constraints — quoted, do not go looking for them

design-spec §4.1：

> MVP 不覆寫 Windows 或其他程式的系統快捷鍵。包含 Windows 鍵、已被註冊或被作業系統保留的組合，註冊失敗時一律拒絕並提醒使用者；`Win+R` 不屬於 NimbleRun 的可用快捷鍵。

design-spec §FR-002：

> - 使用 `RegisterHotKey`，並加入 `MOD_NOREPEAT`。
> - `RegisterHotKey` 失敗或快捷鍵被 Windows 保留時，拒絕該設定，不安裝低階鍵盤 hook，也不攔截任何輸入。
> - 設定新快捷鍵時，先測試註冊成功，再釋放舊快捷鍵；不得靜默切換到候選值。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/settings/settings_editor.cpp:177-236` — `ParseHotkey`。**本 item 只改
  這個函式**（在 `:229` 的 `if (virtual_key == 0) return false;` 之後、
  `:233` 組 `out` 之前插入保留組合檢查）。
- `src/settings/settings_editor.cpp:37-44` — `kNamedKeys`：`Tab`、`Esc` 都是
  合法鍵名，證明 `Alt+Tab`／`Alt+Esc` 目前可解析。
- `src/settings/settings_editor.cpp:200-205` — 既有 `Win` 鍵拒絕分支
  （本 item 的同一道防線的先例）。
- `src/app_host/hotkey.cpp:8-21, 34-62` — `RegisterBinding`／`Swap`：證明
  解析通過後組合會被無條件交給 `RegisterHotKey`（本 item 不改）。
- `src/app_host/settings_dialog.cpp:158-211` — IDOK 流程：`SetHotkey` 失敗
  即 `HotkeyRejectedNotice`＋回填舊值（本 item 的拒絕顯示路徑，不改）。
- `tests/unit/settings_editor_test.cpp` — 既有 `ParseHotkey` 測試所在，
  本 item 在其旁加案例。

## Scope

### 1. `ParseHotkey` 加 shell 保留組合檢查

在 `settings_editor.cpp:229` 的 `virtual_key == 0` 拒絕之後：

```cpp
// NR-086: shell 層級保留的組合（工作切換／開始功能表）。RegisterHotKey
// 對它們不會失敗——Alt+Tab 是 shell 用自己的鍵盤處理實作的，不在 SAS
// 保留清單裡——所以「註冊失敗一律拒絕」的防線攔不到它們；若不在此
// 拒絕，使用者把熱鍵設成 Alt+Tab 會讓 Windows 完全失去視窗切換
// （design-spec §4.1「MVP 不覆寫 Windows 或其他程式的系統快捷鍵」）。
// 只擋這三個 shell 組合；Ctrl+Alt+Del 由 OS 拒絕、Win 組合在解析端
// 已拒絕、Alt+F4 屬應用程式層慣例不在此列。
if ((modifiers & ~MOD_NOREPEAT) == MOD_ALT && (virtual_key == VK_TAB || virtual_key == VK_ESCAPE)) {
    return false;
}
if ((modifiers & ~MOD_NOREPEAT) == MOD_CONTROL && virtual_key == VK_ESCAPE) {
    return false;
}
```

`modifiers` 已含 `MOD_NOREPEAT`（`:233`），比較前先遮掉該位元。`Shift`
修飾的變體（`Ctrl+Shift+Esc` 是工作管理員）**不擋**——它不是被劫持的
組合，且使用者明確選了它。

### 2. 更新測試

在既有 `ParseHotkey` 測試旁新增：

- `Alt+Tab`、`Alt+Esc`、`Ctrl+Esc` → `ParseHotkey` 回 false（拒絕，
  進設定頁的 `HotkeyRejectedNotice` 路徑）。
- `Alt+Space`、`Ctrl+Alt+Space`、`Ctrl+Shift+Esc`、`Shift+Alt+Tab` →
  回 true（不誤傷合法組合與帶 Shift 的變體）。
- `Win+Tab` → 維持回 false（既有 Win 拒絕，回歸）。

### 3. 更新 spec？

design-spec §4.1 已含「MVP 不覆寫 Windows 或其他程式的系統快捷鍵」。
本 item 是讓該句對「OS 接受但 shell 保留」的組合名副其實，不另加條文。

## Non-goals

- **不改 `hotkey.cpp`（`RegisterBinding`／`Swap`）**：OS 層對這些組合
  本就成功，攔截點在解析端（Decisions §2）。
- **不擴充保留清單**（`Alt+F4`、`Ctrl+Alt+Del`、`Win+L` 等一律不擋，
  Decisions §1）；如使用者日後要求，另開 item。
- **不新增設定項或 UI 文案**：拒絕顯示走既有 `HotkeyRejectedNotice`。
- **不動設定頁的「`Ctrl+Alt+Space` 建議值」提示**（Decisions §3）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠。
2. `settings_editor_test`（或既有 hotkey 測試檔）新增案例通過：
   `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 被拒，`Alt+Space`／`Ctrl+Alt+Space`／
   `Ctrl+Shift+Esc`／`Shift+Alt+Tab` 照常接受，`Win+Tab` 維持被拒。

Manual（Release build，逐條打勾）：

1. 設定頁把熱鍵改為 `Alt+Tab` → 顯示「The hotkey is invalid or already in
   use.」且舊熱鍵保留；`Alt+Tab` 仍正常切換視窗。
2. 把熱鍵改為 `Ctrl+Esc` → 同上拒絕；`Ctrl+Esc` 仍開啟開始功能表。
3. 把熱鍵改為 `Ctrl+Shift+Esc` → 接受；工作管理員照常可開
   （此組合不屬於保留清單）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings_ui|hotkey" --output-on-failure
```

```powershell
# 保留組合檢查只在 ParseHotkey 一處：
Select-String -Path src/settings/settings_editor.cpp -Pattern 'VK_TAB|VK_ESCAPE'
# expect: kNamedKeys 既有 2 處 + ParseHotkey 新增 1~2 處

# hotkey.cpp 未動：
Select-String -Path src/app_host/hotkey.cpp -Pattern 'VK_TAB|VK_ESCAPE'
# expect: 零命中

git diff --name-only
# expect: 僅 src/settings/settings_editor.cpp、tests/unit/settings_editor_test.cpp
# （及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

（實作者填寫：改動位置、插入的檢查、測試案例名與斷言、建置與 CTest 結果、
3 條手動驗收結果、sanity greps、偏差、未完成事項。）

- 改動位置：`src/settings/settings_editor.cpp` `ParseHotkey` 的
  `virtual_key == 0` 檢查之後插入保留組合檢查；測試檔新增 3+ 案例。
- 建置與 CTest：Release 建置無新增警告；`ctest --test-dir build
  --output-on-failure` 全綠；`ctest -R "settings_ui|hotkey"` 全綠。
- 手動驗收：本工作區不操作視窗，3 條手動驗收未實跑；由 ParseHotkey
  單元案例覆蓋（RegisterHotKey 行為依 MSDN 文件：Alt+Tab 非 SAS 保留、
  註冊成功）。
- sanity greps：`VK_TAB`／`VK_ESCAPE` 於 settings_editor.cpp 為
  kNamedKeys 既有 ＋ ParseHotkey 新增；hotkey.cpp 零命中。
- 偏差：實作與測試已存在於既有本地 commit `cbf8e39`；本次 opencode job 只做
  clean-worktree 驗證，沒有新增 patch。`Win+Tab` 的舊驗收已由後續 NR-088
  決策覆寫為可解析，但 `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 仍硬性拒絕。手動 GUI
  驗收未執行。
- 未完成：無。
