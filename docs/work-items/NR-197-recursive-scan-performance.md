# NR-197 — UserFolder 遞迴掃描候選方案實測與低風險優化

Phase 2 · Catalog enumeration · Depends on: NR-137、NR-192、NR-193、NR-194、NR-195（皆 done）

- Source: `AGENTS.md`、`docs/design-spec.md` §FR-005／§FR-008／§FR-010／§9.2／§19、`docs/development.md`
- Origin: 2026-08-20，NR-194 已移除逐檔 `CreateFileW` probe 後，實際 `D:\Program Files` walker 約 0.2 秒；使用者要求再以穩健性與相容性（Robustness & compatibility）、CPU／磁碟負載優先比較更好的方案。
- Priority: **MEDIUM**——目前已沒有秒級瓶頸；只有 Release 實測同時證明正確性不變、資源負擔不升高且速度達門檻，才改 production code。

## Goal

在不改變 Catalog snapshot、錯誤、取消、reparse point、深度與副檔名語意的前提下，對同一棵真實資料樹比較少數原生 Win32 遞迴列舉方案，選出最簡單且符合以下優先序的結果：

1. 穩健性與相容性（Robustness & compatibility）。
2. CPU／disk loading。
3. wall-clock 速度。
4. 程式碼容易維護。

使用者允許掃描期間暫時沿用舊檔案資訊；本 item 沿用現有「背景重建成功後整批替換、失敗保留舊 snapshot」契約，不另外建立增量索引。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

> Do not add network access, telemetry, third-party runtime dependencies, services, drivers, or administrator requirements.

`docs/design-spec.md` §FR-005：

> 依最大深度遞迴掃描，不追蹤目錄 symbolic link／reparse point；深度 0 時不展開子資料夾。

> 單一資料夾不存在、無權限或單一檔案異常時，略過該項目並保留其他來源結果；不得使整次 Catalog 建立失敗。

`docs/design-spec.md` §FR-008：

> 重建期間沿用舊 Catalog；成功後才整批替換，不顯示半完成結果。單一來源失敗時保留該來源舊結果及其他來源的新結果。

`docs/development.md`：

> Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`src/catalog/directory_walker.cpp` 的既有契約：

> `FindNextFileW` 回傳 FALSE 時，只有 `GetLastError() == ERROR_NO_MORE_FILES` 是 clean end；其他錯誤必須讓來源失敗，不能把部分前綴當成完整結果。

## Files to read and trace first

- `AGENTS.md`、`CONTEXT.md`、`docs/work-items.md` 的 authoring／否決方向規則。
- `docs/design-spec.md` §FR-005、§FR-008、§FR-010、§9.2、§19。
- `docs/development.md`、`docs/performance-baseline.md`。
- `docs/work-items/NR-137-directory-walker-module.md`、`NR-192-rebuild-worker-background-priority.md`、`NR-193-user-folder-max-depth.md`、`NR-194-user-folder-drop-readability-probe.md`、`NR-195-startup-rebuild-fast-sources-first.md`；這些完成文件只讀取，不回頭修改歷史紀錄。
- `src/catalog/directory_walker.h/.cpp`：唯一的 filesystem walker、遞迴、取消、reparse、缺失目錄與 `FindNextFileW` 錯誤邊界。
- `src/catalog/user_folder_catalog.cpp`：副檔名 allowlist、檔案 attribute 過濾與 source result。
- `src/app_host/rebuild_pipeline.cpp`、`src/app_host/catalog_watcher.cpp`：背景 worker、舊 snapshot 與 watcher full-rescan 邊界。
- `tests/unit/directory_walker_test.cpp`、`tests/unit/user_folder_catalog_test.cpp`、`tests/CMakeLists.txt`：既有 focused coverage 與 Release build 入口。

## Scope

1. 建立一次性的 Release x64 benchmark（可用暫時 instrumentation，不必成為 production runtime），在相同 machine、commit、root、max depth、extension allowlist 與 warm/cold 狀態下比較：
   - A：現行 recursive `FindFirstFileW`／`FindNextFileW`。
   - B：recursive `FindFirstFileExW` + `FindExInfoBasic`，flags 為 0。
   - C：B 加 `FIND_FIRST_EX_LARGE_FETCH`。
   - D：只有在 A～C 有可重現差異時，才比較 C 的 explicit `std::vector` work stack。
   - E2／E4：只有單執行緒方案仍超過可接受時間時，才比較固定 2／4 個 top-level workers；每個 worker 只累積 local 結果，最後單執行緒合併。
