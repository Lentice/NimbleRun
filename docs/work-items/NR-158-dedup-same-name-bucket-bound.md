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

完成後在文件底部補齊本 item 的 Handoff 交接備註。
