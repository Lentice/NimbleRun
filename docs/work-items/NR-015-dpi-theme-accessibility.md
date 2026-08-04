# NR-015 — DPI, theme, high contrast, accessibility

- Status: `planned`
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
