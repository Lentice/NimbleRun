# NR-166 — ReadAllBytes 只回 bool，使 ReadVersionedLines 以 stale GetLastError 誤分類為 Missing

Phase 1 · Store read path · Depends on: —

- Source: `docs/design-spec.md` §11（不受信輸入）、§10.4（corrupt 保存與通知契約）、
  NR-058／NR-080／NR-122 先例（store 讀取端的失敗處置）
- Origin: 2026-08-11 第十六次稽核第 2 輪（codex backend，IMPORTANT）。主 Agent
  已重讀 `atomic_text_file.h:83-110,293-310` 驗證。
- Priority: **IMPORTANT**——同 user 可控的 `settings.ini`／`usage.tsv`／
  `favorites.txt`／`catalog.cache` 只要超過 16 MiB 讀取上限（或 ReadFile 中途失敗
  後 CloseHandle 覆寫 last error），在執行緒 last-error 恰殘留 2／3 時被誤判成
  `Missing`，繞過 `.corrupt` 保存與使用者通知，後續正常 Save 直接覆寫原檔。

## Why

`ReadAllBytes`（`src/storage/atomic_text_file.h:83-110`）只回傳 `bool`，失敗原因
完全依賴 Win32 thread-local last-error。但三條失敗路徑都不可靠：

1. **容量上限分支（`:101-104`）**：`out.size() + read > kMaxReadBytes` 時
   `ok = FALSE`，**從不 `SetLastError`**——last error 停留在先前任何 API 的
   殘留值。
2. **`ReadFile` 失敗分支（`:94-96`）**：break 後於 `:108` 先 `CloseHandle(file)`
   才回傳——`CloseHandle` 本身是 Win32 呼叫，成功時不保證保留 last error。
3. **`CreateFileW` 失敗**：這條是唯一可靠的（錯誤碼即來自 CreateFileW），
   但與前兩條共用同一 `return false` 出口，呼叫端無法分辨。

`ReadVersionedLines`（`:304-306`）在 `!ReadAllBytes` 後
`GetLastError()`，若殘留值恰為 `ERROR_FILE_NOT_FOUND`(2) 或
`ERROR_PATH_NOT_FOUND`(3) 就回 `Missing`。後果：超限/不可讀的既存檔被當成
「首次使用」——store `Load()` 回 `Missing` 而非 `Corrupt`，host 不
`PreserveCorrupt`、不通知（NR-058/§10.4 契約），且之後任何 `Save()`
（設定變更、usage 記錄、refresh pins）**直接覆寫原檔**，毀損證據與
「較新版本寫入」保護一併消失。四種 store 檔案全是同 user 可寫入的輸入面。

## Decisions already made — do not reopen

1. **`ReadAllBytes` 增加 `DWORD* error_out`（或回傳含錯誤碼的結果）**：在
   `CreateFileW` 失敗後立即保存 `GetLastError()`；`ReadFile` 失敗後在
   `CloseHandle` **之前**保存；容量超限分支明確 `SetLastError(ERROR_FILE_TOO_LARGE)`
   或直接寫出錯誤碼。呼叫端不再事後讀 thread-local last-error。
2. **容量超限分類為 `Unreadable`**（既有列舉值，各 store 的
   `PreserveCorrupt`＋通知處置已存在），不新增列舉值；「Missing」只保留給
   CreateFileW 真的回 2／3 的案例。
3. 呼叫端 `ReadVersionedLines` 改由 `error_out` 分流 `Missing`／`Unreadable`，
   一行變更；各 store 的 Load 行為與回傳值不變。
4. 新增 stale-last-error 回歸測試：先 `SetLastError(ERROR_FILE_NOT_FOUND)`，
   讀取超過 `kMaxReadBytes` 的既存檔，斷言結果**不是** `Missing`。

## Binding constraints — quoted, do not go looking for them

