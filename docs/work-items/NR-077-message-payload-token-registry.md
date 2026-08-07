# NR-077 — `kRebuildDoneMessage` / `kIconReadyMessage` must not dereference an unvalidated `lParam` as a heap pointer

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §NFR-004（安全：輸入不可直接當指標／命令）／§11
- Origin: 2026-08-08 第四次全 repo 稽核（main.cpp 全檔，untrusted-input 軸）

## Why

`kRebuildDoneMessage`（`WM_APP+8`）與 `kIconReadyMessage`（`WM_APP+9`）的處理器把
`lParam` 直接 `reinterpret_cast` 成堆積指標後立即解參考：

```cpp
// main.cpp:2315-2316
std::unique_ptr<RebuildResult> result(
    reinterpret_cast<RebuildResult*>(l_param));
if (result->failed) {          // deref of lParam
```
```cpp
// main.cpp:2411-2413
std::unique_ptr<nimblerun::IconResult> result(
    reinterpret_cast<nimblerun::IconResult*>(l_param));
g_pending_icon_keys.erase(result->encoded_key);   // deref of lParam
```

這些是**私有的 `WM_APP` 訊息**，任何同 integrity（medium）的 process 都能
`PostMessage` 到我們的視窗（HWND 可經 `EnumWindows` 找到；UIPI 只擋較高 integrity
的送訊者）。`lParam = 0` 或垃圾值 → 立即解參考 → **常駐 tray process 當場崩潰**。
這是目前整條訊息路徑上唯一的不受信輸入 crash 向量。稽核新增發現。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **改用 UI 側 token registry 取代「訊息裡扛裸指標」**：結果物件的擁有權改由 UI
   執行緒持有的 registry（mutex 保護）保存，訊息只扛 token（`lParam` 或 `wParam`）
   ——`lParam` 從此不被信任。形狀：
   - UI 側：`std::mutex g_handoff_mutex;`＋兩個 map
     `g_rebuild_handoffs`／`g_icon_handoffs`（`std::unordered_map<std::uintptr_t,
     std::unique_ptr<...>>`，或共用一個 tagged struct）。token 用指標位址即可（
     唯一且註冊在案）。
   - 送訊端（`StartRebuild` lambda／`IconWorker::Run`）：鎖下插入
     `map[ptr] = unique_ptr(...)`，再 `PostMessageW(..., reinterpret_cast<LPARAM>(ptr))`；
     `PostMessageW` 失敗時鎖下 `erase` 並 delete（維持既有防洩漏語意）。
   - 接收端：鎖下 `find((uintptr_t)l_param)`，命中才 `move` 出並 `erase`；未命中
     → `return 0` 忽略。命中後其餘處理一字不改。
   - `WM_DESTROY` 的兩段 `PeekMessageW` 排空（`main.cpp:2796-2815`）照舊拉掉佇列中的
     訊息，之後各 `lock`＋`clear()` 對應 map，釋放所有在途 payload（含「已插入但
     訊息還沒被處理」者）。
2. **minimal 且與既有風格一致**：token 就是指標位址（不引進遞增計數器），map 用
   `std::unique_ptr` 保存擁有權（與現有 `std::unique_ptr<...> result(...)` 同一套
   RAII）。新增一個 mutex 是為了 worker→UI 的「插入」需要跨執行緒；此 mutex 只在
   handoff 的插入/查詢點觸碰，非熱路徑。
3. **不做完整性檢查（UIPI 之外）的其他攔截**：不驗證送訊者 PID、不記錄未知 token
   （避免被灌爆日誌）。
4. **測試**：receiver 側「未知 token 不崩潰」可由 message-only window 的既有測試
   形狀覆蓋（照 `icon_worker_test` 的 `PostMessageW` 用法，直接對視窗 post 一個
   不存在的 token，斷言 process 存活且結果數不變）；或由 code review＋sanity grep
   覆蓋。送訊端插入/失敗刪除的形狀由 grep 守。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.
- Never simplify away: input validation at trust boundaries.

design-spec §NFR-004：

