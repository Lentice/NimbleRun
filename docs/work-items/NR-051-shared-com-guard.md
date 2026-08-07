# NR-051 — One COM guard, balanced on `S_FALSE`, and no uninitialized Shell buffers

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-004（`.lnk` 以 Shell API 解析）／§FR-006（封裝 App）／§NFR-002（閒置資源）
- Origin: 2026-08-06 repo audit, findings #6 與 #7（正確性）＋ ponytail 稽核（重複程式碼）

## Why

三個檔案各自寫了一份 COM 生命週期樣板，而其中**同一個 bug 被複製了兩次**：

`src/catalog/start_menu_catalog.cpp:57-75` 與
`src/catalog/appsfolder_catalog.cpp:22-40` 的 `ComGuard` 是逐字相同的類別，
兩份都寫：

```cpp
own_ = hr == S_OK;
```

MSDN 要求**每一次成功的 `CoInitializeEx` 都要配一次 `CoUninitialize`，
包含回傳 `S_FALSE` 的那些**（`S_FALSE` 表示這條執行緒已經初始化過，計數 +1）。
現況是巢狀情境下計數只增不減：`EnumerateStartMenuCatalog` 建外層 guard
（`S_OK`，計數 1）→ 呼叫 `EnumerateProgramsDirectory` 建內層 guard
（`S_FALSE`，計數 2）→ 內層 `own_ == false` 不呼叫 `CoUninitialize`（計數仍 2）
→ 外層解構呼叫一次（計數 1）→ **apartment 永遠不被拆除**。每一次 Start Menu
重建（每次 `Ctrl+R`、每次套用設定、每次 debounce 後的 watcher 事件）都洩漏一次。

同一個檔的 `ResolveShortcut`（`start_menu_catalog.cpp:116-129`）有第二個缺陷：

```cpp
wchar_t target[1024];                     // 未初始化
if (SUCCEEDED(shell_link->GetPath(target, 1024, nullptr, SLGP_UNCPRIORITY)) &&
    target[0] != L'\0') { ... }
wchar_t arguments[1024];                  // 未初始化
if (SUCCEEDED(shell_link->GetArguments(arguments, 1024))) {
    result.arguments.assign(arguments);   // S_FALSE 時讀未初始化記憶體
}
```

`IShellLinkW::GetPath` 對「只有 PIDL、沒有檔案路徑」的捷徑（控制台項目、
指向封裝 App 的捷徑）回傳 `S_FALSE`，而 `SUCCEEDED(S_FALSE)` 為真。
`target` 那條有 `target[0] != L'\0'` 擋著，但那一次讀取本身就是讀未初始化的
堆疊記憶體；`arguments` 與 `working` 兩條連那道防線都沒有，`assign` 會在
2 KB 的堆疊框裡掃描 NUL，把殘留 bytes 變成 `search_alias` 與 stable id 的輸入。
這是未定義行為，也是「同一個捷徑在不同次掃描產生不同 identity」這種
難以重現的 bug 的完美溫床。

ponytail 面：`png_codec.cpp:22-28` 另有一個 `ComRelease` deleter，
`shell_icon_provider.cpp:85` 與 `start_menu_catalog.cpp` 多處則是手動
`->Release()`。**同一件事有四種寫法**，而其中兩種帶著同一個 bug。修一次、
定義一次。

## Decisions already made — do not reopen

決定於撰寫本 item 時：

1. **新增一個小的共用 header，而不是就地修兩份。** 平常 ponytail 會反對新增
   抽象，但這裡「抽象」已經存在了——只是被複製了兩份且各自帶 bug。把逐字
   相同的類別收成一份是刪除，不是新增。
2. **只收 `ComGuard`（初始化守衛）與 `ComPtr`（Release deleter）兩樣，
   不做 COM 包裝層。** 不寫 smart pointer 類別、不寫 `HRESULT` 例外轉換、
   不寫 `IID_PPV_ARGS` 的封裝。既有的 `std::unique_ptr<T, ComRelease>` 形狀
   已經夠用，把它移到共用位置即可。
