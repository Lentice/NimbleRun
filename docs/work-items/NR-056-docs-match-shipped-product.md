# NR-056 — Make the docs describe the product that actually ships

Phase 3 · Status `ready` · Depends on: —

- Source: `AGENTS.md` §Work item authoring rules／§Validation／§Safety boundaries（行為變更時同步更新文件）
- Origin: 2026-08-06 repo audit：spec-conformance findings #8/#9/#10/#11、test-and-docs findings #8/#9/#10

## Why

`docs/` 目前有六處與程式碼不符。它們不會讓程式當機，但會讓**下一個
agent 或人類照著做出錯誤的事**——而本 repo 的整個工作方式建立在
「design-spec 是產品的唯一真相、work item 引用它的條文」之上。規格說謊，
每個引用它的 item 就一起說謊。

1. **`docs/testing.md` 停在 Phase 0。** `:11` 寫「The current unit test covers
   the pure search/ranking module」，實際有 22 個單元執行檔加上
   `nimblerun_lifecycle_check`；`:13-20` 的「Manual smoke test for the Phase 0
   probe」還要測試者確認「the fake app grid」有畫出來，而那個假格狀早就被
   真實 catalog 取代。照這份文件走一遍**不會產生任何訊號**。
2. **`docs/performance-baseline.md` 十列有八列是 `Not measured`**，包含全部
   的放行門檻（冷啟動、熱鍵 p95、filter 500 apps p95、working set、
   `icons.cache` 大小），而 `AGENTS.md §Validation` 稱這張表是發布前的
   驗收依據。同時 NR-047 已經有真實的 `SearchApps` 數字
   （5,000 筆 603 µs／204 µs），`release_evidence.ps1` 也已經在量 idle
   working set／private bytes／handles——**數字有了，只是沒回填。**
3. **`docs/release-evidence.md` 的工具版本是假的**：`cmake | Usage`、
   `ninja | ninja: error: loading 'build.ninja'`、`ctest | *****`。
   起因是 `tests/release/release_evidence.ps1:22` 的 `Get-CmdVersion` 宣告了
   一個名為 `$args` 的參數，而 `$args` 是 PowerShell 的自動變數；
   `& $name @args` 因此展開的是呼叫者的（空）引數陣列，`--version` 從未
   傳到工具。這份文件的存在意義就是可重現的證據，而它的環境半邊是編造的。
4. **§10.3 的 stable ID 規則與程式碼不符（程式碼是對的）。**
   AppsFolder 項目在能解析出檔案系統路徑時用 resolved path 雜湊
   （`appsfolder_catalog.cpp:117`），UserFolder 的 `.lnk`／`.appref-ms` 用
   自身路徑而非 resolved target（`user_folder_catalog.cpp:89`，有既有
   `ponytail:` 註解說明原因）。兩處偏差都是深思過的，但規格還寫著舊規則。
5. **§4.8 的右鍵選單少列了兩項**：實作另有「Remove from recent」
   （`main.cpp:2360`）與「Properties」（`:2367`，走 Shell `properties` verb，
   處理在 `:778`）。兩者是 NR-040 交付的，規格沒跟上。
6. **面板可拖曳移動，規格完全沒提**（`main.cpp:2258` 與 `:1856` 把空白處與
   搜尋框的按壓交給 `WM_NCLBUTTONDOWN`／`HTCAPTION`）。§4.1 只說面板出現在
   游標所在螢幕工作區中央。這個行為還有一個副作用：**搜尋框內無法用滑鼠
   選取文字**。
7. **匣選單的「About」是死的**（選單項在 `main.cpp:1730`，處理在 `:2104`
   只有一個 `return 0;` 與一句 `ponytail:` 註解說明「about dialog is not
   specced; this item only provides the dispatch target」）。§4.10 要求匣選單
   有「關於」，所以正確的方向是讓它做事，而不是拿掉。

## Decisions already made — do not reopen

決定於撰寫本 item 時：

