# NR-122 — favorites.txt／usage.tsv 無行數上限：load O(n²)＋Reconcile O(pins×catalog) 每次面板顯示都在 UI 執行緒

Phase 1 · Stores · Depends on: NR-057, NR-072, NR-080, NR-096（皆 done）

- Source: `docs/design-spec.md` §10.2（使用者資料檔）、§11（不受信輸入）、§4.2（喚出面板的即時性）
- Origin: 2026-08-10 第十三次全 repo 稽核（安全性軸）；主 Agent 已驗證 `pin_store.cpp`／
  `usage_store.cpp` 的 load 與 Reconcile 呼叫鏈
- Priority: **MEDIUM**（同 user process／手改檔案可寫入大量合法行 → 每次 Alt+Space 凍結數秒；
  `ReadAllBytes` 無大小上限 → GB 級檔案整份載入記憶體）

## Why

三個 store 的資料檔都是磁碟上不受信輸入（NR-070 已確立），且全部在 UI 執行緒的每次
`ShowPanel → RefreshPanelSnapshot`（`main.cpp:2372`）與每次 snapshot swap（`main.cpp:1360`）
同步跑：

1. **`PinStore::Load` 是 O(n²)**（`pin_store.cpp:76` 一帶：每行對已載入清單線性掃描查重複）；
   **`UsageStore::Load` 同形**（`usage_store.cpp:68`：每行 `find_if`）。
2. **`PinStore::Reconcile` 是 O(pins × catalog)**（`pin_store.cpp:210`：每支 pin 對整個 catalog
   `any_of`），而 `RefreshPins` 每次面板顯示都 `Load → Reconcile → Save`（`main.cpp:1326-1365`）。
3. **兩檔都無行數上限**：手寫 10 萬行 favorites.txt（每行 ≥2 欄即合法）→ 10 萬 pins × 5 千 catalog
   ≈ 5×10⁸ 次 wstring 比較 → 每次 Alt+Space 凍結數秒；pin 數還直接餵進
   `IconCacheCapacityFor`（`icon_cache.cpp:20`），LRU 上限跟著爆。
4. **`ReadAllBytes`（`atomic_text_file.h:61-84`）無大小上限**：GB 級 settings.ini／usage.tsv 在
   DecodeUtf8 之前就先整份載入記憶體，啟動瞬間 Private Bytes 暴增（NR-050 的 32 MiB pack 預算
   只蓋 icons.cache，未蓋文字 store）。

與 NR-121 同根因（「磁碟輸入無界＋UI 執行緒 O(n²)/O(n×m)」），但檔案、演算法、測試都不同，
故拆成獨立 item。

## Decisions already made — do not reopen

1. 沿用 NR-050／NR-121 的形狀：行數／位元組超限 → 走既有 `Malformed` 路徑
   （`PreserveCorrupt`＋「非 Loaded 則空」契約，NR-072/NR-080 已釘），不新增列舉值、不通知。
2. load 去重改 hash（`unordered_set`/`unordered_map`），Reconcile 改以 catalog 建 hash set 查 pin
   （O(n+m)）——**純演算法替換**，`favorites.txt`／`usage.tsv` 的 schema、語意、Save 格式一字不改
   （NR-072 的 newer-schema 保護與 NR-096 的 write guard 不受影響）。
3. 上限常數單一來源（`pin_store.h`／`usage_store.h` 各一個 `kMaxRows`；`atomic_text_file.h`
   一個 `kMaxReadBytes`）；不做「流式讀取」——上限＋既有路徑已足夠。
4. Reconcile 的「空 catalog 完全跳過」（`pin_store.cpp:203`、`usage_store.cpp:157`）維持不動。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> 應用程式必須能讀取使用者資料目錄中的任何檔案，而不崩潰、不誤刪、不耗盡資源。

`docs/design-spec.md` §10.4：

