# NR-141 — SplitLines／ReadVersionedLines 在建立 vector 前行數守門，堵住行數放大

Phase 1 · Untrusted input · Depends on: —（與 NR-140 同系列、不同層：本 item 在共用讀取層）

- Source: `AGENTS.md`（Keep all user data under `%LOCALAPPDATA%\\NimbleRun`…）、
  `docs/design-spec.md` §10.4、NR-121／NR-122（行數上限的既有先例——但上限檢查發生在分配之後）
- Origin: 2026-08-10 第十四次全 repo 稽核（安全軸，MEDIUM）。主 Agent 已讀
  `atomic_text_file.h` 驗證。
- Priority: **MEDIUM**——16 MB 合法檔可觸發 ~400-500 MB 暫態配置與數秒 UI 執行緒卡頓；
  settings.ini 每次 `Alt+Space` 重複支付（NR-140 修完 root 上限後，放大面仍在）。

## Why

`SplitLines`（`src/storage/atomic_text_file.h:185-199`）在**任何行數上限檢查之前**
把每一行 materialize 成一個 `std::wstring`。`ReadVersionedLines`（`:272-333`）的
`kMaxReadBytes`（16 MB）限制的是位元組不是行數；NR-121/122/140 的 per-store 行數上限
在 `SplitLines` **之後**的 per-store 迴圈裡才檢查。

**觸發**：一個 16 MB、每行 1 個 `\n` 的檔案（合法 UTF-8、`schema=1` 檔頭有效）→
約 8M 個 `std::wstring` → 約 400-500 MB 暫態配置、數秒 UI 執行緒停頓——發生在上限邏輯
**跑之前**。對 `favorites.txt`／`usage.tsv`／`catalog.cache` 是啟動時一次（超限後隔離）；
對 `settings.ini` 是**每次 `Alt+Space`**（`main.cpp:1817` 每次顯示面板都 `Load`）。

## Decisions already made — do not reopen

1. **守門位置**：`ReadVersionedLines` 在呼叫 `SplitLines` 前，對解碼後的 `text` 做一次
   有界掃描數 `\n`；超過 `kMaxLines` 直接回 `VersionedReadStatus::Malformed`（既有列舉值，
   各呼叫端對 Malformed 的處置已存在：user-data store 走 `PreserveCorrupt`，cache 走重建）。
   解碼已發生（16 MB 上限內），但省掉的是 8M 次 `wstring` 配置。
2. 上限值：**1,000,000 行**（合法檔案的上界是 NR-121/122 的 20,000 行資料 + 檔頭；
   100 萬是 50 倍餘裕，且 16 MB 全換行檔約 8M 行，會被守門擋下）。
3. **不加列舉值、不改呼叫端簽名**：Malformed 語意「不是合法檔」對超行數檔成立。
4. `SplitLines` 本身不改簽名（仍有直接呼叫端）；守門只加在 `ReadVersionedLines`
   這個共用入口。若實作時發現把參數化行數上限放進 `SplitLines` 更少改動，可改——但
   `ReadVersionedLines` 的 Malformed 行為必須不變。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace first

- `src/storage/atomic_text_file.h`：`:185-199`（`SplitLines`）、`:272-333`（`ReadVersionedLines`）、
  `:61-92`（`ReadAllBytes`／`kMaxReadBytes`）。
- `src/settings/settings_store.cpp`（Malformed 的既有處置範本，`:166-170`）。
- `tests/unit/settings_store_test.cpp`（可覆用於「大型檔快速回 Corrupt」斷言）。

## Scope

1. `ReadVersionedLines` 在 `SplitLines` 前對 `text` 掃描換行數；超過 `kMaxLines`
   （`inline constexpr std::size_t kMaxLines = 1'000'000;`，放 `atomic_text_file.h`）回
   `Malformed`。註解引用本 item 與 NR-121/122 的關聯。
2. 測試（落點以現有測試目標的連結為準，首選 `settings_store_test`）：
   - 一個含 2,000,000 個空行的檔（約 4 MB，小於 16 MB）→ `Load` 回 `Corrupt`
     （Malformed → PreserveCorrupt 路徑），**且配置不爆炸**（測試能跑完即為證明）；
   - 10,000 行的正常檔 → `Loaded` 不變（回歸）。

## Non-goals

- 不改各 store 的既有 per-store 行數上限（NR-121/122 的語意守門仍然有效且必須保留——
  `kMaxLines` 是記憶體守門，不是資料守門）。
- 不優化 `SplitLines` 本身（改成 view-based 是另一回事，無證據需要）。
- 不動寫入端。

## Acceptance

1. 新測試存在並通過：超行數檔快速回 `Corrupt`，正常檔不受影響。
2. grep 驗證 `ReadVersionedLines` 在 `SplitLines` 前有行數守門。
3. Release build 零新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings_store" --output-on-failure
```

## Handoff 交接備註

### 實作（commit `NR-141: line-count guard before line vector allocation`）

- `src/storage/atomic_text_file.h`：`kMaxLines = 1'000'000`（inline constexpr
  std::size_t，放在 `kMaxReadBytes` 旁）；`ReadVersionedLines` 在 BOM strip 後、
  `SplitLines(text)` 前掃描 `text` 數 `\n`，超過 `kMaxLines` 即早退
  `return VersionedReadStatus::Malformed;`（計數在超過上限那一格就停，不數完
  整個檔案）。`SplitLines` 簽名與寫入端未動。
- 測試（`tests/unit/settings_store_test.cpp`，兩個新函式，CTest registration
  維持 31）：
  - `TestLineCountCap`：1,100,001 行 `x=1`（unknown key，避開 NR-140 的
    catalog_root cap；約 2.2 MB < 16 MB read cap）→ `Load` 回 `Corrupt`
    （Malformed → PreserveCorrupt 路徑，`settings.ini.corrupt` 保留）。用
    1,100,001 而非 2,000,000 行，避免測試檔寫入與檔案 I/O 變慢（2.2 MB 已
    足以超過 kMaxLines 並證明無分配爆炸）。
  - `TestManyLinesOk`：10,000 行 `x=1` → `Loaded`（回歸，低於上限不受影響）。

### 驗證結果

- Release x64（llvm-mingw + Ninja）build 零新增 warning；full CTest 31/31 全綠。
- settings_store 專注 `ctest --test-dir build -R nimblerun_settings_test` 通過，
  實測 0.30 s（含 1.1M 行檔案的建檔與載入）——超行數檔快速回 Corrupt 即為
  分配未爆炸的證明（此前同形檔會配置數百 MB）。

### 測試陷阱（本 session 發現）

- `Load` 的 Malformed 處置是 `PreserveCorrupt`（把 `settings.ini` 改名
  `settings.ini.corrupt`），所以 Corrupt 斷言後要檢查「原檔已被移開」而非
  「原檔還在」——與既有 `TestCorrupt`／`TestOversizeFileCorrupt` 同形。
- 行內容選 `x=1`：`settings_store.cpp` 的 per-row 迴圈對 unknown key 直接忽略
  （不會踩 NR-140 的 root cap），而 `ReadVersionedLines` 的守門在 per-row
  迴圈前就會先開火，因此任何行形狀都能觸發；用 unknown key 最乾淨。

