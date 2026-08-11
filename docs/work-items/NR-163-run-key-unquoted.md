# NR-163 — 開機自動啟動的 Run 值未加引號：含空格路徑被 Explorer 誤解析

Phase 3 · Security · Depends on: —

- Source: `AGENTS.md`（Launch apps through Windows Shell APIs… Never build an
  arbitrary command line from search input——同族：不應產生可被誤解析的
  命令字串）、Microsoft Run-key 文件（Explorer 以
  `CreateProcessW(lpApplicationName=nullptr)` 執行該字串）
- Origin: 2026-08-11 第十四次稽核第 5 輪（claude backend，IMPORTANT；high
  confidence）。主 Agent 已核對 `startup_option.cpp:41-58` 驗證。
- Priority: **IMPORTANT**——安裝於含空格路徑（如
  `%LOCALAPPDATA%\Programs\Nimble Run\NimbleRun.exe`）時，登入即啟動失效，
  且同 user 攻擊者可在空格截斷的前綴目錄放置同名 exe 在每次登入取代
  NimbleRun 執行（logon-time 程式替換；有別於已接受的偽造 WM_APP DoS
  家族——這是程式碼執行面不是 DoS）。

## Why

`SetStartupEnabled(true)`（`src/settings/startup_option.cpp:53-55`）把
`CurrentModulePath()` 的裸路徑寫成 HKCU Run 值。Explorer 登入時以
`CreateProcessW(lpApplicationName = nullptr, lpCommandLine = <該值>)`
執行：文件化的空格啟發式——對含空格的路徑會先嘗試空格前的截斷字串
（`C:\Program Files\Nimble Run\NimbleRun.exe` 會先試
`C:\Program Files\Nimble.exe`）。後果二選一：(a) 無同名檔時啟動失敗
（使用者勾了「Launch at startup」卻沒生效）；(b) 前綴目錄 user-writable
時被同 user 進程放同名 exe 而每次登入執行**攻擊者的程式碼**。

`CurrentModulePath()`（`:13-27`）回傳原始路徑，全檔無任何引號處理；
`tests/unit/startup_option_test.cpp` 斷言值等於裸 module path——測試需同步
更新。

## Decisions already made — do not reopen

1. **寫入 `L'"' + path + L'"'`**（Run 值的標準引號形式），`RegSetValueExW`
   一行變更。
2. 更新 `startup_option_test.cpp` 的期望值（含引號）。
3. 不改登錄路徑、不改值名稱（`kRunValueName`）、不改 disabled 分支的
   `RegDeleteValueW`。
4. 不做 PATH/引號以外的進階防禦（Run 值就是命令字串，引號是該格式的
   正規做法；不做二次防禦如重新驗證可執行檔簽名——超出本 item）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Launch apps through Windows Shell APIs. Never build an arbitrary command line from search input.

## Files to read and trace first

- `src/settings/startup_option.cpp:41-58`（寫值點）。
- `tests/unit/startup_option_test.cpp`（期望值斷言）。

## Scope

1. `enabled` 分支：`path` 外包引號後寫入。
2. 測試期望值改為引號形式。
3. 回歸：startup_option 測試全綠；Release build 零新增 warning；CTest 全綠
   （數量不變）。

## Non-goals

- 不處理其他 Registry 值（值名稱、base/subkey 不變）。
- 不為「安裝位置含空格」建立安裝程式約束或文件。

## Acceptance

1. 寫入的 REG_SZ 值為 `"<path>"`（前後引號）。
2. 測試斷言與之相符；CTest 全綠（數量不變）；Release build 零新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R startup_option --output-on-failure
```

```powershell
rg -n "RegSetValueExW" src/settings/startup_option.cpp
# expect: 寫入值含前後引號
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、更新的測試斷言與 build／CTest 結果。

### 交接區（2026-08-11，實作完成）

改兩檔：`src/settings/startup_option.cpp`（8 行）與
`tests/unit/startup_option_test.cpp`（8 行）。

**`startup_option.cpp` enable 分支（:53-60）：** 新增
`const std::wstring quoted = L"\"" + path + L"\"";`，`RegSetValueExW` 改寫入
`quoted`（位元組數依 `quoted.size() + 1` 計算，含 NUL）。附一行註解說明 Run 值
是命令字串、含空格路徑須加引號才能通過命令列 tokenizer（正規做法，非新增防禦）。
disabled 分支的 `RegDeleteValueW`、登錄路徑與 `kRunValueName` 未動。

**測試更新：** 新增 helper `QuotedModulePath()`（`L"\"" + ModulePath() + L"\""`）；
兩處期望值斷言改為 `value == QuotedModulePath()`——`TestEnableCreatesEntry`
（:158）與 `TestRecreateAfterMove`（:203，re-enable 重寫路徑案例）；`WriteValue`
（模擬 stale path 用）未動。

**驗證結果：** rg 確認 `RegSetValueExW` 僅 enable 分支一處（:57）、寫入值為
`quoted`（前後引號）；Release build 零新增 warning（0 warnings/errors）；
CTest 31/31 全綠（數量不變）；`ctest -R startup_option` 1/1 綠。無偏離 item
決策。
