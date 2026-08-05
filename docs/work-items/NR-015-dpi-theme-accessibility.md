# NR-015 — DPI, theme, high contrast, accessibility

- Status: `done`
- Phase: 3
- Depends on: NR-010、NR-012、NR-013
- Source: `docs/design-spec.md` §4.9、§6 NFR-006、§7、AC-009

## Goal

讓列表面板在 per-monitor DPI、系統色彩與基本鍵盤／可存取狀態下保持可使用。

## Scope

- 100%、150%、200% layout calculations。
- system／light／dark theme state。
- high contrast、text scaling、reduced animation flags。
- item accessible name 與非顏色 selection state。

## Non-goals

- 不加入動畫背景、blur、acrylic 或跨語系 UI。
- 不以人工 screenshot 作 Agent item completion。

## Acceptance

- 同一 layout math 對不同 DPI／work area 給出可預期 bounds。
- theme／high contrast state 不會改變 Catalog 或 launch identity。
- selection 有文字／邊框等非顏色訊號。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R dpi_theme_accessibility --output-on-failure
```

使用 synthetic monitor／DPI／system setting fixtures；只驗 state、bounds 與 accessible labels。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §4.9、§6 NFR-006、§7、AC-009、`docs/work-items.md`、本文件；trace `src/app_host/main.cpp`（NR-010/012 面板 render、fixed 30px tile、DPI awareness context、ShowPanel 尺寸）、`src/app_host/panel_model.h`、`src/icons/icon_cache.h`、`src/settings/settings_store.h`（Theme enum）。實作 DPI、theme、high contrast 與 accessibility：100/150/200% layout calculations（layout math 對不同 DPI／work area 給出可預期 bounds）；system／light／dark theme state；high contrast、text scaling、reduced animation flags；item accessible name 與非顏色 selection state（selection 有文字／邊框等非顏色訊號）。不加入動畫背景、blur、acrylic 或跨語系 UI；不以人工 screenshot 作完成條件。建議以純 values state／layout model 驗證 DPI scaling、theme state、high contrast state 與 accessible labels；可接上 NR-010/012 的面板 render。回報修改檔案、測試命令、結果與未完成事項。
- Result: `done`（2026-08-05，主 Agent 直接實作）。新增純值 `ui/panel_layout.{h,cpp}` 與 `ui/panel_palette.{h,cpp}`（`nimblerun_ui` 庫，namespace `nimblerun::layout`／`nimblerun::palette`，無 HWND/COM）：DIP 常數＋`LayoutForDpi(dpi)`（DIP×dpi/96 的 physical px，96/144/192 可預期）＋`ClampWindowSize`（保留 32px 邊距）；`ResolveColors(Theme, system_dark, high_contrast, SystemColors)` 回傳 0xRRGGBB 純值色（light/dark 自訂、HC 用注入系統色、`selected_border` 為獨立非顏色選取訊號）。main.cpp 接上：Render 採 DIP 幾何＋每幀 ResolveColors（色變重建 brushes）、選取列 1–2px 邊框、RowAtY／icon key／VisibleRowCount 用 `LayoutForDpi(GetDpiForWindow)`、ShowPanel 用 `GetDpiForMonitor` 做 DIP 尺寸＋work-area clamp、`WM_DPICHANGED` 用 suggested rect＋重排 search EDIT、主題每次 ShowPanel 重載。accessibility 採 model-level：`PanelModel::AccessibleNameFor`／`SelectedAccessibleName`；未實作 IAccessible（見報告）。Agent checks 通過：`cmake --build build` OK、`ctest -R dpi_theme_accessibility` 1/1、全套件 14/14（原 13 全綠＋新增 1）。
