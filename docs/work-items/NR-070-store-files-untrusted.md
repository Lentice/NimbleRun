# NR-070 — Store data files are untrusted input: ParseUint64 must reject `-`, Reconcile must not overflow

Phase 3 · Depends on: —

- Source: `docs/design-spec.md` §10.4（版本化資料檔、corrupt 處理）／§11（失效與復原）
- Origin: 2026-08-07 第三次全 repo 稽核（store 解析與保留邏輯）

## Why

兩個獨立的缺陷，同一個根源：**使用者的資料檔可以手改，解析端沒有把它當不受信
輸入**。

### 1. `ParseUint64` 接受負號（`src/storage/atomic_text_file.h:206-219`）

```cpp
const unsigned long long parsed = wcstoull(value.c_str(), &end, 10);
if (errno == ERANGE || end == value.c_str() || *end != L'\0') {
```

C 標準規定 `wcstoull` **接受前導 `-`** 並以無號回繞取負：`"-1"` → `ULLONG_MAX`，
且**不設 ERANGE**。三個檢查都擋不到。唯一 caller 是
`src/usage/usage_store.cpp:55`（grep 確認）：手改 `usage.tsv` 的
`total_launches=-1` 會被當合法載入（不觸發 corrupt 路徑），`UsageScore` 的 clamp
（`usage_store.cpp:174-176`）把它釘在 1,000,000 上限——該 app **永久佔據 usage
排序頂端**，且下次 `Save()` 把 18446744073709551615 寫回去，自我永續。
`ParseInt64` 無此問題（`wcstoll` 範圍正確）。

### 2. `PinStore::Reconcile` 帶號溢位（`src/pins/pin_store.cpp:201-203`）

```cpp
} else if (pin.last_seen_utc == 0 ||
           now - pin.last_seen_utc <= kPinRetentionSeconds) {
```

`Load` 的 `ParseInt64` 接受 `INT64_MIN`（手改 `favorites.txt` 可達）。`now - INT64_MIN`
是 **signed overflow → UB**（實務上 wrap 成負數 → 恆 ≤ retention → pin 永不過期，
連 30 天保留期都失效）。`usage_store.cpp:160-163` 的註解明示作者知道這個坑
（「`now_utc - INT64_MIN` is signed overflow」）並改用比較式，**這裡漏掉同款防護**。

## Decisions already made — do not reopen

決定於撰寫本 item 時（2026-08-07 稽核後）：

1. **`ParseUint64` 在 Trim 後拒絕首字元 `-`**（`+` 可留，與 C 語意一致）：一行
   `if (value.front() == L'-') return false;`。被拒的欄位走該 store 既有的
   corrupt 處理（`usage.tsv` → `PreserveCorrupt` 改名保留，§11）——手改檔被
   隔離，不再污染排序。
2. **`Reconcile` 改成比較式**，比照 `usage_store.cpp` 的既有防護：
   `pin.last_seen_utc >= now - kPinRetentionSeconds`。`now` 來自真實時鐘恆為
   正、`kPinRetentionSeconds` 是常數，右側不溢位。`INT64_MIN` 的 pin 在此
   比較下為「過期」→ 被丟棄——不受信極值的合理處置，且無 UB。
3. **不加欄位範圍驗證、不新增列舉值**：只堵「解析成功卻產生荒謬值」的兩個
   缺口，不做 store 級 schema 擴充。
4. **測試覆蓋兩個 store 的既有 fixture 機制**（`usage_store_test`／
   `pin_store_test` 都有 temp 目錄 fixture），不新增測試執行檔。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

- Never simplify away: input validation at trust boundaries, error handling that prevents data loss.
- New non-trivial logic needs one focused runnable test or self-check.

design-spec §10.4（版本化資料檔的 corrupt 處理，如有明確條文引用之）。

## Files to read and trace first

行號只是導航線索；程式碼片段與函式名才是規格。

