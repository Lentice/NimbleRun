# NR-123 — Rebuild thread 的 join 無界：hung Shell call 使關閉／換代卡死 UI 執行緒（§9.4）

Phase 3 · Host lifecycle · Depends on: NR-049, NR-077, NR-098（皆 done）

- Source: `docs/design-spec.md` §9.4（關閉不得無限卡住）、§16（Shell extension hang 風險）
- Origin: 2026-08-10 第十三次全 repo 稽核（正確性軸）；主 Agent 已驗證 `JoinRebuildThreads` 呼叫點
- Priority: **MEDIUM**（機率低但後果是常駐程式無法關閉；NR-098 已知天花板，本 item 只補 shutdown 路徑）

## Why

`StartRebuild`（`main.cpp:1611`）與 `WM_DESTROY`（`:3459`）都先呼叫 `JoinRebuildThreads()`
（`:1587-1596`）無界 join 上一輪 rebuild worker。`g_rebuild_cancel`（NR-098 的 atomic 旗標）
只在枚舉器的**迭代邊界**被檢查；任一 worker 卡在單次不可中斷的 Shell 呼叫內
（`IShellLinkW::Load`、`SHGetKnownFolderItem`、第三方 Shell extension——spec §16 明列為可能）時，
join 永不等完：

- **Ctrl+R／設定套用**（StartRebuild 前的 join）：UI 執行緒卡死，面板完全無回應。
- **WM_DESTROY**（關閉程式）：tray Exit 後視窗不消失，process 賴著不走。

違反 §9.4「關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑」。第四輪稽核曾把
「icon worker `Stop()` 的 join 無限等待」記為已知限制（Shell 呼叫不可中斷、process 結束時 OS 會回收
執行緒），本次稽核確認 **rebuild thread 的 join 同形**且沒有被任何既有 item 覆蓋（NR-098 是
合作式取消，只涵蓋「可中斷的掃描」）。

## Decisions already made — do not reopen

1. **只改 shutdown 路徑（WM_DESTROY）**：Ctrl+R／設定套用前的 join 維持原樣（它有 supersede 語意，
   NR-118 決策 §4 明訂完整 rebuild 的 caller 可取代在途 generation；換代時 join 上一輪是既有契約）。
   若實作者量測發現 Ctrl+R 路徑也需 bounded wait，另開 item，不在本 item 範圍。
2. **bounded wait 的形狀**：`WaitForMultipleObjects` 對 worker thread handles 帶逾時（建議 5 s）；
   逾時後放棄 join，直接繼續退出流程。安全論證（交接區必須載明）：
   - worker 只碰 `settings_snapshot` 值拷貝、`g_rebuild_cancel` atomic 與 handoff token registry
     （NR-049 的捕獲清單契約）；
   - 放棄 join 後 process 立即退出，OS 回收執行緒，無靜態解構競賽（NR-049 的 detach 問題只在
     非退出路徑成立）；
   - 遲到的 `kRebuildDoneMessage` 在 registry 清空後以未知 token 被忽略（NR-077 語意），不 crash。
3. **不用 `TerminateThread`**：在持 COM apartment／Shell 鎖的執行緒上強制終止可能 deadlock 整個
   process，比放棄 join 更糟。
4. §9.4 若需同步描述 bounded-wait 行為，在設計 spec 的關閉條文補一句「等待有界，超時即繼續退出」，
   屬本 item 交付的一部分。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4：

> 關閉不得因等待 Shell extension 無限卡住；worker join 應有可控取消路徑。

`AGENTS.md`：

> Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

- `src/app_host/main.cpp` — `JoinRebuildThreads`（`:1587-1596`）、`StartRebuild`（`:1600-1611`）、
  `WM_DESTROY`（`:3459` 一帶）、`g_rebuild_threads` 容器。
- `docs/work-items/NR-049-rebuild-thread-lifetime.md`、NR-098 — worker 捕獲清單契約與取消語意。
- `src/app_host/message_loop.h`、token registry（NR-077）— 遲到訊息的忽略語意。

## Scope

1. `WM_DESTROY` 的 join 改 bounded wait（建議 5 s）：逾時則放棄剩餘 join 並繼續
   （registry 清空、資源釋放、process 退出）。
2. 保持 Ctrl+R 路徑的 join 行為不變；`StartRebuild` 的 supersede 語意不變。
3. 新增或沿用測試：若實作者能把「bounded wait」抽成可測的純函式（逾時參數注入）則加 focused 測試；
   否則依 NR-060 先例以 sanity grep＋lifecycle check 覆蓋（hang 注入不可自動化）。
4. 若 §9.4 補句，grep 確認 spec 條文與實作一致。

## Non-goals

- 不用 `TerminateThread`；不中斷或取消已卡住的 Shell 呼叫本體。
- 不改 Ctrl+R／設定套用／首輪的 join 行為；不改 `g_rebuild_cancel` 語意。
- 不重開 NR-049（detach→join 的 UAF 修補）／NR-098（合作式取消）的既有決策。

## Acceptance

