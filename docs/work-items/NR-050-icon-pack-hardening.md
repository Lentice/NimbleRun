# NR-050 — A corrupt `icons.cache` header must not grow the file or crash the worker

Phase 3 · Status `ready` · Depends on: —

- Source: `docs/design-spec.md` §10.2（持久化格式與寫入規則）／§NFR-001（資源預算）／§11（失效與復原）
- Origin: 2026-08-06 repo audit, findings #4 與 #5

## Why

`icons.cache` 是**磁碟上的不受信任輸入**：使用者、備份還原、磁碟錯誤、或另一份
NimbleRun 都可能讓它變成任何 bytes。`icon_pack_format` 已經很認真地 CRC 每個
header slot 與每個 index entry，但漏了一件事：**CRC 只證明欄位沒被改壞，不證明
欄位的值合理。** 兩個具體缺陷：

1. **`payload_end` 完全未經界限檢查**（`src/icons/icon_pack_format.cpp:133`
   附近的 `DecodeHeader`）。一個 `payload_end = 0x0000100000000000` 且 CRC 正確
   的 header 會被接受為 `PackStatus::Ok`；第一次 `Flush` 算出
   `new_payload_end = payload_end + new_bytes`、呼叫 `GrowView`，
   `SetEndOfFile` 就把 `%LOCALAPPDATA%\NimbleRun\icons.cache` 撐到約 1 TB，
   把使用者的磁碟塞爆。鏡像情況 `payload_end = 0` 則讓 append 從
   `view_ + 0` 開始 `memcpy`，就地覆寫兩個 header slot 與整個 index。
2. **`GrowView` 失敗時留下不一致的狀態**（`src/icons/icon_store.cpp:265-297`）。
   `SetEndOfFile` 成功、舊 view 已 unmap（`view_ = nullptr`）、然後
   `CreateFileMappingW` 或 `MapViewOfFile` 因位址空間或 commit 壓力失敗 →
   函式回傳 false，但 `view_size_` 仍是舊的非零值。呼叫端接著跑 `ScanIndex()`，
   它以 `DecodeEntry(nullptr, 65536, ...)` 呼叫；`DecodeEntry` 的
   `size < kPayloadStart` 防線因為 `size` 是舊值而通過，於是解參考
   `nullptr + kIndexOffset + slot * 56` → icon worker 執行緒 access violation。

兩者都是「一個欄位／一個變數說謊，下游全部照做」的同一類錯誤，且都落在
`AGENTS.md` 明列不可簡化的範圍（輸入驗證、防資料遺失）。

## Decisions already made — do not reopen

決定於撰寫本 item 時：

1. **不換掉 pack 格式。** 「一個 stable_id 一個 PNG 檔、讓 NTFS 當索引」確實會
   刪掉約 1,800 行，但那是重寫一個已上線、已 spec 化（§10.2）、已有三份測試
   覆蓋的子系統，是獨立的產品決策，需要使用者拍板。本 item 只補洞。
2. **失敗一律降級為「沒有快取」，不保留 `.corrupt` 副本。** 這是
   `icon_store.h:33-37` 已載明的既有規則（pack 是可重建的加速器，不是使用者
   資料），本 item 沿用，不改。
3. **界限檢查放在 `DecodeHeader`，不是每個呼叫端。** `DecodeHeader` 是所有
   header 讀取的唯一入口；在那裡拒絕，所有下游自動安全。這是最小的 diff，
   也是唯一不會漏掉呼叫端的位置。
4. **`GrowView` 失敗後把 store 打成不可用，而不是嘗試復原。** 檔案已被
   `SetEndOfFile` 改過、mapping 已消失，正確且便宜的反應是關掉這個快取直到
   下次啟動，不是重試或重建。

## Binding constraints — quoted, do not go looking for them

design-spec §NFR-001（本 item 直接守護的預算）：

> - `icons.cache` 目標大小 ≤ 32 MiB。

design-spec §10.2：

> - 持久化寫入採暫存檔 + flush + 原子取代，任何時點崩潰都不得損毀既有檔案。
> - 不得就地覆寫使用者資料。

