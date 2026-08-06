# NR-041 — Visual marker for pinned items

- Status: `ready`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.2／§4.3（面板版面）／§4.8／§FR-011（釘選）／§NFR-006（不以顏色單獨傳達狀態）

## Goal

釘選狀態目前**完全沒有視覺呈現**。在空查詢 grid 裡只能靠「排在前面」推測，在搜尋結果裡則毫無線索——而搜尋結果正是使用者按右鍵決定 Pin 還是 Unpin 的時機。本 item 在 grid 格與清單列上各畫一個小記號標示已釘選項目，**兩種狀態都畫**。

純繪製改動：不動資料、不動互動、不動持久化。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.2／§4.3／§4.9／§FR-011／§NFR-006、`docs/work-items.md`、本文件。姊妹 item：`docs/work-items/NR-040-context-menu-properties-forget.md`（選單，與本 item **無程式碼交集**，可平行進行）。

## 現況事實（已查證，不需重新推導）

- 繪製全部在 `Render()` 內（`src/app_host/main.cpp`），分兩個分支：**grid**（空查詢，`main.cpp:945-1034` 的 `for` 迴圈）與 **list**（有查詢，`main.cpp:1056` 起的 `for` 迴圈）。
- grid 每格已用掉的視覺元素：selected → `FillRectangle` ＋ `DrawRectangle` 邊框（`:963-979`）；hover → `FillRectangle` 淡填（`:965-969`）；圖示 40×40 置中於上半（`:984-1009`）；名稱單行置中於下半（`:1015-1021`）；**右上角** NR-024 快選數字框（`:1025-1033`）。**左上角是空的。**
- list 每列：selected 填色＋邊框（`:1067-1085`）；圖示 tile 從 `kListLeftDip + kTileInsetDip` 開始（`:1091-1098`），故 `kListLeftDip` 到 tile 之間有 `kTileInsetDip` 的留白；右側 `kRowHintReserveDip` 保留給快選框（`src/ui/panel_layout.h:34`）。**列的最左緣是空的。**
- 版面常數在 `src/ui/panel_layout.h`：`kCellWidthDip = 101.0f`、`kCellHeightDip = 96.0f`、`kIconSizeDip = 40.0f`、`kRowKeyBoxWidthDip = 20.0f`、`kFooterKeyBoxHeightDip = 20.0f`。
- 可用的既有筆刷（`Render()` 作用域內即可取用）：`g_selected_border_brush`、`g_selected_brush`、`g_hover_brush`、`g_text_brush`、`g_dim_brush`、`g_card_brush`。`g_selected_border_brush` 的顏色來自 `palette::PanelColors::selected_border`（`src/ui/panel_palette.h:32`），註解明寫它是「非顏色的選取訊號」用色，已保證與 fill 有對比，且高對比主題下由系統色注入（`ResolveColors`）。
- 釘選查詢：`g_pins->IsPinned(stable_id)`（`nimblerun::PinStore`，`src/pins/pin_store.h`），`main.cpp:1917` 已在用。`g_pins` 是檔案作用域指標，`Render()` 可直接取用。
- 字型只建立了四種 `Segoe UI` 的 `IDWriteTextFormat`（`main.cpp:379-391`），**沒有** icon 字型。

## 使用者已確認的決策（不要重新設計）

1. **兩種狀態都畫記號**（grid 與 filtered list）。曾考慮只畫其中一邊，已否決：只畫一邊看起來像 bug，而且 filtered 才是最需要這個資訊的地方。
2. **D2D 自畫幾何，不用圖釘字符。** 曾提案 `Segoe MDL2 Assets` 的 U+E718 圖釘，使用者否決：grid 格空間不大，塞圖釘字符視覺會亂。自畫記號同時省掉新的 text format 與字型依賴。
3. **不用底色或外框當記號。** 會直接與 selected（fill＋border）和 hover（fill）撞語彙，且高對比下純靠顏色不可靠，違反 §NFR-006。
4. **形狀用圓點與直條，不用三角形。** `FillEllipse`／`FillRectangle` 各一次呼叫即可，不需要建立 `ID2D1PathGeometry`（那才是畫三角形的代價）。
5. **不改版面常數、不改任何既有元素的位置。** 記號畫在現有的空白處，圖示、名稱、快選框一律不移動。

## 硬約束

- **只改 `src/app_host/main.cpp`，兩處**（grid 迴圈內、list 迴圈內）。不新增檔案、不新增模組、不新增 helper class。
- **不改 `src/ui/panel_layout.h` 的既有常數。** 本 item 的兩個新常數直接以字面值寫在繪製處旁（與 `:986` 的 `12.0f`、`:1016` 的 `56.0f` 等既有做法一致），不新增 layout 欄位。
- 不新增筆刷、不新增 `IDWriteTextFormat`、不新增 D2D 資源（`ID2D1PathGeometry`、`ID2D1StrokeStyle` 等）。重用 `g_selected_border_brush`。
- 不新增第三方依賴、不新增字型依賴、不新增網路／遙測／服務／driver／管理員權限。
- 不寫入任何檔案（本 item 無持久化）。
- 不新增 UI 字串（本 item 無文字）。
- 不改資料流：不改 `PanelModel`、不改 `PinStore`、不改 `AppEntry`、不改 catalog／search／ranking。
- 不改互動：不碰 `WM_LBUTTONDOWN`／`WM_RBUTTONDOWN`／`WM_MOUSEMOVE`／`CellAtPoint`。
- 不動 icon lane（NR-030～NR-037）與 NR-039／NR-040 的任何路徑。
- 不改 design-spec。

