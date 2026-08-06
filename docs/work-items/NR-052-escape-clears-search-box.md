# NR-052 — Esc must clear the visible search box, and a whitespace-only query must stay in the grid

Phase 3 · Status `ready` · Depends on: —

- Source: `docs/design-spec.md` §4.7（鍵盤操作）／§4.3（版面切換）／§4.4（搜尋正規化）
- Origin: 2026-08-06 repo audit, spec-conformance findings #1 與 #2

## Why

搜尋框的文字（Win32 EDIT 子視窗）與 `PanelModel::query_` 是兩份狀態，
而目前有兩條路徑讓它們不一致，兩條都是使用者直接看得到的：

1. **Esc 只清模型，不清 EDIT。** `src/app_host/main.cpp:1961` 的
   `VK_ESCAPE` 分支呼叫 `g_model->Esc()`（`panel_model.cpp:192`），模型的
   query 被清空、面板彈回釘選／常用格狀，**但搜尋框裡的舊文字還在畫面上**。
   使用者看到「格狀畫面 + 搜尋框裡有字」這個不存在於任何規格的狀態，而且
   下一個按鍵是**接在舊字後面**打的。§4.7 的「搜尋欄有內容時 Esc 先清空」
   在模型層做到了，在使用者看到的那一層沒有。
   全檔唯一重設 EDIT 文字的地方是面板顯示時（`main.cpp:1652`）。

2. **只打一個空白就掉出格狀版面。** 版面切換的判準是
   `query_.empty()`（`panel_model.h:60` 一帶、`panel_model.cpp:42`），
   而餵進去的是 EDIT 的原始文字（`main.cpp:2146` 的 `EN_UPDATE` 路徑）。
   打一個空白鍵：`query_` 不是空的 → 切到單欄清單版面 → `SearchApps` 依
   §4.4 把 query 正規化成空字串、於 `search_engine.cpp:130` 早退回傳空集合
   → 使用者看到「No matching apps」。§4.3 說的是「搜尋欄**包含非空白字元**」
   才切換版面，不是「非空」。

兩者根因相同：**「使用者是否正在搜尋」這個問題有兩個不同的答案來源**
（EDIT 的原始文字 vs 正規化後的 query），而沒有人負責讓它們一致。

## Decisions already made — do not reopen

決定於撰寫本 item 時（理由見 How this stays maintainable）：

1. **EDIT 是使用者輸入的唯一真相，模型的 `query_` 是它的衍生值。**
   修法是讓 Esc 去改 EDIT，再讓既有的 `EN_UPDATE` 通知把新值推進模型——
   **不是**在 `Esc()` 之後另外呼叫一次 `SetQuery`。一條資料流向，不是兩條。
2. **「是否在搜尋」以「正規化後是否為空」判定，判定點只有一處。**
   §4.4 的正規化規則（去頭尾空白、連續空白視為一個）已經有唯一實作
   `NormalizeName`；本 item 讓版面判斷用它，不自己寫第二套 `IsBlank`。
3. **不改 `SearchApps` 的早退行為。** 空正規化 query 回傳空集合是對的；
   問題在於根本不該進到搜尋狀態。
4. **不改 Esc 的兩段語意。** §4.7 要的就是「有內容先清空、已空才關面板」，
   `Esc()` 的 bool 回傳值已經正確表達了這件事，本 item 只補上它漏掉的
   UI 副作用。

## Binding constraints — quoted, do not go looking for them

design-spec §4.7：

> - `Esc`：搜尋欄有內容時清空搜尋欄；已為空時隱藏面板。

design-spec §4.3：

> - 只要搜尋欄包含非空白字元，即切換為搜尋結果版面。
> - 每次輸入變更後同步計算；若未來 Catalog 超過 5,000 筆或量測超標，再改用
>   背景工作執行緒。

design-spec §4.4：

> 比對前應：
> - Unicode 大小寫不敏感。
> - 去除頭尾空白。
> - 將連續空白視為一個空白。
> - 同時保留原始名稱與正規化名稱。

design-spec §4.2（Esc 之後應該回到的畫面）：

