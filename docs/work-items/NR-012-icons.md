# NR-012 — Lazy Shell icons and fallback

- Status: `done`
- Phase: 3
- Depends on: NR-005、NR-006、NR-010、NR-019
- Source: `docs/design-spec.md` §FR-009、§NFR-001、AC-006、AC-012

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

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §FR-009、§NFR-001、AC-006、AC-012、`docs/work-items.md`、本文件；trace `src/catalog/stable_id.h`、`src/catalog/app_entry.h`、`src/app_host/main.cpp`（NR-010 目前用 tile placeholder render）、`src/app_host/panel_model.h`、`src/catalog/dedup.h`。實作 lazy Shell icons：icon request key＝stable ID＋size＋DPI；fallback-first rendering state（第一幀先 fallback，真實 icon 就緒後非同步更新，不得造成格位重排）；decoded bitmap cache 上限 64 個 item 的 bounded LRU；COM／Shell resource lifecycle 與 failure isolation（icon failure 不讓 Catalog 或 list 消失）；icon loading 不阻塞 input state processing。只為目前可見列表項目載入。不預載整 Catalog、不做 matrix layout 或 theme polish。測試以 fake icon provider／純 cache fixtures 為主（cache hit/miss/eviction deterministic），Shell smoke 只驗 API failure isolation。可選擇接上 NR-010 的 render 路徑換掉 placeholder。回報修改檔案、測試命令、結果與未完成事項。
- Result: 完成。新增 `src/icons/icon_cache.{h,cpp}`（IconKey/IconBitmap/IconProvider/IconCache，LRU 上限 64，純值資料，provider 失敗不進 cache）與 `src/icons/shell_icon_provider.{h,cpp}`（SHCreateItemFromParsingName＋IShellItemImageFactory::GetImage，HBITMAP→premultiplied BGRA，failure 回傳空 bitmap）；新增 `tests/unit/icon_cache_test.cpp`（fake provider：hit／miss-then-insert／LRU eviction／reinsert 刷新 recency／failure 不 cache／size·dpi 分離 key／default cap）與 `nimblerun_icons` 庫。main.cpp 接上 render：fixed 30px tile 內 cache hit 畫真實 icon、miss 畫 fallback；fallback-first（post `kIconRequestMessage` 至訊息佇列尾，`LoadVisibleIcons` 只載入 viewport 內 rows，失敗 key 記入 `g_requested_icon_keys`、每次 ShowPanel 清除重試）；`ponytail:` 註解記錄 synchronous visible-set load 取捨。Agent checks：`cmake --build build` 成功；`ctest --test-dir build -R icons --output-on-failure` 1/1 通過；全套件 12/12 通過（原 11＋新增 icon cache）。未完成：無。
