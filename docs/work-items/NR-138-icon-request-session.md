# NR-138 — Icon 請求狀態收斂為 `IconRequestSession`（兩個 set 全域、六個散點、兩份形狀不同的「該不該發？」）

Phase 3 · Code structure · Depends on: NR-012、NR-032、NR-099、NR-109、NR-114（皆 done）

- Source: `AGENTS.md`（Keep search, ranking, scoring... independent of HWND and Shell COM
  objects where practical；New non-trivial logic needs one focused runnable test）、
  `docs/design-spec.md` §FR-009（lazy loading）
- Origin: 2026-08-10 架構審查第二輪（opencode 軸候選 2＋候選 3）。主 Agent 已讀原始碼確認。
- Priority: **LOW**（無已知 bug；形狀與 NR-136 同型——UI 執行緒側的純狀態穿著 Win32 外衣。
  NR-131 收的是 worker 側的交接註冊表，這是**沒被搬走的 UI 側另一半**）

## Why

「同一個 key 在途中只發一次請求；本次面板 session 內失敗過的 key 不重發；
dropped key 在下次顯示前排空」這條不變式，由兩個 `std::set<std::wstring>` 全域
（`main.cpp:243` `g_pending_icon_keys`、`:247` `g_requested_icon_keys`）加上 `main.cpp` 內
六個互不相鄰的位置共同持有：

| 位置 | 做的事 |
|---|---|
| `:1019-1037` `PrewarmEmptyStatePage` | 檢查 **cache＋pending＋requested** 後決定是否 `Post` |
| `:1042-1046` `ClearDroppedIconRequests` | 從 `TakeIconDroppedKeys()` 排空 pending |
| `:1188-1202` `RequestVisibleIcon` | 檢查 **pending＋requested** 後決定是否 `Post` |
| `:2477-2481` `ShowPanel` | 重置 requested（讓暫時性失敗下次開啟可重試） |
| `:3190-3225` `kIconReadyMessage` | 收到結果後更新兩個 set |
| `icon_worker.h:61-68` `TakeIconDroppedKeys` | worker 側的 dropped 通道 |

兩處「該不該發？」的判斷**形狀不同**（Prewarm 多查 cache，因為 `RequestVisibleIcon` 的呼叫端
`Render` 已經查過）。現況不是 bug，但它是同一個決策的兩種寫法，且改動其中一處的 agent
看不到另一處。整條不變式埋在 HWND message handler 裡，**無法測試**。

附帶（opencode 候選 3）：`PrewarmEmptyStatePage`（`:1015-1027`）拿到的是裸 `stable_id`，
再對 `g_refresh->Snapshot()` 做線性掃描把每個 id 找回來——O(prewarm × catalog) 次字串比較，
而 `PanelModel` 的 `rows_` 本來就持有那些 `AppEntry`（`RefreshRows` 從同一份 snapshot 建的）。
這個來回讓 snapshot 變成相對於 model rows 的第二個真相來源。

## Decisions already made — do not reopen

1. 落點 `src/app_host/icon_request_session.{h,cpp}`（或 `src/icons/`，擇一並寫明理由），
   **HWND-free、無 Shell COM**，編成可測的 library。
2. 介面五個成員，不多：
   ```cpp
   bool ShouldRequest(const std::wstring& encoded_key, bool cached) const;
   void BeginRequest(const std::wstring& encoded_key);
   void OnResult(const std::wstring& encoded_key, bool ok);
   void OnShow();                                   // 重置 requested
   void DrainDropped(const std::vector<std::wstring>& dropped_keys);
   ```
   `cached` 由呼叫端傳入——**這就是統一兩處判斷的方式**：`Render` 路徑傳
   「已查過、未命中」，Prewarm 路徑傳 `IconCache::Peek` 的結果。不要在模組內持有 cache 指標。
3. **行為零變更**：pending 不在 show 時清（那些請求還在飛，`:241-242` 的既有註解）、
   requested 在每次 show 清（暫時性失敗可重試）——兩條原封搬移，註解帶走。
4. prewarm 改為由 `PanelModel` 直接回傳 **entries**（`Rows()` 上的 range 或 copy），
   missing-pin 過濾留在 model 內；host 端刪掉那圈線性掃描。這是本 item 的第二個 scope 項，
   因為兩者都落在 `PrewarmEmptyStatePage` 同一段碼，分開做會改同一行兩次。
5. 不動 worker 側：`IconWorker`、`IconStore`、`IconCache`、`shell_icon_provider` 一律不碰
   （§已否決的方向已否決 icons 疊層重寫）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Keep search, ranking, scoring, persistence formats, and other core logic independent of
> HWND and Shell COM objects where practical.

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`main.cpp:240-247`（既有規則，不得反悔）：pending 永不在 show 時清除（請求仍在途中）；
requested 每次 show 清除（暫時性失敗下次重試）。

## Files to read and trace first

- `src/app_host/main.cpp`：`:238-248`（兩個全域）、`:1010-1046`（`PrewarmEmptyStatePage`、
  `ClearDroppedIconRequests`）、`:1185-1205`（`RequestVisibleIcon`）、
  `:2470-2485`（`ShowPanel` 重置）、`:3185-3230`（`kIconReadyMessage`）。
