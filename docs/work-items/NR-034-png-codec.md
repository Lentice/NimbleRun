# NR-034 — WIC PNG codec for IconBitmap

- Status: `done`
- Phase: 3
- Depends on: NR-030
- Source: `docs/design-spec.md` §FR-009、§10.2、§9 職責表

## Goal

在 `IconBitmap`（32bpp premultiplied BGRA 純值）與 PNG 位元組之間互轉，供 `icons.cache` 的 payload 使用。用 Windows 內建的 WIC（`windowscodecs`），不引入第三方影像庫。

存 PNG 而非 raw BGRA 的理由：96×96 raw 為 36 KiB，PNG 約 3–8 KiB，差約 5 倍；解碼成本落在背景 worker，不擋 UI。

## 必讀

`AGENTS.md`（含 Work item authoring rules）、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§10.2／§9 職責表、`docs/work-items.md`、`docs/work-items/NR-030-icon-cache-spec-amendment.md`、`src/icons/icon_cache.h`（`IconBitmap` 定義）、`src/icons/shell_icon_provider.cpp`（既有 HBITMAP→BGRA、alpha 補齊與 premultiply 的寫法）、本文件。

依賴檢查：若 NR-030 未 `done`，**回報阻塞**。

## 硬約束

- 只用 WIC（`IWICImagingFactory`、`IWICStream`、`IWICBitmapEncoder`／`Decoder`、`IWICFormatConverter`）。不加第三方庫、不加 GDI+。
- COM 介面指標**不得**外洩到回傳值或參數；本模組對外只交換 `IconBitmap` 與 `std::vector<std::uint8_t>`（§9 職責表「UI 不擁有 Shell COM 指標」的同一原則）。
- 所有 COM 指標以 RAII 釋放；沿用 `shell_icon_provider.cpp` 既有的釋放風格，不要引入新的 smart pointer 模板。
- 假設呼叫端 thread 已 `CoInitializeEx`（實務上是 NR-032 的 worker，STA）。本模組**不得**自行 `CoInitializeEx`。
- 任何失敗都回傳失敗值，**不 throw**、不寫檔、不彈對話框。
- 不新增設定項、不新增 UI 字串。

## PNG 像素格式的關鍵細節

`IconBitmap::pixels` 是 **premultiplied** BGRA（`GUID_WICPixelFormat32bppPBGRA`）。PNG 檔案格式**不支援** premultiplied alpha，WIC 的 PNG encoder 只接受 straight alpha（`GUID_WICPixelFormat32bppBGRA`）。因此：

- **編碼**：以 `GUID_WICPixelFormat32bppPBGRA` 建 `IWICBitmap`（`CreateBitmapFromMemory`），再用 `IWICFormatConverter` 轉成 `GUID_WICPixelFormat32bppBGRA` 交給 encoder。
- **解碼**：decoder 給出 straight BGRA，再用 `IWICFormatConverter` 轉回 `GUID_WICPixelFormat32bppPBGRA` 後 `CopyPixels` 到 `IconBitmap::pixels`。

**不要**自行寫乘除迴圈做 premultiply 轉換：`IWICFormatConverter` 就是做這件事的，自行實作會在 `a == 0` 與四捨五入上與 WIC 不一致，導致 round-trip 測試永遠差一。

## Scope

### 新檔 `src/icons/png_codec.{h,cpp}`（加入 `nimblerun_icons` 庫）

`png_codec.h` 可包含 `<windows.h>`；**不得**包含 D2D 或 Shell 標頭。

```cpp
// Encode a decoded icon as PNG bytes. Returns an empty vector on any failure
// (invalid bitmap, WIC error). The caller treats empty as "do not persist".
std::vector<std::uint8_t> EncodeIconPng(const IconBitmap& bitmap);

// Decode PNG bytes produced by EncodeIconPng (or any PNG WIC can read) into
// 32bpp premultiplied BGRA. Returns an empty IconBitmap on any failure,
// including truncated or corrupt input. expected_size, when > 0, rejects images
// whose width or height differs, so a mismatched cache entry cannot slip
// through as a wrong-sized icon.
IconBitmap DecodeIconPng(const std::uint8_t* data, std::size_t size, int expected_size = 0);
```

實作要點：

- 以 `CreateStreamOnHGlobal`＋`IWICStream::InitializeFromIStream`，或 `IWICStream::InitializeFromMemory`（解碼）／`InitializeFromIStream`（編碼）取得串流。編碼後從 memory stream 讀回位元組。
- 編碼器用 `GUID_ContainerFormatPng`。不設定任何 encoder property（不調 filter、不調壓縮等級）——預設值夠用，調參是沒有量測依據的複雜度。
- 解碼取 frame 0（`GetFrame(0)`），忽略多 frame。
- 拒絕條件（回傳空）：`bitmap.Empty()`、`width`／`height` 為 0 或大於 1024（超過最大 variant 256 的合理上界，防止毀損資料造成大量配置）、`pixels.size() != width * height`、`size == 0`、`data == nullptr`、`expected_size > 0` 且尺寸不符。
- `IWICImagingFactory` 每次呼叫建立即可（`CoCreateInstance`），**不要**為它做全域快取或單例：一次建立約數十 µs，落在 worker 上，換到的是零生命週期管理。若日後量測顯示有感，留 `ponytail:` 註解指出可改為 worker 的 thread-local。

## Non-goals

