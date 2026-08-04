# NimbleRun 系統快捷鍵覆寫研究

最後更新：2026-08-04

## 結論

NimbleRun MVP 只使用 `RegisterHotKey`，目前預設為 `Alt+Space`。快捷鍵無法註冊或被 Windows 保留時，拒絕該設定並提醒使用者；不自動安裝 `WH_KEYBOARD_LL`，也不攔截任何原生輸入。

`WH_KEYBOARD_LL` 已完成評估，但只作未來需求的 reference，不納入目前產品路徑。這避免為了覆寫 `Win+R` 而對整個桌面的每次鍵盤輸入增加常駐處理成本。

## WOX 提供的參考

WOX 官方舊版基本使用文件描述：命令列 plugin 可作為 `Win+R` 的替代介面，且 Wox 的啟動快捷鍵可在設定中變更。現行官方文件仍將 Windows 預設啟動鍵列為 `Alt+Space`，並提供 General 設定修改 hotkey。

這些文件證明使用者需求與產品模式可行，但沒有足夠的第一方證據要求 NimbleRun 複製 WOX 的內部實作。因此 NimbleRun 依 Windows API 的限制自行選擇方法。

## Windows API 評估

### `RegisterHotKey`

適合一般全域快捷鍵：系統成功註冊後，將 `WM_HOTKEY` 放入指定 HWND 或 thread 的 message queue；`MOD_NOREPEAT` 可避免按住按鍵造成重複通知。

限制是 Windows 官方文件明載：包含 Windows 鍵的快捷鍵保留給作業系統。`RegisterHotKey(MOD_WIN, ...)` 不應被當成可靠的 `Win+R` 覆寫方案；註冊失敗也不能靠反覆 retry 解決。

### `WH_KEYBOARD_LL`（評估但不採用）

`SetWindowsHookExW(WH_KEYBOARD_LL, callback, GetModuleHandleW(nullptr), 0)` 是 Windows 官方支援的全域低階鍵盤輸入 hook。Microsoft 的 Win32 範例用同一機制停用 Windows 鍵，並說明標準使用者權限即可使用。

實作要點：

- hook 綁定目前桌面的所有 thread；安裝 hook 的 thread 必須持續處理 message loop。
- 只在 override 模式啟用，停用時立刻 `UnhookWindowsHookEx`。
- 非目標按鍵必須呼叫 `CallNextHookEx`。
- 目標組合的 keydown／keyup 要一起消費，避免 Shell 收到不完整的按鍵序列。
- callback 必須極短；只維護左右 Windows 鍵與目標鍵的狀態，首次匹配時 `PostMessage` 顯示面板，禁止同步做 UI 或 I/O。
- 需處理 hook 安裝失敗，並保留可用的非覆寫快捷鍵與 tray 設定入口。

### 邊界與不承諾

- 這是明確的輸入攔截模式，不是解除 OS 或其他程式已註冊的 hotkey。
- Hook 只作用於目前互動桌面；鎖定畫面、UAC secure desktop 等不同 desktop 不在 MVP 保證範圍。
- Microsoft 文件指出低階 hook 可能因 callback 超時而被系統靜默移除，因此 callback 不得阻塞；需在實機測試中驗證長時間穩定性。
- 不加入 `uiAccess=true`、管理員權限、服務或驅動程式。若未來要求攔截更高完整性層或 secure desktop 的輸入，必須另立安全與部署設計。
- 不記錄一般鍵盤輸入；診斷只記錄 hook 是否成功、配置的 shortcut 類型與 Win32 error。

## NimbleRun 採用決策

- 預設仍為 `Alt+Space`，走 `RegisterHotKey + MOD_NOREPEAT`，符合低待機成本與現有 Phase 0 實作。
- 設定新快捷鍵時先驗證註冊成功，再釋放舊快捷鍵；失敗時保留舊設定。
- 首次註冊失敗時保留 tray，顯示一次非阻擋提醒與設定入口；不得靜默切換到候選快捷鍵。
- 包含 Windows 鍵或被 Windows 保留的組合（例如 `Win+R`）直接拒絕，讓 Windows 原生行為保持不變。
- `WH_KEYBOARD_LL` 不納入 MVP；若未來重新評估，必須另立效能、安全與使用者同意規格。

## 來源

- WOX 官方 GitHub README：<https://github.com/Wox-launcher/Wox>
- WOX 官方現行 Introduction：<https://wox-launcher.github.io/Wox/guide/introduction.html>
- WOX 官方舊版 Basic Usage（包含 Win+R replacement 與 hotkey 設定）：<https://doc.wox.one/en/basic/>
- Microsoft `RegisterHotKey`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey>
- Microsoft `SetWindowsHookExW`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw>
- Microsoft `LowLevelKeyboardProc`：<https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc>
- Microsoft `KBDLLHOOKSTRUCT`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct>
- Microsoft「Disabling Shortcut Keys in Games」（低階 hook 範例）：<https://learn.microsoft.com/en-us/windows/win32/dxtecharts/disabling-shortcut-keys-in-games>
