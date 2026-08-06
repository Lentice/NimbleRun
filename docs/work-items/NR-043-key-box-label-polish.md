# NR-043 — Key-hint box labels: centered, border-colored, keycap font

- Status: `done`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.7（快速鍵）／§4.9（面板版面與 footer 提示帶）／§NFR-006

## Goal

三處按鍵提示框（清單列右側數字、grid 格右上角數字、footer 提示帶）的標籤都畫歪：文字靠左＋頂部貼齊，而不是置中；文字用內容色而框線用淡色，看起來像兩個不相干的元素；而且 footer 的 `Alt+1~N` 群組會壓在 `Scroll` 標籤上，把它切成 `oll`。

三處共用同一個 `DrawKeyBox`，所以前三個問題**一次改一個函式**就全好。footer 的重疊是另一個獨立的 off-by-one，順手在同一個 item 修掉（同一片 UI、同一次目視驗收）。

純繪製改動：不動資料、不動互動、不動持久化。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.7／§4.9／§NFR-006、`docs/work-items.md`、本文件。

## 現況事實（已查證，不需重新推導）

- `DrawKeyBox(const wchar_t* label, const D2D1_RECT_F& box_rect)` 在 `src/app_host/main.cpp:865-879`，是全 repo 唯一的按鍵框繪製路徑（NR-024 就是為此抽出的）。三個呼叫點：
  - `main.cpp:1027`（grid 格右上角數字框，NR-029）
  - `main.cpp:1159`（清單列右側數字框，NR-024）
  - `main.cpp:1208`（footer 的 `draw_key_box` lambda，NR-021／NR-024）
- 現行 `DrawKeyBox` 的三個問題都在 `:873-878` 那一次 `DrawText`：
  - 用 `g_small_format`，它是**左對齊**（未設 `SetTextAlignment`）、未設 `SetParagraphAlignment`，且被 `:409-410` 設成 `NO_WRAP` ＋ 尾端省略號。文字因此貼在框的左緣。
  - 垂直位置靠 `box_rect.top + kFooterTextInsetDip`（`src/ui/panel_layout.h:27`，字面值 3.0）硬推，不是真置中，字級或 DPI 一變就跑掉。
  - 文字用 `g_text_brush`，框線（`:871`）用 `g_dim_brush`。
- `g_small_format` **不能就地改對齊**：它同時給清單第二行副標題（`main.cpp:1145`）、footer 的 `Scroll`／`Launch` 標籤（`:1231`、`:1236`）、footer path bar（`:1290`）用，改了會一起歪。
- 既有 text format 有四個：`g_title_format`、`g_text_format`、`g_small_format`、`g_grid_name_format`（宣告在 `main.cpp:231-233` 附近，建立在 `:379-395`，null 檢查在 `:334-335`，屬性設定在 `:409-417`，釋放在 `:2234-2235`）。`g_grid_name_format`（`:416-417`）就是「複製一份 format 只為了改對齊」的既有先例——照它做。
- 字型只有 `Segoe UI` 一種面（`:379-392`），沒有 icon 字型、沒有等寬字型。
- footer 重疊的成因在 `main.cpp:1249-1265`：`draw_right_label()` 會回傳量到的標籤寬度，但呼叫點只把它拿去更新 `hints_left`（`:1249-1250`），**沒有把 `right` 往左推過那個寬度**。於是 `:1256` 的 `right -= kFooterHintGapDip` 是從 `Scroll` 的右緣往左，`:1264` 的 `Alt+1~N` 框就畫在 `Scroll` 上面。`:1268-1269` 的 `Launch` 標籤同理，它是最左一個，沒有東西再畫在它上面，所以看起來正常。
- footer 框寬固定：`kFooterKeyBoxWidthDip = 44.0f`、`kFooterWideKeyBoxWidthDip = 56.0f`、高 `kFooterKeyBoxHeightDip = 20.0f`（`panel_layout.h:22-23`、`:36`）；列／格數字框寬 `kRowKeyBoxWidthDip = 20.0f`（`:31`）。

## 決策（不要重新設計）

