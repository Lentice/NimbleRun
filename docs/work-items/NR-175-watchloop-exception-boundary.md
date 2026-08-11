# NR-175 — WatchLoop thread body 無最外層例外邊界，配置失敗逃出 std::thread 呼叫 std::terminate

Phase 2 · Catalog watcher · Depends on: —

- Source: `docs/design-spec.md` §11（worker 例外捕捉邊界）、NR-097 先例
  （worker setup／handoff exception boundary——只蓋了 thread 建立，沒蓋 body）
- Origin: 2026-08-11 第十六次稽核第 4 輪（codex backend，MINOR）。主 Agent
  已重讀 `catalog_watcher.cpp:55-109` 驗證。
- Priority: **LOW**——OOM 才觸發；但每個 catalog root 一支 watcher 執行緒，
  任何一支 body 內未捕捉例外都使整個常駐 process `std::terminate`。

## Why

`WatchLoop`（`src/app_host/catalog_watcher.cpp:55`）是
`CatalogWatcher::Start` 建立的 `std::thread` entry：

```cpp
void WatchLoop(std::shared_ptr<CatalogWatcher::Watch> watch) {
    std::vector<BYTE> buffer(kBufferBytes);   // :56 64 KiB 配置，thread 內第一個動作
    const HANDLE completion = CreateEventW(...);  // :57
    ...
```

`std::thread` 的 entry 若拋出未捕捉例外，標準行為是 `std::terminate`。
`:56` 的 `vector<BYTE>` 配置（64 KiB）、`:57` 之後的 `shared_ptr` 拷貝與後續
STL 操作都可能拋 `std::bad_alloc`；NR-097 只處理「thread 建立失敗」，
沒有涵蓋 thread body 的配置失敗。watcher 是常駐背景執行緒（每 root 一支、
`g_watcher->SetRoots` 建立），body 例外 = 常駐 process 直接終止。

## Decisions already made — do not reopen

1. **`WatchLoop` body 包最外層 `try/catch (...)`**：catch 內靜默 return
   （該 watcher 結束）。欄位 `watch->pending_notify.store(0)` 或類似的
   收尾可選——最小做法是直接 return，host 的 pending-intent 恢復機制
   （NR-105）依賴正常迴圈，OOM 情境下不需保留。
2. **捕捉後 post 一次 full-rescan 通知**（讓 host 知道該來源失聯）——
   選修：若 post 本身可拋（不會，PostMessageW 是 C API），維持 catch 內
   只 return 的乾淨形狀。決策：catch 內只 return，不 post（OOM 下 post
   無意義，且 watcher 死亡後 host 的 watcher error 回饋機制已存在）。
3. 不新增測試 seam（OOM 不可注入，依 NR-076/167/174 先例）。
4. 不改 `WatchLoop` 的正常路徑、不改 `Stop()`／`CancelIoEx` 收尾。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> Worker 發生例外 → UI 不崩潰；捕捉邊界、記錄並丟棄。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

（本 item 是補洞非新邏輯；不新增測試 seam。）

## Files to read and trace first

- `src/app_host/catalog_watcher.cpp:55-109`（WatchLoop 本體與 `Stop()` 的
  join 互動——確認 catch return 後 `Stop()` 的 `join()` 正常完成）。
- `src/app_host/catalog_watcher.h`（Watch 結構、Start/Stop）。
- `tests/unit/catalog_watcher_test.cpp`（既有測試確認正常路徑不受影響）。

## Scope

1. `WatchLoop` body 包最外層 `try/catch (...)`（catch 內 return）。
2. 既有 watcher 測試全綠（行為不變）。

## Non-goals

- 不新增診斷事件、不新增 UI 字串、不新增設定。
- 不重試（OOM 重試無意義）；不新增 timer。
- 不改 `PostNotification`、`Stop`、`CancelIoEx`。

## Acceptance

1. `WatchLoop` 有最外層 catch（code review 斷言）；`catalog_watcher_test` 全綠。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "watcher" --output-on-failure
```

```powershell
rg -n -A3 "void WatchLoop" src/app_host/catalog_watcher.cpp
# expect: body 有最外層 try/catch
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置與 build／CTest 結果。

## 交接區

- 改動檔案：`src/app_host/catalog_watcher.cpp`（1 處，`WatchLoop` 本體）。
- 改動內容：`WatchLoop`（:55）本體自 `std::vector<BYTE> buffer(kBufferBytes)`
  （:57）起至 `CloseHandle(completion)`（:166）包進最外層 `try { ... }`；
  新增 `catch (...)`（:167-171）內僅 return，無 post、無 log、無任何其他
  動作——該 watcher 執行緒結束，`Stop()` 的 `join()` 正常完成。
  改動僅為縮排搬移＋例外邊界；`WatchLoop` 正常路徑、`Stop()`、`CancelIoEx`、
  `PostNotification`、所有簽章、測試均未變動。
- Build／CTest（Release x64, LLVM-MinGW + Ninja）：設定與 build 成功；
  全數 31/31 tests passed（數量與先前一致）；`-R "watcher"` 1/1 passed。
- Warning：本改動零新增 warning（build 唯一 warning 為 `main.cpp:1410`
  unused variable `target_size`，NR-174 交接區已記錄為改動前即存在）。
- Sanity grep：
  ```
  55:void WatchLoop(std::shared_ptr<CatalogWatcher::Watch> watch) {
  56-    try {
  57-        std::vector<BYTE> buffer(kBufferBytes);
  58-        const HANDLE completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ```
  （body 有最外層 try/catch，符合 Acceptance 1。）
- 提交：`037bb50`（NR-175: bound watcher thread exceptions at the entry）。
- 偏差：無。
