# NR-194 — UserFolder 掃描拿掉同步開檔可讀性檢查

Phase 2 · Catalog enumeration · Depends on: NR-019、NR-137、NR-022（皆 done）

- Source: `docs/design-spec.md` §FR-005、§FR-008
- Origin: 2026-08-20 使用者回報冷開機 rebuild 過慢；已用 codex 實測 benchmark 驗證根因（見下方「實測依據」）
- Priority: **HIGH**——實測證實這是目前唯一顯著的 UserFolder 掃描瓶頸，且目前完全沒有換到任何過濾效果

## Goal

拿掉 `user_folder_catalog.cpp` 的 `IsReadableRegularFile()` 對每個 `.exe`／`.cmd`／`.bat` 候選檔案做的同步 `CreateFileW(GENERIC_READ)`＋`CloseHandle` 開檔探測，改為信任 walker 已提供的檔案屬性（非目錄、非 reparse point），把「檔案是否真的可讀」的判斷完全交給 Shell 在啟動時處理——與 `.lnk`／`.appref-ms` 今天的既有處理方式一致。

## 實測依據（2026-08-20，codex 於真實 `D:\Program files` 樹跑的 benchmark）

針對使用者實際設定的自訂 root（`D:\Program files`，遞迴，3,724 目錄、42,851 檔案、311 個 allowlist 命中）：

| Pass | 時間 |
|---|---|
| 現有 `WalkDirectory()`，不做開檔 probe | 208 ms |
| 現有 `WalkDirectory()`，含 `CreateFileW`+`CloseHandle` probe（現狀） | 25,371 ms |
| 仿最簡陽春 `FindFirstFileW`/`FindNextFileW` walker（無 probe、無 reparse 過濾） | 204 ms |

`directory_walker.cpp` 本身與最簡陽春 walker只差約 4ms，不是問題所在。開檔 probe 讓這 302 次呼叫慢了約 **122 倍**（208ms → 25,371ms），且這棵樹裡 **302 次 probe 全部成功**（0 個不可讀）——這 25 秒目前沒有換到任何過濾效果，純粹是白花的成本。這與冷啟動日誌實測的 `rebuild-ms userfolder 21641` 量級一致。

## 已確認的產品決策

1. **範圍**：只拿掉 `user_folder_catalog.cpp::ProcessFile()` 呼叫 `IsReadableRegularFile()` 的那段開檔動作（`CreateFileW`+`CloseHandle`）。保留 `IsReadableRegularFile()` 內既有的 `FILE_ATTRIBUTE_DIRECTORY`／`FILE_ATTRIBUTE_REPARSE_POINT` 判斷——這兩個屬性 walker 已經取得，幾乎零成本，且是正確性所需（避免把目錄或 symlink 當成程式列出）。
2. **不做成使用者可切換的 toggle**：不新增「驗證自訂資料夾檔案是否可開啟」的設定選項。既有的啟動失敗對話框（NR-022）與背景一次性 refresh 已經是通用、已測試過的失敗處理路徑，不可讀／已鎖定的檔案出現在搜尋結果、使用者點擊後走這條既有路徑即可，不需要新的設定面。
3. **`.lnk`／`.appref-ms` 不受影響**：本來就跳過這個檢查（`shell_validated` 分支），維持原樣。
4. **`launch_verified`（NR-113）不受影響**：這個欄位代表「目前來源列舉是否驗證過，而非快取」的來源驗證語意，跟「檔案是否可開啟」是兩件事，本 item 不觸碰 NR-113 的安全邊界，也不放寬 catalog.cache 的信任模型。
5. **`docs/design-spec.md` §FR-005 需要正式改字，不是悄悄改行為**：現行文字明確要求「`.exe`、`.cmd`、`.bat` 必須是可讀取的普通檔案」，這是本 item 要覆寫的產品決策，必須在文件裡寫清楚新規則與理由（見下方 Scope）。
6. **既有測試需要重寫而不是刪除**：`tests/unit/user_folder_catalog_test.cpp:243-278` 現在明確斷言「鎖住的檔案不能出現在結果」，這條規則本身要改變（見下方 Acceptance），測試要反映新規則，不是單純刪掉。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-005（現行文字，本 item 要覆寫的那一句）：

> `.exe`、`.cmd`、`.bat` 必須是可讀取的普通檔案；`.lnk` 與 `.appref-ms` 交由 Shell 驗證及啟動。

`docs/design-spec.md` §FR-008（既有「單一項目異常時略過，不使整批建立失敗」的既有精神，說明為什麼交給啟動時失敗是安全的）：

> 單一資料夾不存在、無權限或單一檔案異常時，略過該項目並保留其他來源結果；不得使整次 Catalog 建立失敗。

`docs/design-spec.md` §FR-010（啟動失敗的既有處理路徑，本 item 依賴這條既有契約）：

> 啟動失敗時保持面板顯示，呈現簡短錯誤及「重新整理」選項。

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

