# NR-111 — Owner-drawn App rows 必須提供真正的 Windows accessibility tree

Phase 3 · UI accessibility

- Source: `docs/design-spec.md` §NFR-006、§4.9、§7、AC-009
- Origin: 2026-08-09 全 repo 稽核；核對 NR-015 的 model-level accessible label 與實際 HWND/UIA surface
- Priority: HIGH（目前 screen reader 看不到 owner-drawn App rows，NFR 只完成了資料映射而非平台暴露）

## Why

NR-015 已加入 `PanelModel::AccessibleNameFor` 與 `SelectedAccessibleName`，測試也驗證
display name 映射。但 `src/app_host/panel_model.h` 的註解仍寫「may wire these into
`WM_GETOBJECT`/`IAccessible` later」，全 repo 沒有 `WM_GETOBJECT`、`IAccessible`、
`IRawElementProvider*` 或 `UIA_*` 實作。

App 結果區是 `src/app_host/main.cpp::Render` 的 owner-drawn D2D grid/list，不是每列一個
原生 control；因此 Windows accessibility client 沒有 row element、accessible name 或
selection state 可讀取。純 model method 不等於平台 accessibility tree，故 NR-015 的
layout/theme 完成不代表 §NFR-006 已完成。

## Decisions already made — do not reopen

1. 使用 Windows 原生 accessibility API（MSAA 或 UI Automation 擇一），以最小 provider
   暴露現有 owner-drawn window／visible rows；不為每個 row 建立只供 accessibility 的 HWND。
2. Row name 來源仍是 `AppEntry.display_name`／既有 `PanelModel` mapping；不把 launch
   identity、完整個人路徑或搜尋 query 當作預設 spoken name。
3. Accessibility tree 必須反映目前 query、page、selection 與 missing-pin disabled state；
   不改 catalog data ownership，也不讓 UI 持有 Shell COM pointer。
4. English-only MVP、DPI/theme/high-contrast palette 與既有 keyboard behavior 不在此重做。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-006：

> 所有 App item 提供可存取名稱。

> 選取狀態不可只靠顏色表示。

`AGENTS.md`：

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/app_host/main.cpp` — `Render`、`CellAtPoint`、visible-row/page geometry、
  `WindowProc`／`WM_GETOBJECT` boundary、selection and missing-pin state。
- `src/app_host/panel_model.{h,cpp}` — `AccessibleNameFor`、`SelectedAccessibleName`、
  rows/page/query/selection contracts。
- `src/ui/panel_layout.{h,cpp}` — DPI-dependent row bounds used by hit testing and provider geometry。
- `tests/unit/ui_palette_layout_test.cpp` — current model-level accessibility assertions。
- `tests/integration/lifecycle_check.ps1`、`tests/CMakeLists.txt` — possible native-window
  accessibility smoke harness and registration。
- `docs/work-items/NR-015-dpi-theme-accessibility.md`、`docs/design-spec.md` §4.9／§7。

## Scope

1. Implement one native accessibility provider at the main window boundary for the search field,
   visible App rows and footer state; expose display names and current selection/disabled state。
2. Keep provider state synchronized with `PanelModel` after query changes, paging, selection,
   pin/missing state and DPI/layout changes without duplicating catalog data or Shell ownership。
3. Add one runnable provider mapping check plus a Windows smoke check with an accessibility
   inspection client or equivalent standard API query。

## Non-goals

- 不引入 UI framework、第三方 accessibility library 或 per-row child-window hierarchy。
- 不改 search ranking、launch identity、catalog refresh、theme palette 或 visual rendering。
- 不把人工 screenshot 當作 accessibility completion evidence。

## Acceptance

1. A Windows accessibility client can enumerate every visible App row and receive a non-empty
   accessible name matching the current display name。
2. Current selection, missing-pin disabled state and page/query changes are exposed through the
   native accessibility state; selection is not represented only by color。
3. Existing keyboard/mouse hit testing and `PanelModel` tests remain green; no Shell COM pointer
   is stored in catalog/model/UI accessibility data。
4. Release build has no new warnings; the focused mapping test and native-window smoke check pass。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "dpi_theme_accessibility|list_vertical_slice" --output-on-failure
```

```powershell
$hits = rg -n "WM_GETOBJECT|IAccessible|IRawElementProvider|AccessibleNameFor|SelectedAccessibleName" src tests
if ($LASTEXITCODE -ne 0) { exit 1 }
$hits
git diff --name-only
# expect: provider、必要 host/model seam 與 focused test；不改 catalog source。
```

## Handoff

實作者需記錄採用的 Windows accessibility API、element/state mapping、query/page/selection
同步方式、native smoke evidence、build／CTest 與任何 assistive-technology 差異。