3. **不把既有的手動 `->Release()` 全面改寫成 RAII。** 只有本 item 已經在改的
   函式（`ResolveShortcut`）順手轉，其餘維持原樣。全面改寫是與正確性無關的
   churn，且每改一處都是一次引入 double-release 的機會。
4. **緩衝區以 `= {}` 零初始化，不是 `memset`、不是換成 `std::wstring` 動態
   配置。** 一行、零成本、無疑義。

## Binding constraints — quoted, do not go looking for them

design-spec §FR-004：

> - `.lnk` 以 `IShellLinkW`／`IPersistFile` 解析，禁止自行解析二進位格式。
> - 無法解析但 Shell 可正常開啟的捷徑仍可保留，啟動時交給 Shell。

design-spec §10.3：

> - stable ID 不可依顯示名稱、圖示或目前排序產生。

（本 item 相關：未初始化的 `target`／`arguments` 會進入 identity_key，
讓 stable id 變得不可重現——這正是 §10.3 要求「跨執行可重現」的反面。）

design-spec §NFR-002：

> 閒置時不得有忙碌迴圈或高頻計時器。

（COM apartment 洩漏不是計時器，但屬於同一類「閒置時仍在累積資源」。）

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep App Catalog data as ordinary copyable values. UI code must not own Shell
  COM pointers.
- Use the C++ standard library or Win32 native APIs before adding dependencies.
- New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/catalog/start_menu_catalog.cpp:57-75` — `ComGuard`（含 `own_`／`usable_`
  與 `RPC_E_CHANGED_MODE` 的處理）。**`usable_` 的邏輯是對的，不要改**：
  `RPC_E_CHANGED_MODE` 表示這條執行緒已經是別的 apartment 模型，COM 仍可用但
  這一層不擁有它。只有 `own_` 要改。
- `src/catalog/appsfolder_catalog.cpp:22-40` — 第二份，逐字比對確認真的相同
  （若有差異，差異就是規格，要在共用版本裡保留並在交接區說明）。
- `src/catalog/start_menu_catalog.cpp:102-140` — `ResolveShortcut` 全文：
  三個 1024 wchar 緩衝區、`shell_link` 與 `persist` 的手動 Release、
  以及 `ShortcutIsWeb` 的呼叫。
- `src/catalog/start_menu_catalog.cpp:45-55` — `ShortcutIsWeb`，另一處手動
  `item->Release()`。
- `src/icons/png_codec.cpp:19-28` — `ComRelease` deleter 與其註解
  （「same as the manual factory->Release() calls in shell_icon_provider.cpp」——
  這句話本身就是本 item 存在的理由）。
- `src/icons/shell_icon_provider.cpp:85` — 手動 `factory->Release()`。
- `src/icons/icon_worker.cpp:97` — worker 執行緒自己的 `CoInitializeEx`
  （`COINIT_APARTMENTTHREADED`，無 `COINIT_DISABLE_OLE1DDE`）與其配對的
  `CoUninitialize`。**注意它與 catalog 的兩份用的旗標不同**；§1 的共用版本
  必須保留這個差異或明確處理它——見 §1 的旗標小節。
- `src/app_host/main.cpp:2515` — UI 執行緒的 `CoInitializeEx`。**不改**，
  但要知道它存在：catalog 列舉若在 UI 執行緒上跑，`ComGuard` 拿到的就是
  `S_FALSE`，正是本 item 要修的那條路。
- `CMakeLists.txt` — 各 target 的來源清單與 include 路徑。新 header 放在
  哪個既有 target 底下才不需要新增 CMake target，見 §1。
- `tests/unit/start_menu_catalog_test.cpp` — `CreateShortcut` helper 與既有
  fixture（含 `計算機.lnk`、`小算盤.lnk`）。§3 在這裡加案例。

## Scope

### 1. 一個共用 header

新增 `src/win/com.h`（若 `src/win/` 不存在就建立；`src/storage/atomic_text_file.h`
是本 repo「純 header、跨模組共用的小工具」的既有先例，照它的形狀寫）：

```cpp
#pragma once

#include <windows.h>
#include <objbase.h>