design-spec §11：

> - 任一子系統失效時，其餘功能必須續行。
> - 檔案損毀時以可復原的方式處理，不得靜默覆寫使用者資料。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep all user data under `%LOCALAPPDATA%\NimbleRun`.
- New non-trivial logic needs one focused runnable test or self-check.
- Do not add schema migrations or destructive data cleanup as part of unrelated
  changes.

`ponytail`（本 repo 的既定工作方式，見 `docs/work-items.md`）——但**不適用於
輸入驗證**：Never simplify away input validation at trust boundaries. 本 item
整個就是那條例外。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/icons/icon_pack_format.h:17-28` — `kHeaderSize`、`kHeaderSlotCount`、
  `kIndexEntrySize`、`kIndexCapacity`、`kIndexOffset = 64`、
  `kPayloadStart = 28736` 與三條 `static_assert`。**§1 的檢查完全用這些既有
  常數表達，不要引入新常數。**
- `src/icons/icon_pack_format.h:35-40` — `PackHeader`，含 `payload_end` 的
  預設值 `kPayloadStart`。
- `src/icons/icon_pack_format.h:64-70` — `PackStatus`。§1 **不新增列舉值**，
  理由見 §1。
- `src/icons/icon_pack_format.cpp` 的 `DecodeHeader` **整個函式**：它如何挑選
  兩個 slot 中 generation 較新且 CRC 通過的那一個，以及 `NewerSchema` 這條
  「填好 out 但回傳非 Ok」的既有先例——§1 要接在這個結構上。
- `src/icons/icon_pack_format.cpp` 的 `DecodeEntry`：它已經用
  `header.payload_end` 做 payload 界限檢查（`OutOfBounds`）。**這正是為什麼
  `payload_end` 本身必須先被檢查**——整個 entry 界限系統的信任根就是它。
- `src/icons/icon_store.cpp:265-297` — `GrowView` 全文。三條失敗路徑
  （`SetEndOfFile`、`CreateFileMappingW`、`MapViewOfFile`）留下的狀態各不相同。
- `src/icons/icon_store.cpp` 的 `Flush`：`new_payload_end` 的計算、`GrowView`
  的呼叫點、以及呼叫失敗後**接著做了什麼**（審計指出會走到 `ScanIndex()`）。
  逐行追這條路徑，本 item 的 §2 才有意義。
- `src/icons/icon_store.cpp` 的 `ScanIndex`、`MapFile`、`Unmap`、
  `CreateEmptyPack` — 所有會寫 `view_` / `view_size_` / `file_size_` 的地方。
  §2 要讓「`view_` 為 null」與「`view_size_` 為 0」永遠同進同退。
- `src/icons/icon_store.h:52-56` — `StoreState`。§2 使用既有的
  `StoreState::Disabled`，不新增狀態。
- `tests/unit/icon_pack_format_test.cpp` — 既有的 header／entry 損毀案例，
  §3 的新案例照它的形狀與 `Expect()` 慣例寫。
- `tests/unit/icon_store_test.cpp` — 既有的 `IconStorePaths` 注入與暫存目錄
  模式。§3 用它建立惡意 pack 檔。

## Scope

### 1. `DecodeHeader` 拒絕不合理的 `payload_end`

在 `DecodeHeader` 選定一個 CRC 通過的 slot、填好 `out` 之後，加入界限檢查：

```cpp
    // NR-050: a valid CRC proves the field was not corrupted in transit; it
    // does not prove the value is sane. payload_end is the trust root of the
    // whole format -- DecodeEntry bounds every payload against it, and Flush
    // grows the file to it -- so an absurd value turns every downstream check
    // into a rubber stamp. Two failure modes this closes: payload_end far
    // beyond the file makes SetEndOfFile expand icons.cache until the disk is
    // full, and payload_end below kPayloadStart makes an append memcpy over
    // the header slots and the index.
    if (out.payload_end < kPayloadStart || out.payload_end > size) {
        return PackStatus::BothHeadersBad;
    }
