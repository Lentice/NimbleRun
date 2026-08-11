# NR-173 — RebuildPipeline 每次 rebuild 深拷貝整份列舉結果（const 後 copy-assign）

Phase 3 · Rebuild pipeline · Depends on: —

- Source: `AGENTS.md`（Keep search, ranking, scoring, persistence formats, and
  other core logic independent of HWND…）、`docs/design-spec.md` §FR-003
  （enumeration 有界）；NR-170 同檔先例
- Origin: 2026-08-11 第十六次稽核第 4 輪（codex backend，IMPORTANT）。主 Agent
  已重讀 `rebuild_pipeline.cpp:115-125` 驗證。
- Priority: **IMPORTANT**——每次 startup／Ctrl+R／watcher refresh 都複製整份
  `vector<AppEntry>`（每筆含多個 wstring）；大型 user-folder catalog 在
  handoff 前同時持有兩份完整結果，記憶體峰值翻倍、`bad_alloc` 失敗機率上升；
  與 NR-172（磁碟根目錄）合併後影響更明顯。

## Why

`RebuildPipeline::Start` 的 worker lambda（`src/app_host/rebuild_pipeline.cpp:115-125`）：

```cpp
const RebuildEnumeration enumeration =
    enumerate_source_(source, snapshot, &cancel_);   // :121
result->failed = !enumeration.source_ok;
result->entries = enumeration.entries;              // :124 深拷貝
result->diagnostics = enumeration.diagnostics;      // :125
```

`enumeration` 宣告為 `const`，`:124` 的 `=` 是 copy-assign（不是 move-assign）。
`AppEntry` 含 `display_name`／`source_path`／`launch_identity`／`search_alias` 等多個
`std::wstring`；列舉結果幾千到幾萬筆時，worker 執行緒在 `enumerate_source_`
回傳後到 `result` 註冊前，記憶體中同時存在兩份完整結果。local `enumeration`
之後不再被使用（`source_ok` 已拷出、entries/diagnostics 已複製），拷貝沒有
語意用途。

此路徑每次 rebuild 都跑（啟動、Ctrl+R、watcher debounce、launch-failure
refresh）。NR-170 已把 worker 建立與 handoff 收斂在 `RebuildPipeline` 內，
本 item 是同一函式的記憶體收尾。

## Decisions already made — do not reopen

1. **移除 `const`，`:124` 改 `result->entries = std::move(enumeration.entries)`**；
   `:125` 的 `diagnostics` 同樣 `std::move`（小型值，語意不變）。`source_ok`／
   `failed` 的讀取維持在 move 之前。
2. 加一個 move-observable 的 pipeline 測試：fixture 的 `EnumerateSource` 回傳
   帶長字串的 entries，測試斷言結果 entries 與來源等值（行為不變）；
   move 語意以 code review 斷言（無法在測試中觀察 move 發生與否，除非
   用自訂型別——依 repo 先例不做自訂型別 seam）。
3. 不改 `RebuildResult`／`AppEntry` 結構、不改 handoff、不改 coordinator。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`docs/work-items/NR-170`（本函式的既有測試 seam 與 completion 契約，不可回歸）。

## Files to read and trace first

- `src/app_host/rebuild_pipeline.cpp:115-125`（worker lambda 本體）。
- `src/catalog/app_entry.h`（`AppEntry` 成員，確認多 wstring 結構）。
- `tests/unit/rebuild_pipeline_test.cpp`（既有 fixture：`EnumerateSource` seam、
  entries 斷言形狀）。

## Scope

1. `:121` 移除 `const`；`:124-125` 改 `std::move`。
2. 測試：既有 rebuild 測試的 entries 等值斷言全綠（fixture 已有長字串更好，
   沒有就加一筆長 display_name 案例）。
3. 註解：`:123-125` 補一行「列舉結果 move 進 RebuildResult，不再持有副本
   （NR-173）」。

## Non-goals

- 不為 AppEntry 加 move 觀測型別、不新增自訂 allocator。
- 不改 `EnumerateSource` seam 的簽章、不改 handoff token 路徑。
- 不碰 UI 執行緒側（snapshot 組裝已由 NR-134 收斂）。

## Acceptance

1. `rebuild_pipeline_test` 全綠（entries 等值斷言證明行為不變）。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "rebuild" --output-on-failure
```

```powershell
rg -n "std::move\(enumeration" src/app_host/rebuild_pipeline.cpp
# expect: entries 與 diagnostics 各一處 move
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。

## 交接區

- 實作 commit：`ec683a2`（NR-173: move the enumeration result into
  RebuildResult）；文件 commit：`6cd90d8`（NR-173: close the ticket）。
- 改動檔案：`src/app_host/rebuild_pipeline.cpp`、
  `tests/unit/rebuild_pipeline_test.cpp`。
- 改動內容：
  - `rebuild_pipeline.cpp:121-127`（worker lambda）：`enumeration` 移除
    `const`；`:123` 的 `result->failed = !enumeration.source_ok;` 維持在 move
    之前讀取；`:126-127` 改為 `result->entries = std::move(enumeration.entries);`
    與 `result->diagnostics = std::move(enumeration.diagnostics);`；`:124-125`
    補一行 NR-173 註解（列舉結果 move 進 RebuildResult，不再持有副本）。
  - `rebuild_pipeline_test.cpp`：既有 fixture（`Enumerate`）的 entries 只有
    短字串，依 item Scope 2 加一筆長 display_name 案例——匿名命名空間新增
    `kLongStartName`（1024 字元 + 後綴），StartMenu 分支的 display_name 改用
    它；`TestForgedDeliveryFailureIgnored` 的 fresh-snapshot 斷言從
    `== L"Start"` 改為 `== kLongStartName`（entries 等值斷言，行為不變的
    證明）。AppsFolder 分支維持 `L"Apps"` 短字串（該 source 在 fixture 中
    固定 `source_ok=false`，走失敗路徑）。
- 驗證結果：Release build（LLVM-MinGW + Ninja）零新增 warning；完整 CTest
  31/31 passed（數量與改動前相同）；`ctest -R "rebuild"` 1/1 passed。
- Sanity grep 輸出（`rg -n "std::move\(enumeration"`）：
  ```
  src/app_host/rebuild_pipeline.cpp:126: result->entries = std::move(enumeration.entries);
  src/app_host/rebuild_pipeline.cpp:127: result->diagnostics = std::move(enumeration.diagnostics);
  ```
  （entries 與 diagnostics 各一處 move，符合 item 預期。）
- 偏差：無。
