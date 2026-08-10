# NR-154 — ParseInt 收斂只做了一半：settings_store 與 settings_dialog 仍是兩份拷貝

Phase 1 · Code structure · Depends on: —（NR-144 的續集）

- Source: `AGENTS.md`（Prefer the smallest working change. Reuse existing code…）、
  NR-127（helper 收斂運動）、NR-144（本該收斂卻只對齊）
- Origin: 2026-08-10 第十四次稽核第 2 輪（ponytail 軸，MEDIUM；high confidence——
  兩份逐字比對）。主 Agent 已讀兩檔驗證。
- Priority: **MEDIUM**——NR-127 要消滅的 `ParseInt` 拷貝從 ×3 只降到 ×2。

## Why

NR-144 把 `settings_store.cpp` 的 `ParseInt`（`:31-42`）對齊到
`settings_dialog.cpp:310-321`（`ParseCountText`）的形狀——兩者現在是同一
「`ParseInt64` + int 範圍守門」的兩份拷貝（dialog 回 -1、store 回 bool，其餘逐行
相同）。NR-144 的交接稱 dialog 版本「已示範收斂形狀」，實際是把第二份拷貝改成與
第一份相同，沒有抽出共用函式。

## Decisions already made — do not reopen

1. 在 `src/storage/atomic_text_file.h` 的 `ParseInt64` 旁新增
   `inline bool ParseInt(std::wstring_view text, int& out)`（`ParseInt64` + int 範圍
   守門），兩個呼叫端改用。
2. **零行為變更**：store 版回 bool、dialog 版維持回 -1 的包裝（dialog 的
   `ParseCountText` 若只是轉發也可刪掉改用 `ParseInt`，視其實際呼叫端而定——以
   「dialog 檔內不再出現第二份整數解析」為準）。
3. 不新增測試目標：`settings_store_test`／`settings_editor_test` 即回歸網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/storage/atomic_text_file.h`：`:214-227`（`ParseInt64`）。
- `src/settings/settings_store.cpp`：`:31-42`（`ParseInt`）。
- `src/app_host/settings_dialog.cpp`：`:310-321`（`ParseCountText`）。
- 兩檔的 include 現況（`atomic_text_file.h` 是否已 include）。

## Scope

1. `atomic_text_file.h` 新增 `ParseInt`（`ParseInt64` 內層 + `INT_MIN/INT_MAX` 守門）。
2. `settings_store.cpp` 的 `ParseInt` 改為引用共用版（刪本地拷貝）。
3. `settings_dialog.cpp` 的 `ParseCountText` 改用共用版（保留回 -1 的語意）。
4. 驗證：`git diff` 只含上述改動；全部測試通過。

## Non-goals

- 不重構 `ParseInt64`、不新增第三種整數解析。
- 不順手收斂其他檔案的同類殘留。

## Acceptance

1. grep 驗證 `settings_store.cpp`／`settings_dialog.cpp` 內沒有第二份
   `wcstoll`／`INT_MAX` 範圍守門迴圈。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings" --output-on-failure
```

```powershell
rg -n "numeric_limits<int>|INT_MAX" src/settings/settings_store.cpp src/app_host/settings_dialog.cpp
# expect: 零命中（守門只存在於 atomic_text_file.h）
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
