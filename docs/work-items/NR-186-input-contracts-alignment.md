# NR-186 — 輸入契約對齊：滾輪不搬選取、無對應項目時 Alt+digit 不吞鍵、搜尋長度上限、空白查詢 prewarm 判定一致

Phase 3 · Input contracts · Depends on: —（四項互不相干、同一主題「輸入與規格契約一致」；獨立於 NR-181~185）

- Source: `docs/design-spec.md` §4.7（現文 `design-spec.md:237`）／§4.8（現文 `design-spec.md:261-262`）／§4.3（空白查詢維持格狀）
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 報告 I-6；codex 報告 M3／L1／L3）
- Priority: **MEDIUM**（每項都是使用者可見的契約違反，各自小修）

## Why

四項輸入合約漂移，全部違反 spec 明文：

1. **滾輪偷改 Enter 目標（claude I-6）**：`PanelModel::ScrollBy`（`panel_model.cpp:188-197`）強制 `selected_ = first_visible_`。它同時服務 PgUp/PgDn（鍵盤翻頁帶著選取走，合理）與 `WM_MOUSEWHEEL`（`:2698`，滑鼠滾動）。§4.8 明文「hover 不改變選取…Enter 永遠啟動具備選取邊框的那一格」；滾一下滾輪、按 Enter 啟動的是新頁第一格，不是原本選著的那格——選取被無聲搬走。
2. **無對應項目的 Alt+digit 被吞（codex M3）**：`main.cpp:2387-2401`（WM_SYSKEYDOWN）與 `:2412-2420`（WM_SYSCHAR）只要 digit 就 `return 0`，即使 `RowForVisibleSlot(slot)` 回 -1。§4.7 明文「沒有對應項目的數字不綁定」——少於 10 個可見項、清單尾端、footer 空白處按數字，鍵該交給預設處理（系統 beep／其他 App）。
3. **搜尋輸入超過 1023 字元靜默截斷（codex L1）**：`main.cpp:2651` 每次 `EN_UPDATE` 固定讀 `wchar_t buffer[1024]`，EDIT 未設 `EM_LIMITTEXT`。貼上長字串在 UI 與 `PanelModel` 中靜默截斷，搜尋結果與使用者看到的字串不一致。
4. **空白查詢的 prewarm 判定漂移（codex L3）**：`panel_model.cpp:214` 用 `!query_.empty()`，而 `RefreshRows`（:53）用 `NormalizeName(query_).empty()`。輸入一個或多個空格：畫面維持格狀，卻不 prewarm 下一頁圖示 → fallback icon。同一規則兩份判定，漂移。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.7（現文 `design-spec.md:237`）：

> `Alt` ＋數字 … 依序指派給當前可見項目的前 10 個；**沒有對應項目的數字不綁定**

`docs/design-spec.md` §4.8（現文 `design-spec.md:256`、`design-spec.md:261`）：

> 格狀狀態下指標停在某格時，該格顯示淡填色並在 footer 顯示其路徑；**不改變鍵盤選取**。… 滾輪只在結果超過可見容量時捲動。

## Files to read and trace first

- `src/app_host/panel_model.{h,cpp}` — `ScrollBy`（:188-197）、`EmptyStatePrewarmEntries`（:210-233）、`RefreshRows`（:53）的 NormalizeName 判定、`ScrollBy` 的全部呼叫點。
- `src/app_host/main.cpp` — WM_SYSKEYDOWN／WM_SYSCHAR handler（:2373-2420）、`WM_MOUSEWHEEL`（:2698 一帶）、PgUp/PgDn（:2454-2459）、`EN_UPDATE`（:2648-2652）、`g_search_edit` 建立處（設 EM_LIMITTEXT 的位置）。

## Scope

