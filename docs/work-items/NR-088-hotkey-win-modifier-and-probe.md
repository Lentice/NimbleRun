# NR-088 — ParseHotkey 允許 Win 修飾鍵（降級為警告）＋新增唯讀衝突探測函式

Phase 4 · Depends on: —

- Source: `docs/design-spec.md` §4.1（行 135）、§FR-002（`docs/design-spec.md:296-302`）
- Origin: 2026-08-08 使用者需求討論（設定頁快速鍵改為擷取式 UI），詳見
  [hotkey-override-research.md](../hotkey-override-research.md) 與本頁 §計畫決策紀錄新增段落
- 這是 NR-089（快速鍵擷取對話框 UI）的前置 item：NR-089 需要本 item 產出的
  `TryRegisterHotkey` 探測函式與「Win 鍵不再被 ParseHotkey 直接拒絕」的行為

## Why

使用者要求把設定頁的快速鍵輸入方式從自由輸入文字改成「按鍵擷取」，且擷取
時必須能按下並顯示 **Win 鍵**（連同 Ctrl/Alt/Shift 一起，允許多修飾鍵同時
組合，例如 `Ctrl+Alt+E`）。擷取到不合法（含系統衝突）的組合時，使用者要求
「僅在下方顯示提示訊息，不阻擋，使用者按下確認鍵後就套用新快速鍵，衝突與否
由使用者自行處理」。

這與現況兩處明文的既有決策衝突，必須在本 item 內明述覆寫：

1. `docs/design-spec.md:135`（§4.1）：「MVP 不覆寫 Windows 或其他程式的系統
   快捷鍵。包含 Windows 鍵、已被註冊或被作業系統保留的組合，註冊失敗時一律
   拒絕並提醒使用者；`Win+R` 不屬於 NimbleRun 的可用快捷鍵。」
2. `src/settings/settings_editor.cpp:200-202`：`ParseHotkey` 目前對 `Win`／
   `Windows` token 直接 `return false`（連同的註解「Windows-key combos are
   reserved (design-spec §4.1)」），使用者完全無法設定含 Win 鍵的快捷鍵，連
   「儲存但事後衝突」的機會都沒有。

**本 item 的覆寫範圍很窄**：不是取消「不搶奪系統快捷鍵」的產品立場，而是把
「Win 鍵組合」從『語法層直接拒絕』降級成『語意層警告，使用者可自行決定套
用』——跟現有 `RegisterHotKey` 失敗時的既有行為（`HotkeyRejectedNotice`，
`settings_dialog.cpp:163`）分開處理：現況「輸入衝突組合→UI 顯示拒絕通知→
保留舊快捷鍵」的行為對**其他所有組合**維持不變，本 item 也不改這條路徑。
本 item 新增的是「使用者透過 NR-089 的擷取對話框主動確認要套用一個已知會
衝突（含 Win 鍵）的組合」時的**新路徑**：確認鍵按下就直接套用，不擋。

無法用既有的 `RegisterHotKey` 測試註冊（`GlobalHotkey::Swap`，
`src/app_host/hotkey.cpp:34-62`）來即時預覽衝突，因為它是「测试成功才切換」
的 commit-style API，呼叫一次就會真的動到目前生效中的快捷鍵 id 配置。
NR-089 的擷取對話框需要在使用者按下確認鍵**之前**，用「試註冊、立刻
unregister」的方式単純探測是否會失敗，探測本身不能有副作用。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08）：

1. **`ParseHotkey` 不再拒絕 `Win`／`Windows` token**：改成正常解析出
   `MOD_WIN`（Win32 常數，與 `MOD_CONTROL`／`MOD_ALT`／`MOD_SHIFT` 同類）。
   `HotkeyBinding::modifiers` 之後可能含 `MOD_WIN`。
