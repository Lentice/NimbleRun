# NR-148 — 「開啟檔案位置／內容」未與啟動共用 launch_verified 守門

Phase 2 · Security · Depends on: —（NR-113 的守門家族補齊；一行守門）

- Source: `AGENTS.md`（Launch apps through Windows Shell APIs. Never build an arbitrary
  command line from search input）、`docs/design-spec.md` §10.2／§10.4（cache 是磁碟上
  不受信輸入）、NR-113（cache 項目未經來源驗證不得啟動）
- Origin: 2026-08-10 第十四次全 repo 稽核（安全軸，LOW；high confidence——agent 讀了
  選單分支與 launch 守門兩處）。主 Agent 已對照驗證。
- Priority: **LOW**——無執行風險（properties verb 只開屬性頁）；但讓 cache 內容驅動
  Explorer 對未列舉路徑的動作，與 NR-113 的守門精神不一致，一行可補。

## Why

`ShowItemMenu`（`src/app_host/main.cpp:2071-2077`）把「開啟檔案位置」與「內容」gate 在
`IsPathIdentity(entry.launch_identity)`；而啟動路徑（`LaunchEntry` →
`src/launch/shell_launch.cpp:8-10`）gate 在 `launch_identity` 非空 **且 `launch_verified`**
（NR-113：cache 載入的項目 `launch_verified = false`，`app_entry.h:37` 預設 true、
cache 讀回時設 false）。

冷啟動 cache 種子（NR-116：rebuild 完成前面板顯示 cache 內容）期間，一個被改寫的
`catalog.cache` 可以把 `launch_identity` 設成任意絕對路徑（如 `C:\Windows\System32\calc.exe`
或任意文件）。該列**不能啟動**（launch_verified 擋住），但右鍵 → 「開啟檔案位置」會
`SHOpenFolderAndSelectItems`、或「內容」會以 Shell properties verb 對該路徑動作——
Explorer UI 被 cache 內容驅動到使用者從未列舉過的路徑。無任意程式碼執行
（properties verb 只開屬性頁），是守門不一致的越界，不是漏洞升級。

## Decisions already made — do not reopen

1. `ShowItemMenu` 的 Shell 動作分支加 `entry.launch_verified`：
   `if (IsPathIdentity(entry.launch_identity) && entry.launch_verified)`。
2. **與啟動共用同一條守門**（NR-113 的形狀），不另寫第三種驗證。
3. 不為一行守門新增測試目標；既有 suite 即驗證網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Launch apps through Windows Shell APIs. Never build an arbitrary command line from search input.

（本 item 不組命令列；守門對象是「哪些路徑值得讓 Shell 動作」的邊界一致性。）

## Files to read and trace first

- `src/app_host/main.cpp`：`:2071-2077`（`ShowItemMenu` 的 Shell 動作分支）。
- `src/launch/shell_launch.cpp`：`:8-10`（啟動守門，對照形狀）。
- `src/catalog/app_entry.h`：`:37`（`launch_verified` 語意）。
- `src/catalog/catalog_cache.cpp`（cache 讀回時如何設 `launch_verified = false`，確認
  欄位在 menu 路徑可用）。

## Scope

1. 一行守門：`ShowItemMenu` 的 `IsPathIdentity` 分支加 `&& entry.launch_verified`；
   註解引用 NR-113 與本 item（「與啟動共用同一守門」）。
2. 確認 cache-sourced 列的 `launch_verified` 在 `ShowItemMenu` 可讀（是 `AppEntry`
   copy，已是）。
3. 驗證：Release build + CTest 全綠。

## Non-goals

- 不提供「cache 列不可啟動但可開位置」的替代路徑（NR-116 的 cache 種子是暫時顯示，
   重建完成即由真實列取代）。
- 不改 launch 守門本身、不改 `catalog_cache` 的寫入。
- 不為此加 UI 或測試。

## Acceptance

1. grep 驗證 `ShowItemMenu` 的 Shell 動作分支含 `launch_verified`。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -A 6 "IsPathIdentity\(entry.launch_identity\)" src/app_host/main.cpp
# expect: 條件含 launch_verified
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
