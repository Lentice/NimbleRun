# NR-040 — Context menu: Properties and Remove from recent

- Status: `ready`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.8（滑鼠操作）／§FR-010（啟動層）／§FR-011（釘選與排序）／§NFR-006

## Goal

面板的項目右鍵選單目前只有「Pin／Unpin」與「Open file location」（`src/app_host/main.cpp:1906` 起）。本 item 補上兩項：

1. **Properties** —— 與檔案總管在捷徑上按右鍵／內容相同的系統對話框。
2. **Remove from recent** —— 把該項目從常用（recent）清單移除，只在該項目目前確實顯示於 recent 區時出現。

Pin／Unpin 與 Open file location **已經存在且行為正確**，本 item 一字不改。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.2／§4.3／§4.8／§FR-010／§FR-011／§10.2（持久化）、`docs/work-items.md`、本文件。姊妹 item：`docs/work-items/NR-041-pinned-marker.md`（純繪製，與本 item **無程式碼交集**，可平行進行）。

## 現況事實（已查證，不需重新推導）

- 右鍵選單全部集中在父視窗 `WM_RBUTTONDOWN`（`src/app_host/main.cpp:1906-1963`）。清單（filtered）與格狀（空查詢）**共用同一段程式碼**：`CellAtPoint()` 已處理兩種版面，`g_model->Rows()[cell]` 即命中項目。因此新選單項自動在兩種狀態都生效，不需要分支。
- 選單命令 id 常數在 `main.cpp:80-82`（`kCmdPin = 11`、`kCmdUnpin = 12`、`kCmdOpenLocation = 13`）；選單文字在 `namespace context_menu_strings`（`main.cpp:88-92`）。錯誤對話框文字在 `namespace dialog_strings`（`main.cpp:115-123`）。
- `OpenFileLocation()`（`main.cpp:651-670`）示範了本 item 要照抄的模式：先 `IsPathIdentity()` 守門 → Shell 呼叫 → 失敗時 `g_diag->Write(...)` ＋ `ShowErrorDialog(...)`。
- `nimblerun::IsPathIdentity()` 定義於 `src/catalog/stable_id.h:14`。AppsFolder／UWP 項目的 `launch_identity` 是 parsing name 不是檔案路徑，對它們 `SHOpenFolderAndSelectItems` 與 properties verb 皆不適用，故現行程式碼對它們**不顯示** Open file location；Properties 沿用同一條件。
- `g_context_menu_active`（`main.cpp:1934-1937`）只在 `TrackPopupMenu` 期間為 true，用來擋掉 `WM_KILLFOCUS`（`main.cpp:1968`）造成的面板隱藏。`ShowErrorDialog()`（`main.cpp:524-529`）自行維護 `g_dialog_active`。
- `UsageStore`（`src/usage/usage_store.h`）目前只有 `Clear()`（全清），**沒有**單筆移除。`Recent(cap)` 回傳依 last-launch 由新到舊排序的紀錄。
- 主機端 recent 的重建路徑已存在：`RefreshPanelSnapshot()`（`main.cpp:738-756`）以 `g_usage->Recent(g_settings.recent_count)` 對照 catalog snapshot 解析成 `AppEntry`，再 `g_model->SetRecent(...)`，最後 `RefreshPins()`。
- `PanelModel::RefreshRows()`（`src/app_host/panel_model.cpp:41-71`）在空查詢時先塞 pinned（可在 catalog 中解析到的）再塞 recent（跳過已 pin 者）；查詢非空時 `rows_ = SearchApps(...)`，**沒有分區概念**。因此 rows 目前無法分辨某一列屬於 pinned 還是 recent 區。
- `UsageStore` 的既有測試：`tests/unit/recent_usage_test.cpp`（CTest 目標 `nimblerun_recent_usage_test`，`tests/CMakeLists.txt:226-249`）。
- `PanelModel` 的既有測試：`tests/unit/panel_model_test.cpp`（CTest 目標名為 `nimblerun_list_vertical_slice_test`，`tests/CMakeLists.txt:251-274`）。

## 使用者已確認的決策（不要重新設計）