1. **程式碼對的地方改文件，文件對的地方改程式碼。** 逐項判定如下，
   不要重新討論：
   - #4（identity 偏差）：**程式碼對**，改 §10.3。兩處偏差都有實作理由，
     且改 identity 會讓所有既有的 `favorites.txt`／`usage.tsv` 記錄失效。
   - #5（右鍵選單）：**程式碼對**，改 §4.8。NR-040 是使用者驗收過的功能。
   - #6（面板拖曳）：**程式碼對**，改 §4.1／§4.8 補述。但**搜尋框內不該
     被拖曳吃掉**，那一條是 bug，見 §4。
   - #7（About）：**規格對**，補一個最小的 About 顯示。
   - #1／#2／#3：文件與腳本的問題，改文件與腳本。
2. **About 用 `MessageBoxW`，不做對話框資源。** 一個含產品名與版本的
   訊息框滿足 §4.10。不加圖示、不加連結、不加授權文字。
3. **效能表格只填「你真的量到的數字」。** 量不到的維持 `Not measured`
   並在該列註明還缺什麼。**絕不填估計值。** 一張半滿但誠實的表格，
   比一張填滿了猜測的表格有用得多。
4. **不改 `release_evidence.ps1` 的輸出格式或量測項目**，只修 `$args` 這個
   bug 並重新產生文件。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- The design specification in `docs/design-spec.md` is the product source of
  truth. Do not add features that are listed as out of scope there.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.
- Project documentation may use Traditional Chinese.
- NimbleRun application UI text must be English.
- Before release, validate the acceptance criteria and resource budgets in
  `docs/testing.md` and `docs/performance-baseline.md` with a Release x64 build.
- When an item overrides an earlier decision, state the override inside the new
  item. Never edit a completed item's document.

design-spec §4.10（本 item #7 的依據）：

> 通知區選單提供：顯示面板、設定、關於、結束。

design-spec §10.3（本 item #4 要修的條文）：

> - Start Menu 項目：以正規化 Shell parsing identity／resolved target 加必要
>   參數產生 SHA-256 或穩定雜湊表示。
> - stable ID 不可依顯示名稱、圖示或目前排序產生。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `docs/testing.md` **整檔**。特別是 `:11` 的單一測試敘述與 `:13-20` 的
  Phase 0 smoke test 段落。
- `docs/performance-baseline.md` **整檔**（約 16 行的表格）。看清楚每一列的
  項目名稱與單位，§2 只填值，不改列。
- `docs/release-evidence.md` — 目前的產出，`:13` 一帶是壞掉的版本欄位。
- `tests/release/release_evidence.ps1` **整檔（223 行）**，特別是 `:22`
  的 `Get-CmdVersion`。**先讀懂它整體在量什麼**，再改那一個參數名。
- `tests/integration/lifecycle_check.ps1` — 另一支腳本，確認 §1 重寫
  `testing.md` 時把它的存在寫進去。
- `docs/design-spec.md` §4.1／§4.8／§4.10／§10.3 — §3 要改的四處。
  **只改這四處。**
- `src/catalog/appsfolder_catalog.cpp:110-125` — resolved path 的 identity
  處理（§3 要照它的實際行為寫規格）。
- `src/catalog/user_folder_catalog.cpp:85-95` — `.lnk` identity 的既有
  `ponytail:` 註解（§3 的第二處依據）。
- `src/app_host/main.cpp:2350-2375` — 右鍵選單的實際項目（§3）。
- `src/app_host/main.cpp:778-805` — `ShowItemProperties`（Shell
  `properties` verb）。
- `src/app_host/main.cpp:1722-1747` — `ShowTrayMenu`，含 About 項。
- `src/app_host/main.cpp:2100-2110` — `DispatchTrayCommand` 的 About 空實作
  與其 `ponytail:` 註解。§5 取代它。
- `src/app_host/main.cpp:2250-2265` 與 `:1850-1860` — 面板拖曳的
  `WM_NCLBUTTONDOWN`／`HTCAPTION` 兩處（§3 補述、§4 修搜尋框）。
