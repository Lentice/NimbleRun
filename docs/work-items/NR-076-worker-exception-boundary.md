# NR-076 — Background workers need the §11 exception catch boundary (an exception must not kill the process)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §11（Worker 發生例外 → UI 不崩潰）／§NFR-003（任一項目失敗不得使 Catalog 建立失敗）
- Origin: 2026-08-08 第四次全 repo 稽核（icons 子系統與 main.cpp）

## Why

design-spec §11 錯誤表明文：

| 情境 | 系統行為 |
|---|---|
| Worker 發生例外 | **捕捉邊界、記錄並丟棄該次結果**；UI 不崩潰 |

實作中兩類背景 worker 都沒有例外捕捉邊界，任何例外從 `std::thread` 的函式本體逃逸 →
`std::terminate` → **整個常駐 tray process 死亡（UI 一起）**：

1. **Icon worker**：`IconWorker::Run()`（`src/icons/icon_worker.cpp:94-186`）的任務本體
   （`:115-172`）沒有 try/catch。可拋例外的地方都是真實存在的：`Lookup` 的
   `std::vector<std::uint8_t>` 拷貝（`icon_store.cpp:334`，配合 NR-075 的巨型檔可達
   GB 級）、`DecodeIconPng`／`EncodeIconPng` 的配置、`new IconResult`——
   `std::bad_alloc` 在記憶體壓力下即可觸發。
2. **Catalog rebuild threads**：`StartRebuild` 的 lambda（`src/app_host/main.cpp:1249-1289`）
   沒有 try/catch；`new RebuildResult`、`std::move(res.entries)` 的配置失敗同樣會
   `std::terminate`。

先前稽核（第三輪）聲稱 icons 子系統「執行緒 handoff 複查為乾淨」，但並未把 §11 的
例外邊界納入。稽核新增發現。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **Icon worker**：任務本體包 `try { ... } catch (...) { ... }`。catch 裡**仍然
   post 一個空 bitmap 的 `IconResult`**（讓 UI 側 `g_pending_icon_keys` 清掉該 key，
   不重複請求；fallback 照常顯示），並寫一行診斷（照既有 `store_ ? store_->WriteLog
   :` 的形狀，`store_ == nullptr` 時寫入 `IconStore` 的 log 不存在，改為不寫或沿用
   既有可用的診斷出口，由實作決定並在交接區載明）。絕不讓例外逃出 `Run()`。
2. **Rebuild threads**：lambda 包 `try { ... } catch (...) { ... }`。catch 裡設定
   `result->failed = true` 並照常 `PostMessageW`——coordinator 走既有的
   `ApplySourceFailure`（NR-063 已建好），該來源保留舊結果，正是 §NFR-003 的隔離
   語意。寫一行診斷（`g_diag`）。
3. **不做例外型別分類**：`catch (...)` 即可——所有例外都是「丟棄該次結果」，記錄
   與否由診斷行承擔，不需要知道型別。
4. **測試**：`icon_worker_test` 的 fake `IconProvider` **可以**被改成拋例外（唯一可
   注入的 seam）——新增 case：provider 拋例外 → worker 存活、UI 收到空 bitmap 結果、
   後續請求照常處理。rebuild 路徑的枚舉器不可注入，由 code review＋sanity grep
   覆蓋。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- New non-trivial logic needs one focused runnable test or self-check.
- Keep the idle path event-driven.

design-spec §11：

- Worker 發生例外 → UI 不崩潰；捕捉邊界、記錄並丟棄該次結果。

design-spec §NFR-003：

