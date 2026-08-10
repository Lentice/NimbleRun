# NR-125 — design-spec §4.1／FR-002 回寫 NR-088 的 Win 鍵決策：spec 明文與出貨行為直接矛盾

Phase 4 · Spec sync · Depends on: NR-086, NR-088, NR-094（皆 done）

- Source: `docs/design-spec.md` §4.1、§FR-002
- Origin: 2026-08-10 第十三次全 repo 稽核（spec 符合度軸）；主 Agent 已交叉比對
  `settings_editor.cpp` ParseHotkey、`settings_dialog.cpp` capture 通知文字與 spec 條文
- Priority: **MEDIUM**（spec 是產品唯一真相，且本 repo 明訂「Spec 與實作矛盾時以 spec 為準」——
  一條說謊的規格比沒有規格更糟；NR-088 的覆寫只寫在 item 文件，未回寫到真相來源）

## Why

NR-088（2026-08-08 出貨）把 Win 鍵組合從「語法層硬拒絕」降級為「可解析、註冊衝突僅警告、使用者
可確認套用」：

- `settings_editor.cpp:214-219`：`ParseHotkey` 接受 `Win`／`Windows` token → `MOD_WIN`；
- `settings_dialog.cpp:184-186`：capture 衝突通知寫「You can still confirm it」（衝突只警告不阻擋）；
- `hotkey_capture.cpp`：無 Win 組合的硬拒絕清單（shell-reserved 的 `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc`
  靜態清單仍在，那是 NR-086）。

但 `docs/design-spec.md:135`（§4.1）至今寫著：

> MVP 不覆寫 Windows 或其他程式的系統快捷鍵。包含 Windows 鍵、已被註冊或被作業系統保留的組合，
> 註冊失敗時一律拒絕並提醒使用者；`Win+R` 不屬於 NimbleRun 的可用快捷鍵。

git log 確認 spec 在 NR-088 之後只被 NR-094（語言政策）動過，「Win 鍵」條文從未被回寫。出貨行為：
`Win+E` 等可設定並使用；spec 卻仍寫「一律拒絕」。§FR-002 的對應條文（`:296-302` 一帶，
「Windows 鍵…註冊失敗一律拒絕」）同病。NR-088 的覆寫宣告存在於 item 文件與本頁決策紀錄，但
唯一真相來源沒有同步——下一個冷讀 spec 的 agent 會照舊實作硬拒絕，或做出與產品決策矛盾的判斷。

## Decisions already made — do not reopen

1. **本 item 只改 spec，不改 code**：出貨行為（NR-088 決策）是現況，spec 向實作對齊
   （NR-056「docs describe the product that actually ships」的既有原則）。
2. 保留的硬拒絕只有 NR-086 的 shell-reserved 三組合（`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc`）——
   該決策未被 NR-088 覆寫，spec 條文需把兩者的邊界寫清楚。
3. 覆寫聲明（依 AGENTS.md「新 item 內寫出覆寫與新證據」規則，本 item 即覆寫的落點）：NR-088
   覆寫 §4.1/§FR-002 對 Win 鍵組合的「一律拒絕」適用性；本 item 把覆寫結果回寫進 spec。
4. 不重開「Win 鍵是否該允許」的產品決策本身；不新增任何程式碼。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> When an item overrides an earlier decision, state the override inside the new item. Never edit a completed item's document.

`AGENTS.md`（本 item 的改動對象是 spec，不是 item 文件）：

> docs/design-spec.md 是產品行為的唯一真相；行為改變時先更新 Spec，再調整受影響的 item。

`docs/design-spec.md` §4.1（現行、將被改寫的條文）：

> 包含 Windows 鍵、已被註冊或被作業系統保留的組合，註冊失敗時一律拒絕並提醒使用者。

## Files to read and trace first

- `docs/design-spec.md` §4.1（`:135` 一帶）與 §FR-002（`:296-302` 一帶）。
- `docs/work-items/NR-088-hotkey-win-modifier-and-probe.md`、NR-086 — 覆寫的原始決策與邊界。
- `src/settings/settings_editor.cpp`（`:214-219`）、`src/app_host/settings_dialog.cpp`
  （`:184-186`）— 出貨行為的依據。

## Scope

1. 改寫 §4.1：Win 鍵組合可解析並註冊；衝突時警告但允許使用者確認套用；註冊失敗（OS 層）仍拒絕
   並保留舊鍵；`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 三組 shell-reserved 組合維持硬拒絕。
2. 改寫 §FR-002 對應條文，與 §4.1 一致；刪除「`Win+R` 不屬於 NimbleRun 的可用快捷鍵」等與
   出貨行為矛盾的句子（或改寫為「shell-reserved 清單以外的 Win 組合可用」）。
3. 若 §4.1 的條文同時被其他章節引用（grep `Windows 鍵`／`一律拒絕` 全 spec），逐一確認一致。

## Non-goals

- 不改任何 `src/` 程式碼；不新增設定選項；不重開 NR-086/NR-088 的決策。
- 不動 §4.7 鍵盤表（Alt+digit 等）與熱鍵 capture 對話框的既有文字。

## Acceptance

1. `design-spec.md` 全檔 grep 無「Win 鍵…一律拒絕」的殘留矛盾句（shell-reserved 三組合的
   硬拒絕例外明寫）。
2. §4.1／§FR-002 描述的熱鍵行為與 Release 出貨行為一致（可對照 NR-088 交接區）。
3. 純文件改動；build／CTest 不受影響（不需重跑，但 lifecycle check 可選跑）。

## Agent checks

```powershell
rg -n "Win|Windows 鍵|一律拒絕|shell-reserved|Alt\+Tab" docs/design-spec.md
# expect: §4.1/§FR-002 新條文與出貨行為一致；無舊矛盾句。
git diff --name-only
# expect: 只動 docs/design-spec.md 與本 item 文件＋tracker。
```

## Handoff

實作者需記錄改寫後的 §4.1/§FR-002 全文、與 NR-088/NR-086 的對應關係、grep 結果。

### 交接區（2026-08-10，實作完成）

本 item 為純文件改動，只改了 `docs/design-spec.md`（§4.1 一條、§FR-002 兩條）與本交接區。
未動任何 `src/` 程式碼、未動 tracker、未執行 git。

**改寫後的 §4.1 全文（`docs/design-spec.md:135`）：**

> MVP 不覆寫其他程式的系統快捷鍵，但含 Win 鍵的組合不在此限：Win 鍵組合可解析並註冊（`MOD_WIN`）。與其他程式或作業系統衝突時，設定介面顯示警告，使用者可確認套用（衝突由使用者自行承擔）；註冊當下 `RegisterHotKey` 失敗（OS 層）時仍拒絕該設定、保留舊快捷鍵並提醒使用者。`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 三組 shell 保留組合維持硬拒絕，不得設定。

