# NR-007 — Stable identity and deduplication

- Status: `planned`
- Phase: 2
- Depends on: NR-005、NR-006
- Source: `docs/design-spec.md` §FR-006、§10.3

## Goal

讓同一 App 在不同來源出現時能穩定識別、去重，並讓 pins／usage 不依賴顯示名稱或排序。

## Scope

- Start Menu 與 AppsFolder identity normalization。
- stable ID 產生與版本化測試 fixtures。
- dedup precedence：可啟動性、icon 品質、User Start Menu、Common Start Menu。
- 無法可靠判定時保留項目並記錄診斷值。

## Non-goals

- 不以 display name 作唯一 merge key。
- 不在本 item 修改搜尋 ranking 或 usage formula。

## Acceptance

- 相同 fixture 產生相同 stable ID。
- 不同 App 不會只因同名被錯誤合併。
- dedup 結果排序穩定且可複製測試。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R identity --output-on-failure
```

使用純 values fixtures；不需要 HWND、Shell UI 或人工判讀。
