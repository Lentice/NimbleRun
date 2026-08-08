# NR-084 — 格狀翻頁永遠碰不到「最後一頁的尾端項目」

Phase 3 · Depends on: —（無依賴，可與 NR-085、NR-086 平行）

- Source: `docs/design-spec.md` §4.2（空白狀態翻頁）／§4.9（6 欄 × 4 列 = 一頁 24 格）
- Origin: 2026-08-08 第六次全 repo 稽核（格狀分頁數學與「項目必須都能被翻到」比對）

## Why

`PanelModel::ClampFirstVisible`（`src/app_host/panel_model.cpp:122-131`）把
`first_visible_` 夾在 `[0, max(0, RowCount() - viewport*columns)]` 之後再向下
取整到 `columns` 的倍數。當**項目總數不是「一頁容量」的整數倍**時，這個
「先夾再取整」讓最大可達的 `first_visible_` 小於真正能涵蓋全部項目的值，
尾端的一整組項目永遠無法被翻到：

- 例：50 筆項目、6 欄 × 4 列 = 一頁 24 格。`clamp` 上界 = `50-24 = 26`，
  向下取 6 的倍數 → `26 - 26%6 = 24`。於是 `first_visible_` 只可能是
  `{0, 6, 12, 18, 24}`，可見視窗最遠涵蓋 `[24, 48)`，**第 49、50 筆永遠
  不可見**。`PgDn`（`ScrollBy(4)` → `first_visible_ += 24` → 48）再夾回 26、
  取整回 24——**停在 24，再也翻不動**。
- 不整除的總數在 `count % 24 != 0 && count > 24` 時都會發生：
  `25..29` 漏 1..5 筆、`31..35` 漏 1..5 筆、`37..41` 漏 1..5 筆……（總數恰為
  24 的倍數或 ≤ 24 時無此問題，既有測試 `TestGridScrollByPages` 用 60 筆
  = 24×2.5，`TestGridFirstVisibleAlignedToColumns` 用 50 筆 = 漏 2 筆）。

這不是純理論。空白狀態的內容 = 釘選 + 常用（§4.2），`recent_count` 可設到
40、釘選數無上限。真實觸發：使用者釘了 26 個 App（0 筆常用）→ 第 25、26 個
釘選 App 在格狀狀態**完全消失、無法點也無法翻到**；或釘 30 個＋20 筆常用
（共 50）→ 最後 2 筆常用 App 消失。鍵盤也救不回：`SelectRow(49)` 後
`EnsureSelectionVisible` 把 `first_visible_` 推到 30，但 `ClampFirstVisible`
再夾回 24（`30 ≤ 26` 不存在 → 26 → 24），選取落在**未繪製的格子**上，
`Enter` 仍會啟動一個螢幕上看不到的 App。

根因是把「尾端被 clamp 掉的部分」誤當成可捨棄：`ScrollBy`/`EnsureSelectionVisible`
的數學本來就能算出對齊的尾頁位置（`EnsureSelectionVisible` 的
`(selected - selected%columns) + columns - visible_capacity` 對 `selected=49`
算出 30，正確），是 `ClampFirstVisible` 的**上界公式**把它壓死了。

**覆寫既有決策**：此行為是 NR-029 引入、並被
`TestGridFirstVisibleAlignedToColumns`（`tests/unit/panel_model_test.cpp:532-544`）
明文鎖定的——「尾端視窗夾到 `RowCount - page` 再向下取整到整列」（`:541`）。
新證據：該取整讓「最後一頁若不足一頁，其項目不可達」，而 §4.2「超出時以
PgUp／PgDn 或滾輪翻頁」的前提是**翻頁能到達全部項目**。兩者衝突時，讓使用者
看不到自己釘的 App 比「最後一列內容不滿」更糟。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **允許最後一頁的最後一列部分填入**（起點仍永遠對齊整列、從整列開始）。
   這是「全部項目可達」與「不顯示半列」唯一可同時成立的組合：要翻到尾端
   項目，最後一頁必然以不足一頁的內容結束。spec §4.2「不得出現半列」按
   本決策重新理解為「**可見範圍的起點**不得從半列開始」，最後一頁內容允許
   少於一頁（見 Scope §2 的 spec 修訂）。
2. **修在 `ClampFirstVisible` 的唯一上界公式**：上界改為「對齊後仍能涵蓋
   最後一筆」的最大 `first_visible_`，即
   `max(0, (ceil(count/columns) - viewport_rows) * columns)`。`ScrollBy` 與
   `EnsureSelectionVisible` 的既有公式在此上界下自動給出正確尾頁，**不改**。
