# NR-031 — Icon variant key, draw-time downscaling, derived LRU capacity

- Status: `done`
- Phase: 3
- Depends on: NR-030
- Source: `docs/design-spec.md` §FR-009、§4.2、§4.3、§9 職責表

## Goal

把圖示快取鍵從「stable ID ＋ 要求尺寸 ＋ DPI」收斂為「stable ID ＋ 標準尺寸 variant」，並讓 renderer 於繪製時降尺寸到目標矩形。同時把記憶體 LRU 上限從寫死的 64 改為由釘選數、`recent_count` 設定與一頁格數推導。

這是純值先行的 item：**不動 worker thread、不動磁碟持久化**。做完之後既有行為不變（仍是 UI thread 同步載入），只是鍵空間變小、跨 DPI 可重用、上限有依據。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§4.2／§4.3、`docs/work-items.md`、`docs/work-items/NR-012-icons.md`、`docs/work-items/NR-029-empty-state-grid.md`、`docs/work-items/NR-030-icon-cache-spec-amendment.md`、本文件。

依賴檢查：若 NR-030 未 `done`（§FR-009 仍寫「DPI key」「上限 64」），**回報阻塞**，不要自行改 Spec。

## 與既有 item 的關係（重要）

- **本 item 覆寫 NR-012 的 `IconKey`（stable_id ＋ size ＋ dpi）與 `IconCache::kDefaultMaxItems = 64`。** NR-012 的 fallback-first、失敗不進 cache、不造成格位重排等其餘決策不變。
- **不回頭修改** NR-012／NR-029 文件；覆寫指示只寫在本文件。
- `stable_id` 一字不改，既有 pin 與使用紀錄零遷移。

## 硬約束

- 核心邏輯不得依賴 HWND 或 Shell COM：variant 選擇與容量推導必須是純值運算，可在不建視窗下測試。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限。
- 不新增設定項；variant 階梯與容量公式不可由使用者調整。
- App UI 文字一律英文（本 item 不新增 UI 字串）。
- 最小可行改動：不新增抽象層、不為三個常數建 class。

## Scope

### 1. Variant 階梯（`src/icons/icon_cache.h`）

```cpp
// Standard on-disk / in-cache icon sizes in physical pixels. 48 is a native
// Windows icon resource size; 96 and 256 are the escalation tiers. 96 is not a
// native resource size (Shell derives it from 256) but keeps high-DPI machines
// from paying ~5x the bytes and decode cost for an image drawn at 60-80 px.
inline constexpr int kIconVariants[] = {48, 96, 256};

// Smallest variant >= needed_px, clamped to the largest variant. needed_px <= 0
// returns the smallest variant.
int IconVariantForPixels(int needed_px);
```

實作為一個 range-for ＋ 回傳最後一個元素，不要寫成三個 if。

### 2. `IconKey` 收斂（`src/icons/icon_cache.h`）

```cpp
// Icon request key (design-spec §FR-009): stable ID + standard-size variant.
// Neither the on-screen size nor the DPI is part of the key: the renderer
// downscales at draw time, so one entry serves the 40 DIP grid cell and the
// 30 DIP list row at every DPI within the same variant tier.
struct IconKey {
    std::wstring stable_id;
    int variant = 0;   // one of kIconVariants

    std::wstring Encode() const;  // stable_id + L'|' + variant
};
```

- 移除 `size`、`dpi` 兩個欄位與 `Encode()` 內的 DPI 四捨五入邏輯，連帶移除 `#include <cmath>`（若無其他用途）。
- `IconBitmap`、`IconProvider`、`IconCache` 的其餘介面不變。

### 3. 容量推導（`src/icons/icon_cache.h`／`.cpp`）

```cpp
// Grid page size (design-spec §4.3): one full page of cells, i.e. the working
// set of a single search result page.
inline constexpr std::size_t kIconCacheWorkingSetItems = 24;

// pinned_count + recent_count + one grid page. The first two terms are the
// prewarm set; the third stops a search from evicting the prewarmed pins and
// forcing a refetch on the next panel show.
std::size_t IconCacheCapacityFor(std::size_t pinned_count, std::size_t recent_count);
```

- `IconCache` 新增 `void SetMaxItems(std::size_t max_items);`：更新上限，若新上限小於現有筆數則從 LRU 尾端淘汰到符合；`max_items == 0` 視為 1（不允許零容量）。
- 移除 `kDefaultMaxItems = 64`；建構子預設值改為 `IconCacheCapacityFor(0, 20)`（= 44），僅作為尚未載入設定時的起始值。

### 4. Renderer 接線（`src/app_host/main.cpp`）

- `IconKeyFor(entry)` 改為 `IconKeyFor(entry, needed_px)`，內部呼叫 `IconVariantForPixels(needed_px)`。`needed_px` 由既有 `LayoutForDpi()` 算出的實體像素給（grid 用 `kIconSizeDip` 轉換後的值、清單用 `kTileSizeDip` 轉換後的值），**不要**在 render 迴圈裡自己乘 dpi/96。
- 繪製改為指定目標矩形讓 D2D 縮放：`DrawBitmap(bitmap, dest_rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR)`，`dest_rect` 為原本的 icon 幾何（grid 40 DIP、清單 30 DIP 換算後的實體像素）。bitmap 本身尺寸為 variant，兩者不再相等，這是預期行為。
- `g_requested_icon_keys`（NR-012 的失敗不重試集合）沿用，鍵字串自動跟著變短。
- 在 `ShowPanel` 既有的 pins reload／`SetPins` 之後呼叫一次 `g_icon_cache.SetMaxItems(IconCacheCapacityFor(pins.size(), settings.recent_count))`；設定 Apply 後與 pin／unpin 後也各呼叫一次（沿用既有那些點已經在做的 refresh，不新增訊息或 timer）。