- `src/storage/atomic_text_file.h:206-219` — `ParseUint64`。主場之一。
- `src/pins/pin_store.cpp:185-208` — `Reconcile`。主場之二（`:201-203` 的比較）。
- `src/usage/usage_store.cpp:50-60`（`ParseUint64` 唯一呼叫端）、`:155-180`
  （`UsageScore` 的 clamp 與 `INT64_MIN` 比較式註解——`Reconcile` 要抄的形狀）—
  **只讀不改**（除確認語意）。
- `tests/unit/usage_store_test.cpp` 與 `tests/unit/pin_store_test.cpp` — 新 case
  的家；照既有「手寫 fixture 檔 → Load → 斷言」的寫法。

## Scope

### 1. `ParseUint64` 拒絕負號

```cpp
const std::wstring value = Trim(text);
if (value.empty()) {
    return false;
}
if (value.front() == L'-') {
    return false;   // NR-070: wcstoull accepts "-1" -> ULLONG_MAX without ERANGE
}
```

### 2. `Reconcile` 防溢位

```cpp
} else if (pin.last_seen_utc == 0 ||
           pin.last_seen_utc >= now - kPinRetentionSeconds) {
```

（`now - kPinRetentionSeconds`：`now` 為正、常數減法，無溢位。`last_seen_utc == 0`
的「未知年齡：保留」特例維持。）

### 3. 測試

- `usage_store_test`：手寫 `usage.tsv` 含 `total_launches=-1` 的列 → `Load`
  走既有 corrupt 路徑（改名保留），**不**把 `-1` 載入成合法值（斷言
  `Record` 集合不含該 id 或其 score 未被污染）。以既有 corrupt fixture 的
  斷言形狀為準。
- `pin_store_test`：手寫 `favorites.txt` 含 `last_seen = -9223372036854775808`
  （INT64_MIN）的列 → `Load` 正常、`Reconcile` 在該 pin 對應 app 缺席時把
  pin 丟棄（過期），不 UB、不永續保留。

### 4. 更新 spec？

不需。§10.4 的 corrupt 處理語意不變（被拒欄位走既有路徑），spec 描述的
行為層級未動。

## How this stays maintainable

**與 `UsageScore` 同一套「不受信時間戳」防護現在成對**：比較式避免減法溢位，
負號拒絕避免無號回繞。`atomic_text_file.h` 是四個 store 的共用解析器（NR-057
收斂後唯一一份），修在這裡所有 store 自動受益；日後新增欄位只需遵守同一規則。

## Non-goals

- **不加欄位值域驗證框架**（例如「total_launches ≤ 某上限」）——那是把 store
  語意硬塞進共用解析器。
- **不改 `ParseInt64`**（`wcstoll` 範圍正確，無缺陷）。
- **不改 `UsageScore` 的 clamp**。
- **不新增列舉值、不換 schema。**

## Acceptance

Automated：

1. Release 建置無新增警告；`ctest` 全綠（既有 23 項＋新增 case）。
2. 兩個新 case 通過（§Scope 3 的斷言）。

Manual：

3. 手改 `usage.tsv` 把某列 `total_launches` 設為 `-1`，啟動 NimbleRun：該列被
   隔離（`.corrupt` 改名，若有此機制），排序正常，無 crash。
4. 手改 `favorites.txt` 某列 `last_seen` 為 `-9223372036854775808`，啟動
   NimbleRun：無 crash，該 pin 在 app 缺席時到期被移除（或維持既有保留
   語意，以實作為準）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 負號拒絕在共用解析器：
Select-String -Path src/storage/atomic_text_file.h -Pattern "front\(\) == L'-'"
# expect: 1 處

# Reconcile 用比較式（無減法溢位）：
Select-String -Path src/pins/pin_store.cpp -Pattern 'last_seen_utc >= now'
# expect: 1 處

# 改動範圍：
git diff --name-only
# expect: src/storage/atomic_text_file.h、src/pins/pin_store.cpp、
#         tests/unit/usage_store_test.cpp、tests/unit/pin_store_test.cpp
```

## 交接區

（實作者填寫：修改的位置、兩個新 case 的 fixture 寫法、corrupt 路徑的實際
行為（改名或忽略）、建置與 CTest 結果、sanity greps、偏差、未完成事項。）
