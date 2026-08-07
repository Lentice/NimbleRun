# NR-075 — IconStore still accepts unbounded memory growth in two guard gaps (Put under a dead view; no pack-budget cap on reads)

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §FR-009（可降級、可完全重建的加速器）／§NFR-001（`icons.cache` ≤ 32 MiB）
- Origin: 2026-08-08 第四次全 repo 稽核（icons 子系統）

## Why

兩個獨立缺口，同一條不變式：「IconStore 的記憶體／檔案必須有界」。都未被 NR-068
（`pending_` 無界增長）與 NR-050（`payload_end` 界限）的既有守衛形狀蓋住。

### 1. `Put` 在「Ready 但 view 已失效」時仍接受寫入 → `pending_` 再次無界增長

`Put` 的守衛（`src/icons/icon_store.cpp:344`）只檢查 `state_`：

```cpp
if (state_ != StoreState::Ready || payload.empty()) {
    return;
}
```

而 `Compact()`（`icon_store.cpp:554-621`）在成功以 `.tmp`＋replace 換檔之後，最後一步
`MapFile()` 失敗（`:614-616`，AV 掃描器在 CloseHandle 與 CreateFileMapping 之間佔檔、
或 mapping 任一環節失敗）時**直接 `return false`，不降級 state**——此時 `view_` 已因
`:589` 的 `Unmap()` 而為 null，`state_` 仍是 `Ready`。之後：

- 每個 `Put` 通過 `:344` 守衛 → `pending_` 每筆完整 PNG 持續累積；
- 每個後續 `Flush` 在 `:364`（`view_ == nullptr`）直接 `return false`，且**不清
  `pending_`**。

正是 NR-068 明文要消滅的「pending 無界增長」類別。NR-050 的交接區自己指出這個邊緣
（「擋 Compact remap 失敗留下的 Ready＋null view 邊緣」），但只堵了 `Flush` 端
（`:364`），沒堵 `Put` 端（`:344`）。違反 `icon_store.h:52-56` 的 `StoreState` 契約
（`Ready`＝readable and writable）與 §FR-009 的可降級加速器語意。

### 2. 讀取端沒有 pack 預算上界 → 巨型且 CRC 正確的 `icons.cache` 整段被拷進記憶體

`DecodeHeader`（`src/icons/icon_pack_format.cpp:128-131`）對 `payload_end` 的上界只用
**檔案大小**：

```cpp
if (payload_end < kPayloadStart || payload_end > size) {
    continue;
}
```

NR-050 刻意以檔案大小當上界（擋截斷），但副作用是：一個 CRC 全部正確、卻被膨脹到
數 GB 的 `icons.cache`（位元翻轉、舊版 bug、或手改資料檔——`%LOCALAPPDATA%` 是使用者
自己的目錄）會被當 `Ready` 接受，而 `Lookup`（`icon_store.cpp:334`）把單筆 payload
（`payload_len` 是 u32，最多近 4 GB）整段拷進 `std::vector` → 瞬間記憶體尖峰，配合
NR-076 的「無例外捕捉邊界」可能直接崩潰。§NFR-001 明文 `icons.cache` ≤ 32 MiB。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-08 稽核後）：

1. **缺口 1 修在 `Compact` 失敗出口**（而非 `Put` 加 `view_ == nullptr`）：`:614-616`
   remap 失敗時 `state_ = StoreState::Disabled;` 並 `pending_.clear();`，讓
   「`Ready` ⟺ 有活 view」永遠成對——`Put`／`Flush` 的既有守衛（`:344`／`:364`）就此
   自動完整，不需要在 `Put` 再堆一層。與 NR-050 交接區的措辭一致。`Disabled` 是正確
   降級（檔已換、mapping 不在，任何寫入都無法安全落盤）。
2. **缺口 2 修在 `DecodeHeader`**（與 NR-050 同一入口，未來任何讀取者自動受保護）：
   `payload_end` 加上 pack 預算上界。**單一常數來源**——把 32 MiB 的數字定義在純值層
   `icon_pack_format.h`（本 item 在該檔新增 `kPackByteBudget`），讓 `icon_store.h:67`
   的 `kMaxPackBytes` 引用它（或反向讓格式層持有唯一定義），不可出現第二份 32 MiB
   字面值。合法 pack 的 `payload_end` 由 Flush 的 eviction 保證 ≤ 預算，此檢查不會
   誤殺合法檔。