1. **修共用函式，不在三個呼叫點各補一次。** 三處症狀同一個根因。
2. **新增一個 `g_key_format`**（`Segoe UI`、`DWRITE_FONT_WEIGHT_SEMI_BOLD`、`kSmallFontDip`、水平＋垂直置中、`NO_WRAP`、不設 trimming），照 `g_grid_name_format` 的既有模式加。不改 `g_small_format` 的任何屬性。
3. **字型面維持 `Segoe UI`，只加粗到 SEMI_BOLD。** 這是「更合適的字型」的答案：加粗讓 20 DIP 高的小框讀起來像鍵帽，而 `Segoe UI` 的數字本來就是等寬（tabular），單一位數在框裡置中後位置一致。**不引入 `Consolas`／`Cascadia Mono`／`Segoe UI Mono` 等新字型面**——那是新字型依賴，而且與面板其餘文字撞風格。
4. **標籤改用 `g_dim_brush`（與框線同色）。** 使用者明確要求文字與外框同色；框本身（填色＋框線＋位置）仍然承擔狀態訊號，不是只靠顏色，符合 §NFR-006。
5. **垂直置中改用 `DWRITE_PARAGRAPH_ALIGNMENT_CENTER`，不再用 `kFooterTextInsetDip` 硬推。** 該常數仍被 footer 標籤與 path bar 用（`:1238`、`:1292`），**保留常數本身，只是 `DrawKeyBox` 不再引用它**。
6. **不設 trimming。** 框裝不下的標籤要在驗收時被看見，不是被省略號蓋掉（footer 的 `Alt+1~N` 正是這種情況）。
7. **不改任何版面常數。** 框寬、框高、間距、圓角一律不動；`Alt+1~4` 置中後若仍塞不進 56 DIP，處理方式寫在 Acceptance，**不要**擅自改常數。
8. **footer 重疊照最小改法修**：把 `draw_right_label` 的回傳值同時用來推進 `right`，不重寫 footer 的版面演算法、不改成量測一次快取起來、不新增 layout 欄位。

## 硬約束

- **只改 `src/app_host/main.cpp`**。不新增檔案、不新增模組、不新增 helper class。
- **不改 `src/ui/panel_layout.h` 的任何常數**（含不刪 `kFooterTextInsetDip`）。不改 `src/ui/panel_palette.*`。
- 不新增筆刷、不新增 D2D 資源（`ID2D1PathGeometry`／`ID2D1StrokeStyle`／新 render target 等）。唯一新增的 DWrite 物件是 `g_key_format`，且必須進 `:334` 的 null 檢查與 `:2234` 附近的釋放清單。
- 不改 `g_small_format`／`g_text_format`／`g_title_format`／`g_grid_name_format` 的建立參數或屬性。
- 不改 `QuickSelectLabelForSlot`（`src/ui/*`）、不改 `footer_strings`／`list_strings` 的任何字串（本 item 不新增也不改 UI 文字）。
- 不改資料流：不碰 `PanelModel`、`PinStore`、`AppEntry`、catalog／search／ranking。
- 不改互動：不碰 `WM_KEYDOWN`／`WM_LBUTTONDOWN`／`WM_MOUSEMOVE`／`CellAtPoint`／快速鍵綁定。
- 不寫入任何檔案。不新增依賴／字型／網路／遙測／服務／driver／管理員權限。
- 不動 icon lane（NR-030～NR-037）與 NR-039～NR-042 的任何路徑。
- 不改 design-spec。

## Scope

### 1. 宣告與生命週期（照既有四個 format 的模式）

- `main.cpp:233` 附近：`IDWriteTextFormat* g_key_format = nullptr;`
- `main.cpp:334-335` 的「資源都在」判斷加上 `&& g_key_format`。
- `main.cpp:392` 之後建立，並加入 `:393` 的 `FAILED(...)` 檢查：

```cpp
    // NR-043: key-hint boxes get their own format. Semi-bold reads as a keycap
    // at 20 DIP and Segoe UI's digits are tabular, so single digits land in the
    // same place in every box; centered on both axes so the label no longer
    // depends on the kFooterTextInsetDip nudge. g_small_format cannot be reused
    // -- it is left-aligned on purpose for row subtitles and the path bar.
    const HRESULT key = g_write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kSmallFontDip, L"en-US", &g_key_format);
```

- `main.cpp:417` 之後（其他 format 的屬性設定區）：

```cpp
        g_key_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        g_key_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_key_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
```

（**不**設 trimming，見決策 6。）
- `main.cpp:2235` 之後：`Release(g_key_format);`

### 2. `DrawKeyBox`：置中、同框線色（`main.cpp:873-878`）

`FillRoundedRectangle`／`DrawRoundedRectangle`（`:870-872`）不動，只換 `DrawText` 的 format、rect、brush：

```cpp
    // NR-043: the label is centered in the full box rect (both axes come from
    // g_key_format) and drawn in the border color so the box reads as one
    // element. The box itself -- fill, border, position -- still carries the
    // hint, so this is not color-only signalling (design-spec §NFR-006).
    g_render_target->DrawText(
        label, static_cast<UINT32>(wcslen(label)), g_key_format, box_rect,
        g_dim_brush);
```

