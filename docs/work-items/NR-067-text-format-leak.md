# NR-067 — Device-resource recreation must not leak the five IDWriteTextFormat objects

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §NFR-002（資源）／NR-015（device resources 生命週期）
- Origin: 2026-08-07 第三次全 repo 稽核（main.cpp `CreateDeviceResources`／`DiscardDeviceResources`）

## Why

`CreateDeviceResources`（`src/app_host/main.cpp:397-536`）的進入守衛要求
`g_render_target && ... && g_title_format && g_text_format && g_small_format &&
g_grid_name_format && g_key_format` 全非空才早退（`:398-404`）。但
`DiscardDeviceResources`（`:321-331`）只釋放 render target 與九支 brush，
**不釋放五支 text format**——而 text formats 是 device-independent，本就不該釋放。

於是每次「render target 重建」路徑（兩處觸發）：

1. `:1329` — Render() 偵測主題／高對比變更 → `DiscardDeviceResources()`；
2. `:1770` 一帶 — `EndDraw` 回傳 `D2DERR_RECREATE_TARGET`（裝置遺失）。

守衛因 `g_render_target == nullptr` 失敗而往下走，`:456-477` 的五個
`CreateTextFormat` **無條件執行**，把新指標直接寫進仍非空的 `g_title_format` 等
全域——舊的 IDWriteTextFormat 從此失去參考，直到訊息迴圈結束後 `:2926-2930`
才統一 Release 一次。**每次主題切換／裝置遺失固定洩漏 5 個 COM 物件。**

對照同函式內其他資源：factory（`:409` `if (!g_d2d_factory)`）、dash style
（`:419` `!g_dash_style`）、ellipsis sign（`:487` `!g_ellipsis_sign`）都有 null
守衛，唯獨五支 format 沒有——這是明顯的疏漏。部分失敗重試路徑（`:478-481` 任一
format 失敗 → `return false` → 下一個 `WM_PAINT` 重進）也靠同一守衛修掉
（重試時已成功的 format 不會被覆寫）。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **五個 `CreateTextFormat` 各加 `if (!g_*_format)` 守衛**。text formats 是
   device-independent（與 factory 同層），本就不需要隨 render target 重建；
   守衛讓它們成為「建立一次、存活到程式結束」，與 `g_dash_style`／`g_ellipsis_sign`
   同形。
2. **不改 `DiscardDeviceResources`**：不釋放 formats。釋放它們會讓下一幀重建
   所有 format（浪費），而且釋放後 `Render()` 的其他路徑可能先於重建使用——
   現況的「存活到結束」就是正確生命週期，問題只在「被覆寫」。
3. **不改 `WM_DESTROY` 的釋放順序**（`:2926-2930`）：所有資源在 render target
   釋放後統一 Release 是既有正確順序，本 item 只是讓中間不再有「換手」。
4. **不加單元測試**：D2D/DWrite 生命週期無法在既有單元測試中驅動（需要
   HWND render target 與真實裝置遺失）。行為由 sanity greps＋程式碼複查覆蓋，
   並在交接區載明（NR-050 的「OS 失敗路徑不加注入 seam」先例）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Keep changes scoped to the requested task.

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/app_host/main.cpp:397-407` — 進入守衛與 `DiscardDeviceResources` 呼叫點。
- `src/app_host/main.cpp:456-481` — 五個 `CreateTextFormat`。**本 item 只改這裡。**
- `src/app_host/main.cpp:321-331` — `DiscardDeviceResources`。**只讀不改。**
- `src/app_host/main.cpp:2920-2935` — `WM_DESTROY` 的資源釋放。**只讀不改。**
- `src/app_host/main.cpp:1327-1336`（主題變更）與 `:1760-1775`（`D2DERR_RECREATE_TARGET`）—
  兩個觸發路徑。**只讀不改。**

## Scope

### 1. 五支 format 的 null 守衛

`g_title_format`／`g_text_format`／`g_small_format`／`g_grid_name_format`／
`g_key_format` 的 `CreateTextFormat` 各包 `if (!...)`。形狀照既有
`g_dash_style`（`:419-425`）：

```cpp
if (!g_title_format) {
    const HRESULT title = g_write_factory->CreateTextFormat(...);
    if (FAILED(title)) { return false; }
}
```

（以現場程式碼為準；保持「任一失敗 → `return false`，下個 WM_PAINT 重試」的
既有行為——守衛只擋「已存在就覆寫」，不擋失敗重試。）

### 2. 更新 spec？

不需。§NFR-002 資源描述是行為層級，本 item 是資源生命週期內的修補。

## How this stays maintainable

**五支 format 與 factory／dash style／ellipsis sign 共用同一種「建立一次、
存活到結束」的生命週期，守衛形狀與鄰近程式碼完全一致。** 日後新增第六支 format，
照抄守衛形狀即可；不會再有人誤以為 formats 屬於 device resources 而重複建立。

## Non-goals

- **不釋放 text formats**（Decisions §2）。
- **不改 `DiscardDeviceResources`／`WM_DESTROY` 的釋放清單或順序。**
- **不把五支 format 換成陣列或 loop**（它們建立參數各不相同，陣列化是為了
  節省五行程式碼而犧牲可讀性）。
- **不處理 `g_brush_colors` 相關的既有行為**（`Render` 的調色比較與本 item 無關）。

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項，本 item 不新增測試）。

Manual：

2. 反覆切換深／淺色主題（或 Windows 高對比開關）10 次以上，以工作管理員或
   `Task Manager → Details` 觀察 `NimbleRun.exe` 的 GDI 物件數不隨主題切換增長
   （IDWriteTextFormat 是 COM 物件，工作管理員看不到；此項為近似觀察，真正
   的證明是第 3 項的程式碼複查）。
3. 程式碼複查：五支 format 的 `CreateTextFormat` 都在 `if (!...)` 內，全 repo
   `g_*_format` 的賦值點各只有一處。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 五支 format 各只有一個 CreateTextFormat 呼叫點，且都在守衛內：
Select-String -Path src/app_host/main.cpp -Pattern 'CreateTextFormat'
# expect: 5 處，每處上一行都是 if (!g_..._format)

# DiscardDeviceResources 釋放清單沒有新增 format：
Select-String -Path src/app_host/main.cpp -Pattern 'Release\(g_.*format'
# expect: 0 處（formats 不在 DiscardDeviceResources 內釋放）

# 改動範圍：
git diff --name-only
# expect: 僅 src/app_host/main.cpp
```

## 交接區

（實作者填寫：修改的位置、守衛形狀、建置與 CTest 結果、sanity greps、偏差、
未完成事項。）
