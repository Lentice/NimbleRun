# NR-069 — GetStartupStatus must treat the Run value as untrusted input (no OOB write, no throw)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-012（開機啟動）／§11（失效與復原）
- Origin: 2026-08-07 第三次全 repo 稽核（registry 讀取）

## Why

`src/settings/startup_option.cpp:70-77`：

```cpp
std::wstring value(size / sizeof(wchar_t), L'\0');
status = RegQueryValueExW(key, kRunValueName, nullptr, &type,
                          reinterpret_cast<BYTE*>(value.data()), &size);
...
value.resize(value.find(L'\0'));
```

`RegQueryValueExW` **不保證 REG_SZ 帶 NUL 結尾**（MSDN 明載；`cbData` 只說
「bytes」，不含終止字元是合法的），而 HKCU\...\Run 值任何同使用者 process
都能寫。兩個缺陷：

1. **無 NUL 的偶數 byte 值**：buffer 每個 wchar 都被覆寫（`size/2` 個字元），
   `find(L'\0')` 回傳 `npos` → `resize(npos)` 拋 `std::length_error` → 未捕捉 →
   **process 終止**。
2. **奇數 byte 值**：配置 `(size/2) * 2 = size - 1` bytes，卻告訴 API 有 `size`
   bytes → **1-byte 越界寫**（`RegQueryValueExW` 會寫滿 `cbData` bytes）。

目前 `GetStartupStatus` 只有測試在呼叫（grep 全 repo 確認 production 無 caller），
所以是潛在的；一旦接上 UI（例如設定頁顯示啟動狀態）就變成可被本機任意軟體
觸發的 crash。`SetStartupEnabled` 無此問題（byte count 含 NUL，正確）。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **buffer 配置 `size / sizeof(wchar_t) + 1` 並以 `L'\0'` 預填**——同時修掉
   奇數 size（API 寫入 ≤ 實際容量）與無 NUL 兩案；`find` 找不到時截斷到
   讀取長度（`size / sizeof(wchar_t)`）作為 fallback，不拋例外。
2. **返回值不變**：不把「無 NUL／怪異值」升級成 `UnknownError`——它仍是合法的
   Run 值（只是沒有 NUL），截斷後照常與 module path 比較；比較不中就是
   `EnabledMoved`，語意不變。**不新增列舉值。**
3. **測試直接覆蓋**：`startup_option_test` 有 HKCU 測試機（
   `StartupOptionRegistry{base, subkey}` seam），可以用 `RegSetValueExW` 寫出
   無 NUL 與奇數 byte 兩種值，呼叫 `GetStartupStatus` 斷言不 crash、回傳合理值。
4. **不做重試、不清理別人的 Run 值**。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Never simplify away: input validation at trust boundaries, error handling that prevents data loss.
- New non-trivial logic needs one focused runnable test or self-check.

design-spec §FR-012（引用項目所依的條文，如有）：

> - 開機啟動以 HKCU Run 值實作（不寫 Startup folder）。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/settings/startup_option.cpp:59-85` — `GetStartupStatus`。**本 item 只改
  這裡的 buffer 配置與 resize。**
- `src/settings/startup_option.h` — `StartupOptionRegistry` seam 與
  `StartupStatus` 列舉。
- `tests/unit/startup_option_test.cpp` — 既有測試的 registry fixture 寫法
  （HKCU\Software\NimbleRunTest\<pid>）。新 case 放這裡，不新增測試執行檔。

## Scope

### 1. 安全讀取

```cpp
std::wstring value(size / sizeof(wchar_t) + 1, L'\0');   // +1 給強制 NUL
status = RegQueryValueExW(key, kRunValueName, nullptr, &type,
                          reinterpret_cast<BYTE*>(value.data()), &size);
RegCloseKey(key);
if (status != ERROR_SUCCESS) {
    return StartupStatus::UnknownError;
}
const std::size_t length = value.find(L'\0');
value.resize(length == std::wstring::npos ? size / sizeof(wchar_t) : length);
```

（`size` 在第二次呼叫後是 API 回寫的實際位元組數，`size / sizeof(wchar_t)` 是
「讀到的字元數」的保守上界；`find` 有結果就以結果為準。以現場程式碼為準，
維持 `type != REG_SZ` 的既有檢查。）

### 2. 測試

`startup_option_test` 新增兩個 case（fixture 與既有相同，寫入改用
`RegSetValueExW` 的原始 byte 形式）：

- **無 NUL 的 REG_SZ**：寫入 `L"abc"` 但 `cbData = 3 * sizeof(wchar_t)`（不帶
  終止字元）→ `GetStartupStatus` 正常回傳（不 crash；與 module path 不同 →
  `EnabledMoved`）。
- **奇數 byte 的 REG_SZ**：寫入 5 bytes（如 `L'a'` + 1 raw byte）→
  `GetStartupStatus` 正常回傳、不 crash（無越界寫）。

兩個 case 的目的都是「不 crash＋回傳合理值」，不要斷言具體回傳值以外的行為。

### 3. 更新 spec？

不需。§FR-012 描述的是功能層級；本 item 是讀取端的穩健性。

## How this stays maintainable

**registry 值是「同使用者可寫」的不受信輸入，讀取端用與
`atomic_text_file.h` 相同的態度處理**（buffer 上界＋終止字元防線）。`GetStartupStatus`
日後接上設定頁 UI 時，穩健性已經就位，不會把「別人的 Run 值」變成 crash 面。

## Non-goals

- **不新增 `StartupStatus` 值**（Decisions §2）。
- **不改 `SetStartupEnabled`**（已是對的）。
- **不清理／不修復非 NimbleRun 的 Run 值。**
- **不把 registry 讀取包進 `try/catch`**（防線應該讓它永不拋，不是接住後繼續）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋新增 2 case，仍在
   `nimblerun_startup_option_test` 內）。
2. 兩個新 case 通過：無 NUL 與奇數 byte 的 Run 值下 `GetStartupStatus` 不 crash、
   回傳合理值。

Manual：

3. 用 regedit 在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 寫一個
   帶引號路徑的 NimbleRun 值，執行 `GetStartupStatus` 相關路徑（或直接跑
   測試）確認 `Enabled` 判定仍正常（回歸）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_startup_option_test --output-on-failure
```

```powershell
# 強制 NUL 的 buffer 存在，且 resize 對 npos 有 fallback：
Select-String -Path src/settings/startup_option.cpp -Pattern 'sizeof\(wchar_t\) \+ 1|npos'
# expect: 各 1 處（buffer 配置＋resize fallback）

# 改動範圍：
git diff --name-only
# expect: src/settings/startup_option.cpp、tests/unit/startup_option_test.cpp
```

## 交接區

（實作者填寫：修改的位置、buffer 形狀、兩個新 case 的 fixture 寫法（raw byte 的
`RegSetValueExW` 呼叫）、建置與 CTest 結果、sanity greps、偏差、未完成事項。）