> - 釘選項目優先顯示於最前。
> - 未釘選常用項目，依使用分數排序。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:1954-1971` — `SearchEditProc` 的 `VK_RETURN` /
  `VK_ESCAPE` / `Ctrl+R` 分支。§1 只改 `VK_ESCAPE` 這一支。
- `src/app_host/main.cpp` 的 `EN_UPDATE`（或 `EN_CHANGE`）處理處
  （審計指出約在 :2146）——把 EDIT 文字取出、餵給 `g_model->SetQuery` 的
  那一段。**這是本 item 的關鍵：Esc 清空 EDIT 之後，這條既有路徑會自動
  把空 query 推進模型。** 先讀懂它，確認它真的在文字被程式碼改變時也會
  觸發（`SetWindowTextW` 會送出 `EN_UPDATE`；若本 repo 用的是
  `EN_CHANGE`，同樣會送）。
- `src/app_host/main.cpp:1640-1660` — `ShowPanel` 裡重設 EDIT 文字的既有寫法。
  §1 照抄它的 API 用法（`SetWindowTextW` 與可能的選取範圍重設）。
- `src/app_host/panel_model.h` 的 `Esc()` 宣告與 `query_` / `IsSearching`
  之類的存取器（若有）。
- `src/app_host/panel_model.cpp:31-74` — `Reset()`、`SetQuery()`、
  `RefreshRows()`。**`RefreshRows()` 的 `if (query_.empty())` 就是 §2 要改的
  那一行**，而它同時決定 rows 的內容與（透過 `Columns()`）版面。
- `src/app_host/panel_model.cpp:192` 一帶 — `Esc()` 的實作與其 bool 語意。
- `src/search/search_engine.h` / `.cpp:88` — `NormalizeName` 的宣告與定義。
  §2 要用它。確認 `panel_model` 所在的 CMake target 已經連結
  `nimblerun_search`（NR-047 交接區指出 `nimblerun_catalog` 有；
  **`nimblerun_panel_model` 要自己確認**，見 §2 的 CMake 小節）。
- `src/search/search_engine.cpp:121-135` — `SearchApps` 開頭的 query 正規化與
  空 query 早退。**不改**，但要理解 §2 之後這條路徑不再會被空白 query 走到。
- `tests/unit/panel_model_test.cpp` — 既有的 `Expect()` 慣例與 model 測試
  組織方式。§3 在這裡加案例。

## Scope

### 1. Esc 清空使用者看得到的搜尋框

`VK_ESCAPE` 分支改為：

```cpp
            case VK_ESCAPE:
                // NR-052: Esc's two-stage behavior lives in PanelModel::Esc()
                // (design-spec §4.7), but the model's query is a derived value
                // -- the EDIT control is what the user sees and types into.
                // Clearing only the model left the old text on screen under the
                // pinned/recent grid, and the next keystroke appended to it.
                // Clear the EDIT and let the existing EN_UPDATE path push the
                // empty query into the model, so there is still exactly one
                // route from typed text to query state.
                if (g_model->Esc()) {
                    HidePanel(GetParent(edit));
                } else {
                    SetWindowTextW(edit, L"");
                }
                return 0;
