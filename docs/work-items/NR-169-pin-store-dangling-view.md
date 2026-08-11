# NR-169 — PinStore::Load 的 dedup 集合持有懸空 wstring_view（SSO move 後仍指向區域變數）

Phase 1 · Store read path · Depends on: —

- Source: `docs/design-spec.md` §10.2（favorites.txt 是不受信輸入的可解析格式）、
  `AGENTS.md`（Keep App Catalog data as ordinary copyable values）
- Origin: 2026-08-11 第十六次稽核第 3 輪（codex backend，IMPORTANT）。主 Agent
  已重讀 `pin_store.cpp:50-99` 驗證。
- Priority: **IMPORTANT**——同 user 修改 `favorites.txt` 可穩定觸發；短 stable
  ID（SSO 內嵌 buffer）下 `seen` 集合持有指向已析構區域變數的 view，後續
  hash／rehash／查詢讀取已結束生命週期的物件（UB），且重複 pin 可能漏過。

## Why

`PinStore::Load`（`src/pins/pin_store.cpp:50-99`）用
`std::unordered_set<std::wstring_view> seen` 做 O(n) 去重。迴圈順序：

```cpp
PinRecord pin;
pin.stable_id = UnescapeText(fields[0]);   // 區域變數
...
if (!seen.insert(pin.stable_id).second) {   // :95 插入指向 pin 的 view
    continue;
}
pins_.push_back(std::move(pin));            // :98 把 pin move 進 pins_
```

`:52-54` 的註解假設「move 保留字串 buffer、pins_ 預先 reserve 使 view 全程有效」。
但對 SSO 短字串（libstdc++ 的 wstring 內嵌 buffer 為 7 個 wchar）不成立：move 把
**內嵌字元**複製到 `pins_.back().stable_id` 的 buffer，區域變數 `pin` 在迴圈
疊代結束時析構——`seen` 裡的 view 指向已析構物件。後續任何 `seen` 操作（rehash
或重複 ID 的 `find`）都讀取已釋放的 stack 記憶體。既有測試用 `app1` 等短字串，
恰好全是 SSO 案例，懸空內容可能暫時保持原值所以測試照樣通過——不能證明安全。

favorites.txt 是同 user 可寫的不受信檔案（NR-070/122 同一輸入面），loader 接受
任意非空 stable ID，短 ID 完全合法。

## Decisions already made — do not reopen

1. **`seen` 改為 `std::unordered_set<std::wstring>`（擁有鍵值）**——最直接、
   不依賴 SSO/move 細節；`UnescapeText` 的結果已是要存的字串，多一次複製成本
   在 Load 冷路徑可忽略。
2. 保留「重複 ID 保留首位置」與 `pins_.reserve` 語意不變；`continue` 分支、行數
   上限、corrupt 路徑一字不改。
3. 新測試：手寫含重複短 ID（如 `abc` 兩次）的 favorites.txt，斷言 Load 後只有
   一份、順序與現有 duplicate 測試一致；並以 AddressSanitizer 或 valgrind
   不可行時，用「重複 ID 後再放長 ID 觸發 rehash」的案例強化（rehash 才會真的
   重新走一遍 view 內容）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.2（store 資料檔）：

> favorites.txt 是使用者資料；損壞檔改名 `.corrupt` 保存，不得原地覆寫。

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/pins/pin_store.cpp:50-99`（Load 的 dedup 迴圈）。
- `src/pins/pin_store.h`（PinRecord、PinLoadResult、kMaxRows）。
- `tests/unit/pin_store_test.cpp`（既有 duplicate 測試與 Load fixture 形狀）。

## Scope

1. `seen` 型別改 `std::unordered_set<std::wstring>`；插入改
   `seen.insert(pin.stable_id)`（隱含複製，行為與現況相同但擁有鍵值）。
2. 測試：新增重複短 ID 案例（含 rehash 觸發案例）到 `pin_store_test`。
3. 註解 `:50-54` 更新為「set 擁有鍵值，不依賴 move 保留 buffer」。

## Non-goals

- 不改 `UnescapeText`、`SplitFields`、行數上限、corrupt 處理。
- 不改 usage_store 的對應迴圈（`usage_store.cpp:89-95` 用
  `unordered_map<std::wstring,...>` 已是擁有鍵值，無此問題——驗證後不需動）。
- 不引入 sanitizer 設定（工具鏈不支援，且型別修正後不需要）。

## Acceptance

1. `seen` 為擁有鍵值的集合（code review 斷言）；重複短 ID 測試全綠。
2. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "pin" --output-on-failure
```

```powershell
rg -n "unordered_set" src/pins/pin_store.cpp
# expect: 型別為 std::unordered_set<std::wstring>
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置、新增測試與 build／CTest 結果。
