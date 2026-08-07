# NR-059 — Render() paints the same icon fallback and empty state twice

Phase 3 · Status `ready` · Depends on: —

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
  這確實是每格每幀一次的 D2D bitmap 建立，但（a）它是**效能**問題不是重複
  問題，（b）修法是在 device resource 生命週期裡快取 `ID2D1Bitmap`，
  需要在裝置遺失與圖示更新時失效，是一個有真實風險的改動，
  （c）**目前沒有任何量測顯示它是瓶頸**。要做就先量再開 item，
  不要夾帶在一個宣稱「像素級零變更」的項目裡。
- **改 `EN_UPDATE` 每次按鍵整窗失效的行為。** 同上，那是一個
  「量測並決定」的獨立 item。
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

（實作者填寫：兩段程式碼逐字比對的結果與所有發現的差異、
兩個新函式的最終簽章、drag ghost 保留的理由註解、
六項手動驗收的實測結果與截圖比對結論、`Render()` 改動前後行數、
`ctest` 結果、上列 sanity greps 的實際輸出、任何必要偏差。）
