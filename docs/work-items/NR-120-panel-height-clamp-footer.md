# NR-120 — 面板高度 clamp（work-32px）在高 DPI 小螢幕裁掉 footer band；spec §4.9「工作區 70%」上限未實作且未對齊

Phase 3 · Panel layout · Depends on: NR-015, NR-082, NR-103（皆 done）

- Source: `docs/design-spec.md` §4.2（path bar）、§4.9（高度上限與 key hints）
- Origin: 2026-08-10 第十三次全 repo 稽核（spec 符合度軸＋正確性軸交叉發現）；主 Agent 已驗證
  `panel_layout.cpp` 公式與 spec 條文
- Priority: **IMPORTANT**（spec 明訂要繪製的 path bar 與按鍵指引在 ≥200% DPI 小螢幕上整條不可見；
  且 spec 的 70% 上限數字從未實作，兩文件與實作三方矛盾）

## Why

`ClampWindowSize`（`src/ui/panel_layout.cpp:37-43`）：

```cpp
out.width  = std::min(layout.panel_width,  std::max(1, work_width  - 32));
out.height = std::min(layout.panel_height, std::max(1, work_height - 32));
```

`kPanelHeightDip = 488`（`panel_layout.h:12`）。1366×768 筆電開 200% DPI（工作區約 728px）：
面板高度 488×2 = 976px → clamp 到 696px。內容以 DIP 繪製：list 最後一列止於約 456 DIP
（= 912px）被裁；footer band（path bar ＋ 按鍵指引，`panel_layout.h` 的 462~482 DIP 區間，
第五輪稽核紀錄已量測）落在 924~964px → **整條不可見**。grid 狀態的最後一列同樣被裁。

spec 側（`docs/design-spec.md` §4.9）：「預設寬度 640 DIP、高度 488 DIP；高度依內容調整，
上限為目前螢幕工作區的 **70%**」——「70%」從未實作（實作是 work-32px），「高度依內容調整」
也是舊設計殘留（面板高度固定 488 DIP）。三方（spec 數字／`panel_layout.h:96-98` 的
「keep 32 px margin」註解／實際行為）彼此矛盾。第五輪稽核把 footer 裁切記為已知低度發現
（「屬 ClampWindowSize 與 footer 幾何的版面設計決策，非 hit-test 範圍」），但從未以 item
追蹤；本次稽核重新判定為 IMPORTANT：§4.2/§4.9 明訂要繪製的元素整條消失，屬使用者可見功能損失，
且 spec 違反（70% 從未生效）持續至今。

## Decisions already made — do not reopen

1. 本 item 是**版面決策**，不是 hit-test 修補：NR-064/NR-082 的 `CellAtPoint` 界限（「只命中繪製中的
   格／列」）不動；若面板在壓縮後仍保持「最後一列可能不完整」，該兩案的既有語意維持。
2. 推薦修法（Option A）：**footer 恆可見**——完整 488 DIP 放不下時壓縮 list/grid 的可見列數
   （列高不動、`ViewportRows` 縮水），使 path bar＋key hints 永遠在 client 內；clamp 維持
   work-32px（不改成 70%，因為 70% 在 200% DPI 小螢幕上比 work-32px 更小，仍裁 footer，且
   work-32px 的「保留邊距」設計在 `panel_layout.h` 有明文意圖）。同步把 spec §4.9 的上限條文改寫為
   「完整面板放不下時壓縮可見列數；footer 恆可見」。
3. 若產品決策改採 Option B（接受裁切、只改 spec 記錄限制），本 item 縮小為純文件同步——但
   **優先採 A**，因為 §4.2/§4.9 的元素是規格承諾，裁切即違約。
4. 列高／面板寬度／字型等版面常數一律不動；只改「放不下時能少畫幾列」這個自由度。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.9：

> 預設寬度 640 DIP、高度 488 DIP；高度依內容調整，上限為目前螢幕工作區的 70%。