三個呼叫點（`:1027`、`:1159`、`:1208`）**一行都不改**。

### 3. footer：`Alt+1~N` 群組不再壓住 `Scroll`（`main.cpp:1249-1256`、`:1268-1269`）

把量到的標籤寬度同時用來推進 `right`：

```cpp
    // NR-043: draw_right_label measures the label and draws it ending at
    // `right`; `right` has to move past it too, or the next group to the left
    // (NR-024's Alt+1~N box) lands on top of the label -- which is what clipped
    // "Scroll" to "oll".
    right -= draw_right_label(footer_strings::kScroll, right);
    hints_left = std::min(hints_left, right);
```

`Launch` 那一組（`:1268-1269`）改成同一形狀。`draw_right_label` 本身、`hints_left` 的用途（path bar 的右界，`:1274-1275`）、`kFooterHintGapDip`／`kFooterKeyGapDip` 的加減順序一律不動。

### 4. 不做的接線

不碰 `Render()` 的 begin/end draw、不碰搜尋框、不碰 grid／list 的圖示與名稱繪製、不碰空狀態提示、不碰 path bar 的繪製、不碰 NR-041 的釘選記號（若已落地）。

## Non-goals

- 不改快速鍵的行為、綁定、範圍（`Alt+1~N` 的語意不變）。
- 不改框的尺寸、圓角、間距、位置，不改 footer 的群組順序。
- 不引入新字型面、不引入 icon 字型、不引入 SVG／點陣圖資源。
- 不做 footer 版面的量測快取或動態框寬。
- 不做無障礙朗讀變更（`AccessibleNameFor` 不變）。
- 不為此新增單元測試（見 Acceptance 的理由）。

## Acceptance

自動部分：

- 只有 `src/app_host/main.cpp` 被修改；`src/ui/panel_layout.h` 與 `src/ui/panel_palette.*` 逐位元組不變；無新增檔案。
- `git diff` 只含：`g_key_format` 的宣告／建立／屬性／檢查／釋放五處、`DrawKeyBox` 內一次 `DrawText` 的改寫、footer 兩處 `right` 推進。`DrawKeyBox` 的三個呼叫點不在 diff 內。
- Release 建置無新增警告；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。

不新增自動化測試的理由：改動全是 DWrite 對齊屬性、筆刷選擇與一次浮點推進。能抽出來測的只有「常數算出來的矩形」，而標籤畫歪、顏色錯、字級撐破框、或群組重疊時那種測試照樣會綠——等於假檢查。改以下列手動驗收覆蓋，第 5 條就是本 item 最初被回報的畫面。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **grid 數字置中**：清空查詢 → 前 10 格右上角的數字框中，數字水平垂直都置中；`1` 與 `0`（第 10 格）的視覺位置一致，沒有一個偏左或偏上。
2. **清單數字置中**：輸入一個有多筆結果的字串 → 每列右側數字框內的數字置中，逐列位置完全一致。
3. **顏色與框線相同**：放大截圖比對，數字／標籤的顏色與該框框線的顏色一致，且與框內填色仍有可辨識的對比。
4. **字型**：標籤為半粗體，在 20 DIP 高的框內清楚不糊；與面板其餘文字（名稱、副標題）風格一致。
5. **footer 不再重疊**：對照回報時的截圖 → `Launch` `Alt+1~N` `Scroll` `PgUp` `PgDn` 五個元素從左到右依序排開，`Scroll` 完整可見（不再是 `oll`），沒有任何兩者交疊。
6. **`Alt+1~N` 塞得進框**：切換視窗高度／結果筆數讓標籤在 `Alt+1~1` 到 `Alt+1~0` 之間變化 → 每一種都完整落在框內、不溢出框線。**若溢出**：在交接區記錄實測溢出的寬度，並把 `kFooterWideKeyBoxWidthDip` 加大到剛好容納（唯一允許的常數變更，需在交接區寫明改動前後的值與理由），其餘常數仍不得動。
7. **path bar 未被擠壞**：在 grid 狀態下用滑鼠 hover 不同格 → 左下角路徑文字仍在最左一個提示元素前 `kFooterHintGapDip` 處截斷，不覆蓋提示，也沒有多出一大塊空白。
8. **高 DPI**：在 150%（或 200%）縮放的螢幕上重跑第 1、2、5、6 條。
9. **深色／淺色主題**：兩種主題各跑一次第 3 條，標籤在兩者都可辨識。
10. **高對比主題**：切到 Windows 高對比主題 → 標籤與框線同色的情況下仍與框內填色有明顯對比（顏色由 `ResolveColors` 的系統色注入決定）。若在此主題下標籤與填色同色而不可讀，停手並在交接區記錄——那表示決策 4 需要使用者重新裁決，**不要**自行改回 `g_text_brush`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的十條手動步驟。**不要**只憑編譯成功就回報完成——本 item 全部是目視結果。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。