```

三個決定，照做不要改：

- **上界是 `size`（實際檔案／view 大小），不是 `kMaxPackBytes`。**
  `payload_end` 描述的是這個檔案裡已使用到哪裡，永遠不可能超過檔案本身。
  用檔案大小當上界，順帶把「檔案被截斷」也一併擋掉。
- **回傳既有的 `PackStatus::BothHeadersBad`，不新增列舉值。** 語意正確：
  這個 slot 不可用。呼叫端對 `BothHeadersBad` 的既有反應（重建空 pack）
  正是我們要的，於是不用改任何呼叫端。
- **檢查放在「選定 slot 之後」而不是「每個 slot 解析時」**，除非你在讀
  `DecodeHeader` 時發現它逐 slot 挑選的結構讓後者更自然。**若兩個 slot 中
  一個 `payload_end` 合理、另一個不合理，正確行為是選用合理的那一個**——
  這正是雙 header slot 存在的理由。若既有結構讓這件事只需把檢查併進既有的
  slot 有效性判斷，那樣做，並在交接區說明你選了哪一種形狀。

`kPayloadStart` 這個下界為什麼是對的：payload 區從 `kPayloadStart` 開始
（`icon_pack_format.h:24` 的 `static_assert` 保證），所以 `payload_end` 小於它
在任何情況下都不可能是真的。空 pack 的 `payload_end == kPayloadStart`，
所以下界必須是 `<` 而不是 `<=`。

### 2. `GrowView` 的失敗狀態必須自洽

`GrowView` 的**每一條**失敗路徑都必須讓 `view_`、`view_size_`、`file_size_`
一致。最小改法：在函式開頭、unmap 之前不動；在 unmap 之後的所有失敗
`return false` 之前，把大小歸零。

```cpp
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
        // NR-050: view_ and view_size_ must move together. Leaving a stale
        // non-zero size behind a null pointer defeats every downstream bounds
        // check -- DecodeEntry's `size < kPayloadStart` guard passes on the old
        // size and then dereferences nullptr + kIndexOffset.
        view_size_ = 0;
    }
```

並在 `CreateFileMappingW` / `MapViewOfFile` 的兩條失敗 return 之前確保
`view_size_ == 0`（上面那一行已經涵蓋，但**要讀過整個函式確認沒有別條路徑
在中間又設回去**）。

`SetEndOfFile` 失敗那條路徑在 unmap 之前，view 仍有效，**不要動它**。

接著，讓失敗不可被忽略。在 `Flush` 裡 `GrowView` 回傳 false 的地方：

```cpp
        // NR-050: the file has already been resized and the mapping is gone, so
        // there is nothing coherent left to write into. Disable the store for
        // this run rather than continuing with a null view; icons.cache is a
        // rebuildable accelerator (icon_store.h), so "no cache" is a complete
        // and safe degradation (§11) and the next launch starts clean.
        state_ = StoreState::Disabled;
        WriteLog(L"icon-store", L"grow-failed");
        return false;
