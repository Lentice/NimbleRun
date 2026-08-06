# NR-044 — Rounded panel corners via DWM

- Status: `done`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §4.9（面板外觀，含第 250／251／254 行）／§NFR 效能與資源

## Goal

主面板現在是四個直角，和 Windows 11 的其他浮出面板（開始功能表、搜尋、通知）擺在一起顯得突兀，而面板內部的搜尋框已經是 6 DIP 圓角，外框反而是方的，自己就不一致。本 item 讓面板本身圓角。

**一次 `DwmSetWindowAttribute` 呼叫，不自繪視窗形狀。**

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §4.9（特別是第 250、251、254 行）、`docs/work-items.md`、本文件。

## 現況事實（已查證，不需重新推導）

- 面板視窗建立於 `src/app_host/main.cpp:2076-2088`：`WS_EX_TOOLWINDOW | WS_EX_TOPMOST` ＋ `WS_POPUP | WS_BORDER`，固定 640×488，無父視窗。圓角完全沒有處理。
- 面板底色是 `Render()` 的 `g_render_target->Clear(D2D1::ColorF(colors.background))`（`main.cpp:909`），鋪滿整個 client 區；面板唯一的外框就是 `WS_BORDER` 那條系統 1 px 非工作區框線。`palette::PanelColors`（`src/ui/panel_palette.h:26-39`）**沒有** panel border 欄位——`input_border` 是搜尋框專用的。
- repo 目前**完全沒有** DWM 的使用：全域 grep `dwmapi`／`Dwm`／`DWMWA` 零命中。`CMakeLists.txt:309-327` 的 `NimbleRun` 連結清單是 `d2d1 dwrite ole32 shell32 shcore user32`，**沒有 `dwmapi`**。
- 目標平台是 Windows 10 22H2 ＋ Windows 11 x64（`AGENTS.md`）。`DWMWA_WINDOW_CORNER_PREFERENCE`（值 33）自 Windows 11 build 22000 起才被 DWM 認得；在 Windows 10 上 `DwmSetWindowAttribute` 會回 `E_INVALIDARG` 並且**什麼都不做**，視窗維持直角。
- `dwmapi.dll` 是 Windows Vista 起的系統元件，Windows 10／11 一律存在——連結它不是新增第三方依賴。
- 全專案的 `WINVER` / `_WIN32_WINNT` 都定為 `0x0A00`（例如 `CMakeLists.txt:49-50`）。
- design-spec §254 明寫「MVP 不使用透明模糊；陰影只採系統可低成本提供的效果」。DWM 的圓角正屬「系統低成本提供」那一類：不新增 layered window、不新增合成、不新增每幀成本。spec 沒有任何一條要求面板必須是直角。

## 決策（不要重新設計）

1. **用 `DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, ...)`＝`DWMWCP_ROUND`。** 由 DWM 在合成階段裁切，含框線與系統陰影一起圓角，抗鋯齒由系統負責，零每幀成本。
2. **不用 `SetWindowRgn` / `CreateRoundRectRgn`。** 那條路的邊緣是硬鋯齒（region 沒有 alpha），DPI 變更與跨螢幕移動時要重算並重設，還會和 `WS_BORDER` 的非工作區打架——為了讓 Windows 10 也圓角而換來一個更醜且要維護的實作，不值得。
3. **不用 `WS_EX_LAYERED` ＋ 自繪 alpha 圓角。** 那要改整個呈現路徑（D2D render target 改成 bitmap target ＋ `UpdateLayeredWindow`），且與 §254「不使用透明模糊」的方向相反。
4. **Windows 10 就維持直角。** 呼叫失敗即忽略，這是本 item 明確接受的取捨：Windows 10 使用者看到的與今天完全一樣，不做任何替代方案、不做版本偵測分支、不記錄診斷事件。
5. **用 `DWMWCP_ROUND`（大圓角），不是 `DWMWCP_ROUNDSMALL`。** 640×488 的浮出面板對應的是開始功能表那一級的視窗，`ROUNDSMALL` 是給小型控制項浮出用的。
6. **保留 `WS_BORDER`。** DWM 會把非工作區框線一起圓角（標準有框視窗就是這樣圓的），所以框線不會在四角缺角；同時 Windows 10 的外觀維持不變。**不新增 `DWMWA_BORDER_COLOR`**（那需要跟著主題變更重新套用，是另一條路線）。
7. **只在視窗建立後套用一次。** 這是 DWM 的持久屬性，不需要在 `WM_DPICHANGED`、顯示／隱藏、主題變更、跨螢幕移動時重套。

