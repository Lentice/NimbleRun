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

## Measurement rules

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