1. **Properties 與 Open file location 同守門條件。** `IsPathIdentity` 為 false 時兩項都不顯示，不要為 UWP 另尋替代方案（例如開設定頁的 App 資訊），那是另一個功能。
2. **Remove from recent 只在該 row 目前位於 recent 區時出現。** 曾考慮「只要有 usage 紀錄就顯示」，已否決：pinned 格上按下去畫面毫無變化，看起來像壞掉。同理，filtered 結果（無 recent 區）也不顯示此項。
3. **選了 Properties／Open file location 之後不特別留住面板。** 系統對話框取得前景後面板自然因 `WM_KILLFOCUS` 隱藏，這與「啟動 App 後隱藏」一致（§4.8「點擊面板外，面板自動隱藏」）。**不新增第三個焦點例外旗標。**
4. **不做確認對話框。** Remove from recent 只是丟掉一筆使用統計，不是刪檔；再啟動一次就回來了。
5. **不做「清空全部 recent」的選單項。** `UsageStore::Clear()` 已存在且屬設定頁範疇，本 item 不碰。

## 硬約束

- 只改這些檔案：`src/usage/usage_store.h`、`src/usage/usage_store.cpp`、`src/app_host/panel_model.h`、`src/app_host/panel_model.cpp`、`src/app_host/main.cpp`、`tests/unit/recent_usage_test.cpp`。**不新增原始檔、不新增模組、不改 `tests/CMakeLists.txt`**（新測試加進既有測試檔）。
- `UsageStore` 與 `PanelModel` 必須維持純值：不得引入 `windows.h`、HWND、Shell COM（AGENTS.md「核心邏輯與 HWND／Shell COM 解耦」）。
- 不新增設定項、不新增 timer、不新增執行緒、不新增輪詢。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限。
- 持久化沿用 `UsageStore::Save()` 既有的 tmp＋flush＋atomic replace（§10.2）；**不得**新增第二條寫入路徑、不得原地覆寫。
- App UI 文字一律英文；新字串必須加進既有的 `context_menu_strings` / `dialog_strings` 集中區（§NFR-006），不得散落在呼叫點。
- 不改 `docs/design-spec.md`。§4.8 只列舉了釘選與開啟檔案位置，本 item 新增的兩項屬同一互動的自然延伸，不與任何「不做」條款衝突（§104 的 out-of-scope 講的是檔案搜尋）。
- 不動 icon lane（NR-030～NR-037）的任何檔案，不動 NR-039 的 `WM_LBUTTONDOWN`／`SearchEditProc`。

## Scope

### 1. `UsageStore::Forget`（`src/usage/usage_store.h` / `.cpp`）

在 `Clear()` 之後新增：

```cpp
    // Drops a single app's usage record (NR-040 "Remove from recent"). Returns
    // false when there is no record for stable_id, in which case nothing
    // changed and the caller should not Save(). Persistence is the caller's
    // job, exactly as it is for RecordLaunch().
    bool Forget(std::wstring_view stable_id);
```

- 實作：在 `records_` 找相符的 `stable_id`，找到就 erase 並回傳 `true`，否則回傳 `false`。空字串一律回傳 `false`。
- **不要**在 `Forget` 內呼叫 `Save()`——`RecordLaunch` 也沒有，呼叫端負責，兩者保持一致。
- 需要 `#include <string_view>`。

### 2. `PanelModel::RecentStartIndex`（`src/app_host/panel_model.h` / `.cpp`）

在 `Rows()` 附近新增：

```cpp
    // NR-040: index of the first recent-region row in Rows(), or -1 when there
    // is no recent region (a non-empty query produces search results, which
    // belong to neither region). Rows before this index are the pinned region.
    // Equals Rows().size() when the empty-query view happens to be all pins.
    int RecentStartIndex() const { return recent_start_; }
```

- 私有成員 `int recent_start_ = -1;`。
- 在 `RefreshRows()` 內維護：空查詢分支中，pinned 迴圈跑完後、recent 迴圈開始前設 `recent_start_ = static_cast<int>(rows_.size());`；非空查詢的兩個分支設 `recent_start_ = -1;`。**只加這兩處賦值，不改任何既有邏輯。**

### 3. 主機端接線（`src/app_host/main.cpp`）

**新常數**（接在 `main.cpp:82` 之後）：

```cpp
constexpr UINT kCmdProperties = 14;
constexpr UINT kCmdForgetRecent = 15;
```

