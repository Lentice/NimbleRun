# NR-121 — catalog.cache 無行數上限＋DeduplicateCatalog O(n²) 在 UI 執行緒（冷啟動與每次 generation 完成）

Phase 2 · Catalog cache · Depends on: NR-011, NR-057, NR-073, NR-079, NR-113（皆 done）

- Source: `docs/design-spec.md` §10.2（cache 是 speed-only snapshot）、§FR-003（catalog 規模上限）、
  §11（不受信輸入不得癱瘓 UI）
- Origin: 2026-08-10 第十三次全 repo 稽核（安全性軸＋正確性軸交叉發現，同一根因）；主 Agent 已驗證
  `catalog_cache.cpp:122-155` 與 `dedup.cpp:96-106`
- Priority: **MEDIUM**（同 user process／手改檔案可寫入大量合法格式行 → 冷啟動 UI 凍結數秒以上；
  合法大 catalog 也在每次 rebuild 完成時承受 O(n²)）

## Why

兩件事疊在同一個「每次 generation 完成＋冷啟動」的 UI 執行緒路徑上：

1. **`LoadCatalogCache`（`catalog_cache.cpp:122-155`）不設行數上限**：`out.reserve(lines.size())`
   全收，然後 `out = DeduplicateCatalog(out).entries`（`:155`）。`catalog.cache` 是磁碟上不受信輸入
   （NR-070 已把 store 檔列為 untrusted；手改或同 user process 可寫入數萬筆**欄位格式合法**的行，
   不需要損壞檔案）。呼叫點在 `wWinMain` 的 message loop 啟動前（`main.cpp:3626` 一帶）——視窗出現
   之前就凍結。
2. **`DeduplicateCatalog` 的 name-collision 掃描是嚴格 O(n²)**（`dedup.cpp:96-106`）：每對 entry
   做兩次 `ToLower(a.display_name)`／`ToLower(b.display_name)`（`UnjudgeableNameCollision`
   `:38-48`）＋字串比較。`dedup.cpp:96-97` 的 ponytail 註解說「catalog is bounded by design
   (FR-003, <5k entries)」——但 **FR-003 約束的是枚舉器輸出，不是 cache 檔**；cache 檔案沒有
   對應的界。5,000 筆 → 1,250 萬對；50,000 筆 → 12.5 億對，每次冷啟動與每次
   `kRebuildDoneMessage` 完成（`RebuildMerged` → `DeduplicateCatalog`）都在 UI 執行緒跑。

影響：手改 5 萬行 cache → 冷啟動凍結數秒到數十秒（視窗不出現）；正常但較大的 catalog（接近 FR-003
上限）→ 每次 Ctrl+R／watcher rebuild 完成都凍結數百 ms 級。搜尋的「已否決的方向」量測（603 µs）
**不涵蓋 dedup**——那是 `SearchApps`，這是每次 generation 都跑一次的去重掃描。

## Decisions already made — do not reopen

1. 不重開「搜尋太慢」的已否決方向（debounce／背景執行緒等）——本 item 與 `SearchApps` 無關。
2. 沿用 NR-050 的「不受信檔案超界 → 走既有 corrupt 路徑」形狀：行數超限視為 `Malformed`
   （`PreserveCorrupt`＋重建），不新增列舉值、不新增通知。
3. O(n²) 掃描改為 name-keyed 分桶：`UnjudgeableNameCollision` 只在
   `ToLower(name)` 相等時才可能 true，故以 lowercased name 為 key 分桶、桶內兩兩比較，
   結果與現行全對掃描**逐位元相同**（ambiguous 標記、`ambiguous_kept` 計數、輸出順序皆不變）——
   是純演算法替換，不是行為變更。
4. 行數上限常數單一來源（如 `catalog_cache.h` 的 `kMaxCacheRows`）；不與 icon pack 的
   32 MiB 預算混用（那是 byte budget，這是 row count）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §11：

> 應用程式必須能讀取使用者資料目錄中的任何檔案，而不崩潰、不誤刪、不耗盡資源。

`docs/design-spec.md` §10.2：

