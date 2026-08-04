# NR-012 — Lazy Shell icons and fallback

- Status: `planned`
- Phase: 3
- Depends on: NR-005、NR-006、NR-010
- Source: `docs/design-spec.md` §FR-008、§NFR-001、AC-006

## Goal

只為目前可見列表項目載入 Shell icon，失敗時保持固定 fallback，不阻塞搜尋或造成格位重排。

## Scope

- icon request key：stable ID、尺寸、DPI。
- fallback-first rendering state。
- decoded bitmap cache，最多 64 個 item 的 bounded LRU。
- COM／Shell resource lifecycle 與 failure isolation。

## Non-goals

- 不預載整個 Catalog 的圖示。
- 不在本 item 做 matrix layout 或 theme polish。

## Acceptance

- icon failure 不會讓 Catalog 或 list 消失。
- cache hit／miss／eviction 有 deterministic test。
- icon loading 不阻塞 input state processing。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R icons --output-on-failure
```

測試以 fake icon provider／純 cache fixtures 為主；Shell smoke 只驗 API failure isolation，不操作 UI。