（本 item 交付時需把此條改寫為與 Option A 一致的新條文——改寫後的條文以本 item 的 Decisions §2 為準。）

`docs/design-spec.md` §4.2：

> path bar（空白狀態顯示目前目錄）與 footer 按鍵指引必須在面板內可見。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/ui/panel_layout.{h,cpp}` — `kPanelHeightDip`（h:12）、`ClampWindowSize`（cpp:37-43）、
  幾何常數（h:96-98 註解）。
- `src/app_host/main.cpp` — `ClampWindowSize` 呼叫點（`:2338-2341` 一帶）、`ViewportRows` 的
  消費端（list/grid 繪製迴圈、`CellAtPoint`）。
- `src/app_host/panel_model.{h,cpp}` — `ViewportRows`／列數決定。
- `tests/unit/ui_palette_layout_test.cpp` — 既有 layout 測試（`LayoutForDpi`／`ClampWindowSize` 案例）。
- `docs/design-spec.md` §4.2、§4.9。

## Scope

1. 先量測：列出「DPI × 工作區高度」組合中 488 DIP 面板放不下、footer 被裁的組合集
   （200%＋768px 已知失敗；100%/125%/150% 與 1080p/1440p 是否安全）。把量測結果寫進
   `docs/performance-baseline.md` 或本 item 交接區。
2. 依 Option A 實作：`ViewportRows`（或繪製迴圈的列數來源）在 `client_height` 不足以容納
   「search box＋footer band」時減少可見列數，footer band 恆在 client 內；grid/list 兩態都要覆蓋。
3. 同步 `docs/design-spec.md` §4.9：把「高度依內容調整、上限 70%」改寫為 Option A 的實際規則
   （完整面板放不下時壓縮可見列數；上限維持 work-32px 邊距語意），並在 §4.2 確認 path bar
   可見性條文不再與實作矛盾。
4. 新增 focused 測試：`ui_palette_layout_test`（或 panel_model 測試）對「clamp 後 footer 可見、
   可見列數正確縮水」的幾何案例；測試不依賴實際 HWND（純 `layout::`／model 值域）。

## Non-goals

- 不改 `CellAtPoint`／hit-test 界限（NR-064/NR-082 範圍）；不縮面板寬度；不縮列高／字型；
  不加捲動第二頁（NR-021 的既有翻頁語意不動）。
- 不做「動態改 488 DIP」的 adaptive 高度；不改 `kPanelHeightDip`。
- 不開「70% 上限」的歷史決策（本 item 決策 §2 已覆寫：維持 work-32px 邊距語意並改寫 spec）。

## Acceptance

1. ≥200% DPI 小螢幕（如 1366×768）上：path bar 與按鍵指引完整可見，list/grid 最後一列不超出 client。
2. 正常尺寸螢幕（1080p 100%~150%）行為與現況逐像素相同（不壓縮、不改變任何幾何）。
3. `design-spec.md` §4.9/§4.2 與實作一致（grep 無「70%」殘留或已改寫為新規則）。
4. Release build 無新增 warning；完整 CTest 與 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "panel_layout|palette|list_vertical_slice" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "70%|0\.70|work_height" docs/design-spec.md src/ui
# expect: spec 已改寫為壓縮規則；ClampWindowSize 語意一致。
git diff --name-only
# expect: 只動 panel_layout/panel_model、layout 測試與 design-spec。
```

## Handoff

實作者需記錄量測組合表、壓縮規則（哪個函式改、grid/list 兩態、邊界）、spec 改寫後的新條文、
測試案例與 build／CTest 證據。

### 交接區（2026-08-10，實作完成）

**量測組合表**（工作區高度 W、scale = DPI/96；面板 px = `lround(488×scale)`，clamp 後高 =
`min(面板px, W-32)`；client DIP = clamp 後高 / scale；本 item 採 Option A，故 footer band
恒貼齊 client 底緣：footer top = `min(456, client_dip − 32)`）：