### 5. 不做的接線

`LoadVisibleIcons()` 仍留在 UI thread、仍同步呼叫 Shell。NR-012 留下的 `ponytail:` 註解保留，並在其中補一行指向 NR-032。本 item 不得順手搬 thread。

## Non-goals

- 不搬到 worker thread（NR-032）。
- 不做磁碟持久化（NR-033～NR-036）。
- 不做預熱（NR-037）。
- 不改 grid／清單的圖示 DIP 尺寸、列高、格子幾何、面板尺寸。
- 不改 `stable_id`、catalog、dedup、usage、pin 的邏輯或格式。
- 不改 `ShellIconProvider` 的取得方式（仍是 `SHCreateItemFromParsingName` ＋ `GetImage(SIIGBF_ICONONLY|RESIZETOFIT)`），只是傳入的尺寸現在是 variant。
- 不新增 `PanelColors` 欄位、不改 palette。

## Acceptance

- `IconVariantForPixels`：`30`／`40`／`48` → 48；`50`／`60`／`80`／`96` → 96；`97`／`120`／`256`／`999` → 256；`0`／`-1` → 48。
- 同一個 App 在 grid（40 DIP）與清單（30 DIP）於 100% DPI 下產生**同一個** `Encode()` 鍵，只向 Shell 取得一次。
- 100% 與 125% DPI 下 grid 需要 40／50 px，分別落在 48／96 層；150% 與 200% 皆需 60／80 px，同落 96 層，即 150%→200% 的 DPI 變更**不觸發**重新取得。
- `IconKey::Encode()` 不含任何 DPI 數值；repo 內搜尋不到 `IconKey` 的 `dpi` 欄位。
- `IconCacheCapacityFor(0, 20) == 44`；`(12, 20) == 56`；`(0, 8) == 32`；單調不減。
- `SetMaxItems` 降低上限時淘汰最久未使用者，且不影響剩餘項目的相對 recency；升高上限不淘汰任何項目。
- 面板顯示時圖示外觀與 NR-029 完成時一致（40 DIP grid 內圖示置中、清單 30 DIP 垂直置中），不出現格位重排或幾何跳動。
- 96／144／192 DPI 下圖示皆填滿其目標矩形、無變形（等比例）、無裁切。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icons" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/icon_cache_test.cpp` 修改與新增（純值，不需視窗）：

- 既有 case 中所有 `Key(id, size, dpi)` helper 改為 `Key(id, variant)`；原本「size·dpi 分離 key」那個 case 改為「variant 分離 key」（同 stable ID 的 48 與 96 為兩筆）。
- `IconVariantForPixels` 的邊界表（上述 Acceptance 第一條全部值）。
- 同一 stable ID 以 30 px 與 40 px 需求求得的鍵相等；以 40 px 與 60 px 需求求得的鍵不相等。
- `IconCacheCapacityFor` 三組值與單調性。
- `SetMaxItems` 縮小到小於現有筆數時淘汰正確的鍵、`Size()` 等於新上限；`SetMaxItems(0)` 後 `Size() <= 1`。
- `SetMaxItems` 放大後既有項目全部仍可 `Peek` 到。

## 交接區

- Start: 2026-08-05。已依「必讀」讀完 AGENTS.md、docs/development.md、design-spec §FR-009／§4.2／§4.3、work-items.md、NR-012／NR-029／NR-030；確認 NR-030 為 `done`（§FR-009 已改 variant key、無「DPI key」「上限 64」殘留）。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/icons/icon_cache.{h,cpp}`、`src/icons/shell_icon_provider.{h,cpp}`、`src/app_host/main.cpp`（`IconKeyFor`、`LoadVisibleIcons`、`Render()` 的 grid 與清單兩個分支、`ShowPanel`）、`src/ui/panel_layout.h`、`tests/unit/icon_cache_test.cpp`。先確認 NR-030 已 `done`，否則回報阻塞。實作 Scope 1～4，明確不要動 Scope 5 所述的 thread 歸屬。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。修改檔案：`src/icons/icon_cache.h`（`kIconVariants[]`、`IconVariantForPixels`、`IconKey{stable_id, variant}`＋`Encode()`、`kIconCacheWorkingSetItems`、`IconCacheCapacityFor`、`IconCache::SetMaxItems`、預設上限改 `IconCacheCapacityFor(0,20)`、移除 `kDefaultMaxItems` 與 `<cmath>`）、`src/icons/icon_cache.cpp`（上述實作＋`IconKey::Encode`＝`stable_id+'|'+variant`）、`src/icons/shell_icon_provider.cpp`（`key.size`→`key.variant`）、`src/app_host/main.cpp`（`IconKeyFor(entry, needed_px)`；`LoadVisibleIcons` 與 `Render` grid/清單兩分支由 `LayoutForDpi()` 給 needed_px；`ShowPanel`／設定 Apply／pin/unpin 各呼叫一次 `SetMaxItems(IconCacheCapacityFor(...))`；`DrawDecodedIcon` 維持 `DrawBitmap(bitmap, tile, 1.0f, LINEAR)` 即符合繪製時降尺寸；`ponytail:` 註解補一行指向 NR-032）、`tests/unit/icon_cache_test.cpp`（`Key(id, variant)`、variant 分離 key、`IconVariantForPixels` 邊界表、30/40px 同鍵與 40/60px 異鍵、capacity 三值＋單調性、`SetMaxItems` 縮小／0／放大）。Agent checks：configure＋build 成功無 warning；`ctest --test-dir build -R icons --output-on-failure` 1/1 通過；全套件 19/19 通過。未完成：無（視覺縮放品質屬人工驗證；`LoadVisibleIcons` 留在 UI thread，搬移屬 NR-032）。
