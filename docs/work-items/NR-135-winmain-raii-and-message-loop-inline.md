# NR-135 — `wWinMain` 的 RAII 收尾（`HandleGuard`＋既有 `ComGuard`）與刪除 `message_loop.h`

Phase 3 · Code cleanup · Depends on: NR-051（`ComGuard`，done）、NR-117（`ShouldDispatchMessage`，done）

- Source: `AGENTS.md`（Prefer the smallest working change. Reuse existing code before adding
  helpers or abstractions）、`docs/development.md`
- Origin: 2026-08-10 架構審查（Claude 軸候選 3 與 6a）。主 Agent 已讀原始碼確認。
- Priority: **LOW**（無使用者可見風險；但六段手抄的 `CloseHandle` 序列會隨每個新 early return
  繼續長，而 `message_loop.h` 是 repo 內「為了可測而抽出、真正的風險留在隔壁沒測」的樣板案例）

## 覆寫聲明

本 item **覆寫 NR-117 的抽出決策**（把 `ShouldDispatchMessage` 抽成獨立 header＋獨立測試）。
NR-117 修的 bug 是真的、修法的**行為部分**保留不動；被覆寫的只是「抽成模組」這個形狀。
新證據：抽出後的模組全文只有 `return get_message_result > 0;`，套用刪除測試——刪掉它，
複雜度**零轉移**：呼叫端的 message-loop 判斷變成 `if (get_result <= 0) break;`，比
`if (!nimblerun::ShouldDispatchMessage(get_result)) break;` 更短也更直白。而 NR-117 的實際風險
（`-1` 分支的 logging 與收尾）**那個測試根本沒碰到**——抽出提供的是假覆蓋率。

## Why

**(a) `wWinMain` 的手抄收尾。** 六個 early return 各自手抄自己的
`CloseHandle(mutex)`／`CloseHandle(startup_ready)`／`CoUninitialize()` 序列，而序列會隨流程變長；
第七個 return 要寫三行。repo **已經有**對的工具在隔壁一層——`src/win/com.h` 的 `ComGuard`——但
啟動段的 `CoInitializeEx` 沒用它，
且沒有對應的 `HandleGuard`。

**(b) `message_loop.h`。** 一個 header、一個公開 `nimblerun::` 符號、一段 CMake target、
一筆 CTest 註冊、一個測試檔，全部為了 `> 0`。

## Decisions already made — do not reopen

1. `HandleGuard` 放 `src/win/`，緊鄰 `ComGuard`，形狀比照它（RAII、no-copy、
   `explicit`、`Get()`／`operator bool`）。**不做泛型 handle traits、不做 policy 參數**。
2. `wWinMain` 改用 `ComGuard` 與 `HandleGuard`（`mutex`／`startup_ready`），
   六段收尾塌成 `return 1;`／`return 0;`。**銷毀順序必須與現況等價**——`ComGuard` 的
   `CoUninitialize` 相對於視窗銷毀與 worker join 的先後不得改變，實作時逐一比對。
3. 刪除 `src/app_host/message_loop.h`、`tests/unit/message_loop_test.cpp` 與其 CMake target；
   message loop 內聯為 `if (get_result <= 0) break;`，並把 NR-117 的三結果註解
   （0＝WM_QUIT、-1＝失敗、>0＝可 dispatch）搬到內聯處，**保留 NR 編號**。
4. **CTest 編號**：移除項放清單末端，避免既有測試註冊順序不必要地位移，
   並同步 `docs/testing.md` 的測試數與 release-evidence 計數。
5. 本 item **不碰** `main.cpp` 的 ~28 個子系統全域與它們的 null check（那是獨立候選，
   已記在 `docs/work-items.md` 的候選清單，且應排在 NR-132／NR-134 之後才動）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> 必須保持既有 build／CTest 可用；不得用關閉測試來取得綠燈。

`src/win/com.h:19-25`（NR-051 的既有論證）：`SUCCEEDED` 涵蓋 `S_OK` 與 `S_FALSE`，
`RPC_E_CHANGED_MODE` 可用但不得配對 `CoUninitialize`。新 guard 不得破壞這條。

## Files to read and trace first