- 使用者點擊的項目必須對應 Catalog 中已解析的 launch identity；不把輸入當成命令列
  或 URI 執行。（本 item 的精神：不把不受信訊息的欄位直接當指標。）

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2311-2346`（`kRebuildDoneMessage`）、`:2406-2427`
  （`kIconReadyMessage`）— 接收端主場。
- `src/app_host/main.cpp:1280-1286`（`StartRebuild` 的 post＋失敗 delete）與
  `src/icons/icon_worker.cpp:158-161`（同形）— 送訊端主場。
- `src/app_host/main.cpp:2785-2827`（`WM_DESTROY` 的兩段排空＋資源清理）— drain 主場。
- `src/icons/icon_worker.cpp:104-173` — worker 送訊上下文（`PostMessageW` 在
  `:158`）；**只讀**（若實作把 token 放進現有 `Post` 流程）。
- `tests/unit/icon_worker_test.cpp` — message-only window 用法範例。

## Scope

### 1. UI 側 registry（`src/app_host/main.cpp` 檔案範圍）

宣告一個 `std::mutex g_handoff_mutex;` 與兩個 map：

```cpp
// NR-077: worker threads hand result objects to the UI thread by token, never
// by a raw pointer in a WM_APP message -- any same-integrity process can post
// to our HWND, and dereferencing an unvalidated lParam is a crash vector.
std::mutex g_handoff_mutex;
std::unordered_map<std::uintptr_t, std::unique_ptr<nimblerun::RebuildResult>> g_rebuild_handoffs;
std::unordered_map<std::uintptr_t, std::unique_ptr<nimblerun::IconResult>> g_icon_handoffs;
```

（型別名以實際宣告位置為準；若兩者同為檔案範圍匿名 namespace 內型別，照搬。）

### 2. 送訊端

- `StartRebuild` lambda：`new RebuildResult` 後、post 前：
  `{ std::lock_guard<std::mutex> lock(g_handoff_mutex); g_rebuild_handoffs[(uintptr_t)result] = std::unique_ptr<RebuildResult>(result); }`
  post 失敗時在 lock 內 `erase`＋delete（維持 NR-063 防洩漏語意）。
- `IconWorker::Run`：同形插入 `g_icon_handoffs`；post 失敗時 lock 內 erase＋delete。

### 3. 接收端

兩個處理器各在解參考前查 registry；未命中直接 `return 0`：

```cpp
case kRebuildDoneMessage: {
    std::unique_ptr<nimblerun::RebuildResult> result;
    {
        std::lock_guard<std::mutex> lock(g_handoff_mutex);
        const auto it = g_rebuild_handoffs.find(static_cast<std::uintptr_t>(l_param));
        if (it == g_rebuild_handoffs.end()) {
            return 0;   // NR-077: unknown token -- not one of our handoffs
        }
        result = std::move(it->second);
        g_rebuild_handoffs.erase(it);
    }
    // ...其餘照舊（result->failed 等）...
}
```

`kIconReadyMessage` 同形查 `g_icon_handoffs`。

### 4. `WM_DESTROY` 排空

兩段 `PeekMessageW(... PM_REMOVE)` 排空後，各加：

```cpp
{ std::lock_guard<std::mutex> lock(g_handoff_mutex); g_rebuild_handoffs.clear(); }
{ std::lock_guard<std::mutex> lock(g_handoff_mutex); g_icon_handoffs.clear(); }
```

（順序與既有停 worker／join rebuild 的順序配合，確保排空後沒有在途 payload。）

### 5. 測試

- `icon_worker_test`（或既有 message-only window 測試檔）新增：對視窗 `PostMessageW`
  一筆不存在的 token（例如 `lParam = 1`）的 `kIconReadyMessage` → 斷言不 crash、
  不改變既有結果計數。若既有測試檔的 HWND 可取得，直接加 case；否則以 code review
  覆蓋（由實作決定，交接區載明）。

### 6. 更新 spec？

不需。§NFR-004 的精神本就如此；行為層級未動。

## How this stays maintainable

「訊息欄位一律不信」成為接收端的第一步；擁有權集中於 UI 側 map，`WM_DESTROY` 時
一次清空所有在途 payload，比「訊息佇列自己扛生命週期」更接近 §9.4 的關閉語意。日後
新增 worker 照同形註冊即可。

## Non-goals

- **不驗證送訊者 PID／integrity，不記錄未知 token。**
- **不引入遞增 token 計數器**（指標位址已唯一）。
- **不改 `RebuildResult`／`IconResult` 型別與其餘處理邏輯。**
- **不動 `IconWorker` 的佇列／mutex（`mutex_`）結構。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項＋新增 case）。
2. 未知 token 的 `PostMessageW` 不崩潰、被忽略。

Manual：

3. 以 PowerShell＋user32 P/Invoke 對執行中的 NimbleRun 主視窗 `PostMessage` 一筆
   `WM_APP+8`／`WM_APP+9`、`lParam = 0`：process 存活、無 crash、面板操作正常。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 兩個接收端都先查 registry 再解參考：
Select-String -Path src/app_host/main.cpp -Pattern "g_rebuild_handoffs|g_icon_handoffs"
# expect: 宣告＋送訊插入＋接收查詢＋WM_DESTROY clear 各至少 1 處

# 送訊端 post 失敗時維持 erase＋delete：
Select-String -Path src/app_host/main.cpp -Pattern "g_rebuild_handoffs.erase"
# expect: 至少 1 處

# 改動範圍：
git diff --name-only
# expect: src/app_host/main.cpp、src/icons/icon_worker.cpp、
#         可能的 tests/unit/icon_worker_test.cpp
```