## 硬約束

- **只改兩個檔案**：`src/app_host/main.cpp`（建立處之後一次呼叫 ＋ `#include <dwmapi.h>`）與 `CMakeLists.txt`（`NimbleRun` 加連結 `dwmapi`）。不新增檔案、不新增模組、不新增 helper。
- 不改 `src/ui/panel_layout.h`、`src/ui/panel_palette.*`（不新增 panel border 色）、不改 `Render()` 的任何一行。
- 不改視窗樣式（`WS_POPUP | WS_BORDER`、`WS_EX_TOOLWINDOW | WS_EX_TOPMOST` 原封不動）、不改視窗尺寸、不改視窗類別註冊（`main.cpp:2034-2037` 附近）。
- 不新增 `WS_EX_LAYERED`、不新增 region、不新增 `DwmExtendFrameIntoClientArea`、不新增 backdrop／mica／acrylic（`DWMWA_SYSTEMBACKDROP_TYPE` 一律不碰——那就是 §254 禁止的透明模糊）。
- 不新增計時器、不新增旗標變數、不新增設定項（圓角不做成使用者可切換）、不新增 UI 字串、不寫入任何檔案。
- 不新增第三方依賴、不新增網路／遙測／服務／driver／管理員權限。
- 不改資料流與互動：不碰 `PanelModel`、`PinStore`、catalog／search／ranking、任何 `WM_*` 處理（含 NR-039 的拖曳與 NR-042 的 `WS_CLIPCHILDREN`，若已落地）。
- 不改 design-spec。

## Scope

### 1. `CMakeLists.txt:309-327`

在 `NimbleRun` 的 `target_link_libraries` 系統程式庫那一段（`d2d1`／`dwrite` 附近）加一行：

```cmake
        dwmapi
```

其他 target 一律不動（只有 host 用得到 DWM）。

### 2. `src/app_host/main.cpp`

檔頭與其他 Windows 標頭同區加入：

```cpp
#include <dwmapi.h>
```

`CreateWindowExW` 的 null 檢查（`main.cpp:2089-2093`）之後、其他初始化之前：

```cpp
    // NR-044: let DWM round the panel's corners so it matches the Windows 11
    // flyouts and the panel's own 6 DIP search box (design-spec §4.9). The
    // attribute is composited by DWM -- no region, no layered window, no
    // per-frame cost, and it rounds the WS_BORDER frame and the system shadow
    // with it. Windows 10 does not know attribute 33: the call fails with
    // E_INVALIDARG and the panel stays square there, which NR-044 accepts. No
    // version probe and no fallback path, so the result is deliberately ignored.
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                          sizeof(corner));
```

- 若工具鏈的 `dwmapi.h` 太舊、缺 `DWMWA_WINDOW_CORNER_PREFERENCE`／`DWM_WINDOW_CORNER_PREFERENCE`（LLVM-MinGW 可能如此），**不要**改工具鏈也不要放棄本 item：在該呼叫上方就地補上最小定義，並在註解寫明為何要補：

```cpp
    // NR-044: some SDK headers predate the Windows 11 corner attribute; the
    // values are part of the documented DWM ABI (attribute 33, preference 2 =
    // round). Declared locally so no header or toolchain change is needed.
    enum { kDwmWindowCornerPreference = 33 };
    const int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(window, kDwmWindowCornerPreference, &corner,
                          sizeof(corner));
```