- `src/resources/NimbleRun.rc` 與 `src/resources/resource.h` — 版本資訊
  （`VS_VERSION_INFO`／`FILEVERSION`）的所在。§5 的 About 要顯示的版本
  **從這裡取**，不要寫死第二份版本號。

## Scope

### 1. 重寫 `docs/testing.md` 的失效段落

- `:11` 的敘述改為實際狀況：**先執行一次
  `ctest --test-dir build -N` 取得當下的測試清單與數量**，用那個結果寫，
  不要抄本文的「22」。敘述要說明測試分成單元測試（`tests/unit/`）、
  整合檢查（`tests/integration/lifecycle_check.ps1`）與發布證據
  （`tests/release/release_evidence.ps1`）三類，各自何時跑。
- **刪掉整個「Manual smoke test for the Phase 0 probe」段落**，換成一份
  對應現況的手動冒煙測試。內容以既有 work item 的手動驗收為素材，
  取其中最基本的一組（不要複製全部）：
  1. `Alt+Space` 開啟面板，出現在游標所在螢幕的工作區中央。
  2. 空狀態顯示釘選／常用格狀（NR-053 落地後應為填滿的格狀）。
  3. 打字切換為搜尋清單，`Enter` 啟動選取項目，面板隱藏。
  4. `Esc` 清空搜尋欄，再 `Esc` 隱藏面板。
  5. 右鍵選單的釘選／取消釘選生效，重啟後仍在。
  6. 匣選單的四個項目都可用（顯示面板、設定、關於、結束）。
  7. 200% DPI 下版面與圖示正常。

  每一條寫成「操作 → 預期」，讓沒有前情的人可以照著打勾。
- 若檔內其他地方提到「fake app grid」「probe」「Phase 0」，一併處理。
  用 `Select-String -Path docs/ -Pattern 'Phase 0|probe|fake app'` 掃過
  整個 `docs/`，把命中的每一處判斷一次（`AGENTS.md` 自己的
  「Current baseline」段落也提到 Phase 0——**那一段不改**，它描述的是
  repo 的起點，是準確的歷史敘述）。

### 2. 回填 `docs/performance-baseline.md`

**只填你在這次工作中真的量到的數字**，每一列附上量測方式與日期。

已知可立即回填的：

- **Filter 500 apps p95**：用 NR-047 已有的 `SearchApps` 數字。
  注意那是 5,000 筆不是 500 筆，**所以要如實標註規模**：
  「5,000 筆、`L"e"` 查詢、603 µs（NR-047，2026-08-06）」，
  不要把它當成 500 筆的數字填進去。若表格的列名是 500 筆，
  就在該列填上 5,000 筆的實測值並註明「量測規模大於門檻規模，故為
  保守上界」。
- **Idle working set / private bytes / handles**：`release_evidence.ps1`
  已經在量，且 §3 修好之後會產生可信的輸出。從新產生的
  `docs/release-evidence.md` 抄過來。

其餘（冷啟動、熱鍵 p95、`icons.cache` 大小）若你有辦法在 Release 建置上
量到就量並填；**量不到就維持 `Not measured`，並在該列加一句還缺什麼
（例如「需要一支量測熱鍵到首幀的計時器，尚未實作」）**。
`Not measured` 加上「缺什麼」比裸的 `Not measured` 有用，而編造的數字
比兩者都糟。

表格的列與單位**不要改**。

### 3. 四處 spec 修正（程式碼是對的）

`docs/design-spec.md`，繁體中文，維持周邊條列風格。**只改這四處。**

**§10.3** — 補上兩條實際規則（不要刪既有條文，補述在其後）：