1. **滾輪**：`ScrollBy` 加參數 `bool move_selection`（或拆出第二函式）；鍵盤路徑傳 true、滾輪傳 false。滾輪路徑之後不呼叫 `EnsureSelectionVisible`（§4.8 允許選取離開可見範圍；Enter 仍啟動那一格，方向鍵回來時 `MoveSelection` 既有的 `EnsureSelectionVisible` 自然恢復可見）。
2. **Alt+digit**：`WM_SYSKEYDOWN` 只有 `RowForVisibleSlot(slot) >= 0` 且完成啟動時才 `return 0`；無 row 時 fall through 讓系統處理。`WM_SYSCHAR` 的吞鍵分支同步（只在有對應項目時吞）。
3. **搜尋長度**：`g_search_edit` 建立後設一次 `EM_LIMITTEXT`（1023），讓截斷發生在使用者看得到的地方（與 buffer 上限一致，不放大上限）。
4. **prewarm 判定**：`EmptyStatePrewarmEntries` 改用 `NormalizeName(query_).empty()`（與 `RefreshRows` 同一規則；不新增 helper）。
5. 測試：
   - 滾輪不搬選取：`panel_model_test` 加案例（`ScrollBy(delta, false)` 後 `SelectionIndex()` 不變）。
   - Alt+digit：視窗層（sanity grep 覆蓋，hang 測試不可行）。
   - prewarm 判定：既有 `panel_model_test` 若覆蓋 `EmptyStatePrewarmEntries` 則補空白字串案例。
6. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-186 列。

## Non-goals

- 不改 `ScrollBy` 對 PgUp/PgDn 的行為（move_selection=true 路徑與現況等價）。
- 不處理「拖曳中按 Alt+digit」的病態操作（claude 報告標為觀察、不建議開 item）。
- 不做無限長搜尋輸入（明確上限即合約，1023 維持）。
- 不改搜尋排名／過濾邏輯。

## Acceptance