1. 正常路徑：關閉程式的 join 行為與現況相同（暖機下近 no-op）。
2. worker 卡死（人工注入或實測）時：Exit 後最多約 5 s 視窗消失、process 退出，不 crash。
3. 釋放後的任何遲到訊息不 crash（NR-077 語意驗證）。
4. Release build 無新增 warning；完整 CTest 與 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "JoinRebuildThreads|WaitForMultipleObjects|TerminateThread" src
# expect: join 只在 WM_DESTROY 有界；TerminateThread 零命中。
git diff --name-only
```

## Handoff

實作者需記錄 bounded-wait 形狀與逾時值、安全論證（worker 捕獲清單／registry／遲到訊息）、
Ctrl+R 路徑未動的確認、是否補了 §9.4 條文、build／CTest 證據。

## 交接區（2026-08-10，實作完成）

### bounded-wait 形狀與逾時值

`JoinRebuildThreads`（`src/app_host/main.cpp:1611`）加參數 `DWORD timeout_ms = INFINITE`：

- **逾時值**：`constexpr DWORD kRebuildJoinTimeoutMs = 5000;`（`:1610`，5 秒）。
- **形狀**：先照舊 `g_rebuild_cancel.store(true)`（NR-098 的 cancel→wait 順序不變）；收集 `joinable`
  worker 的 `native_handle()` 成 vector，以 `WaitForMultipleObjects(handles, TRUE, timeout_ms)`
  一次等待全部（至多 3 條 worker，遠低於 64 上限）。
  - 逾時（或 wait 失敗）→ 對仍 joinable 的 worker 逐一 `detach()` 再 `clear()`，**不回存**
    `g_rebuild_cancel`，直接返回讓 WM_DESTROY 繼續退出流程（registry 清空、資源釋放、
    PostQuitMessage 皆在後續既有程式碼）。
  - 全部完成（`WAIT_OBJECT_0`）→ 逐一 `join()`、`clear()`、`g_rebuild_cancel.store(false)`，
    與原本行為等價。
  - `timeout_ms == INFINITE`（StartRebuild 呼叫）不呼叫 `WaitForMultipleObjects`，直接走 join
    迴圈——與原實作逐行相同。

### 安全論證

- **worker 捕獲清單契約（NR-049）**：worker 只碰 `settings_snapshot` 值拷貝、`g_rebuild_cancel`
  atomic、`g_handoff_mutex`＋`g_rebuild_handoffs` token registry 與既有 `g_diag`（皆在既有
  mutex 保護或 exit-path 可忽略）；不碰 `g_settings`／`g_refresh` 的可變狀態。逾時放棄 join 後
  **process 立即退出**，OS 回收執行緒——NR-049 的 detach 問題只在非退出路徑成立，本路徑
  （WM_DESTROY 逾時→退出）不符合該情境。
- **不用 TerminateThread**：零命中（見下），避免在持 COM apartment／Shell 鎖的執行緒上強制
  終止造成全 process deadlock。
- **遲到訊息（NR-077）**：放棄 join 的 worker 若稍後才 `PostMessageW(kRebuildDoneMessage)`，
  `kRebuildDoneMessage` handler（`:2997-3000`）查不到 token 即 `return 0` 忽略；若視窗已銷毀則
  message 直接丟棄；`g_rebuild_handoffs.clear()` 與後到的 insert 以 `g_handoff_mutex` 序列化，
  最壞情況是 registry 在 process 退出時尚有殘留物件（OS 回收），無 crash。
- **detach 而非 clear() joinable thread**：逾時後若直接 `clear()`，`std::thread` 解構對
  joinable 執行緒會 `std::terminate`；先 `detach()` 使其 non-joinable 再 clear 才是正確形狀。

### Ctrl+R 路徑未動的確認

- `StartRebuild`（`:1664`）仍呼叫 `JoinRebuildThreads()`（無引數 → `INFINITE`），supersede 語意
  （NR-118 決策 §4）與 NR-098 的 cancel→join 順序完全不變；`g_rebuild_cancel` 的 set／reset 時機
  與語意未改。`TerminateThread` 零命中；未新增 thread、timer 或 polling。

### §9.4 條文

有補。`docs/design-spec.md` §9.4 末句改為「關閉不得因等待 Shell extension 無限卡住；worker join
應有可控取消路徑。**等待有界，超時即繼續退出。**」未動 §9.4 以外條文。實作與條文一致
（grep 確認：WM_DESTROY 的 join 為 bounded）。

### 測試覆蓋方式

依 Decisions §3 的 NR-060 先例以 sanity grep＋lifecycle check 覆蓋：bounded wait 依賴真實 OS
thread handle，無法抽出有意義的純函式（抽純函式只會把「是否 detach」的決策與真實 wait 切割成
人工的抽象），hang 注入（卡死單次不可中斷 Shell call）亦不可自動化。既有
`nimblerun_lifecycle_check`（tray Exit 後行程須正常結束）即為 shutdown 生命週期檢查，有界等待
不影響其正常路徑。

### build／CTest 證據

- `cmake -S . -B build-wi-nr123 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`
  → configure 成功。
- `cmake --build build-wi-nr123` → 成功，**0 warning 0 error**（`-Wall -Wextra -Wpedantic`）。
- `ctest --test-dir build-wi-nr123 --output-on-failure` → **26/26 全綠**（含
  `nimblerun_lifecycle_check` 7.44 s、`nimblerun_catalog_refresh_test` 1.08 s）。
- sanity grep：`JoinRebuildThreads` 定義 1＋StartRebuild 無界呼叫 1＋WM_DESTROY bounded 呼叫 1；
  `WaitForMultipleObjects` 1（join helper 內，另 `MsgWaitForMultipleObjectsEx` 是既有 message loop）；
  `TerminateThread` **零命中**。

### 偏差

- 無。未動 `docs/work-items.md`、其他 src 檔案、其他 docs/；未執行 git 命令。