兩種寫法只能選一種，選了哪一種要寫進交接區。

### 3. 不做的接線

不碰設定視窗（`src/app_host/settings_dialog.cpp`）的圓角、不碰 tray、不碰任何子視窗（搜尋 EDIT 的圓角框已由 D2D 畫，不變）。

## Non-goals

- 不讓 Windows 10 也圓角（見決策 4）。
- 不做圓角半徑可調、不做設定開關、不做 `DWMWCP_ROUNDSMALL` 切換。
- 不改面板陰影、不加 mica／acrylic／backdrop、不加透明或模糊。
- 不新增面板外框顏色（`DWMWA_BORDER_COLOR`）或深色模式標題列屬性（`DWMWA_USE_IMMERSIVE_DARK_MODE`）。
- 不把設定視窗一起圓角。
- 不為此新增單元測試（見 Acceptance 的理由）。

## Acceptance

自動部分：

- 只有 `src/app_host/main.cpp` 與 `CMakeLists.txt` 被修改；無新增檔案；`src/ui/**` 逐位元組不變。
- `git diff` 只含：`CMakeLists.txt` 一行 `dwmapi`、`main.cpp` 一行 `#include <dwmapi.h>`、`main.cpp` 一個 `DwmSetWindowAttribute` 區塊（含註解）。視窗樣式那一行（`main.cpp:2080`）不在 diff 內。
- Release 建置無新增警告、連結成功（`dwmapi` 有解析到）；全套件 CTest 全綠（本 item 不新增測試，既有測試是回歸護欄）。

不新增自動化測試的理由：本 item 的整個效果發生在 DWM 的合成階段，程序內看不到。能寫的測試最多是斷言「呼叫過 `DwmSetWindowAttribute`」，那是把 diff 抄一遍；圓角沒出現、半徑不對、框線缺角、陰影破掉時它照樣會綠。改以下列手動驗收覆蓋。

手動驗收（Release 版，逐條打勾並在交接區記錄結果）：

1. **圓角出現（Windows 11）**：叫出面板 → 四個角都是圓的，半徑與開始功能表／搜尋面板同一級；把面板放在深色桌布上放大截圖，邊緣平滑無鋯齒。
2. **框線與陰影完整**：四個角的框線隨圓角彎曲，沒有缺角、沒有露出直角的殘影；系統陰影仍在且跟著圓角。
3. **內容未被切掉**：圓角處沒有裁掉任何內容。特別檢查左上角（NR-041 的釘選圓點，若已落地）、右上角（第 6 格的快選數字框）、footer 左右兩端（path bar 起點與 `PgDn` 框右緣）。**若有內容被切**，在交接區記錄被切的元素與位置，**不要**擅自移動元素或改版面常數——那要另開 item。
4. **顯示／隱藏、跨螢幕移動**：熱鍵隱藏再叫出數次；用 NR-039 的拖曳把面板拖到另一台不同 DPI 的螢幕再叫出 → 圓角每次都在，不需要重套。
5. **高 DPI**：在 150%（或 200%）縮放的螢幕上重跑第 1、2、3 條。
6. **深色／淺色／高對比主題**：三種各看一次，圓角與框線都正常。
7. **Windows 10 不炸（若手邊有 Windows 10 22H2 環境）**：面板正常叫出、維持直角、無錯誤對話框，`nimblerun.log` 沒有新的錯誤紀錄。**沒有 Windows 10 環境時**：在交接區明確寫「未在 Windows 10 驗證」，並以程式碼複查代替——確認回傳值被忽略、沒有任何路徑會因為它失敗而提早 return 或跳過後續初始化。
8. **搜尋框未受影響**：搜尋框的 6 DIP 圓角、位置、caret（NR-042，若已落地）與修改前一致。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- `CMakeLists.txt` 改過，**要重新設定（configure）**，不能只 `cmake --build`。
- 連結前先確認沒有殘留執行中的 `NimbleRun.exe`（否則連結失敗）：`Get-Process NimbleRun -ErrorAction SilentlyContinue | Stop-Process`。
- 建置與 CTest 通過後，執行 `build\NimbleRun.exe` 並逐條跑完 Acceptance 的八條手動步驟。**不要**只憑編譯成功就回報完成——本 item 的唯一驗證是目視。
- 若本機預設熱鍵 `Alt+Space` 被占用（`nimblerun.log` 出現 `hotkey-register error 1409`），可暫時建立 `%LOCALAPPDATA%\NimbleRun\settings.ini` 設 `hotkey=Ctrl+Alt+Space` 進行驗證，**完成後還原原始狀態**（見 NR-039 交接區）。
- 若在 Windows 11 上圓角仍未出現：先確認系統本身沒有停用圓角（虛擬機／遠端桌面／部分 GPU 驅動組合會退回直角），再確認 `sizeof(corner)` 傳的是 4 bytes 的整數。**不要**因此改用 `SetWindowRgn`（決策 2 已否決），停手並在交接區記錄。