- 滾輪捲動不改變 `SelectionIndex`；PgUp/PgDn 行為與現況逐位元相同。
- 少於 10 個可見項目時按無對應數字的 Alt+digit：`return 0` 不再發生（fall through 系統預設）；有對應項目時行為不變。
- EDIT 建立後有 `EM_LIMITTEXT`；貼上超過 1023 字元在輸入框即被截斷，UI 與 model 一致。
- 空白字串查詢（`" "`）的 prewarm 與格狀判定一致。
- Release build 無 error／新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "ScrollBy|RowForVisibleSlot|EM_LIMITTEXT|NormalizeName\(query_\)\.empty" src/app_host
```

驗證：build 無 error／新增 warning；CTest 全 Passed（含新增的 ScrollBy 案例）；`ScrollBy` 呼叫點兩類（鍵盤 true／滾輪 false）；`EM_LIMITTEXT` 一命中；prewarm 與 RefreshRows 用同一判定。

## 交接區

實作完成 2026-08-12（commit `NR-186: align input contracts (wheel, alt-digit, limittext, prewarm)`）。

### 1. 滾輪不搬選取（claude I-6）

- **參數形狀**：`PanelModel::ScrollBy(int delta_rows, bool move_selection = true)`（`panel_model.h:139`）。採「加參數＋預設 true」而非拆第二函式：PgUp/PgDn 現況逐位元等價（Non-goals 第一條），既有呼叫不用改。
- **實作**：`panel_model.cpp:188-202` — `selected_ = first_visible_` 只在 `move_selection` 為 true 時執行；滾輪路徑完全不碰 `selected_`，因此也不存在 EnsureSelectionVisible 需求（§4.8 允許選取離開可見範圍；`MoveSelection` 既有的 EnsureSelectionVisible 在方向鍵回來時恢復）。
- **呼叫點清單**：
  - `main.cpp:2477` VK_PRIOR → `ScrollBy(-ViewportRows(), true)`
  - `main.cpp:2482` VK_NEXT → `ScrollBy(ViewportRows(), true)`
  - `main.cpp:2725` WM_MOUSEWHEEL → `ScrollBy(-steps * lines, false)`
- 測試既有 `TestScrollBy*`（true 路徑）全數原樣通過；新增兩個 false 路徑案例（見下）。

### 2. Alt+digit fall-through（codex M3）

- **WM_SYSKEYDOWN**（`main.cpp:2396-2428`）：`return 0` 移進 `row >= 0` 分支內；`slot >= 0` 但 `RowForVisibleSlot(slot) == -1`（少於 10 個可見項、清單尾端、footer 空白）時不再 return 0，fall through 到 `break` 走 `DefWindowProcW` 預設處理（系統 beep／其他 App 收到鍵）。
- **WM_SYSCHAR**（`main.cpp:2435-2445`）：吞鍵條件從「是 digit」改為「`slot >= 0 && RowForVisibleSlot(slot) >= 0`」，與 WM_SYSKEYDOWN 的 guard 同步；並補 `g_model != nullptr` 防護（`RowForVisibleSlot` 會解引用 model）。有對應項目時兩條訊息行為與 NR-024 現況一致（launch 照舊、beep 照吞）。
- 舊註解「A bound digit never beeps, even when no row maps to it」一併移除（正是 M3 的病根）。

### 3. EM_LIMITTEXT（codex L1）

- `main.cpp:3375`：`g_search_edit` 建立並 subclass 後、`RepositionSearchEdit` 前，`SendMessageW(g_search_edit, EM_LIMITTEXT, 1023, 0)` 一次。EN_UPDATE 的 `wchar_t buffer[1024]` 維持不動（1023 字元＋NUL，兩邊一致）；截斷現在發生在使用者看得到的輸入框內，UI 與 model 一致。

### 4. prewarm 判定（codex L3）

- `panel_model.cpp:222`：`!query_.empty()` → `!NormalizeName(query_).empty()`，與 `RefreshRows`（`:53`）同一判定；不新增 helper。空白字串查詢（`" "`、`"   "`）維持格狀（§4.3）且照常 prewarm 下一頁圖示。

### 5. 新增測試（`tests/unit/panel_model_test.cpp`）

- `TestScrollByWheelKeepsSelection`：`ScrollBy(5, false)`／`ScrollBy(100, false)`／回滾後 `SelectionIndex()` 全程不變；`ScrollBy(5, true)` 仍搬選取。
- `TestScrollByWheelKeepsSelectionInGrid`：格狀（6 欄）下 `ScrollBy(4, false)` 選取不變；`MoveSelection(0)` 後 `EnsureSelectionVisible` 把離螢幕選取拉回可見。
- `TestEmptyStatePrewarmEntriesWhitespaceQuery`：`" "` 與 `"   "` prewarm 結果與空查詢逐項相同。
- `TestEmptyStatePrewarmEntriesWhitespaceStaysGridRows`：空白查詢維持 6 欄格狀，prewarm 數等於 `Rows()` 數。

### 6. Build／CTest 證據

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` OK。
- `cmake --build build`：Release x64 無 error；唯一 warning 為**既有**的 `main.cpp:1534 unused variable 'target_size'`（stash 驗證 pre-existing，非本 item 引入）。
- `ctest --test-dir build --output-on-failure`：**32/32 Passed**（含 `nimblerun_list_vertical_slice_test`＝panel_model_test 別名）。
- rg greps：`ScrollBy` 呼叫點三處（鍵盤 true×2、滾輪 false×1）；`RowForVisibleSlot` 在 WM_SYSKEYDOWN/WM_SYSCHAR 各一；`EM_LIMITTEXT` 一命中；prewarm 與 RefreshRows 共用 `NormalizeName(query_).empty()` 判定。
- 踩坑紀錄：第一版 prewarm 判定誤寫成 `NormalizeName(query_).empty()`（方向反了，空查詢反而變 no-op，`TestEmptyStatePrewarmEntriesPinsThenRecent` 立刻抓出）；修正為 `!NormalizeName(query_).empty()` 後全綠。