> 使用者資料檔案不得因讀取失敗而被覆寫或遺失。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/pins/pin_store.cpp` — Load 行迴圈（`:76` 一帶）、Reconcile（`:203-230`）。
- `src/usage/usage_store.cpp` — Load（`:68` 一帶）、Reconcile（`:157` 一帶）。
- `src/storage/atomic_text_file.h` — `ReadAllBytes`（`:61-84`）、`ReadVersionedLines`。
- `src/app_host/main.cpp` — `RefreshPanelSnapshot`（`:1326-1365`、`:2372`）、`IconCacheCapacityFor`
  呼叫（`:3079-3081`）。
- `tests/unit/pin_store_test.cpp`、`tests/unit/recent_usage_test.cpp`、`tests/unit/settings_store_test.cpp`。

## Scope

1. `ReadAllBytes` 加讀取上限（建議 `kMaxReadBytes`，如 16 MiB；超限視為不可讀 → 走既有
   `Unreadable/Malformed` 分支，不改名或 `PreserveCorrupt` 依各 store 既有規則）。
2. `PinStore::Load`／`UsageStore::Load` 各加行數上限（建議 `kMaxRows`，如 20,000；超限 →
   `Malformed`），並把 O(n²) 去重改 O(n) hash。
3. `PinStore::Reconcile` 改以 catalog 的 stable_id hash set 查 pin（O(n+m)）；`UsageStore::Reconcile`
   若同形一併改。
4. 新增 focused 測試：超限行數／超限大小的 fixture → 既有 corrupt 路徑（`.corrupt` 保留、live store
   空）；load／Reconcile 的結果與現行行為在既有 fixture 上逐筆相同；5 萬 pins × 5 千 catalog 的
   timing block（Release build，數字寫進交接區）。
5. 量測結果視情況回填 `docs/performance-baseline.md`。

## Non-goals

- 不換 store 格式、不 bump schema、不改 `Save` 序列化器、不動 NR-072/NR-096 的 write guard。
- 不做背景執行緒 load、不做 lazy load、不加 cache 層。
- 不重開 NR-070（Parse 溢位）／NR-080（部分狀態）／NR-058（通知）的既有決策。

## Acceptance

1. 手寫 10 萬行 favorites.txt：Alt+Space 不凍結超過實測上限（數字入交接區），檔案被
   `PreserveCorrupt` 保留，live pins 為空（既有契約）。
2. GB 級 settings.ini：啟動不暴增記憶體，走既有 corrupt 處理。
3. 正常檔案的 load／Reconcile 結果與改動前逐筆相同；既有 CTest 全綠。
4. Release build 無新增 warning。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "pin_store|recent_usage|settings_store|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kMaxRows|kMaxReadBytes" src tests
git diff --name-only
# expect: 只動 storage/atomic_text_file.h、pin_store、usage_store、對應測試與 baseline。
```

## Handoff

實作者需記錄上限值與依據、O(n) 化前後的行為等價證明、超限 fixture 證據、timing 量測、
build／CTest 結果。

### 交接區（2026-08-10，實作完成）

未 commit。改動檔案：`src/storage/atomic_text_file.h`、`src/pins/pin_store.{h,cpp}`、
`src/usage/usage_store.{h,cpp}`、`tests/unit/pin_store_test.cpp`、
`tests/unit/recent_usage_test.cpp`、`tests/unit/settings_store_test.cpp`、本 item。

#### 上限值與依據

- `kMaxReadBytes = 16 MiB`（`atomic_text_file.h`，`inline constexpr std::size_t`）。依據：
  NR-050 的 32 MiB pack 預算只蓋 icons.cache，文字 store 的每份檔案各給一半的量級；
  16 MiB 足以容納 5,000 筆 catalog 與 20,000 列 favorites/usage 的合理放大。超限在
  `ReadAllBytes` 內回傳 false → `ReadVersionedLines` 依既有規則視為 `Unreadable` →
  各 store 的既有 default 分支（`PreserveCorrupt`＋空 store／預設值）。
- `PinStore::kMaxRows = 20000`、`UsageStore::kMaxRows = 20000`（各 header 內 class
  `static constexpr`，單一來源）。依據：FR-003 catalog 上限約 5,000 筆，20,000 是
  實務 pin／usage 量的 4 倍以上，同時把餵進 `IconCacheCapacityFor` 的 pin 數也封頂。
  兩者同名但放在各自 class 內，避免同 namespace 的 ODR 衝突（`settings_store_test`
  同時 include 兩個 header）。

#### O(n) 化前後的行為等價證明

- **PinStore::Load**：原逐行 `IsPinned()`（對已載入清單線性掃描）→ 改
  `std::unordered_set<std::wstring_view>` 成員測試（O(n)）。語意「重複 stable_id
  保留第一個出現位置、last_seen 與 display_name」不變（set 只回答「是否已見」，命中即
  `continue`）。view 指向的 buffer 只被 move（從未修改或重新配置；`pins_` 預先
  `reserve`）故 view 全迴圈有效。既有 fixture 的逐筆結果由既有測試（schema=1 升級、
  trailing fields、INT64_MIN 等）逐筆斷言，全部照舊。
- **UsageStore::Load**：原逐行 `find_if`（O(n²)）→ 改 `std::unordered_map<std::wstring,
  std::size_t>` 的 id→首次出現 index（O(n)）。語意「重複 id 最後一列勝出、首次出現位置
  保留」不變：map key 是自有 copy（records_ 覆寫會銷毀舊 buffer，不能用 view），重複時
  `records_[it->second] = move(record)` 原位覆寫。
- **PinStore::Reconcile**：原每 pin `any_of(catalog)`（O(pins×catalog)）→ 先建 catalog
  stable_id 的 `unordered_set<std::wstring_view>`（view 指向 const catalog，呼叫期間
  不可變）、每 pin 一次 `find`（O(n+m)）。empty-catalog 早退（design-spec §FR-011）
  維持不動；absent 但 30 天內保留的邏輯一字未改。