```

**注意分支方向**：`Esc()` 回傳 true 表示「已經是空的，該隱藏面板」。
先讀 `panel_model.cpp:192` 確認實際語意，**以程式碼為準**，若與本文相反就
反過來寫，並在交接區指出。

`SetWindowTextW(edit, L"")` 會觸發 `EN_UPDATE`，既有處理會呼叫
`SetQuery(L"")` 並 `InvalidateRect`。**驗證這一點**（下中斷點或加暫時
`OutputDebugStringW`），因為整個 §1 的正確性建立在它之上。若確認它**不會**
觸發，那才改為在 `SetWindowTextW` 之後明確走一次既有的「重新讀 EDIT →
SetQuery → 重繪」路徑（把那段抽成一個檔案範圍的小函式讓兩處共用），
並在交接區說明。

### 2. 只有非空白字元才算在搜尋

`PanelModel::RefreshRows()`：

```cpp
void PanelModel::RefreshRows() {
    // NR-052: design-spec §4.3 switches layout when the box "contains a
    // non-whitespace character", not when it is non-empty. A lone space used to
    // drop out of the grid into the single-column list and then show "No
    // matching apps", because SearchApps normalizes the query to empty and
    // returns nothing. Reuse the one §4.4 normalizer rather than writing a
    // second blank test that could drift from it.
    if (NormalizeName(query_).empty()) {
```

`SetQuery` 內**不要**先正規化再存：`query_` 必須保留使用者打的原文
（§4.4：「同時保留原始名稱與正規化名稱」的同一個原則），因為 EDIT 的內容
就是原文，而 `SearchApps` 自己會再正規化一次。

**成本**：`NormalizeName` 對每次 `RefreshRows` 多跑一次，輸入是使用者打的
查詢字串（數個字元），一次配置。與同一個函式緊接著要做的 5,000 筆掃描
（NR-047 實測 603 µs）相比不可測量。**不要**為此加快取或 dirty flag。

**CMake**：確認 `panel_model` 所屬 target 能取得 `search_engine.h`。
`grep` `CMakeLists.txt` 裡 `nimblerun_panel_model` 的
`target_link_libraries`。若尚未連結 `nimblerun_search`，加上
`PUBLIC nimblerun_search`（比照 `nimblerun_catalog` 的既有寫法，見
`CMakeLists.txt:118-121` 一帶）。這是本 item 唯一可接受的 CMake 變更。

若連結關係讓你覺得需要新增抽象層才能取得 `NormalizeName`：**停手，改為在
`panel_model.cpp` 內寫一個三行的 `static bool IsBlank(...)`**，並在交接區
說明為什麼共用不可行。兩害相權，一個三行的重複好過一個新的連結層——但
先試共用。

### 3. 測試

**`tests/unit/panel_model_test.cpp`**，用既有 `Expect()` 慣例：

- `SetQuery(L" ")`、`SetQuery(L"   ")`、`SetQuery(L"\t")`：
  `Columns() > 1`（仍在格狀）、rows 等於釘選＋常用的內容，
  **不是**空集合。
- `SetQuery(L" a ")`：切到搜尋版面（`Columns() == 1`），且結果與
  `SetQuery(L"a")` 相同（§4.4 去頭尾空白）。
- `SetQuery(L"")` 之後 rows 與 `Reset()` 之後相同。
- `Esc()` 的兩段語意回歸：query 非空時回傳 false（或依實際語意）且模型
  query 被清空；query 已空時回傳 true。
- **`Esc()` 在只有空白的 query 上的行為**：打一個空白後按 Esc，應該先清空
  搜尋欄而不是關閉面板（因為「搜尋欄有內容」以使用者看到的原文為準）。
  這一條要對照 `Esc()` 的實作決定預期值，**若實作與此不符，以 §4.7 的
  「搜尋欄有內容時清空」為準去改 `Esc()`**，並在交接區記錄。

EDIT 子視窗的行為（`SetWindowTextW` → `EN_UPDATE`）無法單元測試，
由手動驗收 #1／#2 覆蓋，交接區要註明。

### 4. 更新 spec

`docs/design-spec.md` §4.7 的 `Esc` 條目後補一句（維持繁體中文與周邊 bullet 風格）：

> 清空搜尋欄指的是清空使用者可見的輸入框本身，面板顯示的內容隨之回到 §4.2
> 的釘選／常用畫面；兩者不得出現「已回到格狀但輸入框仍有殘字」的狀態。

§4.3 的版面切換條目後補一句：

> 「包含非空白字元」以 §4.4 的正規化結果判定：正規化後為空字串者一律視為
> 未在搜尋，維持 §4.2 的格狀版面。

不要動其他條文。

## How this stays maintainable

**一個輸入，一個真相，一個方向。** 使用者打字進 EDIT → `EN_UPDATE` →
`SetQuery` → `RefreshRows`。本 item 之後，程式碼想清空查詢也必須走同一條路
（改 EDIT，讓通知推下去），於是不存在「模型清了但 UI 沒清」這個狀態。
**未來任何需要以程式碼改變查詢的功能（例如從歷史帶入一個查詢）都必須
`SetWindowTextW`，不得直接呼叫 `SetQuery`。** 這是本 item 留下的唯一契約，
Agent checks 的 grep 守住它。

**「是不是在搜尋」只有一個判準。** §4.4 的正規化器是唯一實作，版面判斷、
搜尋執行都問它。若哪天正規化規則改變（例如 MVP 之後加入拼音展開），
版面切換會自動跟上，不會留下一個各自解讀空白的第二套判斷。

## Non-goals

- **改 `SearchApps` 或 `NormalizeName` 的行為。** 兩者都是對的。
- **搜尋防抖（debounce）、增量收斂、背景執行緒。** NR-047 實測 5,000 筆
  最壞路徑 204 µs、上限 50 ms；§4.3 明文「未來超過 5,000 筆或量測超標」才談。
  **有數字明確反對，不要開。**
- **改 §4.2 的空狀態內容或排序。** 那是 NR-053。本 item 只保證 Esc 與空白
  查詢會**回到**空狀態，不管那個狀態長什麼樣。
- **改 `HidePanel`、面板顯示流程、或 `ShowPanel` 的 EDIT 重設。**
- **在 EDIT 上加 cue banner／placeholder 文字。** 未在規格中。
- **處理搜尋框的滑鼠選取與面板拖曳的衝突**（審計另外指出的
  `WM_NCLBUTTONDOWN`／`HTCAPTION` 行為）。那是 NR-056 的 spec 補述範圍。

## Interaction with other open items

- **NR-053**（空狀態填滿與排序）改的是 `RefreshRows()` 的 `query_.empty()`
  **分支內部**；本 item 改的是那個 `if` 的**條件式**。同一個函式、不同行，
  **兩者都落地時會有相鄰的 diff**。先落地本 item（較小），NR-053 接著改
  分支內容，衝突面最小。
- **NR-048**（讓搜尋測試真的斷言）不碰 `panel_model`；可任意順序，但先落地
  它會讓本 item §2 對 `NormalizeName` 的依賴獲得真正的回歸保護。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §3 的全部新案例存在且通過。

Manual（Release build，逐條打勾）：

1. `Alt+Space`，打 `note`，按 `Esc`：**搜尋框變空**、畫面回到釘選／常用格狀。
   再按 `Esc`：面板關閉。
2. 承上，第一次 `Esc` 之後**直接打 `calc`**：搜尋框內容是 `calc`，
   不是 `notecalc`。
3. `Alt+Space`，按一下空白鍵：**畫面維持格狀**，不出現「No matching apps」。
   再按幾下空白：仍維持格狀。
4. 打「空白 + `note` + 空白」：結果與只打 `note` 相同。
5. 打一個空白後按 `Esc`：搜尋框清空、面板不關；再按 `Esc` 才關。
6. 一般搜尋、方向鍵、`Enter` 啟動、`Alt+數字` 快選的行為與本 item 前一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R panel_model --output-on-failure
```

```powershell
# 版面判斷用唯一的正規化器，不是自訂空白判斷：
Select-String -Path src/app_host/panel_model.cpp -Pattern 'query_.empty\(\)|NormalizeName'
# expect: RefreshRows 用 NormalizeName(query_).empty()；沒有殘留的裸 query_.empty() 版面判斷

# 沒有第二套空白判斷：
Get-ChildItem -Recurse -Include *.cpp,*.h -Path src | Select-String -Pattern 'IsBlank|iswspace'
# expect: no match（若 §2 的退路被啟用，交接區必須解釋）

# Esc 清的是 EDIT：
Select-String -Path src/app_host/main.cpp -Pattern 'VK_ESCAPE' -Context 0,8
# expect: 分支內出現 SetWindowTextW(edit, L"")

# 沒有繞過單一資料流（程式碼不直接改模型查詢）：
Select-String -Path src/app_host/main.cpp -Pattern 'SetQuery'
# expect: 只有 EN_UPDATE/EN_CHANGE 那一處

# 搜尋引擎未被改動：
git diff src/search/
# expect: empty

# 改動範圍：
git diff --name-only
# expect: main.cpp、panel_model.cpp、（可能）CMakeLists.txt、
#         panel_model_test.cpp、design-spec.md
```

## 交接區

（實作者填寫：修改的位置、`Esc()` 的實際 bool 語意、`SetWindowTextW` 是否
真的觸發既有的 query 更新路徑（如何驗證的）、是否需要 CMake 連結變更、
建置與 CTest 結果、6 條手動驗收結果、sanity greps、偏差、未完成事項。）

### 修改的位置

- `src/app_host/main.cpp:1999-2013` — `SearchEditProc` 的 `VK_ESCAPE` 分支：保留既有
  `if (g_model->Esc()) { HidePanel(...) }`，else 臂新增 `SetWindowTextW(edit, L"")`。
- `src/app_host/panel_model.cpp:48` — `RefreshRows()` 的版面分支條件改
  `NormalizeName(query_).empty()`。
- `src/app_host/panel_model.h` — `Columns()` getter（原 `query_.empty() ? grid_columns_ : 1`）
  改 `NormalizeName(query_).empty() ? grid_columns_ : 1`，並補 `#include "search/search_engine.h"`。
- `tests/unit/panel_model_test.cpp` — 新增 `TestWhitespaceQueryStaysInGrid`、
  `TestTrimmedQuerySameAsUntrimmed`、`TestSetQueryEmptyMatchesReset`、
  `TestEscOnWhitespaceQueryClearsFirst` 四函式＋註冊（既有 `TestEscClearsThenHides` 已覆蓋
  Esc 兩段語意回歸）。
- `docs/design-spec.md` — §4.3「版面切換點唯一」bullet 後補一句、§4.7 鍵盤表後補一句。

### `Esc()` 的實際 bool 語意

`panel_model.cpp:192-198`：**回傳 true ＝「query 已空，該隱藏面板」**；非空時內部
`SetQuery(L"")` 清空後回傳 false。與 §1 給的分支方向一致（true→HidePanel、false→清 EDIT）。
空白 query 也非空 → 回傳 false，先清空再隱藏，符合 §4.7「搜尋欄有內容時清空」；因此
**未改 `Esc()`**，§3「Esc 在只有空白 query 上的行為」以 `Esc()==false` 為預期值，測試通過。

### `SetWindowTextW` 是否真的觸發既有的 query 更新路徑

是。以讀程式碼驗證（未下中斷點／未加 `OutputDebugStringW`）：`SetWindowTextW` 會對父視窗
送出 `WM_COMMAND`/`EN_UPDATE`（Win32 文件化的 EDIT 行為）；`main.cpp:2181-2193` 的
`WM_COMMAND` case 正是 `LOWORD(w_param)==kSearchId && HIWORD(w_param)==EN_UPDATE`，讀回
`g_search_edit` 全文 → `g_model->SetQuery` → `UpdateViewportRows` → `InvalidateRect`。因此
Esc 清 EDIT 後空 query 自動推進模型，不需要在 `Esc()` 後另行 `SetQuery`。EDIT 子視窗的
通知行為無法單元測試，由手動驗收 #1／#2／#5 覆蓋。

### 是否需要 CMake 連結變更

**否**。`nimblerun_panel_model` 已 `PUBLIC nimblerun_search`
（CMakeLists.txt:273-276），`panel_model.cpp` 本已 `#include "search/search_engine.h"`。
共用 `NormalizeName` 可行，未動用 §2 的三行 `IsBlank` 退路。

### 建置與 CTest 結果

- configure＋build（Release, LLVM-MinGW）：成功，無新增警告。
- `ctest --test-dir build --output-on-failure`：**23/23 全綠**。
- `ctest --test-dir build -R panel_model`：回傳「No tests were found!!!」——既有註冊名是
  `nimblerun_list_vertical_slice_test`（執行檔名 `nimblerun_panel_model_test`），regex 不含
  `panel_model`；此命名屬既有狀態（NR-055 的清理範圍）。改以直接執行
  `build\tests\nimblerun_panel_model_test.exe` 驗證 exit 0（含新案例）。

### 6 條手動驗收結果

全部為人工視覺／操作驗證，依 `AGENTS.md` 交付規則不在 Agent 範圍，未執行。

### sanity greps

- `panel_model.cpp` 的 `query_.empty()|NormalizeName`：`RefreshRows` 用
  `NormalizeName(query_).empty()`（:48）；其餘兩處 `query_.empty()`（:163 prewarm guard、
  :199 `Esc()` 兩段語意）非版面判斷，符合預期。
- `IsBlank|iswspace`（src/）：唯一 `iswspace` 命中 `search_engine.cpp:20`（§4.4 正規化器的
  `CollapseWhitespace` 既有實作，本 item 未改）；無第二套空白判斷。
- `VK_ESCAPE`（-A 8）：分支內含 `SetWindowTextW(edit, L"")`（:2011）。
- `SetQuery`（main.cpp）：僅 `:2194`（`EN_UPDATE` 臂）一處，無繞過單一資料流。
- `git diff src/search/`：空。
- `git diff --name-only`：`docs/design-spec.md`、`src/app_host/main.cpp`、
  `src/app_host/panel_model.cpp`、`src/app_host/panel_model.h`、`tests/unit/panel_model_test.cpp`。

### 偏差

item §2 只列 `RefreshRows()`；實作額外改 `panel_model.h` 的 `Columns()` getter。理由：
§3 驗收測試明寫「`SetQuery(L" ")` → `Columns() > 1`（仍在格狀）」，而 `Columns()` 是版面
決定的**第二個**判定點，原樣保留 `query_.empty()` 時空白 query 仍回傳 1（切單欄）、第一條
新測試即紅。此變更與 §2「判定點只有一處」的決策一致（兩處版面判斷共用唯一正規化器），
非新增抽象層；header 僅補 include＋getter 一行。其餘與 item 無出入。

### 未完成事項

無。6 條手動驗收為人工操作；`ctest -R panel_model` 的命名錯位屬既有狀態，若要讓 item 的
Agent check 原樣可跑，屬 NR-055（測試 CMake 樣板清理）的範圍。