> - AppsFolder 項目：能解析出檔案系統路徑時以該 resolved target 產生
>   stable ID，其餘情況以 AUMID／parsing name 產生。目的是讓同一個程式
>   透過不同來源被找到時收斂為同一個身分。
> - 使用者自訂資料夾中的 `.lnk`／`.appref-ms`：以捷徑自身的正規化路徑產生
>   stable ID，不解析其 target。該列舉器刻意不在掃描時使用 Shell COM，
>   因此跨來源的去重在此較為保守。

**§4.8** — 右鍵選單條目補上兩項：

> 搜尋結果與清單列的右鍵選單提供：釘選／取消釘選、開啟檔案位置、
> 自常用清單移除、內容（交由 Shell 的 properties verb 顯示）。

**§4.1／§4.8** — 面板拖曳補述（放在版面或滑鼠操作的段落，擇一，
不要兩邊都寫）：

> 面板出現於游標所在螢幕工作區中央後，使用者可按住面板空白處拖曳移動；
> 拖曳僅改變本次顯示的位置，不持久化。搜尋輸入框內的按壓一律屬於文字
> 選取，不觸發拖曳。

最後那一句是 §4 要實作的行為，**先寫進規格再改程式碼**。

### 4. 搜尋框內的按壓不再被拖曳吃掉

`main.cpp:1856` 一帶——搜尋 EDIT 的訊息處理把按壓轉給
`WM_NCLBUTTONDOWN`／`HTCAPTION` 的那一處。移除**搜尋框內**的那條轉發，
保留面板空白處（`:2258` 一帶）的：

```cpp
    // NR-056: dragging the panel by its empty area is fine (see spec §4.1), but
    // the search box is a text field first. Forwarding its clicks to
    // HTCAPTION made mouse text selection impossible inside the one control the
    // user types into. Panel dragging stays available everywhere else.
```

**先確認 `:1856` 的那段真的是搜尋框的按壓轉發**——讀懂上下文再刪，
若它其實負責別的事（例如點搜尋框時把焦點交回），只移除轉發那一行。

### 5. About 做一件事

`DispatchTrayCommand` 的 About 分支，取代空的 `return 0;`：

```cpp
        // NR-056: design-spec §4.10 requires an About entry in the tray menu.
        // A menu item that does nothing is worse than no menu item: it reads as
        // a bug every time it is clicked. A MessageBox with the product name
        // and version satisfies the clause; anything more (icon, links, license
        // text) is not specced.
```

- 產品名用既有的集中式 UI 字串機制（`AGENTS.md`：多於一個畫面需要時要
  集中）。**若目前只有這一處需要，就用一個檔案範圍的常數**，不要為了
  一個字串新增字串表項目——除非它與設定頁共用（設定頁標題可能已有
  產品名，先查 `settings_editor.h` 的 `DialogTitle`）。
- 版本號**從 `src/resources/NimbleRun.rc` 的 `VS_VERSION_INFO` 取**，
  用 `GetModuleFileNameW` + `GetFileVersionInfoW` / `VerQueryValueW`，
  或若 `.rc` 已把版本定義成一個 header 常數就直接用那個常數。
  **絕不在 `main.cpp` 硬寫第二份版本號**——那是保證會漂移的東西。
  若取版本需要連結 `version.lib`，加上它（這是本 item 唯一可接受的
  CMake 變更）。若取版本的成本明顯超過本 item 的分量，
  **退而顯示產品名與一句簡短描述、不顯示版本**，並在交接區說明。
- `MessageBoxW` 的 owner 傳面板 HWND，`MB_OK | MB_ICONINFORMATION`。
- 標題與內文為英文（`AGENTS.md`：UI 文字用英文）。

### 6. 修 `release_evidence.ps1` 並重新產生證據

`tests/release/release_evidence.ps1:22` 的 `Get-CmdVersion`：把參數
`$args` 改名（例如 `$Arguments`），並同步改函式內的 `@args` 展開。

```powershell
# NR-056: $args is a PowerShell automatic variable, so declaring a parameter
# with that name silently shadowed nothing useful -- `& $name @args` splatted
# the caller's own (empty) argument array and --version never reached the tool.
# Every recorded tool version in docs/release-evidence.md was therefore an
# error message.
```