> 快取是可重建的加速快取，不是真相來源；內容一律視為未驗證。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/catalog_cache.cpp` — `LoadCatalogCache` 行迴圈（`:122-155`）、`kFieldCount`、
  `PreserveCorrupt` 呼叫形狀（NR-050/NR-079 先例）。
- `src/catalog/dedup.cpp` — `UnjudgeableNameCollision`（`:38-48`）、O(n²) 掃描（`:96-106`）。
- `src/app_host/main.cpp` — 冷啟動 cache load（`:3626` 一帶）、`kRebuildDoneMessage` →
  `RebuildMerged` → dedup 的呼叫鏈。
- `tests/unit/catalog_refresh_test.cpp`、`tests/unit/identity_dedup_test.cpp` — 既有 dedup／cache 案例。
- `docs/work-items/NR-050-icon-pack-hardening.md`、NR-079 — 不受信檔案的既有決策形狀。

## Scope

1. `LoadCatalogCache` 加行數上限（建議 `kMaxCacheRows`，如 20,000；超限 → 走既有 `Malformed`
   分支：`PreserveCorrupt`＋`return false`，保持 NR-079 的 newer-schema 分支不受影響）。
2. `DeduplicateCatalog` 的 name-collision 掃描改 name-keyed 分桶（每 entry 一次 `ToLower` 入桶、
   桶內比較），輸出與計數逐位元不變；`ambiguous`／`removed_duplicates` 語意不變。
3. 新增 focused 測試：
   - `catalog_refresh_test`：手寫超限行數 cache → 回 false＋`.corrupt` 保留＋不 OOM；
     恰在限內的合法檔行為不變。
   - `identity_dedup_test`：分桶版與全對版在既有 fixture（含 name-collision、unjudgeable pair、
     verified/unverified 混合）上結果相同；加一個 5,000 筆 timing block（既有
     `search_engine_test` timing 的先例形狀），門檻依實測填寫後寫進交接區。
4. 量測：5,000 筆與 50,000 筆的 dedup 耗時（Release build），實測數字寫進交接區與
   `docs/performance-baseline.md`（若該文件有對應表格列）。

## Non-goals

- 不把 dedup 移到背景執行緒、不 debounce rebuild 完成路徑、不加 cache schema 版本。
- 不重開 NR-073（完成時只刷新一次）／NR-079（newer-schema 保護）／NR-113（launch provenance）。
- 不改 `SearchApps` 與搜尋排名。

## Acceptance

1. 手寫 50,000 行合法格式 cache：冷啟動不凍結超過既有上限（實測數值寫入交接區），檔案被
   `PreserveCorrupt` 保留並重建。
2. 正常 ≤5,000 筆 catalog：dedup 結果與行為（含 ambiguous 計數、順序）與改動前相同，
   focused timing 顯示量級改善（預期 O(n) 後為 µs 級）。
3. Release build 無新增 warning；完整 CTest 與 lifecycle check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "catalog_refresh|identity_dedup|lifecycle" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kMaxCacheRows|DeduplicateCatalog" src tests
git diff --name-only
# expect: 只動 catalog_cache/dedup、對應測試與 performance-baseline。
```

## Handoff

實作者需記錄上限值與依據、分桶實作與等價證明、timing 量測（5k／50k）、超限路徑證據
（`.corrupt` 保留＋重建）、build／CTest 結果。

### 交接區（2026-08-10，實作完成）

**改了哪些檔**

- `src/catalog/catalog_cache.h` — 新增 `constexpr std::size_t kMaxCacheRows = 20000;`（單一來源，
  位於 namespace `nimblerun` 內，含 NR-121 註解）。
- `src/catalog/catalog_cache.cpp` — `LoadCatalogCache` 在 `Loaded` 分支、解析與 dedup 之前加入行數上限檢查。
- `src/catalog/dedup.cpp` — name-collision 掃描改 lowercased-name 分桶；移除已無呼叫的
  `UnjudgeableNameCollision` helper（其語意併入分桶掃描與註解）。
- `tests/unit/catalog_refresh_test.cpp` — 新增 `TestCacheOverRowCapQuarantines`（超限 → 回 false＋
  `.corrupt` 保留）、`TestCacheAtRowCapLoads`（恰在限內 → 載入不變）。
- `tests/unit/identity_dedup_test.cpp` — 新增 `ReferenceAmbiguousScan`（全對掃描參考實作）、
  `TestBucketedAmbiguityMatchesAllPairs`（分桶 vs 全對，於混合 fixture 上逐位元一致）、
  `TestDedup5000Timing`（5,000 筆 timing block，仿 `search_engine_test` 形狀）。
- 本檔 Handoff 節（此交接區）。

`src/catalog/catalog_refresh.cpp`、`src/app_host/main.cpp`、`docs/work-items.md`、
`docs/performance-baseline.md` 皆未動。

**kMaxCacheRows 值與依據**

`kMaxCacheRows = 20000`。依據：FR-003 約束枚舉器輸出 <5,000 筆，cache 檔無對應界；20,000 是該量級
4 倍以上的餘裕，同時把最壞情況單一 name bucket 的 O(n²) 比較（每個 pair 一次 stable_id 比較＋一次
enum 比較）壓在可接受範圍（見 timing）。Row budget 與 icon pack 的 32 MiB byte budget 分開，不混用
（item Decisions §4）。

**分桶實作與等價證明**

`UnjudgeableNameCollision(a,b)` 展開為三個條件合取：`stable_id` 不同 ∧ `ToLower(display_name)` 相等 ∧
恰一方為 `AppsFolder`（Shell 側 XOR）。第三個條件要求 lowercase name 相等，因此分桶掃描只比較
lowercased-name 相同的 pair——正是原全對掃描中可能為 true 的 pair 子集。桶內殘留測試即前後兩條件
（name 相等已由桶保證），與原函式逐位元相同。

```
ambiguous[a]=ambiguous[b]=true  ⟺  a,b 同 bucket ∧ a.stable_id≠b.stable_id ∧ (a_shell≠b_shell)
```