> All "lightweight" claims must be verified with Release hardware data, not by feel or EXE size.（對應 §19 第 10 點：所有「輕量」主張都以 Release 實機數據驗證）

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`、`docs/work-items.md` 的使用方式、Agent 交付規則、Item 總覽與「已否決的方向 — 不要重開」。
- `docs/design-spec.md` §FR-005、§FR-008、§FR-010、§19。
- `docs/work-items/NR-019-user-folder-catalog.md`、`NR-137-directory-walker-module.md`、`NR-022-launch-failure-dialog.md`、`NR-113-catalog-cache-launch-provenance.md`（確認本 item 不觸碰的邊界)；完成 item 文件只讀取，不回頭修改歷史紀錄。
- `src/catalog/user_folder_catalog.cpp`：`IsReadableRegularFile()`（:29-43）、`ProcessFile()`（:45-63）——本 item 唯一要改的產品程式碼。
- `src/catalog/directory_walker.h/.cpp`：確認 walker 提供的 `find_attributes` 已包含 `FILE_ATTRIBUTE_DIRECTORY`／`FILE_ATTRIBUTE_REPARSE_POINT`，不需要額外查詢。
- `src/launch/shell_launch.cpp`：啟動失敗的既有處理路徑，確認一個原本被 probe 擋掉、現在會進入 catalog 的不可讀檔案，點擊後走這條既有失敗路徑而非崩潰或靜默失敗。
- `tests/unit/user_folder_catalog_test.cpp:243-278`：現有斷言「鎖住的檔案不可進入結果」，要改寫成新規則。

## Scope

1. **移除開檔 probe**：`user_folder_catalog.cpp::IsReadableRegularFile()` 移除 `CreateFileW`＋`CloseHandle` 那段（:36-41），保留 `FILE_ATTRIBUTE_DIRECTORY`／`FILE_ATTRIBUTE_REPARSE_POINT` 判斷。函式改名或保留原名皆可，但要更新頭部註解（現行 `// FR-005: .exe/.cmd/.bat must be readable regular files` 需同步新規則）。
2. **`docs/design-spec.md` §FR-005 改字**，例如：

   > `.exe`、`.cmd`、`.bat` 只需是非目錄、非 reparse point 的檔案系統項目即進入 catalog；是否真的可開啟交由 Shell 在啟動時判斷，與 `.lnk`、`.appref-ms` 一致。啟動失敗時走既有的啟動失敗處理（§FR-010）。

   （精確文字由實作者依既有 spec 文風潤飾，但語意必須涵蓋：不再要求開檔驗證、失敗交給啟動時處理。）
3. **重寫既有測試**：`tests/unit/user_folder_catalog_test.cpp:243-278` 目前斷言「Locked.exe 不進入結果」，改為斷言「不可讀／鎖定的候選檔案（若可用測試手法模擬鎖定）仍然進入結果，只有目錄與 reparse point 被排除」。若既有測試手法（例如建立一個鎖定檔案）在新規則下已無意義，改成直接驗證「檔案不被開啟——即不呼叫 `CreateFileW`」的行為證據（可用一個會在開啟時失敗但屬性正常的測試檔案,確認它仍被列入）。

## Non-goals

- 不新增「驗證自訂資料夾檔案可讀性」的使用者可切換設定。
- 不修改 `.lnk`／`.appref-ms` 的既有處理路徑。
- 不修改 NR-113 的 `launch_verified`／catalog.cache 信任模型。
- 不修改 NR-022 既有的啟動失敗對話框或背景 refresh 邏輯。
- 不處理 NR-192（thread priority）、NR-193（max depth）、NR-195（拆 generation）；技術上互不依賴，可獨立完成。
- 不新增網路、telemetry、第三方 runtime、服務、driver 或管理員權限。

## Acceptance

1. `user_folder_catalog.cpp` 不再對任何候選檔案呼叫 `CreateFileW`／`CloseHandle`；`FILE_ATTRIBUTE_DIRECTORY`／`FILE_ATTRIBUTE_REPARSE_POINT` 判斷保留且行為不變。
2. 一個真實存在、屬性正常、但實際開啟會失敗的檔案（例如另一個 process 以獨占模式鎖定）現在會出現在 catalog 中；使用者點擊啟動它時走既有的啟動失敗對話框（§FR-010／NR-022），不崩潰、不靜默失敗。
3. 目錄與 reparse point 仍被正確排除，不進入 catalog。
4. `docs/design-spec.md` §FR-005 反映新規則，不再宣稱「必須是可讀取的普通檔案」。
5. `tests/unit/user_folder_catalog_test.cpp` 反映新規則，既有「鎖定檔案不進入結果」的舊斷言已移除或改寫為新語意；不得只是刪掉測試而不新增涵蓋。
6. 針對使用者實際的 `D:\Program files` 樹（或同等規模的測試樹）重新量測 `rebuild-ms userfolder`（沿用既有 NR-124 診斷輸出），確認耗時從原本的量級大幅下降；把量測結果寫進交接區。
7. Release build 無新增 warning；完整 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "user_folder_catalog" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "CreateFileW|IsReadableRegularFile" src/catalog/user_folder_catalog.cpp
rg -n "必須是可讀取的普通檔案|readable regular file" docs/design-spec.md
```

Focused runnable coverage 必須包含：`nimblerun_user_folder_catalog_test` 中至少一個案例證明「屬性正常但無法開啟的檔案仍進入結果」，以及既有的目錄／reparse point 排除案例保留。

## Handoff requirements

交接時記錄：

- 移除開檔 probe 前後的實際程式碼 diff（檔案／行號）。
- `docs/design-spec.md` §FR-005 的新文字與行號。
- 重寫後的測試案例內容與其驗證的具體行為。
- 針對真實或等規模樹重新量測的 `rebuild-ms userfolder` 數字，與量測方法（沿用既有診斷輸出或臨時 instrumentation）。
- Agent checks 的完整命令與結果。