掃過整支腳本有沒有第二處同樣的錯誤
（`Select-String -Path tests/release/release_evidence.ps1 -Pattern '\$args'`）。

修好後**重新執行腳本並重新產生 `docs/release-evidence.md`**：

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
pwsh tests/release/release_evidence.ps1     # 以腳本實際的呼叫方式為準
```

確認 `cmake`／`ninja`／`ctest` 三個版本欄位是真的版本字串。
產生出來的數字接著餵給 §2。

### 7. 測試

本 item 大部分是文件，但 §4 與 §5 是程式碼：

- §5 的 About：`main.cpp` 無法單元測試。若版本讀取被實作成一個可測的
  純函式（例如「把 `VS_FIXEDFILEINFO` 格式化成 `1.2.3.4`」），
  **就替那個函式加一個小測試**，放進最合適的既有測試執行檔
  （避免新增執行檔——見 NR-055 的互動）。若整段都是 Win32 呼叫，
  由手動驗收覆蓋，在交接區註明。
- §4 的搜尋框按壓：由手動驗收覆蓋。
- §6 的腳本：修好後跑一次就是它的測試；把三個版本字串貼進交接區。

## How this stays maintainable

**規格與程式碼的偏差要嘛修碼、要嘛修規格，不能兩邊都留著。**
本 item 之後，`docs/design-spec.md` 的每一條都描述實際出貨的行為，
於是下一個 item 引用條文時不需要先驗證那條是不是還算數。
**任何未來的 item 若發現第三種偏差，處置方式與本 item 相同：當場判定
哪一邊是對的，然後在同一個 item 裡修另一邊。**

**證據文件要嘛是真的，要嘛標明是空的。** `Not measured` 是誠實的狀態；
`cmake | Usage` 不是——它看起來像資料。§2 的規則
（只填量到的、量不到就寫還缺什麼）讓這張表永遠可以被信任。

**版本號只有一個出處。** §5 明文禁止在程式碼裡硬寫第二份。

**死的 UI 入口不留。** About 的 `ponytail:` 註解誠實記錄了它是佔位，
但佔位在出貨的選單裡就是 bug。本 item 之後，選單裡的每一項都會做事。

## Non-goals

- **實作 §4.6 完整的 7/30 日 usage bucket**、**§4.2 的空狀態填充**、
  **§FR-011 的設定頁 pin 清理**。前兩者是 NR-053，第三者尚未開 item。
  本 item 只處理「文件說謊」，不處理「功能未做」——**§10.1 的
  `logs\` 位置是 NR-054，不要在這裡順手改。**
- **完整的 About 對話框**（圖示、授權、連結、更新檢查）。Decisions §2。
- **改 `release_evidence.ps1` 的量測項目、輸出格式或新增量測。**
  只修 `$args`。
- **實作缺少的效能量測工具**（冷啟動計時器、熱鍵到首幀的量測）。
  §2 明文允許維持 `Not measured` 並註明缺什麼。
- **移除面板拖曳功能。** Decisions §1：程式碼是對的，只是規格沒寫。
- **改右鍵選單或匣選單的項目組成。** 只補規格 + 讓 About 動起來。
- **重寫 `docs/development.md` 或 `docs/work-items.md`。**
- **改 `AGENTS.md` 的「Current baseline」段落。** 它描述的是歷史起點。

## Interaction with other open items

- **NR-054** 也會碰 §10.1 附近的規格與 `settings_dialog.cpp`。
  **本 item 明文不碰 §10.1 的記錄檔位置**，交給 NR-054。兩者若同時進行，
  design-spec 會有相鄰的 diff，注意不要互相覆蓋。
- **NR-052／NR-053** 各自會在 §4.2／§4.3／§4.7 補條文；本 item 改的是
  §4.1／§4.8／§4.10／§10.3。**無重疊條文**，但同一個檔案，
  合併時逐 hunk 檢查。
- **NR-053 落地後**，`docs/testing.md` 的手動冒煙測試第 2 條（空狀態）
  才會是「填滿的格狀」。**若本 item 先落地**，那一條寫成當下的實況並
  在括號註明「NR-053 落地後應為填滿的格狀」。
- **NR-055** 改 `tests/CMakeLists.txt`；本 item 若 §7 要加測試，
  **加進既有執行檔**即可，兩者不衝突。
- 各 item 都可能量到新數字。**`docs/performance-baseline.md` 由本 item
  負責整表回填**，其他 item 只填自己那一列。先落地的先填。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. `release_evidence.ps1` 執行後，`docs/release-evidence.md` 的三個工具
   版本欄位是真實版本字串（不含 `Usage`、`error`、`*****`）。
3. `Select-String -Path docs/ -Pattern 'fake app|Phase 0 probe'` 在
   `docs/testing.md` 中無 match。
4. §7 的測試（若可實作）通過。

Manual（Release build，逐條打勾）：

1. 照重寫後的 `docs/testing.md` 的手動冒煙測試**從頭走一遍**，
   七條全部得到預期結果。這同時是對那份文件本身的驗收——
   **若有任何一條寫得含糊到你自己都要猜，就是文件還沒寫好，回去改。**
2. 匣選單 → 關於：出現含產品名（與版本，若有實作）的訊息框，
   按 OK 關閉，不影響面板狀態。
3. 搜尋框內用滑鼠拖曳選取文字：**文字被選取**，面板不移動。
4. 面板空白處拖曳：面板跟著移動（既有行為未被破壞）。
5. 右鍵選單四個項目與匣選單四個項目**逐一點過**，全部都有反應。
6. 對照 `docs/design-spec.md` 的 §4.1／§4.8／§4.10／§10.3 四處新條文，
   確認描述與你剛才操作的行為一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -N        # §1 的測試數量要用這個結果，不要抄文件
```