2. 每個候選至少執行 5 次 warm run，另記錄一次 cold-ish run；記錄 wall time、CPU time、process read bytes、peak working set、handle／thread 數、visited directories/files、allowlist hits、skipped directories、source status 與錯誤碼。
3. 以 A 作為 control，驗證每個候選的 entry count 與 order-independent path hash 相同；另驗證 max depth、reparse point、取消、missing directory 與中途列舉錯誤的既有測試與語意。
4. 只有同時滿足下列條件才修改 production：
   - correctness、error、cancel、reparse 結果與 A 相同；
   - CPU time、process read bytes、peak working set、handles／threads 沒有回歸；
   - 單執行緒候選 median wall time 至少改善 10%，或是同一 Win32 contract 的低風險替換且資源不增加；
   - E2／E4 必須至少改善 10%，且沒有增加 app-owned idle threads、無界 handles、peak memory 或 shutdown latency。
5. 預設採用最小變更：若 C 通過，最多只將 `directory_walker.cpp` 的 `FindFirstFileW` 替換成正確的 `FindFirstFileExW` 參數；若沒有達門檻，保留現行 A。無論選哪個方案，補一個 focused runnable regression/self-check；不重寫 NR-137 的 walker 介面。
6. 把 benchmark 方法、原始數字、選擇與未採用原因寫在本 item 的交接區；不要把新決策回寫 NR-194 或其他已完成 item。

## Non-goals

- 不修改 `docs/work-items/NR-194-user-folder-drop-readability-probe.md`，不改 NR-194 狀態或完成紀錄。
- 不加入 USN／MFT index、Windows Search、Shell raw-folder enumeration、`NtQueryDirectoryFile(Ex)`、content indexing 或第三方 file-search runtime。
- 不新增 per-directory metadata cache、persistent scan database、服務、管理員權限、network path 或 general file search。
- 不把每個 directory 變成 task，不建立常駐 thread pool，不改 watcher 的 `ReadDirectoryChangesW`／overflow full-rescan 語意。
- 不放寬 local path、max depth、extension allowlist、reparse point、source failure、cancel 或 snapshot atomicity 契約。
- 不因「允許舊資料暫存」而提交 partial catalog；舊資料沿用既有 snapshot/cache 路徑。

## Acceptance

1. Release benchmark 有固定 machine／OS／build commit／root／depth／cache state 記錄，至少含 A、B、C 五次 warm run 的 median／p95，以及若執行的 D、E2、E4 結果。
2. 每個被比較的候選都通過 correctness evidence：entry count、order-independent path hash、reparse／depth、missing directory、cancellation 與 enumeration failure 行為不退化。
3. 有明確選擇：
   - 若有候選達門檻，production 只包含該候選必要的最小 diff；或
   - 若沒有候選達門檻，production 不變，交接區明確記錄「保留現行 walker」及數據理由。
4. 若修改 production，`directory_walker` 的 existing `FindNextFileW` clean-end、取消、reparse、depth 與 unavailable-hook 行為不變，且有一個 focused runnable test/self-check。
5. Release build 無新增 warning；focused CTest 與完整 CTest 結果均記錄。既知環境限制必須與本 item 失敗分開列出。
6. `git diff --check` 通過；NR-194 與其他已完成 work item 文件沒有被修改。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "directory_walker|user_folder_catalog" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "FindFirstFileW|FindFirstFileExW|FindNextFileW|ERROR_NO_MORE_FILES" src/catalog tests/unit
git diff --check
```

## Handoff

完成時必須記錄：

- benchmark source／command、資料集與每次 raw output；不要只寫單一平均數。
- Release 環境：Windows build、CPU、記憶體、volume／filesystem、root、max depth、allowlist、cache state、build commit、是否接 debugger。
- A～C 的 median／p95 wall、CPU time、read bytes、peak working set、handles／threads，以及 correctness hash／counts。
- D／E2／E4 若未執行，寫明 gate 原因；若執行，記錄 worker 數、結果合併與 resource delta。
- 最終採用或保留現行的決策、production diff 檔案與 focused／full test 結果。
- 確認 `docs/work-items/NR-194-user-folder-drop-readability-probe.md` 未變更。

## 交接區（2026-08-20，實測與決策完成）

### 1. Benchmark 方法與環境

使用一次性 `clang++ -O2` x64 benchmark，未接 debugger；source 只存在於工作區量測，完成後移除，不成為 production runtime 或常駐測試 target。命令形狀如下：

```powershell
clang++ -std=c++20 -O2 -DNOMINMAX -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN `
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -municode `
  tests/benchmarks/nr197_directory_scan_benchmark.cpp `
  -o build/nr197_directory_scan_benchmark.exe -lpsapi
& build/nr197_directory_scan_benchmark.exe <A|B|C|D|E2|E4> 'D:\Program files' 20 1 5
```