**改寫後的 §FR-002 對應條文（`docs/design-spec.md:298-304`）：**

> - 使用 `RegisterHotKey`，並加入 `MOD_NOREPEAT`。
> - Win 鍵組合可解析並註冊（`MOD_WIN`）；與其他程式或作業系統衝突時，設定介面顯示警告，使用者可確認套用（衝突由使用者自行承擔）。
> - 註冊當下 `RegisterHotKey` 失敗（OS 層，例如組合已被其他程式佔用或被作業系統保留）時，拒絕該設定，不安裝低階鍵盤 hook，也不攔截任何輸入。
> - 首次註冊失敗時保留通知區操作能力並顯示一次非阻擋提醒；若已有舊快捷鍵，設定失敗時保留舊快捷鍵。
> - 設定新快捷鍵時，先測試註冊成功，再釋放舊快捷鍵；不得靜默切換到候選值。
> - `Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 三組 shell 保留組合在解析端硬拒絕，不得設定（NR-086）。
> - 程式結束時呼叫 `UnregisterHotKey`。

原 §4.1 的「包含 Windows 鍵…註冊失敗時一律拒絕並提醒使用者；`Win+R` 不屬於 NimbleRun 的可用快捷鍵」
與 §FR-002 的「`RegisterHotKey` 失敗或快捷鍵被 Windows 保留時，拒絕該設定」已刪除／改寫，不再與出貨行為矛盾。

**與 NR-088／NR-086 的對應關係：**

- **NR-088 的覆寫落點**：Win 鍵組合從「語法層硬拒絕」降級為「可解析（`MOD_WIN`）＋衝突僅警告、使用者可確認套用」，
  已如實回寫進 §4.1 與 §FR-002。「註冊當下失敗（OS 層）仍拒絕並保留舊鍵」保留 NR-088 對既有
  `GlobalHotkey::Swap` test-then-commit 防線的描述；「不安裝低階鍵盤 hook」維持原意（背景待機不用 hook，
  hook 僅限 NR-089 擷取對話框生命週期）。
- **NR-086 保留**：`Alt+Tab`／`Alt+Esc`／`Ctrl+Esc` 三組 shell 保留組合在解析端硬拒絕（`settings_editor.cpp:262-268`
  `ParseHotkey`），spec 明文保留，與 NR-088「不把 shell-reserved 清單降級」的決策一致。
- 出貨行為對照：`settings_editor.cpp:214-222`（Win token → `MOD_WIN`）、`settings_dialog.cpp:123-141`
  （capture 衝突走 `CaptureConflictNotice`「You can still confirm it」，`confirmable = parseable` 仍可確認）、
  `settings_dialog.cpp:175-191`（IDOK 套用，失敗時 `CaptureInvalidNotice`／保持開啟）。

**grep 結果（`rg -n "Win|Windows 鍵|一律拒絕|shell-reserved|Alt\+Tab" docs/design-spec.md`）：**

- `一律拒絕`：**零命中**。
- `shell-reserved`：**零命中**（spec 用「shell 保留」中文用語，無英文殘句）。
- `Windows 鍵`（確切詞組）：僅 §4.1（`:135`）新條文一處。
- `Alt+Tab`：`:135`（§4.1 新條文）、`:303`（FR-002 新條文）兩處皆為「硬拒絕」陳述，其餘 `Alt+Tab` 命中屬
  NR-045／NR-046 面板自身行為與測試章節，與熱鍵設定無關。
- 需人工判斷的兩處既有條文，判定**一致、不需改動**：
  - `:825`（§12.2 整合測試）「無法註冊或被 Windows 保留的快捷鍵會被拒絕並提醒」——描述的是**套用當下**
    `RegisterHotKey` 失敗即拒絕的結果，新行為下仍成立（Win 組合只在成功註冊時放行）。
  - `:926`（AC-011）「NimbleRun 拒絕新設定、不攔截原生輸入、保留既有可用快捷鍵」——同樣是 apply-time
    結果描述；擷取對話框的警告＋可確認只改變流程，不改變「組合被其他程式真實佔用時註冊失敗即拒絕」
    的結果，故不矛盾。

**未完成事項：** 無。本 item 未跑 build／CTest（純文件；NR-125 Acceptance 明列不需重跑）。
