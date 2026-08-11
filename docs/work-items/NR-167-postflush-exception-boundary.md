# NR-167 — IconWorker::PostFlush 的 queue_.push_back 缺少例外邊界，bad_alloc 穿越 window proc 終止 process

Phase 3 · Icon worker · Depends on: —

- Source: `docs/design-spec.md` §11（worker 例外捕捉邊界）、NR-076／NR-097／
  NR-109 先例（icon worker 的 exception boundary 補洞系列）
- Origin: 2026-08-11 第十六次稽核第 2 輪（codex backend，MINOR）。主 Agent
  已重讀 `icon_worker.cpp:120-170`、`main.cpp:938` 驗證。
- Priority: **LOW**——bad_alloc 才能觸發，但與 NR-076/097/109 修掉的
  「worker 例外終止 process」是同一類；`Post()` 有 catch、`PostFlush()` 漏掉，
  是該系列的明確遺漏。

## Why

`IconWorker::Post()`（`src/icons/icon_worker.cpp:120-145`）把整段 enqueue 包在
`try/catch (...) { return false; }` 裡——「可選功能失敗只退回 fallback」的既有
設計。但 `PostFlush()`（`:147-170`）沒有例外邊界：`queue_.push_back(std::move(task))`
（`:166`）若拋出 `std::bad_alloc`，例外直接向上傳。

呼叫路徑：`HidePanel`（`main.cpp:938` 一帶）在 window proc 的訊息處理中呼叫
`g_icon_worker->PostFlush(pins, now)`。整個 `WindowProc` 無 try/catch（repo
已知事實，NR-139 曾確認），bad_alloc 一路穿過 Win32 window procedure 到
`std::terminate`，殺死常駐 tray process。flush 是 best-effort 快取持久化，
失敗的正確行為是丟棄並記錄，不是終止 process。

`PostFlush` 的 `queue_.size() < kMaxQueuedTasks` 守門只限制元素數，擋不住
deque 配置本身拋例外；`Post()` 已示範正確形狀。

## Decisions already made — do not reopen

1. **`PostFlush` 整個函式包 `try/catch (...)`**（與 `Post()` 同形），失敗時
   靜默丟棄這次 flush（回傳 void，不改變既有簽章）——flush 可由下次
   hide/idle/stop 時機重試，無狀態遺失（pinned list 每次 hide 重取）。
2. 不記錄診斷（`Post()` 的先例就是不記錄；bad_alloc 期間寫 log 可能再拋）。
3. 不新增測試 seam（enqueue 失敗不可注入，沿用 NR-076 的「不為測試加
   注入點」先例）；以 code review 與既有 `icon_worker_test` 回歸覆蓋。
4. 不改 `Post()`、不改 queue 型別、不新增上限。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11（診斷與例外）：

> Worker 發生例外 → UI 不崩潰；捕捉邊界、記錄並丟棄。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

（本 item 的變更是複製 `Post()` 既有邊界，非新邏輯；不新增測試 seam。）

## Files to read and trace first

- `src/icons/icon_worker.cpp:120-170`（`Post` 的既有 catch 形狀與 `PostFlush`）。
- `src/icons/icon_worker.h`（`PostFlush` 宣告與註解）。
- `src/app_host/main.cpp:938` 一帶（`HidePanel` 的呼叫點）。
- `tests/unit/icon_worker_test.cpp`（既有 flush 測試，確認回歸網存在）。

## Scope

1. `PostFlush` 本體包 `try/catch (...)`，catch 內 return（void），無其他動作。
2. 不改簽章、不改呼叫端、不改 `Post`、不改佇列語意。

## Non-goals

- 不新增診斷事件、不新增 UI 字串、不新增設定。
- 不處理 `cv_.notify_one()`（不拋例外，且在 lock 外）。
- 不為 flush 失敗重試排程（既有三時機已覆蓋）。

## Acceptance

1. `PostFlush` 與 `Post` 同樣具備完整例外邊界（code review 斷言）。
2. `icon_worker_test` 全綠；Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon" --output-on-failure
```

```powershell
rg -n -A4 "void IconWorker::PostFlush" src/icons/icon_worker.cpp
# expect: try/catch (...) 包住 enqueue；catch 內靜默 return
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置與 build／CTest 結果。

### 交接區（2026-08-11，opencode）

**改動檔案**：僅 `src/icons/icon_worker.cpp`（另有本次 docs 交接）。

**改動內容**：`IconWorker::PostFlush`（原 `:147-170`）本體整段包入
`try { ... } catch (...) { /* NR-167 註解，無任何動作 */ }`，與 `Post()`
（`:141-143`）同形。catch 內不 log、不 rethrow、無診斷事件；enqueue
（`queue_.push_back` / `*existing = std::move(task)`）與 `cv_.notify_one()`
皆在 try 內。簽章、呼叫端（`main.cpp:938` `HidePanel`）、`Post()`、
queue 型別、NR-099 就地取代邏輯與既有註解均未變動。

**build／CTest**（Release，llvm-mingw + Ninja）：

- `cmake --build build`：成功，零新增 warning（icon_worker.cpp 重編無警示）。
- `ctest --test-dir build --output-on-failure`：**31/31 全綠**（數量不變）。
- `ctest --test-dir build -R "icon" --output-on-failure`：**5/5 全綠**
  （icons_cache / icon_pack_format / icon_store / icon_worker /
  icon_request_session）。

**Agent check grep 結果**：

```
147:void IconWorker::PostFlush(std::vector<std::wstring> pinned_ids, std::uint64_t now_utc) {
148-    try {
149-        IconTask task;
150-        task.kind = IconTaskKind::Flush;
151-        task.pinned_ids = std::move(pinned_ids);
```

try 包住 enqueue，catch 內靜默 return——符合預期。

**偏差**：無。catch 內加了一行 NR-167 說明註解（repo 慣例：每個 catch
附 NR 出處註解；僅為文件性文字，無任何執行期動作）。未新增測試：
item 明令不新增測試 seam（enqueue 失敗不可注入，沿用 NR-076 先例）。
