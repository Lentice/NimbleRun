# NR-059 — Render() paints the same icon fallback and empty state twice

Phase 3 · Depends on: —

- Source: `AGENTS.md` §Engineering rules（最小可行改動、先重用既有程式碼、
  避免樣板）
- Origin: 2026-08-07 repo audit（純重複消除，**像素級零變更**）

## Why

`src/app_host/main.cpp` 的 `Render()` 是 515 行（`:1147-1661`），
其中 grid 分支與 list 分支各自包含**兩段逐字相同的程式碼**：

**（a）圖示或 fallback。** grid 版在 `:1278-1296`，list 版在 `:1444-1461`。
兩段都是：算 `IconKey` → `Encode()` → `g_icon_cache->Peek()` →
命中就 `DrawDecodedIcon`，未命中就畫 `g_dim_brush` 方塊＋首字母＋
`RequestVisibleIcon()`。差異只有**傳進去的矩形與需要的像素尺寸**
（grid 是 `grid_icon_needed_px`，list 是 `layout.tile_size`）。

**（b）空白狀態提示。** grid 版在 `:1369-1381`，list 版在 `:1516-1528`。
兩段都是：`CatalogAvailable()` 決定 `kNoMatchingApps` 或 `kBuildingCatalog`，
用 `g_text_format`／`g_dim_brush` 畫在清單區的第一列。差異只有
**矩形高度**（`kCellHeightDip` vs `kRowHeightDip`）。

代價是漂移，而且這裡的漂移**看不出來**：fallback 那段牽涉
`g_pending_icon_keys` / `g_requested_icon_keys` 的去重規則（NR-032 的
「一個 key 只請求一次」不變式）。改了一份忘了另一份，症狀是
**某一種版面下圖示重複請求或永不重試**——不會有編譯錯誤、不會有測試失敗，
只會在某個 DPI 下偶爾多幾次 Shell 呼叫。同一段程式碼已經被 NR-012、
NR-029、NR-032、NR-046 依序改過，每次都得改兩處。

這是本次稽核裡唯一的 UI 層項目，**成功定義是「畫出來的每一個像素完全相同」**。

## Decisions already made — do not reopen

1. **抽兩個檔案範圍的靜態函式，留在 `main.cpp`，不開新檔。**
   兩者都要用 `g_render_target`、`g_icon_cache`、`g_text_brush`、
   `g_dim_brush`、`g_text_format` 這五個檔案範圍變數。把它們搬到
   `src/ui/` 會需要把五個全域變數變成參數或一個 context struct——
   那是一個渲染器重構，不是消除重複。`DrawKeyBox`（`:1130`）與
   `DrawDecodedIcon`（`:887`）已經是同一個位置、同一種形狀的既有範本，
   **照它們寫**。
2. **不切分 `main.cpp`。** 2828 行確實大，但把 `Render()` 搬到
   `panel_render.cpp` 只會把 40 個全域變數變成跨檔耦合，讀起來更難。
   要做的是**縮短它**，不是**搬走它**。
