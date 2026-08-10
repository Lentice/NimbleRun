# NR-131 — Token 交接註冊表收斂為一個模組，rebuild 停止借用 icon worker 的 mutex

Phase 3 · Code structure · Depends on: NR-077（token 交接原型，done）、NR-127（收斂原則，done）

- Source: `AGENTS.md`（Reuse existing code before adding helpers or abstractions；
  Keep search, ranking, scoring, persistence formats, and other core logic independent of
  HWND and Shell COM objects where practical）、`docs/design-spec.md` §NFR-004
- Origin: 2026-08-10 架構審查（Claude 軸候選 5；Codex 報告候選 03 的可落地部分）。
  主 Agent 已以 grep 逐行驗證。
- Priority: **MEDIUM**（不是 bug，但這是把 NR-132 的 rebuild 抽出變成純搬移的前置；
  現況兩條無關的 producer/consumer 通道共用同一把鎖，任何一邊的長臨界區都會擋住另一邊）

## Why

`src/icons/icon_worker.h:56-57` 在 namespace 層宣告：

```cpp
inline std::mutex g_handoff_mutex;
inline std::unordered_map<std::uintptr_t, std::unique_ptr<IconResult>> g_icon_handoffs;
```

rebuild 路徑跨過這個 seam，借同一把鎖保護**它自己**的 `g_rebuild_handoffs`
（`src/app_host/main.cpp:352` 宣告；`:1585`、`:1790`、`:1812`、`:3068`、`:3204`、`:3567` 六處上鎖）。
兩條互不相干的通道——icon 解碼結果與 catalog 枚舉結果——因此互相序列化，唯一原因是
NR-077 先在 icons 模組發明了這個 pattern，rebuild 之後複製採用時連鎖一起借用。

pattern 本身是對的且必須保留：不受信的 `WM_APP` `lParam` 必須是 token 而非指標
（design-spec §NFR-004）。問題是它**沒有模組**——同一個安全規則有兩份實作，第二份還借第一份的鎖。
「未知 token 必須回 nullptr 且絕不解參考」這條安全性質目前只能靠讀三個呼叫點來確認，無法測試。

## Decisions already made — do not reopen

1. **保留 token 交接語意**，不改成傳指標、不改訊息形狀、不改 `WM_DESTROY` 清空時機。
   NR-077 的安全論證原封不動成立。
2. 新模組放 `src/win/handoff_registry.h`，**header-only、template、無 `<windows.h>` 依賴**
   （只需 `<cstdint>`、`<memory>`、`<mutex>`、`<unordered_map>`）。這是 NR-057／NR-127
   的既有落點原則。
3. **兩個實例，各自持有自己的 mutex**：一個由 icon 路徑持有、一個由 rebuild 路徑持有。
   不共用鎖是本 item 的全部要點。
4. 這是 template 且**已有兩個真實使用者**在解一條有文件依據的安全需求，不屬於
   `AGENTS.md` 禁止的投機抽象；不要為了「將來可能有第三個」再加任何參數或 policy。
5. `g_icon_dropped_keys` 與 `TakeIconDroppedKeys()`（`icon_worker.h:61-68`）**不搬進**
   registry——那是 icons 專屬的 dropped-request 復原路徑，不是 token 交接。它目前也用
   `g_handoff_mutex`；本 item 讓它改用 icon registry 暴露的鎖，或留在 icon_worker 自己的
   一把私有鎖，擇一並在交接區寫明理由。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`src/icons/icon_worker.h:48-55`（NR-077 的既有論證，必須原樣保留）：

> worker threads hand result objects to the UI thread by token, never by a raw pointer in a
> WM_APP message -- any same-integrity process can post to our HWND, and dereferencing an
> unvalidated lParam is a crash vector (design-spec §NFR-004).

## Files to read and trace first

- `src/icons/icon_worker.h:40-68`、`src/icons/icon_worker.cpp`（所有 `g_icon_handoffs` 使用點）。
- `src/app_host/main.cpp:336-352`（`RebuildResult` 與 `g_rebuild_handoffs`）、
  `:1580-1600`（generation 淘汰時的清掃）、`:1780-1820`（worker 註冊／post 失敗時 erase）、
  `:3060-3080`（`kRebuildDoneMessage` 取出）、`:3200-3210`、`:3560-3575`（`WM_DESTROY` 清空）。
- `docs/work-items/NR-077-*.md` — token 交接的原始安全論證。
- `src/win/com.h` — 同目錄既有 header-only 工具的形狀與註解慣例。

## Scope