3. **回傳既有的 `BothHeadersBad`／跳過該 slot**，不新增列舉值、不換格式、不動
   `kPackSchemaVersion`。
4. **不加測試 seam**：`MapFile` 失敗與巨型檔都屬 OS／手改路徑。端到端測試可手寫
   巨型但 CRC 正確的 `icons.cache` fixture（照 NR-050 的竄改重算 CRC 先例）驗證
   讀取端；`Compact` 失敗路徑由不變式＋code review 覆蓋（NR-050 先例）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- New non-trivial logic needs one focused runnable test or self-check.
- Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.
- Prefer the smallest working change.

design-spec §FR-009：

- decoded 圖示持久化於本機單一 pack 檔；該檔為**可完全重建的加速器**，任何毀損或
  版本不符都必須能在不損失使用者資料的前提下降級運作。

design-spec §NFR-001：

- `icons.cache` 檔案大小 ≤ 32 MiB（> 48 MiB 為阻擋門檻）。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/icons/icon_store.cpp:554-621` — `Compact`（`:589` `Unmap`、`:614-616` remap
  失敗出口）。主場之一。
- `src/icons/icon_store.cpp:337-359`（`Put`）與 `:361-369`（`Flush` 守衛）—
  既有守衛形狀，**只讀不改**。
- `src/icons/icon_pack_format.cpp:102-155` — `DecodeHeader`（`:128-131` payload_end
  檢查）。主場之二。
- `src/icons/icon_store.h:52-56`（`StoreState` 契約）、`:67`（`kMaxPackBytes`）、
  `src/icons/icon_pack_format.h:20-50`（格式層常數所在）。
- `tests/unit/icon_store_test.cpp` 與 `tests/unit/icon_pack_format_test.cpp` —
  新 case 的家；照既有「竄改＋重算 CRC」寫法。

## Scope

### 1. `Compact` remap 失敗降級（`src/icons/icon_store.cpp:614-616`）

```cpp
if (!MapFile()) {
    // NR-075: the pack was replaced but cannot be re-mapped; the store is no
    // longer writable. Disabling (not leaving a "Ready" store with a dead view)
    // keeps Put/Flush guards complete and prevents pending_ from growing
    // without bound (NR-068 class).
    pending_.clear();
    state_ = StoreState::Disabled;
    return false;
}
```

### 2. `DecodeHeader` 加 pack 預算上界

在 `src/icons/icon_pack_format.h` 新增唯一 32 MiB 常數（例：

```cpp
// Byte budget for the whole pack (design-spec §NFR-001); the sole source of
// the 32 MiB figure. icon_store.h::kMaxPackBytes references this.
inline constexpr std::uint64_t kPackByteBudget = 32ull * 1024ull * 1024ull;
```

）並讓 `icon_store.h:67` 的 `kMaxPackBytes` 引用它（`kMaxPackBytes = kPackByteBudget`）。
`DecodeHeader` 的 slot 有效性判斷加：

```cpp
if (payload_end < kPayloadStart || payload_end > size ||
    payload_end > kPackByteBudget) {
    continue;
}
```

### 3. 測試

- `icon_pack_format_test`：手寫雙 header slot、`payload_end` 設為
  `kPackByteBudget + 1`、全部 CRC 重算正確 → 兩槽皆被拒 → `BothHeadersBad`（
  依 NR-050 的逐槽判定形狀：壞 A 好 B 時仍選好 B）。`payload_end == kPackByteBudget`
  或合法值 → 照舊 `Ok`。
- `icon_store_test`：手寫含合法 CRC 但 `payload_end` 超預算的 `icons.cache` →
  `Open` 回既有失敗分類（以 `DecodeHeader` 判定為準），不當機、檔案不被改寫。

### 4. 更新 spec？

不需。§FR-009／§NFR-001 描述的行為層級未動。

## How this stays maintainable

「Ready ⟺ 活 view」與「payload_end ≤ 預算」都收在既有的單一入口（`Compact` 失敗出口／
`DecodeHeader`），不依賴呼叫端記得檢查。32 MiB 只有一個常數來源，日後調整預算不會
出現讀寫兩端數字漂移。

## Non-goals

- **不改 `Put`／`Flush` 的既有守衛形狀。**
- **不新增 `StoreState` 列舉值、不換 pack 格式、不動 `kPackSchemaVersion`。**
- **不實作 Compact 失敗後的重試。**
- **不為失敗路徑加注入 seam。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（23 項＋新增 case）。
2. §Scope 3 兩個新 case 通過。
3. sanity grep：32 MiB 常數全 repo 只有 `icon_pack_format.h` 一份定義；`kMaxPackBytes`
   引用它；`Compact` 失敗出口含 `state_ = StoreState::Disabled`＋`pending_.clear()`。

Manual：

4. 手改 `icons.cache` 把某 header slot 的 `payload_end` 設為超大值（> 32 MiB）並重算
   CRC，啟動後經 worker `Open`：store 判定既有失敗分類降級（重建或停用），無 crash、
   無 GB 級讀取。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 32 MiB 常數單一來源：
Get-ChildItem -Recurse -Path src -Include *.h,*.cpp |
  Select-String -Pattern "32ull \* 1024ull \* 1024ull|kPackByteBudget"
# expect: 只有 icon_pack_format.h 一份定義，icon_store.h 引用

# Compact remap 失敗出口降級：
Select-String -Path src/icons/icon_store.cpp -Pattern "StoreState::Disabled"
# expect: 至少含 Compact 失敗出口一處（既有 Disabled 路徑之外）

# 改動範圍：
git diff --name-only
# expect: src/icons/icon_store.cpp、src/icons/icon_store.h、
#         src/icons/icon_pack_format.h、src/icons/icon_pack_format.cpp、
#         tests/unit/icon_store_test.cpp、tests/unit/icon_pack_format_test.cpp
```

