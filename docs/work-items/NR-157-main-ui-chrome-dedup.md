# NR-157 — main.cpp UI chrome 重複：tray balloon 填表×2、面板空白選單與 tray 選單樣板×2

Phase 3 · Code structure · Depends on: —（NR-127 收斂運動的 main.cpp 漏網）

- Source: `AGENTS.md`（Reuse existing code before adding helpers or abstractions；
  UI strings are English and should be centralized when more than one screen needs them）、
  NR-060（面板空白選單與 tray 共用命令——共用的是命令分派，不是選單建構）
- Origin: 2026-08-10 第十四次稽核第 2 輪（ponytail 軸，LOW；high confidence）。
- Priority: **LOW**——重複 10 行與重複字串；漂移成本是「改一漏一不會報錯」。

## Why

`src/app_host/main.cpp` 兩件：

1. **tray balloon 填表兩份**（`:1916-1928` `ShowHotkeyConflictNotice` vs
   `:1934-1945` `ShowLoadIssueNotice`）：同一 `NOTIFYICONDATAW` 的
   NIF_INFO 填法 10 行逐字相同（註解自認「Same NOTIFYICONDATAW info-balloon filling
   as ShowHotkeyConflictNotice」），只差 `szInfo` 內容。
2. **面板空白處右鍵選單重複 tray 選單的建構樣板與字串**（`:2707-2724` vs
   `:1969-1990`）：同一 `CreatePopupMenu → AppendMenu → TrackPopupMenu →
   DestroyMenu → DispatchTrayCommand` 流程，且 `L"Refresh Apps"`／`L"Settings"`／
   `L"About"` 字面值在兩處重複（未進 `context_menu_strings`）。

## Decisions already made — do not reopen

1. 抽 `ShowInfoBalloon(HWND, std::wstring_view text)`（`ShowHotkeyConflictNotice`
   保留標題與 NIIF 行為的既有差異則以參數化或維持兩個薄包裝處理——以「NOTIFYICONDATAW
   填表只有一份」為硬性要求，包裝函式可以存在）。
2. 抽 `AppendAppCommands(HMENU)`（把 Refresh Apps／Settings／About 三項與其字串
   一次建好），tray 選單與面板空白選單共用；命令 id 與 `DispatchTrayCommand`
   不動。字串改用既有 `context_menu_strings`（檢查 `main.cpp` 既有的字串命名空間，
   沒有則就地建一個放三個字串，不得硬寫第二份）。
3. **零行為變更**：選單順序、分隔線、命令 id、分派路徑全不動；既有測試即回歸網。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> UI strings are English and should be centralized when more than one screen needs them.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`：`:1916-1945`（兩個 balloon）、`:1969-1990`（tray 選單建構）、
  `:2707-2724`（面板空白選單建構）、`context_menu_strings` 命名空間的位置與既有字串。
- `tests/` 中與 tray／選單相關的測試（若有）。

## Scope

1. `ShowInfoBalloon` 共用填表。
2. `AppendAppCommands` 共用選單項與字串。
3. 驗證：`git diff` 只含上述；Release build 零新增 warning；CTest 全綠（數量不變）。

## Non-goals

- 不重構 `TrackPopupMenu`／`DispatchTrayCommand` 的分派邏輯。
- 不動 `.rc`、不動 tray icon 的建立/移除。

## Acceptance

1. grep 驗證 `NOTIFYICONDATAW` 的 NIF_INFO 填表只有一份。
2. `L"Refresh Apps"` 等字面值只出現在 `context_menu_strings`（或共用 helper）一處。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "Refresh Apps" src/app_host/main.cpp
# expect: 一處（集中字串或 helper）
rg -n "NIF_INFO" src/app_host/main.cpp
# expect: 一處（ShowInfoBalloon）
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。
