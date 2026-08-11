# NR-161 — icon_worker OOM 路徑 use-after-free：Register 回 0 後解引用已銷毀的 IconResult

Phase 3 · Robustness · Depends on: —

- Source: `AGENTS.md`（Prefer the smallest working change）、NR-077/097/109
  （token 紀律與 OOM 不崩潰的既有決策——本 item 是補齊該承諾在
  `icon_worker` 側的缺口）
- Origin: 2026-08-11 第十四次稽核第 5 輪（claude backend，IMPORTANT；high
  confidence）。主 Agent 已核對 `handoff_registry.h:15-27`、
  `icon_worker.cpp:301-312` 驗證。
- Priority: **IMPORTANT**——heap 耗盡（大型 icon backlog、低記憶體）時
  `Register` 回 0，worker 接著讀取**已刪除**的 `IconResult`（含
  `std::move` 一個已釋放的 `std::wstring`，再從中 copy 到 dropped registry）：
  未定義行為，可能 AV 或損壞堆。僅 OOM 可達，但崩潰面比「默默放棄」糟。

## Why

`HandoffRegistry<T>::Register`（`src/win/handoff_registry.h:15-27`）以
by-value 收 `std::unique_ptr<T>`：`map_.emplace(token, std::move(value))`
拋 `bad_alloc` 時 `catch (...) { return 0; }`，`value` 仍持有物件、scope 結束
即 delete。`icon_worker.cpp:301-312`：

```cpp
std::unique_ptr<IconResult> owned(result);
const std::uintptr_t token = g_icon_handoffs.Register(std::move(owned));
const bool registered = token != 0;
if (!registered) {
    // 註解聲稱「The owned guard still owns an unregistered result」——是假的：
    // Register 已消耗（並銷毀）該物件。
    RememberDroppedRequest(request, std::move(result->encoded_key), ...);
    continue;
}
```

`result` 是懸垂指標；`std::move(result->encoded_key)` 讀並移動已釋放的
`std::wstring`（freed heap pointer + size），`RememberDroppedRequest` 再從中
copy。對照 `rebuild_pipeline.cpp:130-135`：同形狀但正確——回 0 後不碰
`result`。這是「註解斷言 invariant，實作沒有兌現」的第二例（與 NR-162
同源於 icon_worker 的 OOM/滿隊列邊界，但獨立缺陷）。

## Decisions already made — do not reopen

1. **在 `Register` 之前先把 key 抓出來**：`const std::wstring encoded =
   result->encoded_key;`（或先 move out），失敗分支只使用這個 local；
   絕不碰 `result`。
2. 修正誤導註解（「owned guard still owns」→ 事實：物件已被 Register
   消費）。
3. **不動 `Register` 的簽名**（改 `std::unique_ptr<T>&` 會改兩側語意，
   rebuild_pipeline 的既有正確用法不需要）；最小變更只在
   `icon_worker.cpp`。
4. 驗證點：`!registered` 分支不再解引用 `result`；`RememberDroppedRequest`
   收到的是移動前的 encoded key 副本。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/win/handoff_registry.h:15-27`（Register 的 by-value 消費語意）。
- `src/icons/icon_worker.cpp:296-318`（token 保送段與 `!registered` 分支）。
- `src/app_host/rebuild_pipeline.cpp:125-140`（正確的對照用法，回 0 不碰
  payload）。
- `tests/unit/icon_worker_test.cpp`（既有 dropped-request 測試形狀，回歸網）。

## Scope

1. `!registered` 分支：改用註冊前擷取的 `encoded` local；刪除對 `result`
   的一切解引用。
2. 修正 `:305-308` 的註解使其陳述事實。
3. 回歸：icon_worker 測試全綠；Release build 零新增 warning；CTest 全綠
   （數量不變）。

## Non-goals

- 不改 `Register` 簽名；不動 rebuild_pipeline 的既有正確用法。
- 不為 OOM 路徑新增模擬測試（不可測）；以 grep + 既有測試回歸為驗證。

## Acceptance

1. `icon_worker.cpp` 的 `!registered` 分支不再出現 `result->`。
2. 既有 icon_worker 測試全綠；Release build 零新增 warning；CTest 全綠
   （數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R icon_worker --output-on-failure
```

```powershell
rg -n -B 3 -A 6 "if \(!registered\)" src/icons/icon_worker.cpp
# expect: 分支內無 result-> 解引用；encoded local 先行擷取
```
