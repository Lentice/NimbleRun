# NR-009 — Recent usage and ordering

- Status: `done`
- Phase: 1 foundation, extended in Phase 4
- Depends on: NR-004、NR-008
- Source: `docs/design-spec.md` §4.2、§4.6、§4.7、§FR-011

## Goal

記錄 NimbleRun 自己成功發起的啟動，讓第一個列表垂直切片能顯示最多 20 個最近 App；Phase 4 再接上完整 usage scoring。

## Scope

- launch success 才增加 usage／last launch。
- stable ID 對應 recent records。
- 空白狀態最多 20 筆；不足時不以字母排序補位；無紀錄時回傳 empty state。
- 暫時不存在的 pin／usage 不在第一次掃描失敗後立即刪除。

## Non-goals

- 不監控其他程式的啟動。
- 不做拖曳排序、不改變 query ranking。

## Acceptance

- 相同 last-launch 時有穩定 tie-breaker。
- 21 筆以上只回傳最新 20 筆。
- 新啟動 App 會移到第一筆；失敗啟動不更新 recent。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R recent_usage --output-on-failure
```

使用固定時間與純 values fixtures，驗證排序、上限、空狀態與 round-trip。

## 交接區

- Start: 2026-08-05
- Subagent scope: 讀 `AGENTS.md`、`docs/design-spec.md` §4.2、§4.6、§4.7、§FR-011、§10.2（`usage.tsv` 格式與 atomic write）、`docs/work-items.md`、本文件、`docs/work-items/NR-004-settings-store.md`；trace `src/settings/settings_store.cpp`（atomic write 模式）、`src/catalog/stable_id.h`、`src/catalog/app_entry.h`、`src/launch/shell_launch.h`。實作 recent usage store：只在 launch success 時更新；以 stable ID 對應 records；空白狀態最多 20 筆（依最後啟動時間由新到舊），不足不以字母補位、無紀錄回傳空；相同 last-launch 有穩定 tie-breaker；新啟動移到第一筆、失敗啟動不更新；暫時不存在的 pin／usage 不在第一次掃描失敗後立即刪除。持久化比照 settings store 的 tmp＋atomic replace 模式（usage.tsv）。不監控其他程式、不做拖曳排序、不改變 query ranking、不實作 usage score formula（Phase 4）。新增 focused test（固定時間、純 values fixtures：排序、上限 20、空狀態、round-trip、tie-breaker、失敗不更新）。回報修改檔案、測試命令、結果與未完成事項。
- Result: done
- Agent: general subagent
- 修改檔案：`src/usage/usage_store.h`＋`src/usage/usage_store.cpp`（新增）、`src/storage/atomic_text_file.h`（新增，共享 persistence helpers）、`src/settings/settings_store.cpp`（改用共享 helpers，行為不變）、`tests/unit/recent_usage_test.cpp`（新增）、`CMakeLists.txt`（新增 `nimblerun_usage` static lib）、`tests/CMakeLists.txt`（新增 `nimblerun_recent_usage_test`）、`docs/work-items.md`＋本文件（狀態更新）。
- 設計：
  - `UsageStore(directory)` 純值、無 HWND／Shell／COM。`Load()` 取代記憶體 records；`Save()` 原子寫入；`RecordLaunch(stable_id, last_launch_utc)` 只由 caller 在 launch success 時呼叫（失敗＝不呼叫，狀態不變），同一 id 重啟則 total_launches＋1 並更新 last-launch，新 id 建 record（count=1）；`Recent(cap=20)` 依 last-launch UTC epoch 由新到舊排序，同刻以 `stable_id` 升序為確定性 tie-breaker，超過 cap 截斷，無紀錄回傳空。
  - `usage.tsv`：第一行 `schema=1`；資料列 `escaped stable_id\t<total launches uint64>\t<last launch UTC epoch int64>`；UTF-8。stable_id 以共享 backslash escape（`\\`、`\t`、`\n`、`\r`、`\=`）防止污染 TSV。儲存時以 stable_id 升序寫出使重複存檔 byte-identical。7/30 日 buckets 留待 Phase 4 scoring，schema 版本保障 forward-compatible。寫入同 settings：tmp＋`WriteFile`＋`FlushFileBuffers`＋`MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`。
  - 損壞（undecodable／schema 前綴缺失／schema<1／欄位數不符或數值無法解析）→ 改名 `usage.tsv.corrupt` 保留診斷、回傳 Corrupt、records 為空；schema>1 → 回傳 NewerSchema、原檔原封不動。未知 stable id（不存在於 catalog）的 record 不會被刪除（§FR-011），由 caller 決定哪些可顯示。
  - 共享 helper：settings_store.cpp 原有的 `JoinPath`／`EnsureDirectory`／`DecodeUtf8`／`EncodeUtf8`／`ReadAllBytes`／`Escape`／`Unescape`／`PreserveCorrupt`／tmp＋atomic replace 寫入邏輯抽成 header-only `storage/atomic_text_file.h`，settings 與 usage 共用；settings 行為由既有 `nimblerun_settings_test` 回歸驗證。
- Agent checks（2026-08-05）：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` → exit 0；`cmake --build build` → exit 0，無 warning；`ctest --test-dir build -R recent_usage --output-on-failure` → exit 0，1/1 passed；`ctest --test-dir build --output-on-failure` → exit 0，10/10 passed（含 refactor 後的 `nimblerun_settings_test` 回歸綠）。
- 測試覆蓋（固定時間注入、temp dir fixture、`Expect()`＋`exit(1)`）：empty state、newest-first 排序、超過 20 筆截斷至 20、同 last-launch 的 stable_id 升序 tie-breaker（含跨 Save/Load 可重現）、重新啟動移至第一筆且不重複、失敗啟動不更新（caller 不呼叫 RecordLaunch 即不變，另加成功對照組）、round-trip 保留全部 record 且不存在於 catalog 的 id 存活、corrupt 檔案改名保留、malformed row、newer schema 原檔不動、atomic write failure（`.tmp` 目錄擋寫）原檔不變。
- 未完成：成功啟動後 caller（NR-010）接 `RecordLaunch` 與 `Recent` 的整合不在本 item；Phase 4 再接 usage score（7/30 日 buckets）與清除使用紀錄（FR-013）。