## 交接區

（實作者填寫：常數搬移的實際形狀、兩個新 case 的 CRC 重算 fixture 寫法、`Compact`
失敗出口的降級實作、建置與 CTest 結果、sanity greps、偏差、未完成事項。）

實作（2026-08-08）：

- **常數搬移**：`icon_pack_format.h` 在 `kPayloadStart` 的 static_assert 之後新增
  `inline constexpr std::uint64_t kPackByteBudget = 32ull * 1024ull * 1024ull;`
  （全 repo 唯一 32 MiB 字面值）；`icon_store.h` 的 `kMaxPackBytes` 改為
  `= kPackByteBudget`（註解標明單一來源在格式層）。
- **`DecodeHeader` 上限**：`icon_pack_format.cpp` 的 slot 有效性判斷加第三條件
  `payload_end > kPackByteBudget`，NR-075 註解五段說明語意（合法 pack 由 eviction
  保證 ≤ 預算，不會誤殺）。
- **`Compact` 降級**：`icon_store.cpp` 最後一個 `if (!MapFile())` 出口改為
  `pending_.clear(); state_ = StoreState::Disabled; return false;`，NR-075 註解。
  其他失敗出口（CreateFileW/WriteFile/replace 失敗）原樣保留（那時 view 已被
  `Unmap`，但原始檔仍在、`MapFile()` 可恢復，非「Ready＋死 view」狀態）。
- **測試**：`icon_pack_format_test` 新增 `TestPackByteBudget`（fixture 以
  `make_pack` lambda 手寫雙 header＋重算 CRC；`budget+1`→BothHeadersBad、
  `==budget`→Ok、壞 A 好 B→選好 B）；`icon_store_test` 新增 `TestOverBudgetPack`
  （手寫 `kPackByteBudget+1` 大小的 icons.cache、雙槽 payload_end 超預算＋重算
  CRC → `Open` 回 Ready 且 `recreated`、檔重建回 `kPayloadStart` 有界大小）。
- **建置與 CTest**：Release build 無新增警告；`ctest` 23/23 全綠。
- **sanity greps**：`32ull * 1024ull * 1024ull` 全 repo 只有 icon_pack_format.h:33
  一份；`kPackByteBudget` 引用處＝icon_store.h:69＋DecodeHeader；`StoreState::Disabled`
  於 icon_store.cpp 的 Compact 失敗出口（:623）新增一處；`git diff --name-only`＝
  6 檔符合 item 預期。
- **手動驗收**：手改 icons.cache payload_end 為超大值的 worker 開啟行為為實機驗證，
  本工作區未實跑。
- **偏差**：無。未完成事項：無。