| DPI | 螢幕／工作區高 W | 面板 px | clamp 結果 px | client DIP | footer top DIP | list 列數 | grid 列數 | 修前是否裁 footer |
|---|---|---|---:|---:|---:|---:|---:|---|
| 100% | 1366×768（~728） | 488 | 488（完整） | 488 | 456 | 8 | 4 | 否 |
| 100% | 1080p（~1040） | 488 | 488 | 488 | 456 | 8 | 4 | 否 |
| 100% | 1440p（~1400） | 488 | 488 | 488 | 456 | 8 | 4 | 否 |
| 125% | 1366×768（~728） | 610 | 610（完整） | 488 | 456 | 8 | 4 | 否 |
| 125% | 1080p／1440p | 610 | 610 | 488 | 456 | 8 | 4 | 否 |
| 150% | 1366×768（~728） | 732 | 696（clamp） | 464 | 432 | 7 | 3 | **是（新發現）** |
| 150% | 1080p／1440p | 732 | 732 | 488 | 456 | 8 | 4 | 否 |
| 200% | 1280×720（~720） | 976 | 688（clamp） | 344 | 312 | 5 | 2 | **是** |
| 200% | 1366×768（~728） | 976 | 696（clamp） | 348 | 316 | 5 | 2 | **是** |
| 200% | 1080p（~1040） | 976 | 976（完整） | 488 | 456 | 8 | 4 | 否 |
| 200% | 1440p（~1400） | 976 | 976 | 488 | 456 | 8 | 4 | 否 |

結論：footer 被裁的組合條件為 `工作區高 < 488×scale + 32`。除了 item 已知的 **200%＋768px**，
量測發現 **150% 在 1366×768（工作區 728）也會裁 footer**（clamp 到 696px＝464 DIP）——
item 原本只列 200% 為已知失敗，這筆是新證據。100%/125% 在所有目標螢幕安全；1080p/1440p 的
100%/125%/150%/200% 全部安全（200% 需工作區 ≥ 1008px，1080p 的 ~1040px 通過）。

**壓縮規則（Option A，footer 恆可見）**：

- 新增兩個純值幾何函式於 `src/ui/panel_layout.{h,cpp}`（無 HWND，可測）：
  - `layout::FooterTopDip(float client_height_dip)`：footer band 恒高 32 DIP
    （`kPanelHeightDip − kFooterTopDip`）貼齊 client 底緣；完整 488 DIP client 回傳
    `kFooterTopDip` 原值，clamp 後回傳 `client_height_dip − 32`，下限 `kListTopDip`（防
    footer 疊到搜尋框）。
  - `layout::ViewportRowsForHeightDip(float client_height_dip, int columns)`：可見列數 =
    `floor((FooterTopDip − kListTopDip) / row_height)`，`columns>1` 用 `kCellHeightDip`
    （grid）、否則 `kRowHeightDip`（list）；下限 1。列高／格高／面板寬度／字型／
    `kPanelHeightDip` 皆未動。
- `src/app_host/main.cpp` 三處消費端：
  1. `UpdateViewportRows`（原 :778-793）：改吃 `ViewportRowsForHeightDip(client_px / scale,
     Columns())`。修前 `ViewportRows` 用 client 底邊算列數，clamp 時列畫到 footer 下方、
     footer 整條出畫面；修後列止於 footer band 上緣。
  2. `Render()` footer band（原 :2190-2201）：分隔線與 key box 的 Y 改用
     `FooterTopDip(g_render_target->GetSize().height)`（D2D render target 尺寸為 DIP），
     box 垂直置中在 band 內。完整高度時與修前逐 DIP 相同（footer top=456、box 462~482）。
  3. `SyncAccessibility` 的 `footer_bounds`（原 :839-842）：改以同一 `FooterTopDip`
     + `GetClientRect` 計算 band 頂／底，螢幕閱讀器回報位置與實際繪製一致。