- 任一 App 圖示失敗不得使 Catalog 建立失敗；任一損壞捷徑不得造成崩潰或卡住整體掃描。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/icons/icon_worker.cpp:94-186` — `Run()`。主場之一（任務本體 `:103-173`）。
- `src/app_host/main.cpp:1227-1290` — `StartRebuild`（lambda `:1249-1289`）。主場之二。
- `src/app_host/main.cpp:2311-2346` — `kRebuildDoneMessage`（`result->failed` 的既有
  處理路徑，`ApplySourceFailure`）。**只讀不改**。
- `tests/unit/icon_worker_test.cpp` — fake `IconProvider` 所在；新 case 的家。

## Scope

### 1. `IconWorker::Run` 的 load 任務包例外邊界

把 `:123-161` 的 load 任務本體（`const IconRequest& request = task.request;` 起到
`PostMessageW` 前）包進 `try { } catch (...) { }`。catch 內：

```cpp
} catch (...) {
    // NR-076: a throwing Shell/WIC/alloc path must not terminate the process
    // (design-spec §11: catch, log, discard). Report an empty bitmap so the UI
    // clears the pending key and keeps the fallback.
    if (store_ != nullptr) {
        store_->WriteLog(L"icon-worker", L"exception");
    }
    result->bitmap = {};   // keep result allocated; fall through to the post
}
```

（`result` 的配置與 post 留在 try 外，或把整段含 post 包進去但保證 catch 後仍 post——
以「單一 post 點、catch 不雙重 post」為準。）

### 2. `StartRebuild` lambda 包例外邊界

把 `main.cpp:1250-1279`（`auto* result = new RebuildResult;` 起到 `PostMessageW` 前）
包進 `try { } catch (...) { }`。catch 內：

```cpp
} catch (...) {
    // NR-076: an allocation/enumeration exception must not terminate the
    // process (design-spec §11); report a source failure so the coordinator
    // keeps this source's old entries (design-spec §NFR-003).
    result->failed = true;
    if (g_diag) {
        g_diag->Write(L"rebuild", L"exception", 0);
    }
}
```

（`PostMessageW` 維持在 try 外，與既有 `result` 擁有權/洩漏處理一致。）

### 3. 測試（`tests/unit/icon_worker_test.cpp`）

- fake provider 新增「拋出 `std::runtime_error`」模式 → `Post` 一筆 `visible=true`
  請求 → 斷言：worker 執行緒存活、`kIconReadyMessage` 收到**空 bitmap** 的結果、
  後續再一筆請求照常送達並成功。
- 既有 11 個 case 回歸全綠（fake provider 預設不拋）。

### 4. 更新 spec？

不需。§11 錯誤表的行為現在才被兌現，spec 已是正確描述。

## How this stays maintainable

兩個 worker 的例外出口都收斂到「既有失敗語意」（空 bitmap／source failure），不新增
第三種結果型別；UI 側零改動。日後任何新 worker 照同一形狀包 try/catch 即可。

## Non-goals

- **不實作例外型別分類或 partial recovery。**
- **不把 `IconResult`／`RebuildResult` 改成例外安全以外的型別。**
- **不為 rebuild 枚舉器加可拋例外 seam**（其失敗路徑已由 `source_ok`／`failed`
  覆蓋，NR-063）。
- **不改 UI 側的結果處理。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項＋新增 case）。
2. §Scope 3 新 case 通過（provider 拋例外 → worker 存活＋空結果＋後續請求正常）。

Manual：

3. 不必手動操作（例外注入已由 fake provider 覆蓋）；code review 確認兩個 worker 的
   `Run()`／lambda 本體再無裸出口。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 兩個 worker 都有 catch (...) 邊界：
Select-String -Path src/icons/icon_worker.cpp -Pattern "catch \(\.\.\.\)"
Select-String -Path src/app_host/main.cpp -Pattern "catch \(\.\.\.\)"
# expect: 各至少 1 處

# icon worker 的 catch 內仍 post 空結果（不雙重 post）：
# 讀 icon_worker.cpp 確認 PostMessageW 只有 1 處

# 改動範圍：
git diff --name-only
# expect: src/icons/icon_worker.cpp、src/app_host/main.cpp、
#         tests/unit/icon_worker_test.cpp
```

## 交接區

（實作者填寫：try/catch 的實際範圍、catch 內診斷出口的取捨（`store_->WriteLog` vs
其他）、rebuild catch 的 `g_diag` 用法、fake provider 拋例外的寫法與新 case 斷言、
建置與 CTest 結果、sanity greps、偏差、未完成事項。）