3. **不縮小 grid 的頁面幾何**：不改 `kCellWidthDip`/`kCellHeightDip`、
   不改成「最後一頁只畫 N 列」——視窗高度與格子幾何是版面常數，頁面永遠是
   完整的 `viewport_rows` 列高，只是最後一列可能只有部分格子有資料。
4. **更新被鎖定舊行為的那條測試**（`TestGridFirstVisibleAlignedToColumns`：
   50 筆的尾頁 `FirstVisibleRow` 由 24 改 30），並新增「尾端項目可達」的
   斷言案例。`TestGridScrollByPages`（60 筆 → 36）不受影響，原樣通過。
5. **加純值測試、不發明抽象**：`panel_model_test` 已有完整 fixture，直接
   加案例，不需要新 seam。

## Binding constraints — quoted, do not go looking for them

design-spec §4.2：

> - 一次只顯示可見容量的格數；超出時以 `PgUp`／`PgDn` 或滾輪翻頁，不顯示捲軸。可見範圍永遠對齊整列，不得出現半列。

design-spec §4.9：

> - 格狀狀態：6 欄 × 4 列，格子 101×96 DIP，圖示 40×40 DIP 置中於格子上半，名稱單行置中於下半。一頁 24 格。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/panel_model.cpp:122-131` — `ClampFirstVisible`。**本 item 只改
  這個函式的上界公式**（`:128-129`）。
- `src/app_host/panel_model.cpp:133-149` — `EnsureSelectionVisible`：已能算出
  正確尾頁（`selected=49` → 30），不改；本 item 修好上界後它自動正確。
- `src/app_host/panel_model.cpp:167-176` — `ScrollBy`：`+= delta*columns` 後
  走 `ClampFirstVisible`，不改。
- `tests/unit/panel_model_test.cpp:532-544` — `TestGridFirstVisibleAlignedToColumns`：
  **本 item 覆寫其 `:541` 的 24 斷言**（50 筆尾頁 → 30）。
- `tests/unit/panel_model_test.cpp:561-572` — `TestGridScrollByPages`（60 筆），
  不改，作為「整數倍總數行為不變」的回歸守門。
- `docs/work-items/NR-029-empty-state-grid.md` — 引入此行為的原始 item；
  本 item 覆寫其「尾端取整到整列、不顯示半列」的測試化決策。
- `src/app_host/main.cpp:1507-1620` — grid 繪製迴圈：可見項目
  `first_visible_..first_visible_+ViewportRows()*Columns()`，命中/啟動只吃
  model 的 index，本 item 不改 Render。

## Scope

### 1. 修 `ClampFirstVisible` 的上界公式

`src/app_host/panel_model.cpp:128-130`：

```cpp
// NR-084: 上界要涵蓋「最後一筆」，不是「數到 RowCount - page 再取整」。
// 後者在總數不是 page 倍數時（count % 24 != 0 且 count > 24）會把尾端
// 不足一頁的項目永遠擋在視窗外——釘選/常用 App 消失、鍵盤選取落在未繪製
// 格上。ceil(count/columns) 是最後一列的列號，扣掉 viewport 列數就是
// 對齊後仍能涵蓋全部項目的最大起點；最後一頁的最後一列允許部分填入
// （§4.2 修訂：起點對齊整列，內容可不足一頁）。
const int row_count = (count + columns - 1) / columns;
first_visible_ = std::clamp(first_visible_, 0,
    std::max(0, (row_count - viewport_rows_) * columns));
