# NR-158 — dedup 同名分桶內仍是 O(n²)：20k 行同名 cache 使 UI 執行緒停頓 0.5-2 秒

Phase 2 · Robustness · Depends on: —（NR-121 的分桶只解決了不同名碰撞，沒解決同名）

- Source: `AGENTS.md`（Keep App Catalog data as ordinary copyable values…）、
  NR-121（dedup O(n²) 的既有修補——分桶後單桶內仍是二次方）、`docs/design-spec.md`
  §FR-003（catalog 處理有界）
- Origin: 2026-08-10 第十四次稽核第 2 輪（安全軸，LOW；high confidence——數學確定，
  影響有界且暫時）。主 Agent 已讀 `dedup.cpp` 驗證。
- Priority: **LOW**——crafted cache 檔的 UI 執行緒停頓（有界、暫態），與 NR-121/140/141
  同一個「不受信檔 → UI 卡頓」家族，收口一致性。

## Why

`kMaxCacheRows = 20000`（`catalog_cache.h:17`），但 `dedup.cpp:101-115` 的
name-collision 判定在**同名分桶內**仍是兩兩比較：20k 行同一 `display_name`（
stable_id 各異）的合法 schema=2 cache → 單桶 ~2 億次 pair 比較，在冷啟動
（UI 執行緒）與之後每次 generation 完成（來源列舉失敗保留 cache row 時）各停頓
約 0.5–2 秒。NR-121 的分桶把「不同名」從 O(n²) 降到 O(n)，同名桶內的
O(n²) 不在其守門範圍；`dedup.cpp:98` 的註解還以「FR-003 的 5k」為界，實際
上限是 20k（4 倍於註解宣稱）。

## Decisions already made — do not reopen

1. **同名桶設行數上限**：同一個 lowercased name 超過 `kMaxSameNameRows`（取 5000，
   即 FR-003 註解宣稱的界）→ 後續同名行直接進入 `UnjudgeableNameCollision`
   分派（與現有歧義處理同一路徑，`dedup.cpp` 的既有語意），不再做 pair 比較。
   此為**延遲守門**，不是資料守門：超過 5000 個同名 App 是畸形資料，歧義計數
   （`ambiguous_kept`，§FR-007 item 3）本來就會在配對成功時產生——提前把它們
   標為歧義不改變合法資料的任何結果。
2. **若實作時發現更小改動**（例如 pair 比較前先檢查桶大小），可以替代，但
   「同名桶的比較次數有界」是硬性要求。
3. 同步修正 `dedup.cpp:98` 註解的數字謊言（20k 與 5k 的出入，NR-121 交接的殘餘）。
4. 不新增測試目標：`identity_dedup_test` 加斷言。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/catalog/dedup.cpp`：`:90-115`（分桶與 pair 比較、`UnjudgeableNameCollision`）、
  `:98` 註解。
- `src/catalog/catalog_cache.h`：`:17`（`kMaxCacheRows`）。
- `tests/unit/identity_dedup_test.cpp`（既有歧義/碰撞測試形狀）。

## Scope

1. 同名桶行數上限（5000）+ 超限走既有歧義路徑。
2. 修正註解數字。
3. 測試：`identity_dedup_test` 新增「6000 個同名 App（stable_id 各異）在
   有界時間內完成」斷言（有界 = 測試能快速跑完；可加計時或只靠測試規模證明）；
   既有碰撞語意測試全綠（合法資料結果逐位元不變）。

## Non-goals

- 不重寫 dedup 演算法、不新增第三份 name 正規化。
- 不改 `kMaxCacheRows`（那是讀取端資料守門，NR-121 的範圍）。

## Acceptance

1. 6000 同名行的 dedup 快速完成（測試實證）。
2. 合法資料（< 5000 同名）結果與先前逐位元相同（既有測試全綠即證明）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R dedup --output-on-failure
```

## Handoff

實作者需記錄：上限值與依據、超限路徑與既有歧義語意的對應、合法資料逐位元不變的證明、
timing 量測（6k／20k，改動前後）、build／CTest 結果。

### 交接區（2026-08-11，實作完成）

**改了哪些檔**

- `src/catalog/dedup.cpp` — 新增 `constexpr std::size_t kMaxSameNameRows = 5000;`
  （anonymous namespace，含 NR-158 註解）；分桶掃描改為：桶內超過 5000 行的部分直接
  `ambiguous[indices[x]] = true`（超限行逐行標記，無 pair 比較），僅前 5000 行保留既有
  兩兩比較（`y` 上限同樣截在 5000）。修正 `dedup.cpp:98` 原 ponytail 註解的數字謊言
  （原宣稱「kMaxCacheRows 與 FR-003 的 5k 有界 n」，實際 cache 上限是 `kMaxCacheRows =
  20000`）——註解重寫並引用 NR-158。