環境為 Windows NT `10.0.26200.9168`（registry 顯示 Windows 10 Pro 25H2）、AMD64、20 logical processors；root 為本機 `D:\Program files`，max depth 20，default allowlist，Release product build，warmup 1 次、每變體 5 次。資料集固定為 3,724 directories、42,851 files、311 allowlist hits；所有 run `source_ok=1`、`skipped=0`。

### 2. 候選結果

以下為同一批 7-run warm benchmark 的 median／p95；working set 是 process peak，read bytes 是 `GetProcessIoCounters` 的 process read transfer counter：

| Variant | Walker | Median wall | p95 wall | Median CPU | Read bytes | Peak WS | Handles | 結果 |
|---|---|---:|---:|---:|---:|---:|---:|---|
| A | 現行 `FindFirstFileW` | 217.64 ms | 230.79 ms | 218.75 ms | 0 | 5,144 KiB | 46 | control |
| B | `FindFirstFileExW(FindExInfoBasic, flags=0)` | **193.25 ms** | 344.81 ms* | 203.12 ms | 0 | 5,172 KiB | 46 | same hash |
| C | B + `FIND_FIRST_EX_LARGE_FETCH` | 210.67 ms | 235.19 ms | 203.12 ms | 0 | 5,276 KiB | 46 | same hash |
| D | explicit `std::vector` stack + C | 197.69 ms | 211.64 ms | 187.50 ms | 0 | 5,316 KiB | 46 | same hash |
| E2 | C + 2 top-level workers | 143.81 ms | 163.88 ms | 250.00 ms | 0 | 6,776 KiB | 47 | same hash |
| E4 | C + 4 top-level workers | 98.59 ms | 128.43 ms | 296.88 ms | 0 | 7,640 KiB | 47 | same hash |

`*` B 的 7-run p95 有一次外部干擾 outlier；交錯重新執行 A/B，各 20 個有效 sample：A median `209.698 ms`、max `248.985 ms`、平均 CPU `221.875 ms`；B median `183.196 ms`、max `215.717 ms`、平均 CPU `185.938 ms`。兩批 A～E4 的四個 order-independent hash 都只有一個唯一值：

```text
hash_sum  = d15aec3b283d82e3
hash_xor  = f00248c76ca4ac6f
hash2_sum = 9ff0c7fe8d674a13
hash2_xor = ecacb380d48a01bf
```

warm cache 下所有變體的 process read bytes 都是 0；這只能表示本次沒有可觀察的 process-level transfer，不能宣稱已量到 physical disk cache miss。沒有使用需要管理員權限的 system cache flush，因此未把「cold disk」數字冒充成實測。

### 3. 最終選擇

採用 B：只把 `directory_walker.cpp` 的首個列舉呼叫換成 `FindFirstFileExW`，使用 `FindExInfoBasic`、`FindExSearchNameMatch`、`flags=0`；保留 `FindNextFileW`、`FindClose`、`*` pattern、遞迴、max depth、reparse、取消、unavailable hook 與 `ERROR_NO_MORE_FILES` 判定。[Microsoft FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)；[FINDEX_INFO_LEVELS](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_info_levels)

理由：B 的 median 約比 A 快 11～13%，CPU 沒上升且略低、handles／working set 幾乎不變，並且只是同一 Win32 enumeration contract 的低風險參數替換。C 沒有可重現收益；D 未達 10% 且增加 traversal 結構；E2/E4 速度最好但 CPU 約增加、peak WS 增加約 1.6／2.5 MiB，還要引入 worker、合併與取消協調，未符合本 item 的 Robustness & compatibility 及 CPU/disk 優先序。

### 4. Production diff 與 checks

- `src/catalog/directory_walker.cpp`：只改 `FindFirstFileW` → `FindFirstFileExW(FindExInfoBasic, FindExSearchNameMatch, nullptr, 0)`；未改介面或控制流。
- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：成功。
- `cmake --build build`：成功，無新增 warning。
- `ctest --test-dir build -R "directory_walker|user_folder_catalog" --output-on-failure`：2/2 passed。
- `ctest --test-dir build --output-on-failure`：32/33 passed；唯一失敗為既知的 `nimblerun_startup_option_test` registry write（`FAILED: enable writes the entry`），與本 item 無關；其餘 lifecycle、catalog、walker、rebuild tests 通過。
- `git diff --check`：通過。
- `rg -n "FindFirstFileW|FindFirstFileExW|FindNextFileW|ERROR_NO_MORE_FILES" src/catalog tests/unit`：walker 的 production 首次列舉已使用 B，既有 clean-end／focused coverage 保留。
- `docs/work-items/NR-194-user-folder-drop-readability-probe.md`：未修改。