`docs/work-items/NR-058`（store 載入失敗必須到達使用者與日誌）：
「三個 store 精心回傳的載入結果列舉呼叫端一個都沒接…§11「設定損壞→採預設值並通知」
與 §10.4「較新 schema→顯示一次錯誤提示」兩條規格從未實作」。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/storage/atomic_text_file.h:83-110`（`ReadAllBytes`）、`:293-310`
  （`ReadVersionedLines` 的 GetLastError 分流）。
- 四個呼叫端的分類消費：`src/settings/settings_store.cpp:132-144`、
  `src/usage/usage_store.cpp:36-45`、`src/pins/pin_store.cpp`（同形 switch）、
  `src/catalog/catalog_cache.cpp`（Malformed 才 PreserveCorrupt 的差異分支）。
- `tests/unit/settings_store_test.cpp` 或 `settings_editor_test.cpp` 的
  `ReadVersionedLines` 直接案例（NR-057 加的那組）。

## Scope

1. `ReadAllBytes` 簽章增加錯誤碼出口；三條失敗路徑各自保存/設定錯誤碼。
2. `ReadVersionedLines` 的 `!ReadAllBytes` 分支改用出口錯誤碼分流
   Missing／Unreadable。
3. 測試：`SetLastError(ERROR_FILE_NOT_FOUND)` 後讀超限檔 → 非 Missing（建議
   放在 NR-057 的 `ReadVersionedLines` 直接案例旁）；既有各 store 測試全綠。
4. 行為契約：任何 store 的 `Load()` 回傳值、`PreserveCorrupt` 呼叫與通知
   語意**不變**——只有誤分類消失。

## Non-goals

- 不新增 `VersionedReadStatus` 列舉值（`Unreadable` 已覆蓋）。
- 不改各 store 的資料行解析、不 bump schema、不新增設定。
- 不處理 `CloseHandle` 失敗本身（close 失敗的診斷價值低於本 item 範圍）。

## Acceptance

1. 超限檔在 `SetLastError(ERROR_FILE_NOT_FOUND)` 的前提下回 `Unreadable`
   （測試斷言），既不誤報 `Missing` 也不誤報 `Loaded`。
2. 真正不存在的檔（`CreateFileW` 回 2）仍回 `Missing`（既有測試全綠證明）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings|usage|pin|catalog" --output-on-failure
```

```powershell
rg -n "GetLastError|ERROR_FILE_TOO_LARGE|error_out" src/storage/atomic_text_file.h
# expect: ReadAllBytes 不再依賴事後讀 last error；超限分支有明確錯誤碼
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。

## 交接區（NR-166 done, 2026-08-11）

**Files changed**

- `src/storage/atomic_text_file.h` — `ReadAllBytes`、`ReadVersionedLines`
- `tests/unit/settings_store_test.cpp` — 新增回歸測試
- `docs/work-items.md` — NR-166 狀態 `ready` → `done`

**Exact change**

- `ReadAllBytes(path, out)` → `ReadAllBytes(path, out, DWORD* error_out = nullptr)`。
  三條失敗路徑各自保存錯誤碼：`CreateFileW` 失敗立即 `*error_out = GetLastError()`；
  `ReadFile` 失敗在 `CloseHandle` 之前 `error = GetLastError()`；容量超限分支
  明確 `error = ERROR_FILE_TOO_LARGE`（不再依賴 thread-local last error，呼叫端
  也不需事後讀它）。成功時 `error_out` 不被寫入；僅失敗時寫入。未更動
  `VersionedReadStatus` 列舉、未更動任何 store 的 Load／PreserveCorrupt／通知語意。
- `ReadVersionedLines` 的 `!ReadAllBytes` 分支改讀 `error`（區域變數）分流
  `Missing`（僅 2／3）／`Unreadable`（其餘含 size cap），不再 `GetLastError()`。
- 新增測試 `TestReadVersionedLinesOversizeNotMissing`：先 `SetLastError(ERROR_FILE_NOT_FOUND)`
  污染 thread last-error，寫入 `kMaxReadBytes + 4096` 位元組的既有檔後
  `ReadVersionedLines` 斷言為 `Unreadable`（非 `Missing`），置於 NR-057 的
  `TestReadVersionedLines` 旁，並在 `wmain` 註冊。

**Test results**

- Release x64 build：成功。唯一警告為 `src/app_host/main.cpp:1410` 的
  pre-existing unused-variable `target_size`（本 item 未觸及該檔，非新增警告）。
- 全 CTest：31/31 passed（數量不變，與改動前一致）。
- `ctest -R "settings|usage|pin|catalog"`：10/10 passed。

**Sanity grep output**

```
41:    return GetLastError() == ERROR_ALREADY_EXISTS;
83:// NR-166: reports the failure reason through error_out instead of leaving the
87:// error_out is not touched; on failure it receives the captured code.
89:                         DWORD* error_out = nullptr) {
93:        if (error_out != nullptr) {
94:            *error_out = GetLastError();  // NR-166: capture before any other Win32 call
106:            error = GetLastError();  // NR-166: capture before CloseHandle may clobber it
114:            error = ERROR_FILE_TOO_LARGE;  // NR-166: explicit, never a stale last-error
121:    if (!success && error_out != nullptr) {
122:        *error_out = error;
```

（`:41` 是 `EnsureDirectory` 的既有 `GetLastError`，非本 item 範圍。）

**Deviations**

- 無。全部依 item decisions 實作；`error_out` 只在失敗時寫入（決策 1 允許
  「直接寫出錯誤碼」，成功路徑的寫入無呼叫端需要，故省略）。