**新字串**（`context_menu_strings`，`main.cpp:88-92` 內）：

```cpp
constexpr wchar_t kProperties[] = L"Properties";
constexpr wchar_t kRemoveFromRecent[] = L"Remove from recent";
```

**新字串**（`dialog_strings`，`main.cpp:115-123` 內）：

```cpp
constexpr wchar_t kPropertiesFailed[] = L"Failed to open properties.";
```

**新函式**（緊接在 `OpenFileLocation()` 之後，`main.cpp:670` 後）：

```cpp
// NR-040: the Shell's own properties dialog, the same one Explorer shows for
// the shortcut/exe. Only filesystem paths qualify -- AppsFolder parsing names
// have no properties sheet -- so the caller gates on IsPathIdentity() exactly
// as it does for OpenFileLocation().
void ShowItemProperties(HWND window, const nimblerun::AppEntry& entry) {
    if (!nimblerun::IsPathIdentity(entry.launch_identity)) {
        return;
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // SEE_MASK_INVOKEIDLIST is required for the "properties" verb: the Shell
    // has to build the item's context menu to find it. SEE_MASK_NOASYNC keeps
    // the call valid without needing the process to outlive an async handoff.
    info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOASYNC;
    info.hwnd = window;
    info.lpVerb = L"properties";
    info.lpFile = entry.launch_identity.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (g_diag) {
            g_diag->Write(L"properties", L"error " + std::to_wstring(error));
        }
        ShowErrorDialog(window, dialog_strings::kPropertiesFailed);
    }
}
```

**選單建構**（`main.cpp:1919-1929`，在既有 Pin/Unpin 之後、Open file location 的分隔線之前插入 Remove from recent；Properties 接在 Open file location 之後）：

```cpp
        AppendMenuW(menu, MF_STRING, pinned ? kCmdUnpin : kCmdPin,
                    pinned ? context_menu_strings::kUnpin : context_menu_strings::kPin);
        // NR-040: only offered for rows actually showing in the recent region;
        // on a pinned row the command would silently change nothing.
        const int recent_start = g_model->RecentStartIndex();
        const bool in_recent = recent_start >= 0 && cell >= recent_start;
        if (in_recent) {
            AppendMenuW(menu, MF_STRING, kCmdForgetRecent,
                        context_menu_strings::kRemoveFromRecent);
        }
        if (nimblerun::IsPathIdentity(entry.launch_identity)) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kCmdOpenLocation,
                        context_menu_strings::kOpenFileLocation);
            AppendMenuW(menu, MF_STRING, kCmdProperties,
                        context_menu_strings::kProperties);
        }
```

**命令分派**（`main.cpp:1941-1961` 的 if/else 鏈尾端加兩個分支）：

```cpp
        } else if (command == kCmdOpenLocation) {
            OpenFileLocation(window, entry);
        } else if (command == kCmdProperties) {
            ShowItemProperties(window, entry);
        } else if (command == kCmdForgetRecent) {
            // NR-040: drop one usage record, persist, then rebuild the recent
            // rows through the single existing path. Save() failing leaves the
            // previous file untouched, so the view is not refreshed either.
            if (g_usage && g_usage->Forget(entry.stable_id) && g_usage->Save()) {
                RefreshPanelSnapshot();
                InvalidateRect(window, nullptr, FALSE);
            }
        }
```

- `RefreshPanelSnapshot()` 定義在 `main.cpp:738`，位於 `WndProc` 之前，可直接呼叫。
- `entry` 是 `WM_RBUTTONDOWN` 開頭就複製出來的**值**（`main.cpp:1916`），`RefreshPanelSnapshot()` 重建 rows 不會使它失效——不要改成參考。

### 4. 不做的接線

不碰 `WM_LBUTTONDOWN`／`SearchEditProc`／`WM_MOUSEMOVE` hover／`ShowPanel`／`HidePanel`／`RefreshPins`／icon worker／`panel_layout.h`／`panel_palette.*`／設定對話框／`UsageStore::Clear()`。

## Non-goals