2. **NR-086 既有的 shell-reserved 靜態拒絕清單不變、不擴大**：
   `settings_editor.cpp:242-248` 的 `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 三個
   仍然是「語法層直接拒絕，回傳 `false`，連 `HotkeyBinding` 都不產生」——
   這三個不是「使用者可以選擇套用」的衝突，是 shell 消費掉輸入、
   `RegisterHotKey` 根本不會失敗的窄清單（見 NR-086 內文），繼續維持硬拒絕。
   本 item 不把它們也降級成警告。
3. **`Win+R`／`Win+其他` 不加入新的硬拒絕清單**：既然 Win 鍵組合整體從硬拒絕
   降級為警告，就不再對 `Win+R` 這類具體組合特別處理——它會像其他 Win 組合
   一樣走「探測回報衝突，使用者仍可確認套用」的路徑。
4. **`ParseHotkey` 的既有回傳型別與呼叫端契約不變**：仍是
   `bool ParseHotkey(std::wstring_view text, HotkeyBinding& out)`；本 item
   純粹放寬 Win token 的分支，不改函式簽章、不改呼叫端
   （`SettingsEditor::SetHotkey`，`settings_editor.h` 對應宣告）。
5. **新增函式只做「探測」，不做「切換」**：新函式（暫名
   `TryRegisterHotkey`，見下方 Scope）只回答「這個 binding 現在能不能被
   `RegisterHotKey` 成功註冊」，探測完必須自行 `UnregisterHotKey` 還原、
   不觸碰 `GlobalHotkey` 現有的 `active_id_`／`current_` 狀態。不重構
   `GlobalHotkey::Swap` 本身的 test-then-commit 流程，兩者並存、职责分開。
6. **本 item 不含任何 UI 改動**：不動 `settings_dialog.cpp`、不動 `.rc`
   資源、不新增對話框。UI 消費本 item 的成果留給 NR-089。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md:135`（§4.1，本 item 明確覆寫其中「Windows 鍵…註冊失敗
時一律拒絕」這句，其餘「不覆寫其他程式系統快捷鍵」的精神不變）：

> MVP 不覆寫 Windows 或其他程式的系統快捷鍵。包含 Windows 鍵、已被註冊或被
> 作業系統保留的組合，註冊失敗時一律拒絕並提醒使用者；`Win+R` 不屬於
> NimbleRun 的可用快捷鍵。

`docs/design-spec.md:296-302`（§FR-002，同樣本 item 覆寫「快捷鍵被 Windows
保留時拒絕該設定」對 Win 鍵組合的適用性，其餘規則不變）：

> - 使用 `RegisterHotKey`，並加入 `MOD_NOREPEAT`。
> - `RegisterHotKey` 失敗或快捷鍵被 Windows 保留時，拒絕該設定，不安裝
>   低階鍵盤 hook，也不攔截任何輸入。
> - 首次註冊失敗時保留通知區操作能力並顯示一次非阻擋提醒；若已有舊快捷鍵，
>   設定失敗時保留舊快捷鍵。
> - 設定新快捷鍵時，先測試註冊成功，再釋放舊快捷鍵；不得靜默切換到候選值。
> - 程式結束時呼叫 `UnregisterHotKey`。

注意「不安裝低階鍵盤 hook」這句是描述**背景待機時**的正常運作（不用 hook
偵測全域快捷鍵，靠 `RegisterHotKey`），跟 NR-089 只在「使用者主動開啟擷取
對話框的短暫期間」安裝、對話框關閉即移除的低階鍵盤 hook 是不同情境，NR-089
會在自己的 item 內處理這個區分，本 item 不需要動這句文字本身。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding
  helpers or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/settings/settings_editor.cpp:177-253` — `ParseHotkey` 全文，
  `:200-202` 是 Win token 目前的硬拒絕分支，本 item 的主要修改點。
- `src/settings/settings_editor.h` — `HotkeyBinding` 定義（`modifiers`／
  `virtual_key` 欄位）、`ParseHotkey` 宣告、`SettingsEditor::SetHotkey` 宣告。
- `src/app_host/hotkey.h`、`src/app_host/hotkey.cpp:1-72` —
  `GlobalHotkey` 類別全文，特別是 `RegisterBinding`（`:8-15`，本 item 新函式
  可重用的「呼叫 `RegisterHotKey`／記錄 `GetLastError`」寫法）與
  `Swap`（`:34-62`，本 item 不改，但探測函式的「先註冊、判定、再解除」精神
  來源於此）。
- `docs/design-spec.md:296-302`（§FR-002）、`:920-921`（Given/When/Then 驗收
  描述，行 921：「Given 快捷鍵已被其他程式註冊或被 Windows 保留，When 使用者
  設定該組合，Then NimbleRun 拒絕新設定…」——本 item 不改這條給*一般設定路徑*
  的驗收語意，只新增 NR-089 要用的探測 API，一般路徑的拒絕行為不變）。
- `tests/unit/`（找 `settings_editor` 對應的既有測試檔名，例如
  `settings_editor_test.cpp` 或類似命名）— 既有 `ParseHotkey` 測試案例，
  確認新增 Win 案例不破壞既有案例（尤其 NR-086 的 shell-reserved 測試）。

## Scope

1. `settings_editor.cpp:200-202`：移除 Win token 的 `return false` 分支，
   改成 `modifiers |= MOD_WIN;`（`MOD_WIN` 是 `<windows.h>` 既有常數，不需
   自訂）。
2. 新增一個純函式（建議放在 `src/app_host/hotkey.h`／`.cpp`，因為它需要
   `RegisterHotKey`／`UnregisterHotKey`，屬於 Win32 邊界，不屬於
   `settings_editor` 這個「與 HWND 無關」的純邏輯模組）：

   ```cpp
   // Probes whether `binding` could be registered right now, without
   // disturbing any currently-active hotkey. Registers under a scratch id,
   // immediately unregisters, and reports the result. Side-effect free.
   HotkeyResult TryRegisterHotkey(HWND window, const HotkeyBinding& binding);
   ```

   實作：用一個不與 `kGlobalHotkeyId`／`kProbeHotkeyId` 衝突的第三個 id
   常數（例如 `kCaptureProbeHotkeyId`），呼叫 `RegisterHotKey`，記錄結果，
   若成功立刻 `UnregisterHotKey` 還原，回傳 `HotkeyResult`（沿用既有
   `success`／`error` 欄位）。
3. `HotkeyString`／`SettingsString`（`settings_editor.h`／`.cpp`）目前的
   `HotkeyHint`（`L"Alt+Space, Ctrl+Alt+Space, ... Windows-key combos are
   rejected."`，`settings_editor.cpp:112`）文字提到「Windows-key combos are
   rejected」，Win 鍵不再是語法拒絕，這句文案要更新，避免文案與行為不一致
   （例如改成不提 Win 鍵限制，或留給 NR-089 一併調整——若 NR-089 會整體重寫
   這一區的提示文案，本 item 至少要把「rejected」這個不再成立的字眼拿掉）。

