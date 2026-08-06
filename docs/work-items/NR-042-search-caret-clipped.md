# NR-042 — Search caret erased by the panel repaint

- Status: `ready`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.7（輸入與焦點）／§4.9（搜尋框外觀）／§NFR-006

## Goal

搜尋框的插入符號（caret）在打字時會消失或被字形吃掉，使用者看不出目前的輸入位置。原因不在 caret 本身的顏色或大小，而在**父視窗每次按鍵都把 D2D 畫面蓋在子 EDIT 的像素上**。本 item 修掉那個蓋圖，一個視窗樣式旗標。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.7／§4.9／§NFR-006、`docs/work-items.md`、本文件。

## 現況事實（已查證，不需重新推導）

- 搜尋輸入是真的 Win32 `EDIT` 子視窗（`src/app_host/main.cpp:2169-2181`），無邊框；圓角外框由 D2D 在**父視窗**上畫（`main.cpp:922-933`）。EDIT 內縮在框內（`kSearchTextInsetDip = 12`、`kSearchEditInsetYDip = 6`，`src/ui/panel_layout.h:54-55`），由 `RepositionSearchEdit()`（`main.cpp:1469-1481`）以物理像素定位。
- **父視窗建立時沒有 `WS_CLIPCHILDREN`**：`main.cpp:2080` 是 `WS_POPUP | WS_BORDER`。類別註冊（`main.cpp:2034-2037` 附近）也沒有加。
- 每一次按鍵都會讓父視窗整片重畫：`EN_UPDATE` 處理在 `main.cpp:1814`，內部 `InvalidateRect(window, nullptr, FALSE)`；同樣的整窗失效還散布在 `main.cpp:1381`、`1594`、`1598`、`1606`、`1613`、`1619`、`1623`、`1726`、`1795`、`1825`、`1854`、`1868`、`1884`、`1957`、`1995`。
- 沒有 `WS_CLIPCHILDREN` 時，`ID2D1HwndRenderTarget` 的 `EndDraw` 會把整個 client 區（含 EDIT 佔的矩形）present 到父視窗的 DC 上。`InvalidateRect(window, nullptr, FALSE)` **不會**連帶失效子視窗（那要 `RedrawWindow` 加 `RDW_ALLCHILDREN`），所以子視窗不會補畫。
- caret 是系統在 `WM_PAINT` 之外疊上去的覆蓋物，它沒有「重畫」的機會，所以父視窗蓋圖之後 caret 就不見了，直到下一次 blink 或下一次 EDIT 自己重畫才短暫出現——這就是「被字蓋住」的觀感。
- caret 本身已在 NR-023 處理過（`main.cpp:1518-1534`：`WM_SETFOCUS` 時 `CreateCaret(edit, (HBITMAP)1, 0, 0)` ＋ `ShowCaret`，`WM_KILLFOCUS` 時 `DestroyCaret`）。那段是灰色 caret，用來在深色主題下也看得見，**與本 bug 無關，不要動它**。

## 決策（不要重新設計）

1. **修根因，不修症狀。** 父視窗加 `WS_CLIPCHILDREN`，D2D 從此不再碰 EDIT 的像素，caret、文字、選取反白一次全部穩定。
2. **不改 caret 的建立方式。** 不換 `(HBITMAP)1`、不改寬高、不加 `SetCaretPos`、不加 blink timer。那些都是症狀處理，而且會把 NR-023 已經解掉的深／淺主題可見度問題重新打開。
3. **不用 `RDW_ALLCHILDREN` 每次按鍵補畫子視窗。** 那會讓 EDIT 每次按鍵整片重畫，閃爍更明顯，成本也更高；`WS_CLIPCHILDREN` 是反過來讓父視窗少畫。
4. **不把整窗失效改成局部失效。** 那是另一條（效能）路線，已在 NR-038 的交接記錄裡列為未量測、未觸發，本 item 不碰。
5. **旗標加在 `CreateWindowExW` 的樣式，不加在 `WNDCLASSEX::style`（`CS_*`）。** `WS_CLIPCHILDREN` 是視窗樣式，放在建立處與既有 `WS_POPUP | WS_BORDER` 同一行最清楚。

## 硬約束