- 不為 UWP／AppsFolder 項目提供任何形式的「內容」替代品。
- 不新增確認對話框、不新增復原（undo）、不新增「清空全部 recent」。
- 不改 recent 的排序、上限（`recent_count`）或 `Recent()` 的語意。
- 不新增拖曳排序、不動 §FR-011 的 pin 順序調整（那仍未實作，屬別的 item）。
- 不改 design-spec、不新增設定項、不新增診斷事件以外的 log。
- 不為 `ShowItemProperties` 寫自動化測試（見 Acceptance 的理由）。

## Acceptance

自動部分：

- 修改檔案僅限「硬約束」列出的六個；無新增原始檔、無 `tests/CMakeLists.txt` 改動。
- `grep` 確認 `src/usage/usage_store.cpp` 與 `src/app_host/panel_model.cpp` 內無 `windows.h`、`HWND`、`ShellExecute`、`InvalidateRect`。
- Release 建置無新增警告；全套件 CTest 全綠。
- `ctest -R "recent_usage|list_vertical"` 通過，且包含本 item 新增的案例。

新增測試（加進既有測試檔，不新增 CTest 目標）：

- `tests/unit/recent_usage_test.cpp`
  1. `Forget` 既有 id → 回傳 `true`，`Recent()` 不再含該 id，其餘紀錄順序與內容不變。
  2. `Forget` 不存在的 id → 回傳 `false`，`Recent()` 完全不變。
  3. `Forget` 空字串 → 回傳 `false`。
  4. `Forget` ＋ `Save()` 後以新的 `UsageStore` 重載同一目錄 → 該 id 確實消失，其他紀錄與 `total_launches`／`last_launch_utc` 完好。
  5. `Forget` 後對同一 id `RecordLaunch` → 該 id 以 `total_launches == 1` 重新出現（確認移除的是紀錄本身，不是留下墓碑）。
- `tests/unit/panel_model_test.cpp`
  6. 3 個 pin ＋ 5 個 recent（無重疊）→ `RecentStartIndex() == 3`，且 `Rows()[3]` 起確為 recent 項。
  7. 全部 recent 項皆已 pin → `RecentStartIndex() == static_cast<int>(Rows().size())`。
  8. 非空查詢 → `RecentStartIndex() == -1`。
  9. 無 pin → `RecentStartIndex() == 0`。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **filtered 結果的 Properties**：搜尋 `notepad`，在結果列上按右鍵 → 選單含 Pin、Open file location、Properties，**不含** Remove from recent；選 Properties → 出現與檔案總管相同的內容對話框（分頁齊全）。
2. **grid 的 Properties**：清空查詢，在任一格上按右鍵 → 選 Properties → 同上。
3. **Remove from recent 生效**：先啟動兩個不同的 App 讓它們進入 recent 區，清空查詢，在其中一格按右鍵 → 選 Remove from recent → 該格立刻消失、後面的項目遞補、面板不閃爍。按 Esc 隱藏再顯示 → 它仍不在 recent 中。
4. **pinned 格無此項**：把某個 recent App 釘選，在它（現在位於前段 pinned 區）的格子上按右鍵 → 選單**不含** Remove from recent。
5. **UWP 項目**：搜尋一個 Microsoft Store App（例如 `Calculator` 或 `Photos`，其 identity 非檔案路徑），按右鍵 → 選單只有 Pin／Unpin，**不含** Open file location 與 Properties。
6. **Pin/Unpin 未回歸**：任一項目 Pin → 移到空查詢 grid 前段；再 Unpin → 回到原處。面板全程留著不隱藏。
7. **面板隱藏行為**：選 Properties 或 Open file location 後，面板隨系統視窗取得前景而隱藏（預期行為，不是 bug）。

`ShowItemProperties` 不寫自動化測試：它是三行 `SHELLEXECUTEINFOW` 填值加一次 Shell 呼叫，抽出來只能測到自己剛填的欄位，真正會壞的地方（verb 名稱、`SEE_MASK_INVOKEIDLIST` 是否給對）只有真的叫出對話框才看得出來，故改由手動驗收 1／2 覆蓋。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的七條手動步驟。**不要**只憑編譯成功與單元測試綠燈就回報完成——Properties 與選單顯示條件只有手動步驟能驗證。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。

## 交接區

（實作者填寫：修改的行號、建置與 CTest 結果、新測試案例、七條手動驗收逐條實測結果、未完成事項。）