## 交接區

（實作者填寫：registry 的實際宣告位置與型別、送訊/接收/drain 的鎖範圍、未知 token
測試的寫法、建置與 CTest 結果、sanity greps、偏差、未完成事項。）

實作（2026-08-08）：

- **registry 宣告位置**：`g_handoff_mutex` 與 `g_icon_handoffs` 以 `inline` 全域宣告
  在 `icons/icon_worker.h`（`IconResult` 的所在地，`icon_worker.cpp` 與 `main.cpp`
  都 include 它，C++17 inline variable 保證單一實例）；`g_rebuild_handoffs`
  （`RebuildResult` 是 main.cpp 的全域型別）宣告在 `main.cpp` 檔案範圍，與 icon map
  共用同一個 `nimblerun::g_handoff_mutex`。兩 map 型別皆
  `std::unordered_map<std::uintptr_t, std::unique_ptr<...>>`，token＝物件位址。
- **送訊端**：`StartRebuild` lambda 與 `IconWorker::Run` 各在 `PostMessageW` 前 lock
  下 `map[ptr] = std::unique_ptr(...)`；post 失敗在 lock 內 `erase`（unique_ptr
  析構即 delete，維持 NR-063 防洩漏語意）。
- **接收端**：兩個 case 各在解參考前 lock 下 `find((uintptr_t)l_param)`，未命中
  `return 0`，命中 `std::move(it->second)`＋`erase(it)` 後其餘處理一字不改。
- **`WM_DESTROY` drain**：因 map 持有 unique_ptr 擁有權，兩段 `PeekMessageW` drain
  的 `delete reinterpret_cast<...>` **必須移除**（否則與 map 雙重釋放）；drain 只
  PM_REMOVE 拉掉佇列訊息，之後單一 lock 下 `clear()` 兩 map 一次釋放所有在途
  payload。這是對 item 範例的必要調整。
- **測試**：`icon_worker_test` 的三個結果消費點（`PumpResults`／`AnyResultIn`／
  `TestStopDropsQueueAndSilencesNewPosts` 的手工 drain）改為從 registry move-out
  （與 production receiver 同形）；新增 `TestUnknownTokenIgnored`——post 一筆
  `lParam=1` 的未註冊 token → 不被消費為結果、process 存活、後續真實請求照常。
- **建置與 CTest**：Release build 無新增警告；`ctest` 23/23 全綠。
- **sanity greps**：`g_rebuild_handoffs|g_icon_handoffs` 於 main.cpp＝宣告 1＋送訊
  插入 1＋送訊失敗 erase 1＋接收查詢 2＋WM_DESTROY clear 2；`g_rebuild_handoffs.erase`
  至少 1；tests 下再無 `reinterpret_cast` 成結果型別的裸指標轉換。
- **偏差**：registry 放 `icon_worker.h`（非 main.cpp 檔案範圍），因 `IconWorker::Run`
  與接收端跨 TU 都要看到同一個 map，這是「共用全域」的唯一可行位置；item 已預留
  「以實際宣告位置為準」。`git diff`＝main.cpp、icon_worker.cpp、icon_worker.h、
  icon_worker_test.cpp（多出 icon_worker.h，屬預期的宣告搬移）。未完成事項：無。