```

以工作樹中 `WriteLog` 的實際簽章與既有事件名慣例為準（只放事件名與計數，
**永不放路徑、App 名稱或搜尋字串**——§11 與 `AGENTS.md` 的診斷規則）。

然後**驗證**：`Flush` 在 `GrowView` 失敗後不得再呼叫 `ScanIndex()` 或任何
會碰 `view_` 的東西。逐行讀完 `GrowView` 呼叫點之後的程式碼再確認。

最後，替 `view_` 的所有使用點補一道成本為零的防線——只在**目前缺少**檢查的
公開入口加：`Lookup` 已有 `view_ == nullptr` 的早退（`icon_store.cpp:301`），
確認 `Flush`、`ScanIndex`、`Compact`、`EvictOne` 是否同樣安全，缺的補上，
**不要**把 null 檢查散進私有 helper 的每一行。

### 3. 測試

**`tests/unit/icon_pack_format_test.cpp`** — 新增惡意 header 案例，全部照既有
`Expect()` 慣例：

- `payload_end` 遠大於檔案大小（例如 `0x0000100000000000`），**CRC 重新算對**
  （這是重點：測試必須通過 CRC 檢查才能證明 §1 的新檢查有作用，而不是被
  CRC 擋掉）。斷言 `DecodeHeader` 回傳 `BothHeadersBad`。
- `payload_end == kPayloadStart - 1`，CRC 正確。斷言同上。
- `payload_end == 0`，CRC 正確。斷言同上。
- `payload_end == kPayloadStart`（空 pack 的合法值）。斷言 `Ok`。
- `payload_end == size`（payload 剛好填滿）。斷言 `Ok`。
- **slot A 的 `payload_end` 不合理、slot B 合理且 generation 較舊**：斷言
  `DecodeHeader` 選用 slot B 並回傳 `Ok`。這是雙 slot 設計的核心價值，
  §1 不得把它弄壞。
- 既有的 `MakeEmptyPack()` 產出仍解析為 `Ok`（回歸保護）。

**`tests/unit/icon_store_test.cpp`** — 端到端證明磁碟不會被撐爆：

- 用既有的暫存目錄注入寫出一個 `icons.cache`：先 `MakeEmptyPack()`，再手動
  把 header slot 的 `payload_end` 竄改成一個巨大值並修正 CRC（重用
  `EncodeHeader`）。
- `IconStore::Open()` 之後斷言 `State() == StoreState::Disabled` 或
  `Stats().recreated == true`（以你實作後的實際行為為準，並在交接區寫明是
  哪一個、為什麼）。
- **斷言檔案大小沒有暴增**：`Open()` 後檔案不得大於 `kPayloadStart` 加上一個
  合理餘裕。這一條就是本 item 的整個重點，不可省略。
- 再 `Put()` + `Flush()` 一筆並斷言後續行為不當機（無論是寫成功還是被
  Disabled 拒絕）。

`GrowView` 的 mapping 失敗難以在測試中誘發（需要耗盡位址空間），**不要**為它
加注入 seam：§2 的正確性靠「兩個變數同進同退」這個不變式本身，而不是靠測試。
在交接區寫明這一點未被自動化覆蓋。

## Performance

`DecodeHeader` 多兩次整數比較，發生在 store 開啟時一次。不可測量。
`GrowView` 多一次賦值。不可測量。`docs/performance-baseline.md` 的
`icons.cache size` 一列若你在驗收時量到真實數字，順手填上。

## How this stays maintainable

**信任根要在唯一入口驗證。** 這個格式的所有界限檢查（`DecodeEntry` 的
`OutOfBounds`、`VerifyPayload`、`Flush` 的 append 位置）都以 `payload_end` 為
基準。它自己沒被檢查，等於整棟樓蓋在未經檢查的地基上。§1 把它擋在
`DecodeHeader` 這個唯一入口，於是**未來新增任何讀取 header 的程式碼都自動
受保護**，不需要記得加檢查——這就是為什麼不接受「在 `Flush` 裡檢查」這種
呼叫端方案。

**成對的狀態要成對地變。** `view_` / `view_size_` 是同一件事的兩半。
§2 的價值不只是修這一個 null 解參考，而是讓「非 null 的 `view_size_` 意味著
可用的 `view_`」重新成為可以依賴的不變式。日後若要再加一條路徑修改 mapping，
規則只有一條：兩個一起改。

**CRC 檢的是完整性，不是合理性。** 本 item 留下的最重要一句話。這個格式對
CRC 相當用心，容易讓人以為「CRC 過了就安全」。任何未來新增的 header 欄位
（新的容量、新的偏移）都要問同一個問題：這個值有沒有一個不可能的範圍？

## Non-goals

- **換掉 pack 格式（一 icon 一 PNG 檔）。** Decisions §1；那是使用者要拍板的
  產品決策，不是修補。
- **替 `icons.cache` 保留 `.corrupt` 副本。** `icon_store.h:33-37` 已明文拒絕，
  理由（快取不是使用者資料）仍然成立。
- **schema 升版或遷移路徑。** 本 item 不改任何 on-disk 佈局，`kPackSchemaVersion`
  不動。既有檔案在修補後仍完全可讀。
- **稽核 `png_codec.cpp` 的 PNG 解析。** 它已經自行做 chunk CRC 驗證
  （`png_codec.cpp:43-74`），且有 `tests/unit/png_codec_test.cpp`。不在本 item。
- **`icon_worker.cpp:177` 那條「cap 一到就丟整批 backlog」的 `ponytail:` 債。**
  那是取捨不是缺陷，另案處理。
- **替 `GrowView` 的 mapping 失敗加測試注入點。**
- **改 `IconCache`（記憶體 LRU）。** 本 item 只碰磁碟層。

## Interaction with other open items

- **NR-049**（rebuild 執行緒生命週期）也修 crash 級缺陷，但在 `src/app_host/`；
  無檔案交集，可任意順序。
- **NR-051**（共用 COM guard）會碰 `src/icons/png_codec.cpp` 的 `ComRelease`，
  不碰 `icon_store.cpp` 或 `icon_pack_format.cpp`；無衝突。
- **NR-056** 會回填 `docs/performance-baseline.md`；本 item 若量到
  `icons.cache` 大小，兩處都填不會衝突（同一列，先落地者填）。

## Acceptance

Automated：

1. Release 建置無新增警告，`ctest` 全綠。
2. §3 的全部新案例存在且通過，**特別是「檔案大小沒有暴增」與「壞 slot A／
   好 slot B 仍回 Ok」兩條**。
3. `MakeEmptyPack()` 的既有往返測試仍通過（未破壞正常路徑）。

Manual（Release build）：

1. 正常使用一段時間（開面板數次、搜尋、啟動 App），確認圖示照常顯示、
   `%LOCALAPPDATA%\NimbleRun\icons.cache` 正常成長且遠低於 32 MiB。
2. 以十六進位編輯器把 `icons.cache` 的兩個 header slot 的 `payload_end` 都改成
   一個巨大值（CRC 會因此不符，這模擬的是真實磁碟損毀而非精心構造的檔案），
   啟動程式：**不得當機，不得產生巨大檔案**，圖示應重新抓取。
3. 把 `icons.cache` 截斷成 1 KB，啟動程式：不得當機，圖示重新抓取。
4. 把 `icons.cache` 設為唯讀，啟動程式：不得當機，圖示仍顯示（走
   provider，不走快取）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "icon_pack_format|icon_store" --output-on-failure
```

