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

（實作者填寫：九項逐一刪除的確認清單、P-4 合併形狀與理由、P-7 的選擇（bool 或刪包裝）、build／CTest 證據）