- 不做磁碟讀寫（NR-035）。
- 不接線到 worker 或 renderer（NR-036）。
- 不支援 JPEG／BMP／ICO 等其他容器。
- 不做縮放：本模組編碼與解碼的尺寸完全一致，縮放由 D2D 於繪製時做（NR-031）。
- 不做 raw BGRA 的替代格式或格式協商。
- 不改 `IconBitmap`、`IconCache`、`ShellIconProvider`。
- 不自行 `CoInitializeEx`／`CoUninitialize`。

## Acceptance

- Round-trip 保真：對合成的 48×48、96×96、256×256 圖樣（含完全不透明區、完全透明區、半透明區、以及每個角落一個獨特顏色的像素），`DecodeIconPng(EncodeIconPng(x))` 得到與 `x` **逐像素完全相等**的結果（PNG 為無損；premultiplied↔straight 的往返在 `a == 255` 與 `a == 0` 上必須精確）。
- 半透明像素的往返容差：允許每通道 ±1（premultiply 往返的捨入），測試以此容差斷言，並在註解說明為何不是 0。
- `a == 0` 的像素往返後仍為 `a == 0`，且不產生非零顏色通道（premultiplied 語意）。
- 編碼後的位元組以 PNG 簽章 `89 50 4E 47 0D 0A 1A 0A` 開頭。
- 96×96 的典型圖樣編碼後小於 20 KiB（驗證確實有壓縮，而非退化成 raw；不對下界作硬性斷言）。
- 失敗路徑全部回傳空且不崩潰：空 `IconBitmap`、`width * height` 與 `pixels.size()` 不符、`width = 2048`、`data = nullptr`、`size = 0`、只有前 8 個位元組的截斷 PNG、中段位元翻轉的 PNG、完全隨機的 4 KiB 位元組。
- `expected_size = 96` 對 48×48 的合法 PNG 回傳空；`expected_size = 0` 則接受。
- 模組對外介面不出現任何 `IWIC*`／`IStream*`／`HBITMAP` 型別（以 grep 驗證 `png_codec.h`）。
- 建置無新增警告（含 `windowscodecs` 已正確連結）；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "png_codec" --output-on-failure
ctest --test-dir build --output-on-failure
```

新測試 `tests/unit/png_codec_test.cpp`（新 CTest 目標 `nimblerun_png_codec_test`）：測試程式需自行 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`／`CoUninitialize`（本模組不做）。不需建視窗、不需寫檔（全部在記憶體）。涵蓋上述 Acceptance 每一條。

`CMakeLists.txt`：`nimblerun_icons` 需連結 `windowscodecs`（與既有 `shell32`／`ole32` 同一處）。

## 交接區

- Start: 2026-08-05。依「必讀」讀完 AGENTS.md、docs/development.md、work-items.md、NR-030（已 `done`，無阻塞）、icon_cache.h、shell_icon_provider.cpp、icon_pack_format.h、CMakeLists.txt 與 tests/unit 註冊方式。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/icons/icon_cache.h`（`IconBitmap`）、`src/icons/shell_icon_provider.cpp`（COM 釋放風格、alpha 處理、既有 WIC/D2D 相關 include 與連結）、`CMakeLists.txt`（`nimblerun_icons` 目標與其連結庫）、`tests/unit/` 內任一測試的註冊方式。先確認 NR-030 已 `done`，否則回報阻塞。只實作本 item 的 Scope，不得建立 `icon_store`、不得改 worker 或 renderer。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。新檔 `src/icons/png_codec.{h,cpp}`（加入 `nimblerun_icons` 庫，CMakeLists 同處新增 `windowscodecs` 連結）：`EncodeIconPng`（PBGRA → `IWICFormatConverter` 轉 straight BGRA → PNG encoder，`CreateStreamOnHGlobal` 記憶體串流，`Stat`/`Seek`/`Read` 讀回位元組）、`DecodeIconPng`（decoder frame 0 → `IWICFormatConverter` 轉回 PBGRA → `CopyPixels`；`expected_size` 尺寸不符拒絕）；premultiply↔straight 全部交由 `IWICFormatConverter`，無自行乘除迴圈；拒絕條件依文件（空/0/超 1024/size 不符/null/size=0/expected_size）；所有 COM 指標以 `std::unique_ptr`＋匿名 `ComRelease` deleter RAII 釋放（未引入新 smart pointer 模板）；factory 每次 `CoCreateInstance` 建立，`ponytail:` 註解記錄 worker thread-local 升級路徑；模組不自行 `CoInitializeEx`。**重要發現**：實測 WIC PNG decoder 不驗證 chunk CRC——中段單一位元翻轉與截斷到 IDAT 中段都會「成功」解出垃圾，無法履行「corrupt input 回傳空」的契約。故解碼前以既有 `icon_pack_format::Crc32`（與 PNG 同為 IEEE 802.3 標準 CRC-32，實測對 WIC 產出的 PNG 逐 chunk 比對零失配）做 `PngChunkCrcsValid` 逐 chunk CRC 驗證（含簽章、長度越界檢查），pack 層 NR-033 的每 payload CRC 因此屬第二層防禦。新測試 `tests/unit/png_codec_test.cpp`（CTest 目標 `nimblerun_png_codec_test`，自行 `CoInitializeEx(COINIT_APARTMENTTHREADED)`）：round-trip 48/96/256 圖樣（不透明/全透明精確相等、半透明每通道 ±1 附註解、a==0 像素保持全零、四角獨特色）、PNG 簽章、96×96 <20 KiB、全部失敗路徑（空/尺寸不符/2048/null/size=0/8-byte 截斷/中段翻轉/隨機 4KiB）、`expected_size=96` 拒絕 48×48 而 0 接受。`ctest -R png_codec` 1/1、全套件 21/21 通過、clean build 無新增 warning。未完成：無（NR-035/036 接線）。