## Non-goals

- 不做任何 UI 改動（對話框、`.rc`、按鍵擷取邏輯）：全部留給 NR-089。
- 不移除或放寬 NR-086 的 shell-reserved 靜態拒絕清單（Decisions §2）。
- 不新增 `Win+R` 或其他具體組合的專屬硬拒絕清單（Decisions §3）。
- 不重構 `GlobalHotkey::Swap` 的 test-then-commit 流程或改變其呼叫端
  （`settings_dialog.cpp:192-194` 的 `swapper` lambda 不變）。
- 不處理「探測」與「真正套用」之間的 race（例如探測時沒衝突，使用者按確認
  前瞬間被別的程式搶走）——`GlobalHotkey::Swap` 既有的「先測試註冊、失敗就
  保留舊快捷鍵」機制已經是套用當下的最終防線，探測只是給 NR-089 UI 用的
  即時提示，不是保證。

## Acceptance

Automated：

1. `ctest --test-dir build --output-on-failure` 全綠。
2. 新增至少以下 `ParseHotkey` 測試案例：
   - `"Win+E"` 現在解析成功，`modifiers` 含 `MOD_WIN`。
   - `"Ctrl+Win+E"`、`"Ctrl+Alt+Win+E"` 等多修飾鍵組合含 Win 時解析成功。
   - NR-086 既有的 `"Alt+Tab"`／`"Alt+Esc"`／`"Ctrl+Esc"` 拒絕案例維持
     `false`（回歸測試，確認 Win 鍵放寬沒有連帶鬆綁 shell-reserved 清單）。
3. 新增至少一個 `TryRegisterHotkey` 的 self-check：對一個確定未被佔用的
   測試用組合探測應成功，且探測後緊接著再次 `RegisterHotKey` 同一組合（模擬
   別的程式碼路徑）應該也能成功——證明探測有確實 unregister 還原、沒有洩漏
   佔用。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# Win token 不再硬拒絕：
Select-String -Path src/settings/settings_editor.cpp -Pattern 'MOD_WIN'
# expect: 至少一處指派（新增的解析分支）

# 新的探測函式存在且與既有 Swap 分開：
Select-String -Path src/app_host/hotkey.h,src/app_host/hotkey.cpp -Pattern 'TryRegisterHotkey'
# expect: 宣告與定義各一次

git diff --name-only
# expect: src/settings/settings_editor.cpp、src/app_host/hotkey.h、
#         src/app_host/hotkey.cpp、對應測試檔（及本 item 文件與
#         docs/work-items.md 追蹤表格）；不含 src/app_host/settings_dialog.cpp
#         或任何 .rc 檔案
```

## 交接區

實作已存在於既有本地 commit `28cc4b8`：`ParseHotkey` 將 Win token 解析為
`MOD_WIN`，`TryRegisterHotkey` 以 scratch id 註冊後立即解除，並加入 Win modifier
與 probe side-effect regression tests；NR-086 的三個 shell-reserved 組合仍拒絕。
Release build 成功，完整 CTest **24/24 通過**，sanity greps 符合，且未修改 UI。
本次 opencode job 只做 clean-worktree 驗證，沒有新增 patch。`settings_editor.h` 的
舊註解仍提到 Windows key never used，屬文件瑕疵，未擴大本 ticket scope。手動驗收
未執行。未完成事項：無。