- `src/app_host/main.cpp` 的 `wWinMain` 啟動段（六個 early return 與 COM 初始化）、
  message loop 段（`GetMessageW` 與 `-1` 分支）。
- `src/win/com.h` 全部。
- `src/app_host/message_loop.h`、`tests/unit/message_loop_test.cpp`、
  `tests/CMakeLists.txt` 的清單與例外 target 註冊區。
- `docs/work-items/NR-117-message-loop-getmessage-error.md`（被覆寫的那部分）。
- `docs/testing.md`（測試數）。

## Scope

1. 新增 `src/win/handle_guard.h`（或加進 `src/win/com.h`，擇一並寫明理由）。
2. `wWinMain` 改用 `HandleGuard`＋`ComGuard`；六段手抄收尾刪除。
3. 刪除 `message_loop.h`、其測試與 CMake target；`main.cpp` 內聯比較並搬移註解。
4. 同步 `docs/testing.md` 的測試數；重產 release evidence（比照 NR-126 的做法）。

## Non-goals

- 不改任何啟動流程語意、不改 NR-130 的 rendezvous timeout MessageBox 行為、
  不改 single-instance 邏輯。
- 不動 `-1` 分支的 logging 與收尾行為（本 item 只刪抽出的空殼，不改行為）。
- 不引入其他 RAII 包裝（`HWND`、`HFONT`、D2D 物件都不在範圍內）。
- 不動 `full_rescan_throttle.h`（屬 NR-132）。

## Acceptance

1. `wWinMain` 內 `CloseHandle`／`CoUninitialize` 的手寫呼叫歸零（grep 驗證）。
2. `rg -n "message_loop|ShouldDispatchMessage" src tests` 零命中。
3. `docs/testing.md` 的測試數與實際 CTest 數一致。
4. 行為零變更：single-instance 第二實例、COM 初始化失敗、視窗建立失敗、正常關閉四條路徑
   與改動前一致（lifecycle check 通過即算 Agent 側證據）。
5. Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "message_loop|ShouldDispatchMessage" src tests
rg -n "CloseHandle|CoUninitialize" src/app_host/main.cpp
# expect: 前者零命中；後者只剩 guard 以外的正當用途（若有，逐條說明）。
```

## Handoff

### 實作與順序

- `HandleGuard` 落在 `src/win/handle_guard.h`：non-copy、`explicit` 建構子、`Get()`、
  `operator bool`，只在非 null handle 上呼叫 `CloseHandle`。`wWinMain` 用它管理
  `startup_ready`、`mutex` 與測試 semaphore；`OpenTestShowSemaphore` 改回傳 handle，
  由 guard 接管所有權。
- 六段 early return 與正常收尾均改為 scope cleanup；`ERROR_ALREADY_EXISTS`、COM 初始化失敗、
  window register/create 失敗與正常 message-loop 結束的行為分支不變。message loop 將
  `ShouldDispatchMessage` 內聯為 `get_result <= 0`，保留 NR-117 的 `-1` 診斷。
- 正常收尾仍先做 window/D2D/accessibility cleanup、清除測試 semaphore 全域指標、
  `g_rebuild_pipeline.reset()`，再 `com.reset()` 呼叫 `CoUninitialize()`；因此 COM 收尾
  仍早於 function-scope 的 `IconWorker` destructor/worker join，與改動前相同。其餘 handle
  guard 在 scope exit 關閉，且所有 early return 都由 guard 自動收尾。

### 測試與文件證據

- 移除 `nimblerun_message_loop_test` 後，live CTest registration 由 32 降為 31；
  `docs/testing.md` 保持 31，測試清單順序只在移除項之後遞減一筆。
- `pwsh -NoProfile -File tests/release/release_evidence.ps1 -OutPath docs/release-evidence.md`
  已重產 evidence：CTest registration 31、full suite 31/31 全綠。腳本 exit 2 僅因既有
  NFR-001 blocking metrics 未量測而回報 `INCOMPLETE`，不是 build 或 CTest 失敗。
- Release build 成功且無新增 warning；`git diff --check` 通過。完整 CTest 由 release evidence
  記錄為 31/31，另包含 lifecycle check。