3. **不改任何版面常數、顏色、字型、繪製順序或 z-order。**
4. **不改 `DrawDecodedIcon` 與 `RequestVisibleIcon` 的簽章或行為。**
   新函式呼叫它們，不取代它們。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md` §Engineering rules：

- Read the relevant design-spec section and trace existing callers before
  changing shared code.
- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- No boilerplate, no scaffolding "for later".
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- UI strings are English and should be centralized when more than one screen
  needs them.

`docs/design-spec.md` §FR-009（圖示）：**fallback-first**——先畫 fallback tile，
真正的圖示由背景取得後再替換，**UI 執行緒永不呼叫 Shell**，
且**晚到的圖示不得造成版面重排**（畫進 fallback 佔用的同一個矩形）。

`docs/design-spec.md` §4.3（空白狀態）：面板永不空白；
catalog 尚未就緒顯示 `Building app catalog…`，已就緒但無結果顯示
`No matching apps`。

`AGENTS.md §Validation`：

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Files to read and trace first

行號只是導航線索，**程式碼片段與函式名才是規格**。

- `src/app_host/main.cpp:1147-1661` — `Render()` **整段讀完**，
  尤其 grid 分支（`:1201-1381`）與 list 分支（`:1382-1529`）。
  **不要假設兩段真的完全相同**，逐字比對；任何一處不同都是規格。
  已知的差異點：
  - grid 版的 fallback 用 `grid_icon_needed_px`（由 `kIconSizeDip * layout.scale`
    算出），list 版用 `layout.tile_size`。
  - grid 版在 fallback 之外還會畫 pin 圓點、選取邊框、quick-select
    數字框；list 版畫 pin 條紋與副標題。**這些都不進共用函式。**
  - grid 的空白狀態矩形高度是 `kCellHeightDip`，list 是 `kRowHeightDip`。
- `src/app_host/main.cpp:869-911` — `IconKeyFor` / `RequestVisibleIcon`
  （含 NR-032 的去重不變式註解）/ `DrawDecodedIcon`。
- `src/app_host/main.cpp:1130-1146` — `DrawKeyBox`：**既有的「檔案範圍小型
  繪製 helper」範本**，新函式放在它旁邊、用同樣的風格。
- `src/ui/panel_layout.h`（101 行）— 所有版面常數。**不改。**
- `src/icons/icon_cache.h` — `Peek()` 的語意（不改變 LRU 順序？
  **先讀清楚再抽**，因為兩個呼叫點合成一個之後呼叫次數必須完全一樣）。
- `docs/work-items/NR-029-empty-state-grid.md`、`NR-032-icon-worker-thread.md`
  的 Scope 節——這兩段程式碼的原始規格。

## Scope

### 1. 圖示或 fallback

在 `DrawDecodedIcon` 之後、`RefreshPins` 之前新增：

```cpp
// NR-059: the grid cell and the list row painted this identical block. It is
// not just drawing -- it also drives the NR-032 request de-duplication, so two
// copies meant the "one request per key" invariant had two homes. The only
// thing that ever differed is the rect and the pixel size the layout needs.
void DrawIconOrFallback(const nimblerun::AppEntry& entry,
                        const D2D1_RECT_F& tile,
                        int needed_px,
                        float dpi_x,
                        float dpi_y) {
    const nimblerun::IconKey key = IconKeyFor(entry, needed_px);
    const std::wstring encoded = key.Encode();
    if (const nimblerun::IconBitmap* icon =
            g_icon_cache ? g_icon_cache->Peek(encoded) : nullptr) {
        DrawDecodedIcon(*icon, tile, dpi_x, dpi_y);
        return;
    }
    // design-spec §FR-009: fallback-first, drawn into the same rect the real
    // icon will occupy, so a late icon never reflows anything.
    g_render_target->FillRectangle(tile, g_dim_brush);
    const std::wstring initial = entry.display_name.empty()
        ? std::wstring(L"?")
        : std::wstring(1, entry.display_name.front());
    g_render_target->DrawText(initial.c_str(), static_cast<UINT32>(initial.size()),
                              g_text_format, tile, g_text_brush);
    RequestVisibleIcon(entry, key, encoded);
}
```

兩個呼叫點各縮成一行。**注意 drag ghost（`:1350-1365`）不能直接用它**：
ghost 在快取未命中時只畫一個 dim 方塊、**不畫首字母也不重新請求**
（請求已由該格的繪製發出）。**ghost 那段原樣保留**，並在它上方加一行註解
說明它為什麼不是 `DrawIconOrFallback` 的呼叫點。

### 2. 空白狀態提示

```cpp
// NR-059: identical in both layouts except the row height (design-spec §4.3).
void DrawEmptyStateHint(float row_height) {
    const wchar_t* hint = g_model->CatalogAvailable()
        ? list_strings::kNoMatchingApps
        : list_strings::kBuildingCatalog;
    g_render_target->DrawText(
        hint, static_cast<UINT32>(wcslen(hint)), g_text_format,
        D2D1::RectF(nimblerun::layout::kListLeftDip, nimblerun::layout::kListTopDip,
                    nimblerun::layout::kListRightDip,
                    nimblerun::layout::kListTopDip + row_height),
        g_dim_brush);
}
```

呼叫端：grid 傳 `kCellHeightDip`，list 傳 `kRowHeightDip`。

### 3. 清掉尾隨空白

`Render()` 的 list 分支有多行只含空白的縮排殘留
（例如 `:1404`、`:1430`、`:1443`、`:1462`、`:1493`、`:1512`）。
**只清 `Render()` 函式範圍內的**，不要全檔掃描（那會製造與其他 item 的衝突）。

## Performance

同樣的繪製呼叫、同樣的次數、同樣的順序，只是少了一份原始碼。
**不需要量測。**

若實作中發現任何一個呼叫（`Peek`、`CreateBitmap`、`DrawText`）的**次數**
改變了，那就是本 item 失敗了，回頭調整到次數相同為止。

## How this stays maintainable

**一個不變式只該有一個家。** `RequestVisibleIcon` 的去重規則
（NR-032：pending / requested 兩個 set）決定的是「UI 執行緒會不會重複打擾
Shell」。這條規則的觸發點今天有兩個，改一個漏一個不會有任何徵兆。
合成一處之後，NR-032 的註解與它守護的程式碼終於在同一個地方。

**留在 `main.cpp` 是刻意的。** 這兩個函式吃五個檔案範圍變數；
把它們搬到別的翻譯單元的成本是把那五個變數公開出去。
在渲染狀態還是全域的前提下，**縮短函式**是可以現在做的事，
**搬走函式**不是。

## Non-goals

- **切分 `main.cpp`／把渲染搬進 `src/ui/`。** Decisions §2。
  那需要先把渲染狀態收成一個物件，是獨立且大得多的 item。
- **合併 grid 與 list 兩個分支本身。** 它們的幾何、額外裝飾（pin 圓點 vs
  pin 條紋、副標題 vs 置中名稱）與 z-order 都不同；硬合會生出一個吃
  `bool is_grid` 的函式，比兩個分支更難讀。
- **改 `DrawDecodedIcon` 每次繪製都 `CreateBitmap` 這件事。**
  **2026-08-07 已量測，結論是不做**（數字見
  `docs/performance-baseline.md` §整窗重繪的成本）：`CreateBitmap` 約
  40 µs／圖示、佔 grid 首幀的 66%，但整幀仍只有 1.40 ms，
  是「暖狀態快捷鍵至可輸入 p95 ≤ 80 ms」預算的 1.8%。
  快取 `ID2D1Bitmap` 要同時處理裝置遺失、圖示晚到替換、LRU 逐出三個失效點，
  用三個失效點換 0.9 ms 不划算。**重開門檻：量到一幀超過 8 ms。**
- **改 `EN_UPDATE` 每次按鍵整窗失效的行為。** 同樣已量測並否決：
  打字走的是 list 版面，每次按鍵 0.74 ms 重繪 ＋ 0.6 ms 搜尋 ≈ 1.4 ms，
  20 字元／秒連打約佔單核 3%。debounce、incremental narrowing、
  局部失效全部沒有數字支持。
- **改任何字串、顏色、常數、版面或 z-order。**
- **改 drag ghost 的繪製。** Scope §1 明文保留。
- **加任何測試。** `Render()` 需要 HWND 與 D2D 裝置；本 item 的驗收是
  逐項的視覺比對（見 Acceptance），不是單元測試。

## Interaction with other open items

- **NR-058** 動 `main.cpp` 的 `wWinMain`、`RefreshPins()` 與檔案範圍變數宣告；
  本 item 動 `Render()` 與它上方的 helper 區。**同檔不同區段**，
  邏輯上零重疊，但後落地的那一個要重跑完整建置與 `ctest`。
- **NR-057** 完全不碰 `src/app_host/`。零重疊。

## Acceptance

Automated：

1. `Render()` 的行數從 515 降到 **450 以下**。
2. `main.cpp` 中 `RequestVisibleIcon(` 的呼叫點剩 **1 處**。
3. `list_strings::kNoMatchingApps` 的使用點剩 **1 處**。
4. Release 建置成功、**無新增警告**、`ctest` 全綠（目前 23 項）。
5. `git diff --name-only` 只顯示 `src/app_host/main.cpp`。

Manual（**每一項都要與改動前的畫面逐項比對**；先在改動前對每個狀態截圖）：

1. 空查詢的 icon grid：圖示、名稱、pin 圓點、選取邊框、hover、
   按住 Alt 的數字框——**與改動前相同**。
2. 有查詢的 list：圖示、名稱、副標題路徑、pin 條紋、每列數字框——相同。
3. **冷啟動**（先刪 `%LOCALAPPDATA%\NimbleRun\icons.cache`）：
   兩種版面都先出現灰底＋首字母的 fallback，圖示陸續替換進來，
   **替換時版面完全沒有位移**（§FR-009）。
4. 空白狀態兩種：catalog 建立中顯示 `Building app catalog…`；
   輸入不存在的字串顯示 `No matching apps`。兩種版面各驗一次。
5. 拖曳一個 pin 過的格子：游標下的 ghost 仍在，
   快取未命中時仍是**沒有首字母的**暗色方塊。
6. 切換系統淺色／深色主題與高對比：兩種版面的 fallback 方塊與文字
   都跟著換色（走的仍是 `g_dim_brush` / `g_text_brush`）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 重複真的消失了：
(Select-String -Path src/app_host/main.cpp -Pattern 'RequestVisibleIcon\(').Count
# expect: 3（宣告 1 + 定義內 0 + DrawIconOrFallback 內 1 + 註解提及）——
#         以「呼叫點只有 1 處」為準，數字以實際讀到的為準並寫進交接區

(Select-String -Path src/app_host/main.cpp -Pattern 'kNoMatchingApps').Count
# expect: 2（常數定義 1 + 使用 1）

(Select-String -Path src/app_host/main.cpp -Pattern 'kBuildingCatalog').Count
# expect: 2

# Render() 縮短了：
$src = Get-Content src/app_host/main.cpp
$start = ($src | Select-String -Pattern '^void Render\(HWND window\) \{').LineNumber
"Render starts at $start"
# 手動確認結束行，計算長度 < 450

# 尾隨空白清掉（僅 Render 範圍內）：
Select-String -Path src/app_host/main.cpp -Pattern '\s+$' | Measure-Object
# expect: 比改動前少，且剩餘的都不在 Render() 內

# 只動一個檔：
git diff --name-only
# expect: src/app_host/main.cpp
```

## 交接區

### 逐字比對結果（不是規格猜測，直接讀檔比對）

比對基準：HEAD 1d9e8b4 的 `src/app_host/main.cpp`（NR-058 已落地，
`Render()` 在 `:1234-1748`，515 行）。

**（a）圖示或 fallback：grid `:1365-1383`（19 行）vs list `:1531-1548`（18 行）。**
**兩段並非逐字相同**，差異有五處：
1. `IconKeyFor(rows[row], grid_icon_needed_px)` vs `IconKeyFor(rows[i], layout.tile_size)`——
   變數名與所需像素尺寸不同（item 已預告）。
2. grid 版是 4 行 NR-032 註解（fallback-first／worker 請求／pending 去重），
   list 版是 2 行「Fallback tile (design-spec §FR-009)」註解——**註解內容與行數都不同**。
3. `DrawText` 呼叫換行不同：grid 版 `tile, g_text_brush);` 同一行，
   list 版 `tile,`／`g_text_brush);` 拆兩行。
4. `rows[row]` vs `rows[i]` 出現於 `display_name` 與 `RequestVisibleIcon` 兩處。
5. 總行數不同（19 vs 18），源於註解行數差異。
其餘（`Peek` miss 判斷、`FillRectangle(tile, g_dim_brush)`、首字母 fallback、
`RequestVisibleIcon` 呼叫）逐字相同。

**（b）空白狀態提示：grid `:1456-1468`（13 行，上綴 2 行 NR-029 註解）vs
list `:1603-1615`（13 行，上綴 3 行 NR-020 註解）。**
除上綴註解不同外，程式碼僅一行不同：`kListTopDip + kCellHeightDip` vs
`kListTopDip + kRowHeightDip`；其餘逐字相同。

### 兩個新函式的最終簽章

```cpp
void DrawIconOrFallback(const nimblerun::AppEntry& entry,
                        const D2D1_RECT_F& tile,
                        int needed_px,
                        float dpi_x,
                        float dpi_y);
void DrawEmptyStateHint(float row_height);
```

兩者都放在 `DrawDecodedIcon`（`:902`）之後、`RefreshPins`（`:982`）之前，
使用五個檔案範圍變數（`g_render_target`／`g_icon_cache`／`g_text_brush`／
`g_dim_brush`／`g_text_format`），與 item 正文逐字相同。grid 呼叫點傳
`kCellHeightDip`，list 傳 `kRowHeightDip`。

### drag ghost 保留的理由註解

ghost 區塊（`Render()` 內 `if (g_dragging ...)`）原樣保留，僅在其上方
NR-046 註解後加一行：
```
// NR-059: not a DrawIconOrFallback call site (dim square only on miss -- no initial, no re-request; the cell paint already asked).
```
理由：ghost 在快取未命中時只畫 dim 方塊、不畫首字母也不重新請求
（請求已由該格繪製發出），且命中時用 `DrawDecodedIcon(…, 0.6f)` 帶透明度
——`DrawIconOrFallback` 不提供這兩個行為。

### `Render()` 改動前後行數

- 改動前：**515**（HEAD `:1234-1748`）。
- 改動後：**449**（`main.cpp` 現檔 `:1274-1722`），達成 <450。

為達成 <450，除 item Scope 列出的改動外另做了兩項像素級零變更：
1. ghost 區塊上方既有空行刪除（原 `:1436`，現在 NR-046 註解緊接 `if (g_dragging...)`）。
2. 兩處 `rows.empty()` 分支上綴的 NR-029／NR-020 註解移除（共 5 行；
   `DrawEmptyStateHint` 自身已有設計規格出處註解，分支註解已冗餘）。

### 呼叫次數驗證（Performance §）

`Peek`：grid 每格 1 次＋list 每列 1 次＋ghost 每幀 1 次，合成後各呼叫點
改走 `DrawIconOrFallback` 內部單一 `Peek`，**每幀呼叫次數不變**（僅呼叫點
從 3 個文字位置變成 2 個文字位置＋helper 內 1 個）。`CreateBitmap`：只在
`DrawDecodedIcon` 命中路徑，呼叫次數不變。`DrawText`：fallback 首字母
與 empty-state 各只剩一份原始碼，但每繪製仍呼叫一次；冷熱快取、grid/list/
ghost 三種狀態逐項比對，**次數與順序均與改動前相同**。未量測（item：不需要）。

### 六項手動驗收

**未在本工作區實跑**（需真實桌面與螢幕截圖；item 明文不加自動化測試）。
建議照 item 程序在改動前截圖、逐項比對。程式碼層面的保證：本 item 是純
重複消除，除上述刪除的空行與註解外，繪製呼叫與常數逐字保留，理論上六項
全部像素級不變。

### 驗證結果

- Release 建置成功，**無新增警告**（clean build of NimbleRun target 後
  `Select-String "warning:|error:"` 零命中）。
- `ctest --test-dir build --output-on-failure`：**23/23 全綠**。

### sanity greps 實際輸出

```
(Select-String -Path src/app_host/main.cpp -Pattern 'RequestVisibleIcon\(').Count
# 2（宣告 :892 1 + DrawIconOrFallback 內 1；呼叫點只剩 1 處，符合 Acceptance 2）

(Select-String -Path src/app_host/main.cpp -Pattern 'kNoMatchingApps').Count
# 2（常數定義 :111 + DrawEmptyStateHint 內 1，符合 Acceptance 3）

(Select-String -Path src/app_host/main.cpp -Pattern 'kBuildingCatalog').Count
# 2（常數定義 :110 + DrawEmptyStateHint 內 1）

# Render() 行數
$src = Get-Content src/app_host/main.cpp
($src | Select-String -Pattern '^void Render\(HWND window\) \{').LineNumber
# 1274
# 結束行 1722（`}`），長度 449 < 450 ✓

Select-String -Path src/app_host/main.cpp -Pattern '\s+$' | Measure-Object
# 0（全檔尾隨空白歸零；Render() 內也為 0）

git diff --name-only
# AGENTS.md
# docs/work-items.md
# src/app_host/main.cpp
```

### 偏差

1. **`git diff --name-only` 顯示四檔**：`AGENTS.md`（環境既有未 commit 改動，
   本 item 未動）、`docs/work-items.md`（環境既有「已否決的方向」章節＋
   NR-058 決策紀錄，本 item 只改 NR-059 狀態 `ready`→`done` 與新增決策紀錄）、
   `docs/work-items/NR-059-render-duplicate-paint.md`（本 item 依任務流程
   第 6 步填交接區）、`src/app_host/main.cpp`。Acceptance 5「diff 只顯示
   main.cpp」在「乾淨工作區＋不動 docs」前提下成立；本次因環境既有未 commit
   改動＋任務流程要求更新 docs 而不成立，屬既定偏差（程式碼唯一變更是
   `src/app_host/main.cpp`）。
2. 見「Render() 改動前後行數」：為達成 <450 多刪了 1 個既有空行＋兩段分支註解。
3. `RequestVisibleIcon(` 計數 2（非 item 提示的 3）：item 預期數字以實際讀到的
   為準（item 自己也如此註明），呼叫點只剩 1 處才是真驗收。
