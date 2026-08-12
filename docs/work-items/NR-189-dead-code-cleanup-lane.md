# NR-189 — 死碼與重複收斂（第十七次稽核 ponytail 軸，P-1~P-9）

Phase 3 · Cleanup · Depends on: NR-188（建議最後做；所有其他 item 完成後行號不再移動，且不與 NR-181~188 的任何修改交疊）

- Source: 2026-08-12 第十七次全 repo 稽核（claude 報告 P-1~P-9；codex 報告 §1 表）
- Origin: 稽核結論「repo 沒有典型過度設計，只有小型死碼與零成本收斂，合計約 40 行淨減少」
- Priority: **LOW**（零行為變更；延續 NR-128／NR-145／NR-153 的 dead-code cleanup lane）

## Why

兩個稽核 agent 一致結論：無結構性過度設計（無單一實作 interface、無 reinvented stdlib、`docs/work-items.md` §已否決的方向 已擋掉六條抽象化衝動）。以下只是死碼與重複，全部是刪除或收斂，零行為變更。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_host/main.cpp`、`src/app_host/panel_model.{h,cpp}`、`src/app_host/rebuild_pipeline.cpp`、`src/ui/cell_tooltip.h`、`src/catalog/app_filter.h`。
- `docs/work-items/NR-128-dead-code-removal.md`、`NR-145-dead-code-cleanup-sequel.md`、`NR-153-nr147-dead-branch-cleanup.md` — 既有 cleanup lane 的格式與「搬移時把 NR 編號註解一起帶走」先例。

## Scope（每項都是刪除或一行收斂；逐項完成，不要留半）

1. **P-1** `main.cpp:1517`：刪未使用的 `const D2D1_SIZE_F target_size = g_render_target->GetSize();`（build 唯一 warning，`:1786` 已有正確 scope 的另一份）。
2. **P-2** `main.cpp:1272`（前置宣告）＋`:2078-2080`：刪 `ShowLoadIssueNotice` 轉呼叫，兩個呼叫點（`:1334`、`:3374`）改直接呼叫 `ShowInfoBalloon`。
3. **P-3** `main.cpp:978`（前置宣告）＋`:1380`：刪 `StartRebuild` 的第一個 `HWND` 參數（NR-132 後未使用），5 處呼叫點（`:1074`、`:2015`、`:2541`、`:2595`、`:3382`）的 `window` 實參一併刪。
4. **P-4** `main.cpp:2901-2909`：`WM_KILLFOCUS` 的自動隱藏分支與 `WM_ACTIVATE` 路徑合併（NR-085 後前者 dead on the two most common paths；合成同一段或標註共用，兩份隱藏規則不得並存）。
5. **P-5** `main.cpp:731-733`：`UpdateTooltipTimer` 的前兩行（`Hide()`＋`KillTimer`）改呼叫 `HideCellTooltip(window)`（`:750-753` 的集中 hide path）。
6. **P-6** `cell_tooltip.h:69`：刪 `CellTooltip::IsVisible()`（零呼叫者；`visible_` 成員保留，`Hide()` 需要它）。
7. **P-7** `panel_model.h:17-20`／`panel_model.cpp:243-251`：`PanelAction::identity` 只有測試在讀（`tests/unit/panel_model_test.cpp:122`）；生產路徑只讀 `action.launch`。刪 `identity` 欄位、該測試斷言；`PanelAction` 縮成 `bool`（或直接刪包裝讓 `Activate()` 回 bool——二選一，刪小的）。
8. **P-8** `app_filter.h:19-27`：刪 header 內匿名 namespace（`FileName` 已是 `inline`，匿名 namespace 使每個 TU 各持一份 internal-linkage 副本）。
9. **P-9** `main.cpp:337-339` `MonotonicMs()` vs `rebuild_pipeline.cpp:13-15` `NowMs()`：逐字相同的 `GetTickCount64` 包裝，刪其中一份（放 `catalog_refresh.h` 共用或直接在唯一呼叫點內聯——二選一，改小的）。

## Non-goals

- 不重開 §已否決的方向（PanelHost、icons 疊層重寫、泛用 persistence 等全部維持否決）。
- 不改任何行為：這九項全部是「刪除或一行收斂」，CTest 全綠是回歸網。
- 不刪 `docs/adr/*`、不編輯已完成 item 文件。
- 不做 I-7（watcher 1 Hz 退避；已知決策 NR-074，需覆寫聲明才能重開——本 item 不重開）。

## Acceptance

- `cmake --build build` 零 warning（P-1 後唯一 warning 消失）。
- 九項逐一完成（grep 確認：`ShowLoadIssueNotice`、`PanelAction::identity`（含測試）、`IsVisible()`、匿名 namespace 的 `FileName`、`MonotonicMs`／`NowMs` 一份為零）。
- `ScrollBy`／tooltip timer／StartRebuild 呼叫點行為不變（CTest 覆蓋）。
- Release build 無 error／新增 warning；CTest 全綠（32 項）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "ShowLoadIssueNotice|MonotonicMs|NowMs|IsVisible\(\)|identity" src/app_host/main.cpp src/ui/cell_tooltip.h src/app_host/panel_model.* tests/unit/panel_model_test.cpp
rg -n "namespace \{|FileName" src/catalog/app_filter.h
```

驗證：build 0 error 0 warning；CTest 全 Passed；上列符號各只剩合理數量（identity 零命中、一份 MonotonicMs 或 NowMs）。

## 交接區

實作（2026-08-12，NR-189 done）── 九項逐一確認清單：

1. **P-1** 已刪：`Render()` grid 分支的 `const D2D1_SIZE_F target_size = g_render_target->GetSize();` 與其 NR-133 註解（該註解描述的 FooterTopDip/SlotRect 依賴在 NR-145 後已不存在）；`:1817` 的 footer 那份保留。這同時是 build 唯一 warning 的來源，清理後 zero warning。
2. **P-2** 已刪：`ShowLoadIssueNotice` 定義（原 `:2114-2116`）與前置宣告（原 `:1295`）。前置宣告改為 `ShowInfoBalloon`（保留 NR-058 註解與「caller 清旗標、一次至多一個 balloon」語意，合併到前置宣告註解）；兩個呼叫點（`HandlePinLoadResult`、startup store-load 通知）直接呼叫 `ShowInfoBalloon`。
3. **P-3** 已刪：`StartRebuild` 的 `HWND` 參數（宣告 `:985`、定義 `:1411`），5 個呼叫點（`:1091`、`:2044`、`:2574`、`:2628`、`:3428`）的 `window` 實參一併刪除。
4. **P-4** 合併形狀與理由：**直接刪除 panel wndproc 的 `WM_KILLFOCUS` 分支（原 `:2938-2946`），`WM_ACTIVATE(WA_INACTIVE)` 成為唯一 outside-click 隱藏規則**。理由：NR-085 之後 panel 本身永不持有鍵盤焦點（ShowPanel 一律 `SetFocus` 到 search EDIT child），所以 panel 收不到 `WM_KILLFOCUS`——該分支不只「two most common paths 上 dead」，是任何路徑都收不到；`g_search_edit && GetFocus() != g_search_edit` 條件正是這個事實的殘跡。WM_ACTIVATE 的 NR-085 註解改寫為單一規則敘述並補 NR-189 說明，兩個 modal 豁免旗標（`g_context_menu_active`／`g_dialog_active`）只留在 WM_ACTIVATE。EDIT subclass 的 `WM_KILLFOCUS`（caret mirror，原 `:2400`）與此無關，保留。
5. **P-5** 已做：`UpdateTooltipTimer` 前兩行（`g_cell_tooltip.Hide()`＋`KillTimer`）改為 `HideCellTooltip(window)`；因 `HideCellTooltip` 定義在 `UpdateTooltipTimer` 之後，在 `UpdateTooltipTimer` 前加了一行前置宣告（含 NR-178 註解）。
6. **P-6** 已刪：`cell_tooltip.h:69` 的 `CellTooltip::IsVisible()`；`visible_` 成員保留（`Show`/`Hide` 在 cell_tooltip.cpp 仍讀寫）。
7. **P-7** 選擇：**保留包裝、縮成單一 bool**（`struct PanelAction { bool launch = false; };`）——刪 `identity` 欄位、`Activate()` 內刪 `action.identity = ...` 一行、測試 `TestEnterLaunchesSelectedOnly` 的 identity 斷言刪除。不選「刪包裝讓 Activate() 回 bool」：那要改 4 個測試呼叫點＋`using`＋`main.cpp` 的 `VK_RETURN` 呼叫端（約 15 行 vs 3 行），diff 較大；單一 bool 包裝成本可忽略，且測試的「Enter on a selection launches」語意不需改寫。header 註解改寫並註記 NR-189。
8. **P-8** 已做：`app_filter.h` 刪匿名 namespace 的 `namespace {`／`} // namespace` 兩行；`FileName` 已是 `inline`，去掉後為單一定義（外部 linkage），註解補 NR-189 說明。
9. **P-9** 選擇：**刪 `main.cpp` 的 `MonotonicMs()`（3 行），在唯一呼叫點（`ShowPanel` 的 `ShouldRefreshAppsFolder` 檢查）內聯 `static_cast<std::int64_t>(GetTickCount64())`**。不選放 `catalog_refresh.h` 共用：`MonotonicMs` 只有 1 個呼叫點，內聯是更小 diff；`rebuild_pipeline.cpp` 的 `NowMs()`（3 個呼叫點）保留為唯一一份。grep 驗證：`MonotonicMs` 零命中、`NowMs` 只剩 rebuild_pipeline.cpp 的定義＋3 呼叫。

**Sanity grep 證據**（item Agent checks 兩條命令）：`ShowLoadIssueNotice`、`MonotonicMs`、`IsVisible()` 零命中；`PanelAction::identity` 零命中（`identity` 剩餘命中全是 `AppEntry::launch_identity` 與無關註解）；`NowMs` 只剩 rebuild_pipeline.cpp 一份；`namespace {` 在 app_filter.h 零命中、`FileName` 只剩 inline 定義與兩個內部呼叫端（`FileStem`／`Extension`）。

**build／CTest 證據**：Release x64（LLVM-MinGW + Ninja，`cmake --clean-first` 完整重建）**0 error 0 warning**（P-1 後唯一 warning 消失）；CTest **32/32 Passed**（197.67s），測試目標數不變。行為零變更：CTest 為回歸網，九項全部為刪除或一行收斂。

**Commit**：`NR-189: dead code cleanup lane (P-1..P-9)`，含本文件交接區與 `docs/work-items.md` 狀態 `ready`→`done`。