#include <memory>

namespace nimblerun {

// Per-thread COM lifetime. Previously duplicated verbatim in
// start_menu_catalog.cpp and appsfolder_catalog.cpp, both of which balanced
// only S_OK -- so a nested guard that got S_FALSE never called CoUninitialize
// and the apartment was never torn down. MSDN requires one CoUninitialize per
// successful CoInitializeEx *including* S_FALSE.
class ComGuard {
public:
    explicit ComGuard(DWORD flags = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) {
        const HRESULT hr = CoInitializeEx(nullptr, flags);
        // NR-051: SUCCEEDED covers both S_OK (this call initialized COM) and
        // S_FALSE (already initialized on this thread; the reference count went
        // up and must come back down). RPC_E_CHANGED_MODE means the thread is
        // already in a different apartment model -- COM is usable but this
        // guard did not add a reference, so it must not remove one.
        own_ = SUCCEEDED(hr);
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~ComGuard() {
        if (own_) {
            CoUninitialize();
        }
    }

    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;

    bool Usable() const { return usable_; }

private:
    bool own_ = false;
    bool usable_ = false;
};

// Release deleter for std::unique_ptr over a COM interface. Moved here from
// png_codec.cpp, which is where the shape was already correct.
struct ComRelease {
    void operator()(IUnknown* ptr) const noexcept {
        if (ptr != nullptr) {
            ptr->Release();
        }
    }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComRelease>;

} // namespace nimblerun
```

**旗標**：建構子帶預設參數，讓 catalog 的兩處（apartment + disable OLE1DDE）
不必傳參，而 `icon_worker.cpp` 若要改用可以傳它自己的旗標。
**`icon_worker.cpp` 本 item 不改**（它的初始化/反初始化已成對正確），
只在共用 header 的註解裡留下「它可以改用這個」的事實，不留 TODO。

**刪除**兩份 `ComGuard` 與 `png_codec.cpp` 的 `ComRelease`，改為
`#include "win/com.h"`。`png_codec.cpp` 裡的
`std::unique_ptr<IWICImagingFactory, ComRelease>` 可原樣保留（型別別名相同），
或改用 `ComPtr<IWICImagingFactory>` —— **兩者擇一並貫徹整檔**，不要混用。

**CMake**：`src/win/com.h` 是純 header。確認它被需要的 target 找得到——
本 repo 的 include 是以 repo 根為基準（`#include "icons/icon_cache.h"` 這種寫法），
所以**很可能不需要任何 CMake 變更**。先確認，若真的需要，只加最小的
`target_include_directories`，不新增 target。

### 2. `ResolveShortcut`：零初始化 + RAII

三個緩衝區全部零初始化，並讓 `S_FALSE` 不再被當成成功：

```cpp
        // NR-051: GetPath / GetArguments / GetWorkingDirectory return S_FALSE
        // for a link that has no such value (a PIDL-only shortcut to a control
        // panel item or a packaged app), and SUCCEEDED(S_FALSE) is true. The
        // buffers used to be uninitialized, so assign() scanned leftover stack
        // bytes for a NUL -- undefined behavior that feeds garbage into
        // search_alias and the §10.3 identity key, making the same shortcut
        // hash differently between scans. Zero-init makes the existing
        // `[0] != L'\0'` test meaningful, and every buffer now has one.
        wchar_t target[1024] = {};
        if (shell_link->GetPath(target, 1024, nullptr, SLGP_UNCPRIORITY) == S_OK &&
            target[0] != L'\0') {
            result.target.assign(target);
        } else {
            result.web = ShortcutIsWeb(*shell_link);
        }
        wchar_t arguments[1024] = {};
        if (shell_link->GetArguments(arguments, 1024) == S_OK && arguments[0] != L'\0') {
            result.arguments.assign(arguments);
        }
        wchar_t working[1024] = {};
        if (shell_link->GetWorkingDirectory(working, 1024) == S_OK && working[0] != L'\0') {
            ...
        }
```

兩項改動，兩個理由，都要保留在註解裡：

- `SUCCEEDED(...)` → `== S_OK`：明確拒絕 `S_FALSE`。
- `= {}` + `[0] != L'\0'`：即使某個 Shell 實作回了 `S_OK` 卻沒寫入緩衝區，
  行為仍然是定義良好的「沒有值」。

**行為變更檢查**：現況下 `arguments` 在 `S_FALSE` 時會 assign 到垃圾（通常是
空字串，因為堆疊上多半是零），改後一律為空。`arguments` 進入 identity_key，
所以**理論上某些捷徑的 stable id 會改變**——只有那些原本讀到非零垃圾的，
也就是原本 id 就不穩定的那些。這是修好，不是破壞；但要在交接區記錄，
且手動驗收 #3 要確認釘選未大量失效。

順手把 `shell_link` / `persist` 改為 `ComPtr`（本函式已在改，且早退路徑
正是 Release 容易漏的地方）。`ShortcutIsWeb` 的 `item->Release()` 一併轉。
**其餘檔案的手動 Release 不動**（Decisions §3）。

### 3. 測試

**`tests/unit/start_menu_catalog_test.cpp`** — 新增一個「沒有檔案路徑目標」的
捷徑 fixture，直擊 `S_FALSE` 那條路：

用既有的 `CreateShortcut` helper 建一個目標為某個 shell folder 或不存在路徑的
捷徑；若 helper 無法產生 PIDL-only 捷徑，退而求其次：建一個
**目標為空字串**的捷徑。斷言：

- 該項目仍出現在結果中（§FR-004：無法解析但 Shell 可開啟的捷徑仍保留）。
- 它的 `search_alias` 為**空**（不是垃圾）。
- 它的 `arguments`（若測試可觀察到）為空。
- **同一個 fixture 連續列舉兩次，stable id 相同。** 這一條是本 item 的核心
  斷言：未初始化緩衝區造成的正是「兩次不同」。
- 既有的 6→7 筆數字與 `Notepad.lnk`／`Notepad Copy.lnk` 共用 stable id 的
  斷言仍通過（先讀工作樹確認目前的實際筆數，不要相信本文的數字）。

若你確認 `CreateShortcut` 無法造出會讓 `GetPath` 回 `S_FALSE` 的捷徑，
**在交接區明確寫出你試了什麼、為什麼不行**，並保留「連續兩次列舉 stable id
相同」這條（它對任何 fixture 都有意義）。

`ComGuard` 的 `S_FALSE` 平衡難以在單元測試中觀察（要能讀 COM 內部計數）。
**不要為它加測試**；正確性靠「`SUCCEEDED` 取代 `== S_OK`」這一行本身，
以及 Agent checks 的 grep。在交接區寫明未被自動化覆蓋。

## Performance

零。`= {}` 對 3 × 2 KB 的堆疊緩衝區做一次清零，發生在每個 `.lnk` 的解析中，
而該解析本身要跑一次 COM 物件建立與一次檔案讀取。共用 header 是 header-only，
無連結成本。

## How this stays maintainable

**一個平台慣例，一份實作。** 這個 bug 之所以存在兩份，正是因為第二個列舉器
複製了第一個。收成 `src/win/com.h` 之後，第三個列舉器會 include 它而不是
再複製一次；而若 MSDN 的規則哪天需要重新理解，只有一個地方要改。

**`S_FALSE` 是本 repo 的重複踩雷點。** 本 item 一次修了它的兩種形態
（COM 初始化計數、Shell getter 的「沒有值」），且兩處註解都寫出了
`SUCCEEDED(S_FALSE) == true` 這個事實。**任何新的 Shell API 呼叫都要先查
它的 `S_FALSE` 語意**，這是本 item 留下的唯一契約。

**共用 header 刻意極小。** 兩個型別、四十行、零相依（只有 `<windows.h>`、
`<objbase.h>`、`<memory>`）。它不是「COM 層」的第一步，也不該長大；
若哪天有人想往裡面加 `HRESULT` 例外、`IID` 輔助或 apartment 追蹤，
那是一個需要真實第二個用例來論證的新決定。

## Non-goals

- **把全 repo 的手動 `->Release()` 改成 RAII。** Decisions §3。
- **改 `icon_worker.cpp` 或 `main.cpp` 的 `CoInitializeEx`。** 兩者都已成對
  正確；改它們是無關 churn，且 `main.cpp` 的那次是行程層級的初始化。
- **自行解析 `.lnk` 二進位格式**以繞開 `GetPath` 的 `S_FALSE`。§FR-004 明文
  禁止。
- **在 UserFolder 列舉器加 Shell COM 解析捷徑。** 那是另一件事（見
  `user_folder_catalog.cpp:88` 的既有 `ponytail:` 註解與 NR-047 的 Non-goals）。
- **改 stable id 的組成或 §10.3 的規則。** 本 item 只讓既有規則的輸入不再是
  未定義記憶體。
- **新增 CMake target 或第三方 COM 輔助程式庫。**

## Interaction with other open items

- **NR-050** 碰 `src/icons/icon_store.cpp` 與 `icon_pack_format.cpp`；本 item
  碰 `png_codec.cpp`。同目錄、不同檔，可任意順序。
- **NR-049** 碰 `src/app_host/main.cpp`；無交集。
- 若本 item 造成少數捷徑的 stable id 改變（§2 的行為變更檢查），那是一次性的
  修正；**不需要**遷移 `favorites.txt` 或 `usage.tsv`，因為受影響的正是原本就
  不穩定的那些 id。在交接區記錄實際觀察到的數量。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §3 的新斷言存在且通過，特別是「連續兩次列舉 stable id 相同」。
3. repo 內只剩一份 `ComGuard` 與一份 `ComRelease`。
4. `ResolveShortcut` 的三個緩衝區全部零初始化且全部以 `== S_OK` 判斷。

Manual（Release build）：

1. `Alt+Space` 開面板，確認 Start Menu 與 Store App 都照常出現、數量與本 item
   前相當。
2. 連按 `Ctrl+R` 十次，用 Task Manager 或 Process Explorer 觀察
   `NimbleRun.exe` 的 handle 數與執行緒數在最後一次重建完成後**回到穩定值**，
   不隨次數單調上升（這是 COM apartment 洩漏的可觀察面）。把前後數字記進
   交接區——`docs/performance-baseline.md` 的 idle handle 一列用得上。
3. 若你在本 item 前有釘選項目：確認釘選在本 item 後**仍然有效**。若有失效，
   數清楚幾個、是哪一類捷徑，記進交接區（§2 的行為變更檢查）。
4. 若系統上有指向控制台項目或封裝 App 的 `.lnk`（`shell:Programs` 底下常見），
   確認它們仍出現在搜尋結果中且顯示名稱正常。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "start_menu_catalog|appsfolder_catalog|png_codec" --output-on-failure
```

```powershell
# 只剩一份定義：
Get-ChildItem -Recurse -Include *.cpp,*.h -Path src | Select-String -Pattern 'class ComGuard|struct ComRelease'
# expect: src/win/com.h 各一行，別無他處

# S_FALSE 被正確平衡：
Select-String -Path src/win/com.h -Pattern 'own_ = '
# expect: own_ = SUCCEEDED(hr);   （絕不可是 hr == S_OK）

# 三個緩衝區都零初始化：
Select-String -Path src/catalog/start_menu_catalog.cpp -Pattern 'wchar_t (target|arguments|working)\[1024\]'
# expect: 三行，全部帶 = {}

# Shell getter 不再把 S_FALSE 當成功：
Select-String -Path src/catalog/start_menu_catalog.cpp -Pattern 'SUCCEEDED\(shell_link->'
# expect: no match

# 未擴大改動範圍：
git diff --name-only
# expect: src/win/com.h（新增）、src/catalog/start_menu_catalog.cpp、
#         src/catalog/appsfolder_catalog.cpp、src/icons/png_codec.cpp、
#         tests/unit/start_menu_catalog_test.cpp、（可能）CMakeLists.txt
#         —— 不含 icon_worker.cpp、main.cpp、shell_icon_provider.cpp

# identity 規則未被改動：
git diff src/catalog/stable_id.h src/catalog/dedup.cpp
# expect: empty
```

## 交接區

**修改位置**：新增 `src/win/com.h`（header-only，照 `src/storage/atomic_text_file.h` 先例）；`src/catalog/start_menu_catalog.cpp`（刪逐字 `ComGuard`、`ResolveShortcut` 三緩衝區零初始化＋`== S_OK`＋`ComPtr`、`ShortcutIsWeb` 的 `item` 轉 `ComPtr`）；`src/catalog/appsfolder_catalog.cpp`（刪逐字 `ComGuard`）；`src/icons/png_codec.cpp`（刪 `ComRelease`）；`tests/unit/start_menu_catalog_test.cpp`（新增 `No Target.lnk` fixture＋斷言）。`icon_worker.cpp`、`main.cpp`、`shell_icon_provider.cpp` 未動。

**兩份 `ComGuard` 是否逐字相同**：是，逐字比對確認完全相同（`start_menu_catalog.cpp:57-75` 與 `appsfolder_catalog.cpp:22-40`，建構子旗標、`own_ = hr == S_OK`、`usable_`、解構、private 兩旗標一字不差），共用版本無需保留任何差異。

**是否需要 CMake 變更**：不需要。本 repo include 以 repo 根為基準，`#include "win/com.h"` 由 `nimblerun_catalog`／`nimblerun_icons` 既有的 `target_include_directories(src)` 覆蓋；純 header 無 target 需求。

**PIDL-only fixture 是否造得出來**：造得出來，用 item 明訂的 fallback（空字串目標）。實測：`CreateShortcut`（只會 `SetPath`）對空字串——`SetPath(L"")` 回 SUCCEEDED、`Save` 成功、reload 後 `GetPath` 回 **`S_FALSE`**（`GetArgs`/`GetWorkingDirectory`/`GetIDList` 回 S_OK），且 `ShortcutIsWeb` 為 false（項目不會被當網站丟掉）。另試「不存在路徑」→ `GetPath` 回 `S_OK`（不觸發 S_FALSE，不符合本 item 的目標），控制台 CLSID `::{26EE0668-A00A-44D7-9371-BEB064C98683}` → 也回 `S_FALSE`，但空字串最直接。測試斷言項目仍在、`search_alias` 空、連續兩次列舉 stable id 相同；`arguments` 在 `AppEntry` 無欄位，依 item「若測試可觀察到」略過。

**建置與 CTest**：Release 建置無新增警告；`ctest` **23/23 全綠**；`ctest -R "start_menu_catalog|appsfolder_catalog|png_codec"` 3/3。

**未被自動化覆蓋**：`ComGuard` 的 `S_FALSE` 計數平衡無法在單元測試觀察（要能讀 COM 內部計數），依 item 不加測試，正確性靠 `own_ = SUCCEEDED(hr)` 一行與 Agent checks 的 grep。4 條手動驗收（Alt+Space 面板、Ctrl+R 十次 handle 數不回昇、釘選存活、控制台項目捷徑顯示）為人工操作，不在 Agent 範圍。

**sanity greps**：全符合——repo 內 `class ComGuard`／`struct ComRelease` 各只命中 `src/win/com.h` 一次；`own_ = SUCCEEDED(hr);`；三緩衝區全帶 `= {}`；`SUCCEEDED(shell_link->` 零命中；`git diff --name-only` 恰為 5 個預期路徑（新增 `src/win/com.h`）；`git diff stable_id.h dedup.cpp` 為空。

**偏差**：`ComPtr` 配 `IID_PPV_ARGS` 不能用 `&ptr.get()`（clang 對 rvalue 取址編不過），採「raw 中間指標給 `IID_PPV_ARGS`，成功後包進 `ComPtr`」的既有標準寫法——這是唯一偏離 item 文字描寫的實作細節，行為不變。item §2 提到「理論上某些捷徑的 stable id 會改變」：本機開發 Start Menu 無 PIDL-only 捷徑，實測不到受影響數量，判定為修復原本不穩定的 id。

**未完成事項**：無。
