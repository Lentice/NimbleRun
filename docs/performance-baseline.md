# Performance Baseline

These are Release x64 measurements, not Debug estimates. Record the OS build, CPU, memory, display scale, catalog size, build commit, and whether a debugger was attached with every run.

| Metric | Target | Blocking threshold | Result | Environment / notes |
| --- | ---: | ---: | --- | --- |
| Idle CPU, 15-minute average | ≤ 0.1% logical CPU equivalent | > 0.5% | 0.0013% | `tests/release/nfr001_probe.ps1`，2026-08-26；Release x64、無 debugger、60 秒 settle 後取樣 900 秒 |
| Idle working set | ≤ 60 MiB | > 80 MiB | 39.45 MiB peak | 同上；符合目標與 blocking threshold |
| Idle private bytes | ≤ 50 MiB | > 70 MiB | 7.84 MiB peak | 同上；符合目標與 blocking threshold |
| Visible panel with 20 icons | ≤ 75 MiB | > 100 MiB | 80.37 MiB peak | 同上；20-row grid 收到 `VisibleReady` 後量測，符合 blocking threshold 但超過 ≤75 MiB 目標 |
| Cold start to hotkey-ready | ≤ 500 ms | > 1,000 ms | 220.4 ms | `nfr001_probe.ps1`，2026-08-26；由產品 `HotkeyReady` rendezvous 量測，Release x64、無 debugger |
| Warm hotkey to input-ready, p95 | ≤ 80 ms | > 150 ms | 41.54 ms | `nfr001_probe.ps1`，2026-08-26；20 次 hidden→visible，產品在 `SetFocus(search_edit)` 後發出 test-only `InputReady` rendezvous |
| Filter 500 apps, p95 | ≤ 8 ms | > 16 ms | 683 µs（5,000 筆、100 次、`L"e"` 查詢） | `search_engine_test`，2026-08-25；以 5,000 筆（大於 500）取 p95，作為保守上界 |
| Idle app-owned thread count | 2 ＋ watcher root 數 | 超出該式 | 5（已量測，`GetThreadDescription` census） | 見下方「執行緒數的量法」 |
| Idle process thread count | — | — | 10 | `nfr001_probe.ps1`，2026-08-26；參考值，不設門檻，隨系統注入執行緒數波動 |
| `icons.cache` file size | ≤ 32 MiB | > 48 MiB | 1.31 MiB（1,376,256 bytes） | `nfr001_probe.ps1`，2026-08-26；20-row `VisibleReady` 完成後隱藏面板並 flush 後量測 |
| 單次整窗重繪（grid，24 格） | — | — | 1.40 ms（p95 1.94 ms） | 見下方「整窗重繪的成本」，2026-08-07；參考值，不設門檻 |
| 單次整窗重繪（list，8 列） | — | — | 0.74 ms（p95 0.95 ms） | 同上。這是每次按鍵 `EN_UPDATE` 整窗失效的實際代價 |

## Release evidence contract

The nine NFR-001 rows with blocking thresholds are release gates. The values in this
baseline are historical measurements or context unless the current release evidence
report identifies a compliant measurement source. The 2026-08-25 formal probe waited
60 seconds after startup readiness, observed completed source rebuild entries, sampled
idle CPU/memory for 900 seconds, and completed the visible 20-row/icon-cache cycle;
those rows are current gate measurements. The app-owned thread value is measured by a
`GetThreadDescription` census (see below) rather than being an expectation.
`tests/release/release_evidence.ps1`
therefore emits every blocking row and returns `INCOMPLETE` with a non-zero exit code
for any row that is not measured. A process-total thread count, executable size, or
estimate cannot satisfy a blocking row.

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

**量法**：start-address 無法判定 app-owned——這個 UCRT 工具鏈下 `std::thread` 是透過
`_beginthreadex` 建立，OS 回報的 Win32 start address 一律是 `ucrtbase.dll` 內的 CRT
trampoline，與系統注入的 CRT／COM worker thread 完全相同，無法用位址區分（2026-08-26
重新量測時，5 條預期執行緒只有 1 條落在模組範圍內）。改為讓程式自己替常駐執行緒命名：
`main`（`wWinMain` 進入點）、`icon_worker.cpp` 的 `IconWorker::Run`、
`catalog_watcher.cpp` 的 `WatchLoop`，各呼叫一次 `SetThreadDescription(GetCurrentThread(),
L"NimbleRun.*")`；`tests/release/nfr001_probe.ps1` 對每條執行緒呼叫
`GetThreadDescription` 讀回，比對 `NimbleRun.` 前綴計數。

已量測（Release x64，Windows 11 Pro 26200，2026-08-26 formal probe，catalog root =
兩個 Start Menu Programs ＋ `D:\Program files`，未接除錯器，60 秒 settle 後量測）：
app-owned 執行緒 5 條（main、icon worker、3 個 watcher，符合公式 2 + 3 = 5），行程總數
10 條，`icons.cache` 1.31 MiB。