## Scope

### 1. grid：格子左上角實心圓點（`main.cpp` 的 grid 迴圈內）

插在**快選數字框之後**（`main.cpp:1033` 的 `}` 之後、迴圈結尾之前），確保記號畫在選取邊框上方不被蓋住：

```cpp
                // NR-041: pinned marker -- a filled dot in the cell's top-left
                // corner. Drawn last so it sits above the selection border, and
                // placed on the left because the top-right corner is the NR-024
                // quick-select digit box. Shape, not color, carries the state
                // (design-spec §NFR-006); the border color is reused because the
                // palette already guarantees it contrasts with every fill and
                // follows the system colors under high contrast.
                if (g_pins && g_pins->IsPinned(rows[i].stable_id)) {
                    constexpr float kPinDotRadiusDip = 4.0f;
                    constexpr float kPinDotInsetDip = 8.0f;
                    g_render_target->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(cell.left + kPinDotInsetDip,
                                                    cell.top + kPinDotInsetDip),
                                      kPinDotRadiusDip, kPinDotRadiusDip),
                        g_selected_border_brush);
                }
```

- `cell`、`rows`、`i` 都在該迴圈作用域內（`cell` 建立於 `:955`）。
- 圓心 (8, 8)、半徑 4 → 佔用格內 (4,4)–(12,12)，圖示從 `cell_top + 12.0f` 才開始且水平置中（格寬 101、圖示 40 → 左緣約 30.5），**不重疊**。

### 2. list：列最左緣直條（`main.cpp` 的 list 迴圈內）

插在**選取邊框之後**（`main.cpp:1085` 的 `}` 之後）、圖示 tile 計算之前：

```cpp
                // NR-041: pinned marker -- a stripe on the row's leading edge,
                // in the gap kTileInsetDip already leaves before the icon. Same
                // rule as the grid dot: shape, not color, and drawn after the
                // selection border so it stays visible on the selected row.
                if (g_pins && g_pins->IsPinned(rows[i].stable_id)) {
                    constexpr float kPinStripeWidthDip = 3.0f;
                    g_render_target->FillRectangle(
                        D2D1::RectF(row_rect.left, row_rect.top,
                                    row_rect.left + kPinStripeWidthDip,
                                    row_rect.bottom),
                        g_selected_border_brush);
                }
```

- `row_rect` 建立於 `:1063`；`kTileInsetDip` 的留白讓 3 DIP 直條不會碰到圖示。

### 3. 不做的接線

不碰 grid／list 之外的任何繪製（搜尋框、footer path bar、空狀態提示、快選框）、不碰 `Render()` 的 begin/end draw 與資源建立、不碰 `panel_layout.h`／`panel_palette.*`。

## Non-goals

- 不做圖釘字符、不引入 icon 字型、不引入 SVG／點陣圖資源。
- 不改變 pinned 項目的排序、不做拖曳排序（§FR-011 的順序調整仍未實作，屬別的 item）。
- 不為 recent 區、最近啟動、或任何其他狀態新增記號。
- 不新增動畫、不新增 hover 時的記號變化。
- 不做無障礙朗讀（`AccessibleNameFor` 不變）——本 item 只處理視覺。
- 不為此新增單元測試（見 Acceptance 的理由）。

## Acceptance

自動部分：

- 只有 `src/app_host/main.cpp` 被修改；無新增檔案、無新增筆刷／text format／D2D 資源、`src/ui/panel_layout.h` 與 `src/ui/panel_palette.*` 逐位元組不變。
- `git diff` 僅含兩個新增區塊，皆為 `if (g_pins && ...)` 包住的繪製；無既有行被修改。
- Release 建置無新增警告；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。

不新增自動化測試的理由：本 item 只有兩次 D2D 繪製呼叫，抽出可測的純函式只會得到「常數算出來的矩形／圓心」——記號畫錯位置、被蓋住、或顏色在高對比下不可見時，那種測試不會失敗，等於假檢查。改以下列手動驗收覆蓋。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **grid 顯示記號**：釘選 2 個 App，清空查詢 → 前兩格左上角各有一個小圓點，其餘格子沒有。圖示與名稱位置與釘選前完全相同（可與釘選前的截圖比對）。
2. **filtered 顯示記號**：搜尋一個已釘選 App 的名稱 → 該結果列最左緣有一條直條；同一次搜尋中未釘選的結果列沒有。
3. **選取狀態下仍可見**：用方向鍵把選取移到已釘選的格／列上 → 記號仍清楚可見，沒有被選取邊框或填色蓋掉。hover 到該格時同樣可見。
4. **Unpin 後消失**：對已釘選項目按右鍵 → Unpin → 記號立刻消失（grid 與 filtered 各驗一次）。
5. **不與快選框衝突**：前 10 格同時有右上角數字框與左上角圓點，兩者不重疊、不互相遮蔽。
6. **高 DPI**：在 150%（或 200%）縮放的螢幕上顯示面板 → 記號等比放大、位置正確、邊緣不糊成一團。
7. **高對比主題**：切到 Windows 高對比主題 → 記號仍與格子底色有明顯對比（顏色由 `ResolveColors` 的系統色注入決定）。
8. **深色／淺色主題**：兩種主題各看一次，記號皆可辨識。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的八條手動步驟。**不要**只憑編譯成功就回報完成——本 item 唯一的行為驗證是那八條，且第 3、6、7 條需要實際目視。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。

## 交接區

（實作者填寫：修改的行號、建置與 CTest 結果、八條手動驗收逐條實測結果、未完成事項。）