- **grid/list 兩態**都涵蓋：兩個函式以 `columns` 分支列高，消費端同時服務 grid（Columns>1）
  與 list（Columns==1）；完整高度回歸為 8 list／4 grid 列，逐像素不變（Acceptance §2）。
- **邊界**：`CellAtPoint`（hit-test，NR-064/082 範圍）一字未動——NR-082 的
  `row >= ViewportRows()` 下界本就把 clamp 後 footer 區域判為 miss；`y >= footer_top(456)`
  檢查與 Render 幾何如前。列數來源改由 model 的 `ViewportRows()` 提供，hit-test 讀同一值，
  不產生新錯位。
- **未動**：`ClampWindowSize`（維持 work-32px，Decision §2）、`kPanelHeightDip`、
  版面常數、panel_model。

**spec 改寫後的新條文**（`docs/design-spec.md`）：

- §4.9 第一條（原「高度依內容調整，上限為目前螢幕工作區的 70%」）改為：
  > 預設寬度 640 DIP、高度 488 DIP。面板高度上限為目前螢幕工作區高度減 32px（work-32px
  > 邊距）；完整 488 DIP 面板放不下時（小螢幕＋高 DPI），footer 提示帶恆貼齊面板底緣保持
  > 完整可見，改以縮減清單／格狀的可見列數容納，列高、格高、footer 高度與面板寬度皆不變。
- §4.2 path bar 條文末尾補一句：`path bar 與 footer 按鍵指引在面板高度被 clamp 縮短時仍完整
  可見（§4.9）。`——與實作不再矛盾。
- `rg -n "70%|0\.70|work_height" docs/design-spec.md src/ui`：僅剩 `ClampWindowSize` 參數名
  `work_height`，spec 無「70%」殘留。

**測試案例**（`tests/unit/ui_palette_layout_test.cpp`，純幾何、無 HWND）：
1. `TestFooterBandAlwaysVisible`：完整 488 DIP client → footer top == `kFooterTopDip`；六個
   clamp client（348/344/464/458.67/312/240 DIP）→ band 頂＋高 ≤ client 且 ≥ `kListTopDip`；
   200%@768 驗 316、150%@1366×768 驗 432。
2. `TestViewportRowsShrinkForFooter`：完整高度 list 8／grid 4（不變）；348 DIP → list 5／grid 2；
   464 DIP → list 7／grid 3；最後一列底邊 ≤ `FooterTopDip`（list 與 grid 皆驗）。
3. `TestClampedPanelKeepsFooter`：13 組 DPI×工作區組合逐一走 `ClampWindowSize` → client DIP →
   驗證 footer band 在 client 內、list 列止於 band 上緣。

**build／CTest 證據**（本機 LLVM-MinGW，Release x64）：

```
cmake -S . -B build-wi-nr120 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build-wi-nr120            # 成功、無新增 warning
ctest --test-dir build-wi-nr120 -R "dpi_theme_accessibility|list_vertical_slice" --output-on-failure   # 2/2 通過
ctest --test-dir build-wi-nr120 --output-on-failure   # 26/26 全綠（含 lifecycle_check 3.91s）
```

（item Agent checks 的 `-R "panel_layout|palette|list_vertical_slice"` 只匹配到
`nimblerun_list_vertical_slice_test`——承載本次測試的目標實際命名為
`nimblerun_dpi_theme_accessibility_test`（原始檔 ui_palette_layout_test.cpp），故補跑該目標；
全量 CTest 26/26 已含它。）

- 偏差：無，實作與 item Decisions §2／Scope §1-4 一致。
- 未完成事項：無。三條手動驗收（200% 小螢幕視覺確認、1080p 回歸逐像素相同、hit-test 回歸）
  屬人工操作，依 AGENTS.md 交付規則不在 Agent 範圍。