每 entry 一次 `ToLower` 入桶（O(n) lowercase＋hash insert），輸出順序、`ambiguous` 標記集合、
`ambiguous_kept`／`removed_duplicates` 計數皆與舊掃描一致——純演算法替換，行為零變更。stable_id
合併 pass（`Beats`）一字未動。移除 `UnjudgeableNameCollision` 是必要清理：分桶後無任何呼叫端，
留著會觸發 `-Wall` 的 unused-function。等價由 `TestBucketedAmbiguityMatchesAllPairs`（參考實作重算
全對掃描的標記集合，與產出計數比對，並在混合 fixture 上斷言 `ambiguous_kept==2`）與既有全部
dedup 案例（含 `TestPackagedAppAmbiguityKept` 的 `ambiguous_kept==2`、verified/unverified 混合、
順序案例）守住。

**timing 量測（Release x64、LLVM-MinGW 22.1.8，distinct-name 最壞名稱分布：每桶 size 1）**

以獨立 probe（與測試同 flags）量測，測多次取穩定值：

| 輸入 | 分桶版耗時 |
|---|---|
| 5,000 筆 | ≈ 2.2–2.6 ms（測試內實測 2,241 µs；probe 2,271–2,575 µs） |
| 50,000 筆 | ≈ 31–39 ms（probe 31,045 / 36,228 / 39,389 µs） |

5k→50k 近乎線性（約 14 倍）。對照：舊全對掃描 5k 即 1,250 萬對×2 次 `ToLower`，量級為數百 ms；
50k＝12.5 億對，冷啟動凍結數秒以上。測試門檻 `elapsed_us / 1000 < 50`（50 ms），約為實測 2 ms 的
20 倍餘裕，只會攔截重引入全對掃描的回歸。

**超限路徑證據**

- `TestCacheOverRowCapQuarantines`：寫 `kMaxCacheRows+1`（20,001）筆合法格式 cache →
  `LoadCatalogCache` 回 false、`out` 空、`catalog.cache.corrupt` 存在。
- `TestCacheAtRowCapLoads`：恰 `kMaxCacheRows` 筆 → 回 true、全數 round-trip、無 `.corrupt`。
- probe 實測：手寫 50,000 行合法 cache → `LoadCatalogCache` **≈ 60–126 ms** 內拒絕（含讀檔＋
  SplitLines；主要成本是讀入 50k 行），`.corrupt` 保留、`out` 空。舊行為需先解析 50k 行再跑
  O(n²) dedup＝數秒到數十秒凍結。NR-079 newer-schema 分支在其前的 switch 直接回傳，不受影響
  （`TestNewerSchemaCacheReportsAndLeavesOutUntouched` 仍綠）。

**行數計數的實作細節（偏差）**

`SplitLines` 對以 `\n` 結尾的檔會追加一個尾端空行，故最初以 `lines.size()` 計數時，恰 20,000 筆的
合法檔被誤判為 20,001 行而 quarantine（at-cap 測試首跑失敗）。修正：以非空行計數（
`std::count_if(... !line.empty())`），與解析迴圈「empty 跳過」的語意一致——空行永不成為 entry，
故不計入行數上限。此與 NR-122（pin store）同一尾端空行問題、其採用「解析中計數非空行」是同一根因。

**build／CTest 結果**

- `cmake -S . -B build-wi-nr121 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake"
  -DCMAKE_BUILD_TYPE=Release`：configure 成功。
- `cmake --build build-wi-nr121`：成功，**無任何 warning**（`-Wall -Wextra -Wpedantic`）。
- `ctest --test-dir build-wi-nr121 -R "catalog_refresh|identity_dedup|lifecycle" --output-on-failure`：
  **3/3 通過**。
- `ctest --test-dir build-wi-nr121 --output-on-failure`：**26/26 全綠**。
- sanity grep：`kMaxCacheRows` 於 catalog_cache.h（定義 1）＋catalog_cache.cpp（檢查 1）＋
  catalog_refresh_test.cpp（使用 4）；`DeduplicateCatalog` 於 dedup.h／dedup.cpp／catalog_cache.cpp
  ／catalog_refresh.cpp／identity_dedup_test.cpp。

**環境備註（非本次改動造成）**

首次完整 CTest 時 `nimblerun_pinning_test`（NR-122，`nimblerun_pins`）失敗——該 target 不連結
`nimblerun_catalog`，與本次改動無關；以獨立 probe 直接編譯現行 `pin_store.cpp` 證實現行 source
正常（20,000 筆 Load 回 Loaded），判斷為 build-wi-nr121 內殘留的過期 `libnimblerun_pins.a`
（目錄先前已存在、object 比 source 新而未被 ninja 重建）。對該 target 執行 `--clean-first`
重建後 pinning_test 轉綠，完整 26/26 通過。

**未完成事項**

- 量測數字只寫入本交接區，未動 `docs/performance-baseline.md`（依主 Agent 指示由主 agent 處理）。
- 手動驗收（真實使用者 catalog.cache 冷啟動）不屬 Agent 範圍。