- **UsageStore::Reconcile** 本就已是 `unordered_set<std::wstring_view>`（O(n+m)），
  同形不需改。
- schema 與 Save 序列化器、NR-072 的 RefreshPins 守門、NR-096 的 `write_protected_`
  全部未動。

#### 超限 fixture 證據（新增測試全綠）

- `pin_store_test`：`TestTooManyRowsCorrupt`（`kMaxRows+1` 列 favorites.txt → `Corrupt`、
  live pins 空、原檔改名 `.corrupt` 保留）；`TestLoadDedupKeepsFirstPosition`（重複 id
  保首位置）；`TestLoadAndReconcileTiming`。
- `recent_usage_test`：`TestTooManyRowsCorrupt`（usage.tsv `kMaxRows+1` 列 →
  `Corrupt`、records 空、`.corrupt` 保留）；`TestLoadDuplicateLastLineWins`（最後一列
  勝出）。
- `settings_store_test`：`TestOversizeFileCorrupt`（settings.ini 為
  `"schema=1\n" + kMaxReadBytes 個 'x' + "\n"`——內容是會被忽略的合法 unknown key，
  證明是大小上限而非內容觸發隔離 → `Corrupt`、回預設值、`.corrupt` 保留）。

**實作中發現的設計坑**：`SplitLines` 對結尾是 `\n` 的檔（Save() 就是這樣寫）會多產一個
trailing 空列，空列在資料迴圈被略過。若行數上限用原始 `lines.size()` 判斷，我們自己
Save() 出的恰好 `kMaxRows` 列檔會被誤判超限隔離。因此上限改為**在迴圈內數已解析
（非空）列**、超過即中止走 corrupt 路徑（也把 `reserve` 封頂為 `min(lines.size(),
kMaxRows)`），cap-boundary 測試正是卡在「恰好 20,000 列＋trailing `\n`」能正常 Load。

#### timing 量測（Release build，build-wi-nr122，Clang 22.1.8 / LLVM-MinGW）

`nimblerun_pinning_test` 輸出：

```
NR-122: PinStore::Load over 20000 rows took 39961 us (39 ms)
NR-122: PinStore::Reconcile over 50000 pins x 5000 catalog took 1939 us (1 ms)
```

- Load（20,000 列，cap 邊界，含既有逐列 parse 成本）：**39,961 µs ≈ 40 ms**。改動前的
  O(n²) dedup 在 2 萬列是 4×10⁸ 次 wstring 比較（item 的 10 萬列 × 5 千 catalog ≈
  5×10⁸ 的同一量級），實測會是秒級；現在被去重以外的既有 parse 成本主導。
- Reconcile（50,000 pins × 5,000 catalog）：**1,939 µs ≈ 2 ms**。改動前 O(pins×catalog)
  = 2.5×10⁸ 次比較，秒級；現在是建 set＋查 set 的 O(n+m)。
- 測試內門檻為 **2,000 ms**（超過實測兩個數量級以上，只會攔 O(n²)/O(n×m) 回歸）。
- 依 item 指示，未改 `docs/performance-baseline.md`（該文件無對應 store load/reconcile
  表格列）。

#### build／CTest

- `cmake -S . -B build-wi-nr122 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake"
  -DCMAKE_BUILD_TYPE=Release`；`cmake --build build-wi-nr122 --clean-first` 無任何
  warning/error。
- `ctest --test-dir build-wi-nr122 --output-on-failure`：**26/26 全綠**（含既有全量）。
- 任務給的 `ctest -R "pin_store|recent_usage|settings_store|lifecycle"` 因 pin 測試
  CTest 名稱是 `nimblerun_pinning_test`、settings 是 `nimblerun_settings_test`
  （不含 `pin_store`/`settings_store` 子串）只命中 2/2；三個改動測試檔已分別直接
  執行驗證 PASSED（`NR-018 pin store check PASSED`、`NR-009/NR-061 recent usage check
  PASSED`、`NR-004 settings store check PASSED`）。

#### sanity grep

```
rg -n "kMaxRows|kMaxReadBytes" src tests
# src/storage/atomic_text_file.h:66,86（kMaxReadBytes 定義＋ReadAllBytes 檢查）
# src/pins/pin_store.h:72、pin_store.cpp:62,72
# src/usage/usage_store.h:43、usage_store.cpp:60,70
# tests/unit/pin_store_test.cpp、recent_usage_test.cpp、settings_store_test.cpp
```

#### 偏差

- 行數上限判斷從「`lines.size()` 前置檢查」改為「迴圈內已解析列計數＋中止」——這是
  trailing 空列的修正，見上，語意（超限 → 既有 corrupt 路徑）不變。
- `UsageStore::Reconcile` 無需修改（本就 O(n+m)），item 的「若同形一併改」條件未觸發。
- 未執行任何 git 命令；未改 `docs/work-items.md`、`docs/performance-baseline.md` 與其他
  docs。未完成事項：無。