```powershell
# 腳本 bug 已修，且沒有第二處：
Select-String -Path tests/release/release_evidence.ps1 -Pattern '\$args'
# expect: no match

# 證據文件不再是錯誤訊息：
Select-String -Path docs/release-evidence.md -Pattern 'Usage|error|\*\*\*\*\*'
# expect: no match

# Phase 0 的殘留：
Select-String -Path docs/testing.md -Pattern 'Phase 0|probe|fake app'
# expect: no match

# 效能表格沒有編造的數字（每個非 Not measured 的值都要有出處）：
Get-Content docs/performance-baseline.md
# 人工檢查：每一列不是 `Not measured`（附缺什麼），就是實測值（附方法與日期）

# 版本號沒有第二份：
Select-String -Path src/app_host/main.cpp -Pattern '\d+\.\d+\.\d+'
# expect: 無硬寫的版本字串（時間、尺寸、DPI 常數不算）

# 搜尋框不再轉發拖曳：
Select-String -Path src/app_host/main.cpp -Pattern 'HTCAPTION|WM_NCLBUTTONDOWN'
# expect: 只剩面板空白處那一處

# About 不再是空的：
Select-String -Path src/app_host/main.cpp -Pattern 'MessageBoxW' -Context 3,3
# expect: About 分支內有一次；ponytail: about dialog 註解已移除

# 本 item 不碰 §10.1 的記錄檔位置（那是 NR-054）：
git diff docs/design-spec.md
# expect: 只有 §4.1／§4.8／§4.10／§10.3 的 hunk
```

## 交接區

（實作者填寫：`ctest -N` 的實際測試數量、三個工具版本字串、
效能表格回填了哪幾列與各自的量測方法／數字／日期、哪幾列仍是
`Not measured` 及缺什麼、About 是否成功取到版本（若否，為什麼）、
§4 移除的是哪一行與其上下文、建置與 CTest 結果、6 條手動驗收結果
（特別是 #1 走 testing.md 時發現的任何含糊處）、sanity greps、
偏差、未完成事項。）