- `src/icons/icon_worker.h:40-68`（`IconResult`、dropped 通道）。
- `src/app_host/panel_model.{h,cpp}`：`EmptyStatePrewarmIds()`（`panel_model.cpp:210-233` 一帶）
  與 `Rows()`／missing-pin 過濾。
- `docs/work-items/NR-032`、`NR-099`、`NR-109`、`NR-114` 的 Decisions（icon 請求／掉落語意）。

## Scope

1. 新增 `IconRequestSession`，兩個全域搬入，六處散點改為呼叫成員。
2. `PanelModel` 新增回傳 prewarm **entries** 的成員（取代或並存於 `EmptyStatePrewarmIds()`；
   若舊成員沒有其他呼叫者則刪除），`PrewarmEmptyStatePage` 刪掉線性掃描。
3. 新增 `tests/unit/icon_request_session_test.cpp`，**必測案例**：
   - 同一 key 在途中 `ShouldRequest` 第二次回 false（一 key 一請求）
   - 失敗過的 key 在同一 session 內不再請求；`OnShow()` 後可再請求
   - `cached=true` 時不請求
   - `DrainDropped` 後該 key 可被重新請求（pending 已排空）
   - `OnShow()` **不**清除 pending（在途請求不得被誤判為可重發）
4. `panel_model` 的 prewarm entries 加一個 model 測試（順序與 missing-pin 過濾不變）。
   兩者依 NR-055 的 list-plus-loop 註冊，依 NR-129 用 `test_util.h`。

## Non-goals

- 不改 icon 的解碼、快取、磁碟 pack 格式或 worker 執行緒模型。
- 不改 §FR-009 的 lazy loading 行為、不改 prewarm 的數量（24）或觸發時機。
- 不動 NR-131 的 `HandoffRegistry`（那是 worker 側，本 item 是 UI 側）。
- 不順手改 `Render` 的其他部分。

## Acceptance

1. `g_pending_icon_keys`／`g_requested_icon_keys` 在 `main.cpp` 歸零（grep 驗證）。
2. 新模組 grep 不到 `HWND`／`<windows.h>`／`IconCache*`。
3. `PrewarmEmptyStatePage` 內不再有對 `Snapshot()` 的線性掃描。
4. 五類 session 測試與一個 model 測試存在並通過；行為零變更。
5. Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "g_pending_icon_keys|g_requested_icon_keys" src/app_host/main.cpp
rg -n "EmptyStatePrewarmIds" src
# expect: 前者零命中；後者若保留需說明理由。
```

## Handoff

已完成。最終介面落在 `src/app_host/icon_request_session.{h,cpp}`：
`ShouldRequest(encoded_key, cached)` 統一 cache／pending／requested 判斷，
`BeginRequest` 記錄 visible request，`OnResult` 移除 pending 並記錄失敗，
`OnShow` 只清除本 session 的失敗集合，`DrainDropped` 排空 worker 回報的 dropped key。
模組只包含 `std::set`／`std::wstring`／`std::vector`，不依賴 HWND、Windows header、Shell 或
IconCache；另建 `nimblerun_icon_request_session` static library 供主程式與 focused test 共用。

六處散點改法：`PrewarmEmptyStatePage` 以 `ShouldRequest` 傳入
`g_icon_cache->Peek(encoded) != nullptr`；`ClearDroppedIconRequests` 將
`TakeIconDroppedKeys()` 交給 `DrainDropped`；`RequestVisibleIcon` 以 `ShouldRequest(encoded,
false)` 守門，worker `Post` 成功後呼叫 `BeginRequest`；`ShowPanel` 呼叫 `OnShow`；
`kIconReadyMessage` 以 bitmap 是否為空呼叫 `OnResult`；worker 的 dropped 通道維持既有
`TakeIconDroppedKeys` 純值 API，不改 worker。低優先 prewarm 不加入 pending，因既有 worker
只對 visible request 回報 dropped；這保留 CancelPrewarm／下次 hide 的原有語意。

`PanelModel::EmptyStatePrewarmEntries(max_items)` 直接複製 `rows_` 的第一頁並在 model 內
排除 missing-pin placeholder；因此保留 pinned-then-recent 順序、24 項上限與空 query 條件，
`PrewarmEmptyStatePage` 不再掃描 `g_refresh->Snapshot()`。舊的 `EmptyStatePrewarmIds` 已刪除。

測試新增 `tests/unit/icon_request_session_test.cpp` 五類斷言：in-flight 去重、失敗至下次
show 才重試、cached 不請求、dropped 可重試、show 不清 pending；既有
`panel_model_test` 改測 entries，並補 pinned-then-recent 與 missing-pin 過濾測試。
`tests/CMakeLists.txt` 依 list-plus-loop 註冊，CTest 數由 30 增至 31；`docs/testing.md` 已同步。

驗證：Release configure 成功；`cmake --build build` 成功且無新增 warning；focused
`ctest --test-dir build -R "icon_request_session|list_vertical_slice" --output-on-failure`
為 2/2 通過；升權環境完整 `ctest --test-dir build --output-on-failure` 為 31/31
通過。item grep checks 亦已確認：兩個舊 set 與 `EmptyStatePrewarmIds` 均零命中，
新模組不含 HWND、`<windows.h>` 或 `IconCache*`。
