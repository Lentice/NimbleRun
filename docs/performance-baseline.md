# Performance Baseline

These are Release x64 measurements, not Debug estimates. Record the OS build, CPU, memory, display scale, catalog size, build commit, and whether a debugger was attached with every run.

| Metric | Target | Blocking threshold | Result | Environment / notes |
| --- | ---: | ---: | --- | --- |
| Idle CPU, 15-minute average | ≤ 0.1% logical CPU equivalent | > 0.5% | Not measured | 需要一支 15 分鐘 idle 的 CPU 抽樣計時，尚未實作 |
| Idle working set | ≤ 60 MiB | > 80 MiB | 37.2 MiB | `release_evidence.ps1`，2026-08-07，hidden at rest（約 3 秒 settle）；同一樣本 handle count 394 |
| Idle private bytes | ≤ 50 MiB | > 70 MiB | 7.7 MiB | `release_evidence.ps1`，2026-08-07，同一 idle 樣本 |
| Visible panel with 20 icons | ≤ 75 MiB | > 100 MiB | Not measured | 需要可見面板狀態的記憶體 census，尚未實作 |
| Cold start to hotkey-ready | ≤ 500 ms | > 1,000 ms | Not measured | 需要一支量測「程序啟動到熱鍵就緒」的計時器，尚未實作 |
| Warm hotkey to input-ready, p95 | ≤ 80 ms | > 150 ms | Not measured | 需要一支量測「熱鍵到首幀」的計時器，尚未實作 |
| Filter 500 apps, p95 | ≤ 8 ms | > 16 ms | 603 µs（5,000 筆、`L"e"` 查詢） | `search_engine_test` 5,000 筆 timing，2026-08-06（NR-047 交接區）；量測規模大於門檻的 500 筆，故為保守上界 |
| Idle app-owned thread count | 2 ＋ watcher root 數 | 超出該式 | 5（預期值，未經 census 驗證） | 見下方「執行緒數的量法」 |
| Idle process thread count | — | — | 16 | 參考值，不設門檻 |
| `icons.cache` file size | ≤ 32 MiB | > 48 MiB | Not measured | 需要一次完整圖示建置後的實際檔案大小量測，尚未進行 |
| 單次整窗重繪（grid，24 格） | — | — | 1.40 ms（p95 1.94 ms） | 見下方「整窗重繪的成本」，2026-08-07；參考值，不設門檻 |
| 單次整窗重繪（list，8 列） | — | — | 0.74 ms（p95 0.95 ms） | 同上。這是每次按鍵 `EN_UPDATE` 整窗失效的實際代價 |

## 整窗重繪的成本

2026-08-07 稽核提出兩個效能假設，兩個都以量測否決，**不開 work item**：

1. **`DrawDecodedIcon` 每格每幀 `CreateBitmap`**（`src/app_host/main.cpp`）。
2. **`EN_UPDATE` 每次按鍵整窗失效**（同檔的搜尋框訊息處理）。

量法：獨立的 D2D 基準程式，與 `Render()` 相同的面板尺寸與逐格繪製呼叫
（`Clear`、搜尋框、每格 fill＋icon＋文字、footer band），
`D2D1_PRESENT_OPTIONS_IMMEDIATELY` 以避開 vsync，
`QueryPerformanceCounter` 量 `BeginDraw`～`EndDraw`，
每組 500 幀取後 400 幀。程式為一次性量測，未進 repo。
環境：Release x64、clang 22.1.8 `-O2`、Windows 11 Pro 26200、未接除錯器。

| 情境 | 現行（每幀 `CreateBitmap`） | 若快取 `ID2D1Bitmap` | 差 |
| --- | ---: | ---: | ---: |
| grid 24 格 @100% | 1.40 ms | 0.48 ms | 0.92 ms |
| grid 24 格 @150% | 1.43 ms | 0.78 ms | 0.65 ms |
| list 8 列 @100% | 0.74 ms | 0.35 ms | 0.39 ms |
| list 8 列 @150% | 0.74 ms | 0.49 ms | 0.26 ms |