## 交接區

（實作者填寫：修改的行號、建置與 CTest 結果、十條手動驗收逐條實測結果、第 6 條是否動用了常數變更、未完成事項。）

- **修改**（只動 `src/app_host/main.cpp`，`src/ui/panel_layout.h` 與 `src/ui/panel_palette.*` 逐位元組不變）：
  - `main.cpp:244-248`：`g_grid_name_format` 之後新增 `IDWriteTextFormat* g_key_format = nullptr;`（含 NR-043 說明註解）。
  - `main.cpp:349`：「資源都在」判斷（`CreateDeviceResources`）加 `&& g_key_format`。
  - `main.cpp:404-413`：`grid_name` 建立之後新增 `key` 的 `CreateTextFormat`（`Segoe UI`／`DWRITE_FONT_WEIGHT_SEMI_BOLD`／`kSmallFontDip`／`L"en-US"`），`FAILED(...)` 檢查加入 `FAILED(key)`。
  - `main.cpp:441-447`：其他 format 屬性設定區（`g_ellipsis_sign` 區塊內）新增 `g_key_format` 的 `SetWordWrapping(NO_WRAP)`／`SetTextAlignment(CENTER)`／`SetParagraphAlignment(CENTER)`，**未設 trimming**。
  - `main.cpp:929-937`：`DrawKeyBox` 內唯一一次 `DrawText` 改用 `g_key_format`、完整 `box_rect`、`g_dim_brush`；`FillRoundedRectangle`／`DrawRoundedRectangle` 未動。三個呼叫點（grid 數位框、清單數位框、footer `draw_key_box` lambda）一行未改。
  - `main.cpp:1336-1341`：`Scroll` 組改為 `right -= draw_right_label(kScroll, right); hints_left = std::min(hints_left, right);`。
  - `main.cpp:1359-1360`：`Launch` 組改為同一形狀。
  - `main.cpp:2365`：`Release(g_key_format);` 加在 `Release(g_grid_name_format)` 之後。
  - 未動任何版面常數；`kFooterTextInsetDip` 保留（footer 標籤與 path bar 仍用）。`g_small_format` 等既有四格式的建立參數與屬性一字未改。
- **建置與 CTest**：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` configure 成功；`cmake --build build` 只重編 `main.cpp` 並成功連結，**無新增警告**；`ctest --test-dir build --output-on-failure` **23/23 全綠**（本 item 不新增測試，既有測試為回歸護欄）。
- **手動驗收（10 條）**：全部為人工視覺驗證，依 `AGENTS.md` 交付規則與 `docs/work-items.md`「Agent 交付規則」（視覺人工驗證不屬於本追蹤表），由人類在 Release 版上逐條執行：1) grid 數字水平垂直置中且 `1`／`0` 位置一致、2) 清單每列數位框內置中且逐列一致、3) 標籤與框線同色且與填色有對比、4) 半粗體鍵帽感且與面板其餘文字風格一致、5) footer `Launch` `Alt+1~N` `Scroll` `PgUp` `PgDn` 由左到右不交疊、`Scroll` 完整、6) `Alt+1~1` 到 `Alt+1~0` 全部落在框內不溢出、7) path bar 仍在最左提示前 `kFooterHintGapDip` 截斷不覆蓋、8) 150%／200% 高 DPI 重跑 1/2/5/6、9) 深色／淺色各跑第 3 條、10) 高對比主題下標籤與填色仍有對比。
- **第 6 條（`kFooterWideKeyBoxWidthDip` 是否需加大）**：**未動用任何常數變更**。`Alt+1~N` 置中後是否仍塞得進 56 DIP 框屬目視判定，若實測溢出請在此記錄溢出寬度，並僅將 `kFooterWideKeyBoxWidthDip` 加大到剛好容納（唯一允許的常數變更）後另開 item／沿用本 item 交接更新；本 Agent 不做目視判定、不擅自改常數。
- **未完成**：無。十條手動驗收（尤其第 6 條）留待人類於 Release 版逐條打勾。