first_visible_ -= first_visible_ % columns;
```

- `ScrollBy`／`EnsureSelectionVisible`／`SetViewportRows`／`SetGridColumns`
  一字不改。
- 驗算：50 筆 → `row_count = ceil(50/6) = 9` → 上界 `(9-4)*6 = 30` →
  `PgDn` 由 24 → 48 → 夾 30 → 30（`30%6=0`），可見 `[30, 54)` 涵蓋第 49、50 筆；
  60 筆 → `row_count = 10` → 上界 36，與 `TestGridScrollByPages` 現值一致；
  ≤ 24 筆 → `row_count ≤ 4` → 上界 0，無翻頁。

### 2. 更新 spec

design-spec §4.2「一次只顯示可見容量的格數；……可見範圍永遠對齊整列，
不得出現半列。」改為：

> - 一次只顯示可見容量的格數；超出時以 `PgUp`／`PgDn` 或滾輪翻頁，不顯示捲軸。可見範圍的**起點**永遠對齊整列，不得從半列開始；最後一頁若資料不足，最後一列允許部分填入。

（本 item 覆寫「不得出現半列」的舊字面——舊解讀讓尾端項目不可達，
見 Why。起點對齊的約束不變。）

### 3. 更新測試

- `TestGridFirstVisibleAlignedToColumns`：50 筆、`ScrollBy(100)` 後
  `FirstVisibleRow()` 由 24 改為 **30**，並把斷言訊息改成「尾頁起點涵蓋
  最後一列」。
- 新增 `TestGridTailItemsReachable`：50 筆 fixture，`ScrollBy(4)` 兩次
  （第一頁 → 尾頁），斷言 `FirstVisibleRow() == 30` 且
  `RowForVisibleSlot(18) == 48`、`RowForVisibleSlot(19) == 49`
  （尾端項目在可見視窗內）；另做 `SelectRow(49)` + `EnsureSelectionVisible`
  路徑，斷言 `FirstVisibleRow() == 30` 且 `SelectionIndex() == 49`。
- 新增 `TestGridPageNotMultipleStillReachesAll`（或併入上者）：25 筆
  （`25 % 24 = 1`），`ScrollBy(4)` 後 `FirstVisibleRow() == 6` 且
  `RowForVisibleSlot(0) == 6`、`RowForVisibleSlot(18) == 24`
  （第 25 筆可達）。
- 既有 `TestGridScrollByPages`（60 筆）一字不改，作為整數倍回歸。

## Non-goals

- **不改 grid 版面常數／不縮小最後一頁的繪製高度**（Decisions §3）。
- **不改 `ScrollBy`／`EnsureSelectionVisible` 的主體**——它們的公式在上界
  修正後自動正確；只允許在測試證明不足時微調。
- **不改清單（`Columns()==1`）的分頁**：`columns=1` 時 `first_visible_ % 1`
  是 no-op、上界公式 `(count+0)/1 - viewport = count - viewport`，現行為
  已正確，本 item 不動它。
- **不處理「格狀 footer 顯示 Alt+1~4 但實際綁定 10 格」的視覺不一致**
  （既有 LOW 記錄，非本 item）。
- **不改 Render、不新增版面欄位、不新增 seam**。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠。
2. `panel_model_test`：`TestGridFirstVisibleAlignedToColumns` 尾頁斷言改 30；
   新增尾端可達案例通過；`TestGridScrollByPages` 原樣通過。

Manual（Release build，逐條打勾）：

1. 空白狀態總數 >24 且非 24 倍數（例如釘 26 個 App、`recent_count` 設 8 以上
   任選使其 >24）：`PgDn` 到最後一頁，確認所有項目（含最後一個）都出現且可點。
2. 同環境用 `↓` 把選取移到最後一列的最後一格：選取邊框可見，`Enter` 啟動的
   是該格。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R nimblerun_panel_model_test --output-on-failure
```

```powershell
# 上界公式只有 ClampFirstVisible 一處：
Select-String -Path src/app_host/panel_model.cpp -Pattern 'row_count'
# expect: ClampFirstVisible 內 1 處（本 item 新增）

# ScrollBy / EnsureSelectionVisible 主體未動：
Select-String -Path src/app_host/panel_model.cpp -Pattern 'first_visible_ \+=|selected_ - \(selected_ % columns\)'
# expect: 各自原樣存在

git diff --name-only
# expect: 僅 src/app_host/panel_model.cpp、tests/unit/panel_model_test.cpp、
# docs/design-spec.md（及本 item 文件與 docs/work-items.md 追蹤表格）
```

## 交接區

（實作者填寫：改動位置、新公式、測試案例名與斷言、建置與 CTest 結果、
2 條手動驗收結果、sanity greps、偏差、未完成事項。）

- 改動位置：`src/app_host/panel_model.cpp:128-129` 的 `ClampFirstVisible`
  上界公式；`tests/unit/panel_model_test.cpp` 更新
  `TestGridFirstVisibleAlignedToColumns` 並新增尾端可達案例；design-spec
  §4.2 一句。
- 建置與 CTest：Release 建置無新增警告；`ctest --test-dir build
  --output-on-failure` 全綠；`ctest -R nimblerun_panel_model_test` 全綠。
- 手動驗收：本工作區不操作視窗，2 條手動驗收未實跑；由新增單元案例覆蓋。
- sanity greps：`row_count` 僅 `ClampFirstVisible` 1 處；`ScrollBy`／
  `EnsureSelectionVisible` 主體未動。
- 偏差：主要程式碼、spec 文字與測試已在既有本地 commit `5a89d3c`；本次 opencode
  job 只做 clean-worktree 驗證，沒有新增 patch。手動 GUI 驗收未執行。
- 未完成：無。
