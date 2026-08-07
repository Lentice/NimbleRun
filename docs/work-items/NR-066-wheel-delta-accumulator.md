# NR-066 — Mouse wheel: accumulate sub-notch deltas so precision touchpads can scroll

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §4.8（滾輪翻頁）／NR-021（`ScrollBy` 唯一捲動入口）
- Origin: 2026-08-07 第三次全 repo 稽核（main.cpp `WM_MOUSEWHEEL`）

## Why

`src/app_host/main.cpp:2403-2404`：

```cpp
const int steps =
    static_cast<int>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA;
if (g_model && lines > 0 && steps != 0) {
```

`WM_MOUSEWHEEL` 的 delta 不保證是 `WHEEL_DELTA`（120）的倍數：高解析度滾輪與
精準觸控板（precision touchpad）每則訊息只送 30–60 的 delta，且 MSDN 明定
「接收方應累積餘數」。「一次捲動幾行」的系統設定（`SPI_GETWHEELSCROLLLINES`，
通常 3）搭配 120 的整數倍是傳統滾輪的協定；本實作把每次 delta 獨立整除，
餘數直接丟棄，也**沒有任何累積器**：

- 觸控板滑一次＝連續幾則 30–60 的訊息，每則 `steps = 0` → `steps != 0` 恆假 →
  `ScrollBy` 永遠不被呼叫，**面板完全無法捲動**。
- 傳統滾輪（120 的整倍數）不受影響，所以這是特定硬體上的靜默失效。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **加一個檔案範圍的累積器**（`int g_wheel_delta_carry = 0;`，比照既有
   `g_*` 全域慣例），每次收到 delta 先累加，取整數商當步數、餘數留下：
   `carry += delta; steps = carry / WHEEL_DELTA; carry %= WHEEL_DELTA;`。
   這是 MSDN 建議的最小正確實作，一行宣告＋三行改寫。
2. **方向與既有語意不變**：正 delta（向上）往清單起點捲。累積器是純整數，
   正負自然保留；不為「方向相反的下一則」做特殊處理（負 delta 會直接抵銷
   累積的正餘數，這是累積器該有的行為）。
3. **`lines`（每次捲動列數）邏輯一字不改**：`SPI_GETWHEELSCROLLLINES`、
   `WHEEL_PAGESCROLL`、失敗退回 3 都是 NR-021 的既定行為。
4. **不加單元測試**：`WM_MOUSEWHEEL` 在 Win32 訊息路徑上，`ScrollBy`（純值）
   已有 `panel_model_test` 覆蓋且本 item 不改它。行為由手動驗收覆蓋。
   累積器本身是四行整數算術，屬於「不為製造測試點而發明抽象」的範圍（NR-060 先例）。

## Binding constraints — quoted, do not go looking for them

design-spec §4.8：

> - 滾輪只在結果超過可見容量時捲動。

design-spec §4.7（翻頁）：

> - `PgUp`／`PgDn` 與滾輪翻頁時可見範圍不溢出頭尾。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:2393-2410` — `WM_MOUSEWHEEL`。**本 item 只改這裡**
  （外加全域宣告一處）。
- `src/app_host/main.cpp:291` 一帶 — 既有 `g_*` 全域宣告區，累積器放這裡。
- `src/app_host/panel_model.cpp` 的 `ScrollBy` — **只讀不改**，確認呼叫端
  傳正負步數的既有語意（`-steps * lines`）。

## Scope

### 1. 累積器

```cpp
// 全域宣告區：
int g_wheel_delta_carry = 0;   // NR-066: sub-notch wheel deltas accumulate here

// WM_MOUSEWHEEL：
g_wheel_delta_carry += static_cast<int>(GET_WHEEL_DELTA_WPARAM(w_param));
const int steps = g_wheel_delta_carry / WHEEL_DELTA;
g_wheel_delta_carry %= WHEEL_DELTA;
if (g_model && lines > 0 && steps != 0) {
    g_model->ScrollBy(-steps * static_cast<int>(lines));
    InvalidateRect(window, nullptr, FALSE);
}
```

不改 `lines` 的計算，不改 `-steps * lines` 的方向，不 reset 累積器（面板隱藏、
視窗切換期間的殘餘 delta 量極小且方向一致，保留即可；重新設 0 反而是不必要的
狀態重置）。

### 2. 更新 spec？

§4.8 的「滾輪只在結果超過可見容量時捲動」仍然成立。可在 §4.8 滾輪條目補一句
（選用）：

> 觸控板／高解析度滾輪的次刻度 delta 會累積，不會被丟棄。

## How this stays maintainable

**累積器是 `WM_MOUSEWHEEL` 的本地狀態，跟著視窗訊息走，不進 model。**
`ScrollBy` 維持純值、維持既有測試；未來若要支援 `WM_MOUSEHWHEEL` 或觸控板的
`WM_POINTERWHEEL`，同一個累積模式直接複用。

## Non-goals

- **不支援 `WM_MOUSEHWHEEL`／`WM_POINTERWHEEL`**（水平捲動未在規格中）。
- **不把累積器搬進 `panel_model`**（它是訊息層狀態，不是 model 狀態）。
- **不縮放 `lines` 或改 `SPI_GETWHEELSCROLLLINES` 讀法。**
- **不加 `SetTimer`／動畫**（NR-021 的「一次捲動 N 列」語意不變）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，本 item 不新增測試）。

Manual（Release build，逐條打勾）：

1. 傳統滾輪（120 整倍數）：行為與現在完全一致（一齒一行或一齒 N 行）。
2. 若手邊有精準觸控板／高解析度滾輪：連續輕掃面板，清單確實捲動，且捲動量
   平滑（不會有時動時不動）。
3. 搜尋狀態（單欄）與空白狀態（grid）都能捲動；`PgUp`／`PgDn` 行為不變。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 累積器宣告與使用各一處，且 WHEEL_DELTA 只出現一次除法：
Select-String -Path src/app_host/main.cpp -Pattern 'g_wheel_delta_carry'
# expect: 3 處——宣告、累加、取商取餘（或 2+1，視寫法）

# 每次 scroll 仍走唯一入口 ScrollBy：
Select-String -Path src/app_host/main.cpp -Pattern 'ScrollBy'
# expect: 既有數目不變（WM_MOUSEWHEEL 一處＋PgUp/PgDn 等）

# 改動範圍：
git diff --name-only
# expect: 僅 src/app_host/main.cpp（及選用的 docs/design-spec.md）
```

## 交接區

（實作者填寫：修改的位置、累積器宣告處、建置與 CTest 結果、手動驗收結果
（含是否真的測過觸控板）、sanity greps、偏差、未完成事項。）