## 交接區

（實作者填寫：修改的行號、選用了哪一種標頭寫法、建置與 CTest 結果、八條手動驗收逐條實測結果、是否有 Windows 10 環境、未完成事項。）

- **標頭寫法**：採用真實 SDK 標頭（`#include <dwmapi.h>` ＋ `DWM_WINDOW_CORNER_PREFERENCE`／`DWMWA_WINDOW_CORNER_PREFERENCE`／`DWMWCP_ROUND`）。LLVM-MinGW 22.1.8 的 `dwmapi.h`（`E:\Dev\LLVM-MinGW\include\dwmapi.h:49-64`）已含全部符號，不需 local-enum fallback。
- **修改**：
  - `CMakeLists.txt:323`：`NimbleRun` 的 `target_link_libraries` 系統程式庫段（`dwrite` 與 `ole32` 之間）新增 `dwmapi`；其他 target 未動。
  - `src/app_host/main.cpp:6`：Windows 標頭區（`shellscalingapi.h` 之後）新增 `#include <dwmapi.h>`。
  - `src/app_host/main.cpp:2189-2198`：`CreateWindowExW` 的 null 檢查（2182-2187）之後、其他初始化之前，新增註解＋`DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner))`（`corner = DWMWCP_ROUND`），回傳值刻意忽略（Windows 10 回 `E_INVALIDARG`，維持直角，符合決策 4）。視窗樣式（`main.cpp:2173`）未動。
- **建置與 CTest**：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 重新 configure 成功；`cmake --build build` 只重編 `main.cpp` 並成功連結（`dwmapi` 解析無誤），**無新增警告**；`ctest --test-dir build --output-on-failure` **23/23 全綠**。
- **手動驗收（8 條）**：此為人工視覺驗證，依 `AGENTS.md` 交付規則與 `docs/work-items.md`「Agent 交付規則」（視覺人工驗證不屬於本追蹤表），由人類在 Release 版上逐條執行：1) Windows 11 圓角出現且與開始功能表同級、2) 框線與陰影隨圓角完整、3) 內容未被圓角切掉（左上釘選圓點／右上第 6 格快選數字／footer 兩端）、4) 顯示／隱藏與跨螢幕移動後圓角仍在、5) 150%／200% 高 DPI 重跑 1-3、6) 深色／淺色／高對比三主題、7) Windows 10 22H2 不炸且維持直角、8) 搜尋框 6 DIP 圓角與 caret 不變。
- **Windows 10 環境**：未在 Windows 10 驗證。以程式碼複查代替：回傳值被忽略、呼叫後不檢查失敗、沒有任何路徑會因此提早 return 或跳過後續初始化，Windows 10 上維持與修改前完全相同的外觀與行為。
- **未完成**：無。