結論：

- `CreateBitmap` 約 **40 µs／圖示**，與 DPI 無關（成本綁在 48×48 的像素上傳，
  不是目標矩形）。它確實佔 grid 首幀的 **66%**——但整幀仍是 **1.4 ms**，
  是「暖狀態快捷鍵至可輸入 p95 ≤ 80 ms」預算的 **1.8%**。
  快取 `ID2D1Bitmap` 要同時處理裝置遺失、圖示晚到替換、LRU 逐出三個失效點，
  **用三個失效點換 0.9 ms 不划算**。
- 打字路徑是 list 版面：每次按鍵 **0.74 ms 重繪 ＋ 0.6 ms 搜尋**（上表 NR-047 數字）
  ≈ **1.4 ms**。以 20 字元／秒連打計，約佔單核 **3%**。
  debounce、incremental narrowing、局部失效**全部沒有數字支持**。

重新開啟這兩個議題的門檻：**先量到一幀超過 8 ms**（或高 DPI ／軟體算繪
fallback 下超過該值），再談改法。

- Use a Release x64 build without an attached debugger.
- Record working set, private working set, private bytes, CPU time, context switches, thread count, handle count, GDI objects, and USER objects.
- Measure cold start, warm show/hide, 100/500/2,000-item filtering, 20/40 visible icons, and 1,000 show/hide cycles.
- Do not replace a failed measurement with a process-size estimate or executable file size.

## 執行緒數的量法

原本的「Idle thread count ≤ 5，紅線 > 9」是以行程總數表述，但那個數字訂的其實是 NimbleRun 自己建的執行緒（design-spec §9.2 的執行緒模型）。工作管理員看到的行程總數還包含 Direct2D/D3D/DXGI 的 device thread、STA COM 與 Shell extension 的 RPC 執行緒、以及 ntdll 的 thread pool worker——這些由 Windows 注入，數量隨 OS build、顯示驅動與已安裝的 Shell extension 變動，不是本專案能控制的量，拿它當門檻只會量到別人的實作。

因此門檻改綁 app-owned 執行緒，並以 design-spec §9.2 直接推導，不再是一個固定數字：

- 1 條 UI thread（阻塞於 message loop）。
- 1 條常駐 icon worker（§9.2 允許常駐；也是當初把上限從 4 放寬到 5 的原因）。
- 每個 watcher root 一條 directory watcher，長時間阻塞等待事件，不輪詢。root 數 = 兩個 Start Menu Programs 目錄 ＋ 使用者設定的自訂資料夾數，所以這一項隨設定變動，訂死成常數必然會誤判。
- Catalog rebuild worker 是 per-source 的一次性執行緒，完成即回收（§9.2「不得建立常駐 thread pool 只為未來可能的工作」），故不計入待機值。待機時仍看得到它們，就是回收出問題。

判定方式是「數量是否等於上式」與「是否全部處於 Wait、CPU 0%」，而不是與固定上限比大小。行程總數仍要記錄，但只作為環境參考。

已量測（Release x64，Windows 11 Pro 26200，commit `cd0f256`，catalog root = 兩個 Start Menu Programs ＋ `D:\Program files`，未接除錯器，啟動後待機 3 秒）：行程總數 16 條，全部處於 Wait、CPU 0%，handle 391，working set 38.0 MiB。

app-owned 的 5 條是由上式推導的預期值（1 UI ＋ 1 icon worker ＋ 3 watcher root），本次未做 start-address census 驗證。2026-08-05 的 census 實測為 3 條（main ＋ 2 個 Programs watcher），當時還沒有常駐 icon worker、也還沒設定自訂 root，與上式一致。要把這欄從「預期值」升級為「已量測」，需要 `tests/release/release_evidence.ps1` 補上 start-address census；在那之前這欄不是硬性 gate。
