# NR-165 — RecordLaunch 的 total_launches 對 UINT64_MAX 無號回繞為 0

Phase 1 · Untrusted store data · Depends on: —

- Source: `AGENTS.md`（Keep all user data under `%LOCALAPPDATA%`…）、
  `docs/design-spec.md` §10.2（usage.tsv 是不受信輸入的可解析格式）、
  NR-070 先例（store 資料檔是 untrusted input，手改檔案造成的異常值要守）
- Origin: 2026-08-11 第十六次全 repo 稽核（codex backend，MINOR）。主 Agent
  已重讀 `usage_store.cpp:160-177`、`atomic_text_file.h:250-271` 驗證。
- Priority: **LOW**——僅手改/損壞的 usage.tsv 可觸發；無 UB、無 crash，但會把
  lifetime count 與排序靜默破壞並自我永續。

## Why

`UsageStore::Load`（`usage_store.cpp:83`）用 `ParseUint64`（`atomic_text_file.h:250-271`）
解析 `total_launches`；該函式接受完整 `uint64_t` 範圍（`UINT64_MAX` 是合法解析值，
NR-070 只擋了 `-` 前綴）。`UsageStore::RecordLaunch`（`usage_store.cpp:166`）對既有
record 執行 `++record.total_launches`——`UINT64_MAX` 遞增依無號算術回繞為 0。

後果：下一次成功啟動後，該 record 的 lifetime count 變成 0 且 `Save()` 落盤
（回繞自我永續）；`UsageScore()`（`usage_store.h:100-110`）的計數項歸零、
`Recent()` 排序依據受污染。`UsageScore` 的一百萬 clamp（`kMaxLaunchCount`）只保護
計分端，不保護更新端。觸發面是手改 `usage.tsv` 或第三方寫入的損壞檔——NR-070
明文處理的同一輸入面。

## Decisions already made — do not reopen

1. **採 saturating increment**：`total_launches` 已是 `UINT64_MAX` 時不再遞增
   （`if (record.total_launches != std::numeric_limits<std::uint64_t>::max())`）。
   保留 schema 的 lifetime counter 語意，不做 load 端 clamp（load 端 clamp 會
   改變既存合法檔的計數值，影響排序歷史）。
2. 只修 `RecordLaunch` 既有 record 分支；新增 record 的初始值 `1` 不受影響。
3. 不改 `ParseUint64`（`UINT64_MAX` 是合法資料；守門在更新端）。

## Binding constraints — quoted, do not go looking for them

`docs/work-items/NR-070`（store 資料檔不受信輸入的先例）：「手改資料檔的
異常值必須被守，不得污染 store 語意」。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/usage/usage_store.cpp:160-177`（`RecordLaunch`）。
- `src/storage/atomic_text_file.h:250-271`（`ParseUint64`）。
- `src/usage/usage_store.h:15-19`（`UsageRecord::total_launches` 型別）。
- `tests/unit/recent_usage_test.cpp`（`TestRecordLaunch` 等既有案例形狀）。

## Scope

1. `usage_store.cpp` 的 `RecordLaunch`：既有 record 分支改 saturating increment。
2. 測試：`recent_usage_test.cpp` 新增案例——載入 `total_launches = UINT64_MAX`
   的 record（以 Load 注入，或直接構造 record 後以純函式驗證），`RecordLaunch`
   後計數維持 `UINT64_MAX` 且 `Save()/Load()` round-trip 一致。
3. 不需要改 schema、不改 Save 格式、不新增設定。

## Non-goals

- 不實作 §4.6 的完整 usage 公式（lifetime total＋bonus——NR-126 已記為刻意省略）。
- 不為 usage.tsv 的 `UINT64_MAX` 走 corrupt 路徑（計數極大但格式合法，saturate
  即可，不需要整檔隔離）。

## Acceptance

1. `total_launches == UINT64_MAX` 時 `RecordLaunch` 後仍為 `UINT64_MAX`。
2. 既有 `RecordLaunch`／`Save`／`Load` 測試全綠。
3. Release build 無新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "usage" --output-on-failure
```

```powershell
rg -n "UINT64_MAX|total_launches" src/usage/usage_store.cpp tests/unit/recent_usage_test.cpp
# expect: saturating guard 存在；測試涵蓋回繞案例
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。