- `tests/unit/identity_dedup_test.cpp` — 新增 `TestSameNameBucketBound`：6000 個同名 App
  （stable_id 各異）有界時間完成；兩個 fixture 斷言守語意（見下）。
- `docs/work-items.md` — NR-158 行 `ready` → `done`。
- 本檔 Handoff 節（此交接區）。

`src/catalog/catalog_cache.h`（`kMaxCacheRows` 未動）、`src/catalog/dedup.h`、
`src/app_host/main.cpp` 皆未動。

**上限值與依據**

`kMaxSameNameRows = 5000`（item Decisions §1 定案值）。超過 5000 個同名 App 是畸形資料
（FR-003 的合法枚舉量級是 <5k 總 catalog）；此守門只管延遲，不管資料——超限行直接走既有
`UnjudgeableNameCollision` 分派（標 `ambiguous`、計入 `ambiguous_kept`），與「配對成功時
歧義計數本來就會產生」同義，不改變合法資料的任何結果。

**超限路徑與既有語意的對應（合法資料逐位元不變的證明）**

改動只影響「桶內第 5001 行以後」的處理：桶 ≤ 5000 行時 `compared == indices.size()`，
超限標記迴圈為空、兩兩比較迴圈與改動前逐位元相同（同一 predicate、同一 pair 子集），
故 `ambiguous` 標記集合、`ambiguous_kept`、`removed_duplicates`、輸出順序皆不變——
既有全部 dedup 案例（NR-121 的 `TestBucketedAmbiguityMatchesAllPairs` 參考實作比對、
`TestPackagedAppAmbiguityKept`、verified/unverified 混合、順序案例）全綠即證明。
stable_id 合併 pass（`Beats`）一字未動。

`TestSameNameBucketBound` 兩段斷言：

1. 6000 行全 path（無 Shell 側）：`ambiguous_kept == 1000`——前 5000 行與 pre-fix 相同
   （全不標），1000 行超限是守門的直接標記（pre-fix 在此 fixture 為 0，故此斷言會攔截
   移除守門的回歸）。
2. 6000 行中第 0 行改為 AppsFolder（Shell 側）：`ambiguous_kept == 6000`——與 pre-fix
   全對掃描的結果完全相同（該 Shell 行的 pair 標記全部前 5000 行，超限行已預標），證明
   桶內比較語意未被守門破壞。

**timing 量測（Release x64、LLVM-MinGW 22.1.8，同名桶、stable_id 為短字串）**

獨立 probe（與測試同 flags：`-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic`）量測：

| 輸入（全同名、distinct stable_id） | pre-fix | post-fix（本改動） |
|---|---|---|
| 6,000 行 | ≈ 36 ms（1,800 萬對） | ≈ 26 ms（1,250 萬對＋1,000 直接標記） |
| 20,000 行（`kMaxCacheRows`） | ≈ 499 ms（2 億對） | ≈ 30 ms（1,250 萬對＋15,000 直接標記） |

20k 行約 16.7 倍改善；比較次數與桶總大小無關（有界於 O(5000²)）。測試內實測 6,000 行
27 ms（含測試 fixture 構建），門檻 `elapsed_us / 1000 < 1000`（1 s，約實測 37 倍餘裕）只攔截
量級回歸——語意由 `ambiguous_kept` 斷言把守。真實 cache 的 stable_id 是 40+ 字元 hash，
pre-fix 的 0.5–2 s 停頓估計即該輸入下的情況；post-fix 的比較次數與字串長度無關，收斂更快。

**build／CTest 結果**

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake"
  -DCMAKE_BUILD_TYPE=Release`：configure 成功。
- `cmake --build build`：成功，**無任何 warning**（`-Wall -Wextra -Wpedantic`）。
- `ctest --test-dir build --output-on-failure`：**31/31 全綠**（數量與改動前相同，
  未新增測試 target——新案例加在既有 `nimblerun_identity_dedup_test` 內）。
- `ctest --test-dir build -R dedup --output-on-failure`：**1/1 通過**。

**未完成事項**

- 手動驗收（真實畸形 catalog.cache 冷啟動）不屬 Agent 範圍。