- **只改 `src/app_host/main.cpp` 一處**（`:2080` 的樣式）。不新增檔案、不新增模組、不新增 helper。
- 不改 `src/ui/panel_layout.h`、`src/ui/panel_palette.*`、`Render()` 的任何一行。
- 不改 `SearchEditProc`（`main.cpp:1516` 起），特別是 `WM_SETFOCUS`／`WM_KILLFOCUS`／`WM_LBUTTONDOWN`（NR-039 的拖曳路徑）。
- 不改 `RepositionSearchEdit`、`UpdateSearchFont`、`WM_CTLCOLOREDIT`（`main.cpp:1828`）。
- 不新增計時器、不新增旗標變數、不新增 UI 字串、不寫入任何檔案。
- 不新增依賴、不新增網路／遙測／服務／driver／管理員權限。
- 不改 design-spec。
- 不動 icon lane（NR-030～NR-037）與 NR-039／NR-040／NR-041 的任何路徑。

## Scope

`src/app_host/main.cpp:2080`：

```cpp
        // NR-042: WS_CLIPCHILDREN keeps the D2D present out of the search EDIT's
        // rect. Without it every keystroke's whole-window InvalidateRect
        // (EN_UPDATE) repainted over the child, and because that invalidation
        // does not reach children the EDIT never repainted -- erasing the caret,
        // which the system draws outside WM_PAINT and cannot restore. The rounded
        // search frame is unaffected: it is drawn outside the EDIT rect, which is
        // inset by kSearchTextInsetDip / kSearchEditInsetYDip.
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
```

其他一律不動。

## Non-goals

- 不改 caret 的形狀、顏色、寬度、閃爍行為。
- 不做局部失效／重繪成本最佳化（`EN_UPDATE` 的重繪成本仍是獨立議題）。
- 不改搜尋框的圓角、內縮、字型、配色。
- 不恢復 NR-039 拿掉的滑鼠拖曳選字。
- 不為此新增單元測試（見 Acceptance 的理由）。

## Acceptance

自動部分：

- 只有 `src/app_host/main.cpp` 被修改，且 `git diff` 只有 `:2080` 那一行樣式（加上其上的註解）；沒有其他既有行被改動。
- Release 建置無新增警告；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。

不新增自動化測試的理由：改動是一個視窗樣式旗標，它的效果只存在於 GDI／D2D 的實際 present 行為裡。任何單元測試最多只能斷言「`GetWindowLongW(window, GWL_STYLE)` 含 `WS_CLIPCHILDREN`」——那是把 diff 抄一遍，caret 真的還是看不見時它照樣會綠。改以下列手動驗收覆蓋。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **打字時 caret 持續可見**：叫出面板，連續輸入 `ABC` → 每個字之後 caret 都穩定停在文字右側閃爍，不會消失、不會被字形吃掉。
2. **caret 移動正確**：按 Left／Right／Home／End 在 `ABC` 中間移動 → caret 每一步都畫在對應字元之間，字元本身不被蓋掉。
3. **退格與清空**：按 Backspace 逐字刪到空 → caret 一路可見，清空後停在輸入框左緣。
4. **貼上與 Ctrl+A**：`Ctrl+V` 貼一段字、`Ctrl+A` 全選 → 選取反白正常，取消選取後 caret 回到正常閃爍。
5. **搜尋框外框未被裁掉**：對照修改前的截圖，圓角外框、框線、內縮完全相同，四個圓角沒有缺角。
6. **面板其餘繪製無回歸**：空查詢 grid、有查詢的 list、footer path bar、快選數字框、hover／selected 樣式全部與修改前一致；用方向鍵與滑鼠各走一輪。
7. **高 DPI**：在 150%（或 200%）縮放的螢幕上重跑第 1、5 條。
8. **深色／淺色主題**：兩種主題各跑一次第 1 條，caret 皆清楚可見。
9. **隱藏再顯示、跨螢幕移動**：熱鍵隱藏再叫出、把面板拖到另一台螢幕（NR-039 的拖曳）後再輸入 → caret 與外框都正常。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的九條手動步驟。**不要**只憑編譯成功就回報完成——本 item 的整個價值在第 1～4 條的目視結果。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。
- 若第 1 條在加了 `WS_CLIPCHILDREN` 之後**仍然**重現，停手並在交接區記錄，不要再往 caret 的建立參數加補丁——那表示本 item 的根因判斷錯了，需要重新開一個診斷 item。

## 交接區

（實作者填寫：修改的行號、建置與 CTest 結果、九條手動驗收逐條實測結果、未完成事項。）