```powershell
# 界限檢查落在 DecodeHeader，且用既有常數：
Select-String -Path src/icons/icon_pack_format.cpp -Pattern 'payload_end < kPayloadStart|payload_end > size'
# expect: 至少一條

# 沒有新增 PackStatus 列舉值：
Select-String -Path src/icons/icon_pack_format.h -Pattern 'enum class PackStatus' -Context 0,8
# expect: 仍是 Ok / Absent / BadMagic / NewerSchema / BothHeadersBad 五個

# 沒有改動 on-disk 佈局：
git diff src/icons/icon_pack_format.h
# expect: 常數、static_assert、struct 佈局、kPackSchemaVersion 全部未變（只可能有註解）

# view_ 與 view_size_ 成對：
Select-String -Path src/icons/icon_store.cpp -Pattern 'view_size_'
# expect: 每一處設為非零的旁邊都有有效的 view_；每一處 view_ = nullptr 附近都有 view_size_ = 0

# 診斷只有事件名，沒有路徑：
Select-String -Path src/icons/icon_store.cpp -Pattern 'WriteLog'
# expect: 全部是短事件名字串常數，無 path_ / stable_id 之類的變數插值
```

## 交接區

（實作者填寫：修改的位置、§1 檢查最終放在逐 slot 還是選定後（及理由）、
建置與 CTest 結果、惡意 pack 測試中檔案大小的實際數字、4 條手動驗收結果、
未被自動化覆蓋的部分、sanity greps、偏差、未完成事項。）
