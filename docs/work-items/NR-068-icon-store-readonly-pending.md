# NR-068 — IconStore must not accumulate pending writes while the store rejects them

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.2（icons.cache 是可重建的加速檔）／§11（失效與復原）
- Origin: 2026-08-07 第三次全 repo 稽核（icon_store 降級路徑）

## Why

`IconStore::Put`（`src/icons/icon_store.cpp:337-355`）只拒絕 `Disabled` 與空 payload：

```cpp
if (state_ == StoreState::Disabled || payload.empty()) {
    return;
}
...
pending_[KeyFor(stable_id, variant)] = std::move(pending);
```

但 `Flush`（`:357-362`）只在 `state_ == StoreState::Ready` 才消化 `pending_`，
而 `icon_store.h:54` 的 enum 契約明寫 `ReadOnly` 是「readable, writes rejected
(e.g. disk full)」。`Flush` 的三個失敗出口（`:492`、`:504`、`:520`，皆
`FlushViewOfFile` 失敗）把 store 降級成 `ReadOnly`——此後：

- 每個新看到的圖示，worker 都 `Put` 一次（`icon_worker.cpp:148-150`），每次
  `pending_` 新增一筆帶完整 PNG payload 的記錄；
- `Flush` 永遠早退，`pending_` 從此不再消化；
- **`pending_` 在 session 內無界增長**，直到行程結束。PNG 每筆數十～上百 KB，
  磁碟故障後看過的圖示愈多，記憶體吃掉愈多（持續性故障下可達數十 MB）。

同一批失敗出口在 payload 已被 `std::move` 走（`:432` 一帶）之後仍未清
`pending_`——即使不增長，殘留的鍵值也無毒害（ReadOnly 下不會再被讀），但清掉
才是對的。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **`Put` 的守衛改為 `state_ != StoreState::Ready`**（連同空 payload 檢查）：
   `ReadOnly` 與 `Disabled` 都不收寫入，與 `icon_store.h:54` 的契約一致。一行。
2. **三個失敗出口（`:492`、`:504`、`:520`）各自 `pending_.clear()`**：
   失敗當下 `pending_` 已不可能被消化（`Flush` 已早退），保留只會耗記憶體。
   icons.cache 是「可重建的加速檔」（`icon_store.h` 檔頭契約），丟掉未寫入的
   pending 是完整且安全的降級（§11）——與 `Disabled` 路徑的行為同類。
3. **不新增列舉值、不換格式、不改變成功路徑。** `Ready` 的正常讀寫、Compact、
   逐 slot 驗證全部原樣。
4. **不加測試 seam、不強制單元測試**：`ReadOnly` 只能由 `FlushViewOfFile` 失敗
   到達，單元測試無法在不注入失敗的前提下驅動（NR-050 的 `GrowView` 先例——
   「OS 失敗路徑不加注入 seam，靠不變式」）。行為由既有 `icon_store_test`
   全綠（成功路徑回歸）＋sanity greps＋程式碼複查覆蓋。

## Binding constraints — quoted, do not go looking for them

`src/icons/icon_store.h:54`（本 item 引用的契約）：

> `ReadOnly,     // readable, writes rejected (e.g. disk full)`

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep changes scoped to the requested task.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/icons/icon_store.cpp:337-355` — `Put`。守衛是主場。
- `src/icons/icon_store.cpp:462-526` — `Flush` 的失敗出口（`:492`、`:504`、`:520`）
  與成功收尾（`:525` `pending_.clear()`）。失敗出口各加一行 `pending_.clear()`。
- `src/icons/icon_store.h:50-56` — `StoreState` enum 的契約註解。
- `src/icons/icon_worker.cpp:140-165` — worker 的 `Put` 呼叫點與 `PostFlush`。
  **只讀不改。**
- `tests/unit/icon_store_test.cpp` — 既有成功路徑測試。**不加新 case**（無 seam）。

## Scope

### 1. `Put` 守衛

```cpp
if (state_ != StoreState::Ready || payload.empty()) {
    return;
}
```

（`Disabled` 與 `ReadOnly` 統一走早退。`icon_worker` 的呼叫端不需要知道
store 不收——`Put` 的無回傳契約本來就是 fire-and-forget。）

### 2. 失敗出口清 pending

`:492`／`:504`／`:520` 三個 `state_ = StoreState::ReadOnly;` 之前各加
`pending_.clear();`（與 `:476` `Disabled` 出口的行為對齊）。`ScanIndex()` 與
`WriteLog` 照原順序。

### 3. 更新 spec？

§10.2 描述的是格式與正常生命週期；降級行為已在 §11「失效與復原」。不需改 spec。

## How this stays maintainable

**「store 不接受寫入 ⟺ `pending_` 為空」成為一對不變式**：`Put` 在非 `Ready`
不收新寫入，`Flush` 在非 `Ready` 不消化、失敗時清空——任何未來路徑只要記得
「先設 state 再清 pending」（或反之成對），就不會再出現無界增長。契約註解
（`icon_store.h:54`）現在與實作一致。

## Non-goals

- **不讓 `ReadOnly` 自動恢復成 `Ready`**（重試寫入是產品決策，本 item 是止血）。
- **不改變 `Flush` 的回傳值語意**（`false` 表示「沒寫成」維持）。
- **不新增列舉值、不新增狀態欄位。**
- **不改 worker 的重試／排程邏輯。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，`icon_store_test`／
   `icon_worker_test` 原樣通過＝成功路徑回歸）。

Manual（程式碼複查，逐條打勾）：

2. `Put` 守衛為 `state_ != StoreState::Ready`（含空 payload 檢查）。
3. 三個 `ReadOnly` 失敗出口與 `Disabled` 出口都有 `pending_.clear()`。
4. 成功收尾（`:525`）維持唯一的一次 `pending_.clear()`——沒有重複清、沒有
   清在錯誤時機。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# Put 守衛只收 Ready：
Select-String -Path src/icons/icon_store.cpp -Pattern 'state_ != StoreState::Ready'
# expect: Put 內 1 處（Flush 入口若有同形檢查則另計，總數列出）

# pending_ 的清除點成對（失敗出口 3 + 成功收尾 1）：
Select-String -Path src/icons/icon_store.cpp -Pattern 'pending_\.clear\(\)'
# expect: 4 處——三個 ReadOnly/Disabled 失敗出口各一＋成功收尾

# 改動範圍：
git diff --name-only
# expect: 僅 src/icons/icon_store.cpp
```

## 交接區

（實作者填寫：修改的位置、守衛與清除點的實際形狀、建置與 CTest 結果、
sanity greps、偏差、未完成事項。）
