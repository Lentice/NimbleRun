# NR-142 — catalog_refresh 的判定收斂：HasDueRebuild≡DueSources、兩個 Apply 的 event-clear≡

Phase 3 · Code structure · Depends on: —（純重構，零行為變更）

- Source: `AGENTS.md`（Prefer the smallest working change. Reuse existing code before adding
  helpers or abstractions）、本 repo 有紀錄的出事模式（NR-091/092：兩份逐字相同的走訪
  **各自**修同一個 bug 兩次——重複的判定邏輯是 3am bug 溫床）
- Origin: 2026-08-10 第十四次全 repo 稽核（ponytail 軸，MEDIUM）。主 Agent 已讀 `catalog_refresh.cpp` 全文驗證。
- Priority: **MEDIUM**——同一 predicate 兩份迴圈、同一條件清除兩份拷貝，改一漏一不會報錯。

## Why

`src/catalog/catalog_refresh.cpp` 內兩對逐字重複：

| 拷貝 A | 拷貝 B | 內容 |
|---|---|---|
| `HasDueRebuild`（`:30-40`） | `DueSources`（`:42-53`） | 同一 predicate：`pending && (kNever \|\| now-last>=kDebounceMs)`。`ShouldStartRebuild`（`:80-82`）先跑一份、`OnDebounceTimer` 緊接著再跑一份（`rebuild_pipeline.cpp:228-235`） |
| `ApplySourceResult`（`:120-125`） | `ApplySourceFailure`（`:140-145`） | 同一段「event snapshot 未變則清 pending」的條件清除（註解都引用 NR-065） |

`HasDueRebuild` 的迴圈本體與 `DueSources` 逐字相同（差別只在 push 不 push）。
event-clear 那段兩份完全同形。風險模式與 NR-091/092 相同：未來有人改其中一份的
「due 條件」（例如調整 debounce 語意），另一份不會編譯失敗，只會讓
「何時該重建」的判定漂移。

## Decisions already made — do not reopen

1. `HasDueRebuild` 改為 `return !DueSources(now_ms).empty();`——單一判定來源，
   零行為變更（空 vector 即 false，兩者對每個 source 的判定逐位元相同）。
2. 抽 `ClearPendingIfEventUnchanged(source)` 私有 helper（放 `catalog_refresh.cpp` 的
   anonymous namespace 或 coordinator 私有成員，依既有風格），兩個 Apply 呼叫它。
3. **不**把 `ShouldStartRebuild` 併進 `DueSources` 的語意（`IsRebuildInProgress` 守門是
   coordinator 的狀態，不是 predicate）。
4. **不**為純重構新增測試目標；既有 `catalog_refresh_test`＋`rebuild_pipeline_test` 是
   回歸網（generation 語意、debounce 語意、NR-065 的 event-clear 語意都有斷言）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/catalog/catalog_refresh.cpp`：`:30-53`、`:80-82`、`:103-151`。
- `src/app_host/rebuild_pipeline.cpp`：`:228-235`（`OnDebounceTimer` 的雙重呼叫）。
- `tests/unit/catalog_refresh_test.cpp`、`tests/unit/rebuild_pipeline_test.cpp`
  （行為零變更的驗證網）。

## Scope

1. `HasDueRebuild` 收斂為一行；刪除其迴圈本體。
2. 抽出 `ClearPendingIfEventUnchanged(source)`，`ApplySourceResult`／`ApplySourceFailure`
   各改一行呼叫；NR-065 註解搬進 helper（帶 NR 編號，本 repo 慣例）。
3. 驗證：全部既有測試通過；`git diff` 顯示除上述改動外無其他變更。

## Non-goals

- 不改 debounce 常數、不改 generation 語意、不改 `BeginGeneration` 的 snapshot 行為。
- 不抽 `CatalogRefreshCoordinator` 的基底類別或介面。
- 不順手改 `rebuild_pipeline.cpp` 的呼叫形狀。

## Acceptance

1. `catalog_refresh.cpp` 的 due predicate 只有一個實作（grep `kDebounceMs` 在
   `HasDueRebuild` 內零命中）。
2. event-clear 條件清除只有一份（grep `generation_event_snapshot_.at` 在兩個 Apply 內
   只剩 helper 一處——注意：此 grep 應在 NR-139 完成後執行，該 item 會把 `.at(` 改
   `.find(`，helper 抽的是 `if (current == snapshot)` 那段，不衝突）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "catalog_refresh|rebuild_pipeline" --output-on-failure
```

```powershell
rg -n "kDebounceMs" src/catalog/catalog_refresh.cpp
# expect: 只在 DueSources（與 helper）出現
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