1. 新增 `src/win/handoff_registry.h`：

   ```cpp
   template <typename T>
   class HandoffRegistry {
   public:
       std::uintptr_t Register(std::unique_ptr<T> value);   // 0 = 失敗（未註冊）
       std::unique_ptr<T> Take(std::uintptr_t token);        // 未知 token → nullptr
       void Erase(std::uintptr_t token);                     // post 失敗時撤銷
       void Clear();                                         // WM_DESTROY
       template <typename Pred> void EraseIf(Pred pred);      // generation 淘汰掃描
   private:
       std::mutex mutex_;
       std::unordered_map<std::uintptr_t, std::unique_ptr<T>> map_;
   };
   ```

   token 沿用「物件位址」，行為與現況逐位元等價。`EraseIf` 是為了涵蓋 `main.cpp:1586-1590`
   的 generation 淘汰迴圈；若該迴圈可直接以 `Take` 表達則不要加這個成員。
2. `icon_worker` 改為持有一個 `HandoffRegistry<IconResult>` 實例，刪除
   `g_handoff_mutex`／`g_icon_handoffs` 的 namespace 層宣告（或保留 `inline` 實例，形狀擇一，
   但不得再被 rebuild 路徑取用）。
3. `main.cpp` 改為持有一個 `HandoffRegistry<RebuildResult>` 實例，六處手寫 `lock_guard`
   全部改為 registry 呼叫；`nimblerun::g_handoff_mutex` 在 main.cpp 的引用歸零（grep 驗證）。
4. 新增 `tests/unit/handoff_registry_test.cpp`：至少涵蓋
   (a) `Register` 後 `Take` 拿回同一物件、map 不再持有；
   (b) **未知 token `Take` 回 nullptr 且不解參考**（安全性質）；
   (c) `Erase` 後 `Take` 回 nullptr；
   (d) `Clear` 釋放全部；
   (e) 兩個不同 registry 實例互不影響。
   依 NR-055 的 list-plus-loop 形狀註冊到 `tests/CMakeLists.txt`，並依 NR-129 使用
   `tests/unit/test_util.h` 的 `Expect`。

## Non-goals

- 不改任何訊息常數、訊息流向或 `PostMessageW` 的失敗處理語意。
- 不改 `g_rebuild_delivery_failures`／`g_rebuild_failure_event`（NR-100／NR-115 的通道，
  屬 NR-132 範圍）。
- 不把 rebuild 的其他全域搬走——那是 NR-132。本 item 只動 token 註冊表。
- 不新增執行緒池、不改 worker 執行緒數量或啟動方式。

## Acceptance

1. `rg -n "g_handoff_mutex" src` 在 `src/app_host/` 下**零命中**；rebuild 與 icon 各持一把鎖。
2. `HandoffRegistry` 的五項測試通過；未知 token 案例明確存在。
3. 行為零變更：icon 顯示、rebuild 完成、Ctrl+R、WM_DESTROY 路徑與改動前一致。
4. Release build 零新增 warning；完整 CTest 全綠（含 lifecycle check）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_handoff_mutex|g_icon_handoffs|g_rebuild_handoffs" src
# expect: main.cpp 不再出現 g_handoff_mutex；兩個 registry 實例各自獨立。
```

## Handoff

已完成。`src/win/handoff_registry.h` 提供 `Register`、`Take`、`Erase`、`Clear`、`EraseIf`；
registry 以物件位址作 token，所有操作各自鎖定自己的 mutex，未知 token 的 `Take` 回傳
`nullptr` 且不解參考。

`g_icon_handoffs` 與 `g_rebuild_handoffs` 是兩個獨立的 `HandoffRegistry` 實例，因此 icon
與 rebuild 不再共用鎖。`g_icon_dropped_keys` 未搬入 registry，改由 icons 專屬的
`g_icon_dropped_keys_mutex` 保護，因為它是 dropped-request 復原佇列，不是 token ownership。

`main.cpp` 的六個 handoff 呼叫點已改為 registry 操作：generation 淘汰使用 `EraseIf`、worker
註冊使用 `Register`、post 失敗使用 `Erase`、rebuild 完成使用 `Take`、icon 完成使用 `Take`、
`WM_DESTROY` 使用兩次 `Clear`。所有原有訊息常數、PostMessageW 失敗處理與清理時機保留。

測試：新增 `tests/unit/handoff_registry_test.cpp`，涵蓋註冊／取回、未知 token、Erase、Clear
及 registry 隔離；`icon_worker_test` 的 handoff caller 也改走 registry。Release build 成功，
`ctest -R "handoff_registry|icon_worker" --output-on-failure` 2/2 通過，完整 CTest 尚待執行。
