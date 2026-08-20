# NR-194：Windows 大型資料夾遞迴掃描效能研究

最後更新：2026-08-20

## 研究範圍與結論

目標是 Windows 10 22H2／Windows 11 x64、C++20、native Win32 的 NimbleRun，在使用者指定的本機資料夾（例如 `D:\Program Files`）建立 app catalog。外部技術資料只採用 Microsoft Learn／Windows SDK 文件；效能數字以 repo 內 Release 實測為準。

結論很窄：NimbleRun 應繼續使用原生檔案系統列舉，不應把 Shell、USN journal 或 Native API 當成目前的全量掃描器。下一個最小、值得量測的候選只有把現有 `FindFirstFileW` 換成：

```cpp
FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &find_data,
                 FindExSearchNameMatch, nullptr,
                 FIND_FIRST_EX_LARGE_FETCH);
```

保留現有的 `FindNextFileW`、`FindClose`、單一 `*` pattern、副檔名後置過濾、reparse point 不追蹤、深度上限、合作式取消與錯誤分類。`FindExInfoBasic` 不查詢 short name，`FIND_FIRST_EX_LARGE_FETCH` 使用較大的目錄查詢 buffer；Microsoft 只說後者「可能」提升效能，不能把它當成未量測的保證。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)；[FINDEX_INFO_LEVELS](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_info_levels)

目前不採用：

- 平行掃描：沒有現有數據證明它能改善這棵本機樹，卻會增加 worker、同步、合併、取消與 handle 壓力。
- Shell enumeration：保留給 AppsFolder／Shell namespace；不要用它掃 `D:\Program Files`。
- USN journal：適合另一個以 volume 為範圍的增量索引產品，不適合現在這個受控 root 的初次建立。
- `NtQueryDirectoryFile(Ex)`：Windows driver/native service 層 API；目前沒有足以抵銷契約與維護風險的需求。
- 第二套目錄 metadata cache：現有 `catalog.cache`、watcher 與 full rebuild 已足夠；掃描本身只有約 0.2 秒量級，先不要建一個會失效的檔案樹資料庫。

這份研究只新增文件，不改 production code，也不改 NR-194 已完成項目的狀態。

## Repo 現況與對照

Repo 沒有既有 `docs/research/`；格式參照單檔研究筆記 [`docs/hotkey-override-research.md`](../hotkey-override-research.md)：先給決策，再按 API 分析，最後列來源。工作追蹤表將 NR-194 記為 done，並把原瓶頸定義為逐檔 `CreateFileW` 可讀性 probe，而不是 walker 本身。[`docs/work-items.md`](../work-items.md#L229-L235)；[`docs/work-items/NR-194-user-folder-drop-readability-probe.md`](../work-items/NR-194-user-folder-drop-readability-probe.md#L13-L23)

目前 flow：

1. [`user_folder_catalog.cpp`](../../src/catalog/user_folder_catalog.cpp#L61-L95) 逐一驗證 local absolute root，將 extension allowlist 交給 walker 後的 visitor；候選檔只看 `WIN32_FIND_DATAW::dwFileAttributes`，不再逐檔開檔。
2. [`directory_walker.cpp`](../../src/catalog/directory_walker.cpp#L6-L65) 對每個資料夾同步呼叫 `FindFirstFileW(directory + L"\\*")`，逐筆 `FindNextFileW`；資料夾在 `depth < max_depth` 且不是 reparse point 時遞迴；列舉結束只有 `ERROR_NO_MORE_FILES` 才算成功，其餘錯誤回報失敗。
3. UserFolder 使用 NR-193 的有限深度；Start Menu 目前傳入 `std::numeric_limits<int>::max()`，因此兩者的深度風險不同。[`start_menu_catalog.cpp`](../../src/catalog/start_menu_catalog.cpp#L249-L264)
4. [`catalog_watcher.cpp`](../../src/app_host/catalog_watcher.cpp#L55-L163) 以 overlapped `ReadDirectoryChangesW` 監看 root；通知 buffer 溢位或 watcher 錯誤時要求 full rescan。這與 Microsoft 對 buffer overflow 後重新列舉 subtree 的建議一致。[ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)
5. AppsFolder 是獨立的 Shell flow：[`appsfolder_catalog.cpp`](../../src/catalog/appsfolder_catalog.cpp#L126-L180) 取得 AppsFolder item，使用 `IEnumShellItems` 逐項取 display/parsing name；不應與 raw filesystem walker 混成一套。

NR-194 的現有實測樹為 `D:\Program files`：3,724 個目錄、42,851 個檔案、311 個 allowlist 命中。`WalkDirectory()` 無 probe 為 208 ms，最簡 `FindFirstFileW`／`FindNextFileW` walker 為 204 ms；含 302 次 `CreateFileW`＋`CloseHandle` probe 為 25,371 ms。移除 probe 後，四次測試為 196／222／218／219 ms，中位數約 219 ms。這些不是同一支 harness 的逐位元比較，但共同結論是 walker 約 0.2 秒，probe 才是 25 秒級瓶頸。[`NR-194` 實測依據](../work-items/NR-194-user-folder-drop-readability-probe.md#L13-L23)；[`NR-194` 交接量測](../work-items/NR-194-user-folder-drop-readability-probe.md#L129-L135)

## 方案比較

### 1. `FindFirstFileExW`／`FindNextFileW`：首選

`FindFirstFileExW` 的 `FindExInfoBasic` 仍回傳 `WIN32_FIND_DATA`，但不查詢 short file name，且 `cAlternateFileName` 保證為空；NimbleRun 只讀 `cFileName`、`dwFileAttributes`，因此沒有資料契約損失。該 information level 在 Windows 7 起支援，涵蓋 Windows 10 22H2 與 Windows 11。[FINDEX_INFO_LEVELS](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_info_levels)

`FIND_FIRST_EX_LARGE_FETCH` 讓目錄查詢使用較大 buffer，可能減少列舉的查詢成本；文件沒有給出固定加速比例。這是低風險的單一呼叫變更，但仍須在同一台機器、同一棵樹上實測。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)

應繼續使用 `FindExSearchNameMatch` 加 `*`，再在 visitor 內以 allowlist 過濾 extension。Microsoft 說搜尋是依名稱進行，其他 attribute／file type 判斷不是 `FindFirstFileExW` 的搜尋語意；每個 extension 分別掃一次會重複讀取整棵樹，沒有理由這樣做。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)

`FindNextFileW` 不保證排序，只有 `GetLastError() == ERROR_NO_MORE_FILES` 才代表沒有更多項目。現有 walker 已正確保留這個資料完整性邊界；換成 `FindFirstFileExW` 不應順手改掉。[FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)

Microsoft 也提醒 NTFS attribute 在少數高負載情況可能不是最新；只有 catalog 的正確性真的需要重新開 handle 查詢時，才應使用 `GetFileInformationByHandle`。本產品只用列舉結果判斷目錄／reparse 與 extension，NR-194 已證明逐檔 probe 代價很高，因此不應為了理論上的即時 attribute 再加 per-file I/O。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)；[GetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle)

### 2. Shell enumeration：只用在 Shell namespace

`IShellFolder::EnumObjects` 回傳 `IEnumIDList`，而 `IEnumIDList` 的內容是 Shell folder 子項目的 PIDL；取得 parsing/display name 還要再走 Shell 介面。這對 AppsFolder、虛擬資料夾與 packaged app 是正確抽象，但不是只想取得 filesystem name、directory bit、reparse bit 的最短路徑。[IShellFolder::EnumObjects](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-enumobjects)；[IEnumIDList](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ienumidlist)；[IShellFolder::GetDisplayNameOf](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-getdisplaynameof)

因此 Shell enumeration 的效能劣勢是本研究的設計推論，不是 Microsoft 的 benchmark 結論：對 `D:\Program Files` 使用 Shell 會引入 PIDL／COM／namespace property 取得，而目前 native walker 已直接得到所需欄位。保留現有 AppsFolder 的 Shell 路徑，raw user-folder root 不改成 Shell。

### 3. USN journal：適合增量索引，不適合目前初次掃描

`FSCTL_ENUM_USN_DATA` 是對 volume handle（形式為 `\\.\X:`）執行的 MFT/USN enumeration；文件明確要求 volume 為 NTFS，且 SMB 不支援。它不是「只掃 `D:\Program Files`」的 root-scoped API，結果必須自行依檔案與父目錄關係篩選。[FSCTL_ENUM_USN_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_enum_usn_data)

USN record 具有 `FileReferenceNumber`、`ParentFileReferenceNumber`、可變長度 `FileName` 與 `FileAttributes`，所以完整 path／rename／delete 的處理需要另外維護 parent map；這是可行但明顯超出目前 catalog walker 的責任。[USN_RECORD_V2](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-usn_record_v2)

`FSCTL_READ_USN_JOURNAL` 適合從兩個 USN 邊界讀變更；Microsoft 也說 change journal 可能刪除舊記錄，而且 journal 只記錄變更事實與 reason，不足以反向重建所有歷史。任何 journal gap、journal reset 或 volume 不支援都必須退回 full scan。[FSCTL_READ_USN_JOURNAL](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_read_usn_journal)；[Change Journal Records](https://learn.microsoft.com/en-us/windows/win32/fileio/change-journal-records)

官方文件對 USN 操作整體提到 ReFS／NTFS，但 `FSCTL_ENUM_USN_DATA` 與 `FSCTL_READ_USN_JOURNAL` 的專頁都明列特定 volume 必須是 NTFS；對本研究的 `FSCTL_ENUM_USN_DATA` 應採較窄的 NTFS 契約，不把 ReFS／SMB 支援推論擴大。[Walking a Buffer of Change Journal Records](https://learn.microsoft.com/en-us/windows/win32/fileio/walking-a-buffer-of-change-journal-records)；[FSCTL_ENUM_USN_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_enum_usn_data)

NimbleRun 已有 per-root `ReadDirectoryChangesW` watcher，並在 buffer 不完整時 full rescan；在目前約 0.2 秒的 full scan 基線下，加入 volume-wide USN index 不值得。[ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)

### 4. `NtQueryDirectoryFile`／`NtQueryDirectoryFileEx`：不採用

Microsoft 的文件把 `ZwQueryDirectoryFile`／`ZwQueryDirectoryFileEx` 放在 Windows driver `ntifs.h` DDI；它們可將多個 `FILE_XXX_INFORMATION` record 填入呼叫端 buffer，並回傳 file-system-specific status。文件同時說 user mode 應使用 `NtQueryDirectoryFile(Ex)` 名稱，而不是 `Zw` 名稱。[ZwQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwquerydirectoryfile)；[ZwQueryDirectoryFileEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwquerydirectoryfileex)

這條路可能在某些 workload 減少 Win32 wrapper 的欄位或呼叫次數，但那是尚未量測的推論；它不會自動解決 path、reparse、權限、USN gap、取消或 catalog semantics。官方文件也指出 Native system service 是 kernel-mode routine 的 API，user-mode 呼叫會走 system call 參數驗證。[Using Nt and Zw Versions of the Native System Services Routines](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-nt-and-zw-versions-of-the-native-system-services-routines)

在 NimbleRun 的 Windows 10 22H2／Windows 11 MVP 中，這些額外 buffer／status／SDK 邊界換不到已證明的改善；除非 P0 benchmark 證明 Win32 路徑不足，否則不要引入。

## 遞迴、明確 stack 與平行

### 遞迴 vs 明確 stack

目前 user-folder 的 max depth 已是有限整數；`directory_walker.cpp` 的單次 `D:\Program Files` 實測也顯示，現有 walker 與最簡 walker 只有約 4 ms 差距。因此把 recursive function 改成 `std::vector` 明確 stack 不應被宣稱為效能修正；它主要是 call-stack ceiling 與未來 work queue 的結構選擇。[NR-193](../work-items/NR-193-user-folder-max-depth.md)；[NR-137](../work-items/NR-137-directory-walker-module.md#L56-L74)

建議保留目前遞迴，先只量測 `FindFirstFileExW` 變體。若未來要處理 Start Menu 的實質無限 depth，另開 correctness／robustness item，使用 `{path, depth}` 的明確 stack，並原封保留取消、reparse、missing-directory hook、`ERROR_NO_MORE_FILES` 與 partial-result 不提交語意；不要與平行化同一個 diff 綁在一起。

### 平行掃描

平行掃描的效能收益是 I/O、檔案系統、CPU、資料夾形狀與 cache state 的 workload-dependent 推論，Microsoft 的 `FindFirstFileExW` 文件沒有提供可套用的 worker 數或加速保證。對同一個本機 volume，2／4 個 worker 可能隱藏單一慢 subtree，也可能只是讓相同磁碟的 request 互相競爭；同時會增加 per-directory handle、path allocation、結果合併與取消協調成本。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)

若日後實測確實需要，最小實驗是只把 root 的 top-level child 分成 2 個 bounded workers，各自建立 local `vector<AppEntry>`，完成後在單一執行緒合併、排序、dedup；不要新增常駐 thread pool，也不要讓每個 directory 都產生 task。採用門檻應是同一 harness 的 median wall time 至少改善 10%，且沒有 correctness、peak memory、handle、thread 或 cancellation regression；這是本研究提出的專案決策門檻，不是 Windows API 保證。

## 穩健性邊界

### Reparse point

Reparse point 可實作 symbolic link、mounted folder 等不同於一般檔案系統 traversal 的行為；`FindFirstFileExW` 對 symbolic link 回傳的是 link 本身的 `WIN32_FIND_DATA`，不是 target。現有 walker 對 reparse directory 不遞迴，正確避免跨樹、循環與意外掃到不屬於設定 root 的內容；這條不能為了平行或 USN 而放寬。[Reparse points](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-points)；[Reparse Point Tags](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-tags)；[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)

目前 user-folder 對一般候選檔也排除 `FILE_ATTRIBUTE_REPARSE_POINT`，但 `.lnk`／`.appref-ms` 仍走既有 Shell launch 語意；這是 catalog policy，不是單純的 filesystem walker policy。[`user_folder_catalog.cpp`](../../src/catalog/user_folder_catalog.cpp#L29-L57)

### 權限與列舉錯誤

使用 wildcard 列舉時，Microsoft 要求呼叫者能存取 path root 與該 path 上的子目錄；`FindFirstFileExW` 失敗時回傳 `INVALID_HANDLE_VALUE`，應立即讀 `GetLastError()`。[FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)

NimbleRun 應維持目前兩層分類：root／子目錄打不開時呼叫 `on_directory_unavailable` 並略過；已開始的 directory enumeration 若 `FindNextFileW` 以非 `ERROR_NO_MORE_FILES` 結束，回報來源失敗，讓上層保留舊 snapshot。這能同時容納權限競態、刪除中的 directory 與其他 I/O error，不需要提高權限或無界 retry。[`directory_walker.cpp`](../../src/catalog/directory_walker.cpp#L15-L55)；[FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)

### 長路徑

Windows 10 1607 之後，`FindFirstFileW`、`FindFirstFileExW`、`FindNextFileW` 等 API 可在 registry `LongPathsEnabled=1` 且 application manifest 宣告 `longPathAware` 時移除 MAX_PATH 限制；另一條路是使用 `\\?\` extended-length path。每個 component 仍受 volume 的 component limit 約束。[Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)

目前 [`NimbleRun.manifest`](../../src/resources/NimbleRun.manifest#L16-L20) 只有 DPI awareness，沒有 `longPathAware`；因此不能把「FindFirstFileExW 支援 long path」等同於目前產品已端到端支援。若要開放，必須一起驗證 settings path、walker 組 path、stable ID、Shell launch 與 UI display path；不要只在 walker 內偷偷加 prefix。

### 取消

目前 walker 是同步 `FindFirst`／`FindNext`，以 atomic token 在進入 subtree 與每個 iteration boundary 檢查；這保證取消後不把 partial prefix 當成完整 source，但無法在正在阻塞的同步 Win32 call 內即時醒來。[`directory_walker.cpp`](../../src/catalog/directory_walker.cpp#L6-L53)；[FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)

`CancelSynchronousIo` 可以標記指定 thread 的 pending synchronous I/O 為取消，但不等待所有取消完成；操作也可能正常完成，且呼叫者需要檢查結果。這是極端慢 device／network path 的 fallback 研究方向，不應因為本機 `D:\Program Files` 的 0.2 秒掃描而增加到 MVP。[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)；[Canceling Pending I/O Operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)

watcher 是另一條路徑：它使用 overlapped `ReadDirectoryChangesW`，可用 `CancelIoEx` 取消 outstanding I/O；不要為了取消 full enumeration 而改寫 watcher 的既有模型。[CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)

### 快取與增量更新

`catalog.cache` 在設計上只是可重建的加速器，不是真實來源；目前它適合讓 startup 先有舊結果，不能取代背景 full rebuild。`ReadDirectoryChangesW` 的 buffer 溢位時官方要求重新列舉 directory/subtree，因此不能把漏事件後的部分通知當成完整增量狀態。[`docs/design-spec.md`](../design-spec.md#L761-L766)；[ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)

在本 workload 下，不建議保存「每個 directory 的 mtime／size／entry list」來跳過 full walk：它需要處理 rename、delete、reparse、權限變更、watcher overflow、journal gap 與 schema/atomic persistence，且現在 scan 成本已低於這套 cache 的合理複雜度。只有當 benchmark 進入數秒／數十秒，且明確量到 full walk 是新瓶頸，才考慮 path-aware incremental rebuild 或 USN-backed index。

## 建議的最小實作方向（本研究不執行）

1. 只在 [`src/catalog/directory_walker.cpp`](../../src/catalog/directory_walker.cpp#L13-L16) 將 `FindFirstFileW` 換成 `FindFirstFileExW`，參數使用 `FindExInfoBasic`、`FindExSearchNameMatch`、`FIND_FIRST_EX_LARGE_FETCH`。
2. 不改 `WalkDirectory` signature、`WalkOptions`、visitor、深度語意、reparse guard、取消邊界、`FindNextFileW` clean-end 判定或 source failure handoff。
3. 不增加 per-file `CreateFileW`、`GetFileAttributesW`、Shell COM 或 `GetFileInformationByHandle`；列舉資料已有 catalog 需要的欄位。
4. 先維持一個 per-source rebuild worker；不新增 parallel queue、USN state、metadata cache 或 Native API。
5. 以以下 benchmark matrix 驗證；若 B／C 相對 A 的 median wall time 未達本研究的 10% 改善，保留現行最簡實作即可。若 flags 造成任何 entry set、reparse、error 或 cancel 差異，立即不採用。

## Benchmark matrix

### 變體

| ID | Walker | 目的 | 優先級 |
|---|---|---|---|
| A | 現行 recursive `FindFirstFileW`／`FindNextFileW` | control；目前 production semantics | P0 |
| B | recursive `FindFirstFileExW` + `FindExInfoBasic`，flags=0 | 分離 short-name 查詢的影響 | P0 |
| C | B + `FIND_FIRST_EX_LARGE_FETCH` | 量測較大 directory query buffer 的影響 | P0 |
| D | explicit `std::vector` stack + C | 分離 call-stack/stack bookkeeping 影響 | P1；只有 A/B/C 有差異才做 |
| E2/E4 | C + bounded top-level 2／4 workers | 只驗證平行是否值得，不作預設 | P1；若 C 仍超過產品可接受時間 |
| S | Shell enumeration prototype | 架構對照，不是 production candidate | P2 |
| U | USN initial/incremental prototype | 只驗證 volume/index 假設 | P2 |
| N | `NtQueryDirectoryFile(Ex)` prototype | 只在 Win32 方案不足時比較 | P2 |

S/U/N 不應為了填表而實作；它們是被研究的替代路徑，不是 NR-194 的預設工程工作。

### 環境與資料集

每一組都使用 Release x64、無 debugger、同一 commit、同一 settings 與同一 catalog extension allowlist；記錄 OS build、CPU、記憶體、display scale、catalog size 與 volume/file system。repo 的 Release baseline 已要求這些欄位，且明確禁止用 Debug 估計或 EXE size 代替實測。[`docs/performance-baseline.md`](../performance-baseline.md#L3-L4)

| 維度 | 最小組合 |
|---|---|
| OS | Windows 10 22H2 x64、Windows 11 x64；記錄完整 build |
| volume | 本機 NTFS SSD；若產品要支援其他本機媒介，再加 HDD／外接 SSD |
| real tree | `D:\Program Files`：保留實際 3,724 dirs／42,851 files／311 hits 樣本 |
| synthetic tree | 近似 0／5／20／50 深度；寬樹、深樹、少 hit、多 hit；加入 denied directory 與 reparse directory |
| cache state | OS/filesystem cold-ish run 與 warm run 分開；每組至少 5 次 warm，報 median／p95，不混報 |
| depth | UserFolder 的 0、5、20、50；Start Menu 的既有 full-depth 行為單獨記錄 |
| cancellation | root 前取消、單一 subtree 途中取消、取消後 partial output 不得成為完整 snapshot |
| mutation | 掃描中刪除／rename child、拒絕 subtree、建立 reparse point；驗證 source failure 與 retry/full rebuild 行為 |
| path | 一般路徑；另建 >260 字元測試樹，只在 long-path 契約明確後列為相容性測試 |

### 指標、正確性與採用門檻

每次記錄：

- wall time（由同一 harness 計時）、CPU time、peak working set/private bytes、handle count、thread count；release gate 的 baseline 也要求記錄這些 process/resource 欄位。[`docs/performance-baseline.md`](../performance-baseline.md#L64-L70)
- visited directories/files、allowlist hits、skipped directories、source_ok、error code、cancel-to-worker-return latency。
- 與 A 的 entry set／stable ID hash 比對；結果順序若要對外可見，必須在列舉後排序，不能依賴 filesystem enumeration order，因為 Microsoft 明確說回傳順序不保證。[FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)
- reparse directory 不得被走訪；權限失敗不得清空其他 root；中途 `FindNextFileW` 非 `ERROR_NO_MORE_FILES` 不得提交完整 source；取消不得提交 partial snapshot。

本研究提出的工程採用門檻：

1. 任何變體必須先通過與 A 相同的 entry/error/cancel/reparse 結果。
2. 單執行緒 API 變體若 median wall time 少於 10% 改善，就不為它增加額外結構；C 若只有微小差異，仍可因為是同一個 Win32 contract 的低風險替換而保留，但不可宣稱有固定效能收益。
3. 平行 E2/E4 必須至少改善 10% median wall time，且不增加 idle app-owned threads、無界 handles、peak memory 或 shutdown latency；否則採用 A/B/C 的單 worker 版本。
4. 任一 USN／Native／Shell 方案若沒有同時通過初次建立、journal／API failure fallback、root scope、reparse、權限、取消與 Windows 10 22H2 相容性，就不進 production。

## 最終決策

對 NR-194 的最小、有效方向是：**先維持目前 walker 的整體設計，只量測 `FindFirstFileExW(FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH)`；不做平行、不做 USN、不做 Shell raw-folder enumeration、不做 Native API、不做第二套 scan cache。**

這個決策與 repo 現有證據一致：原生 walker 已約 0.2 秒，真正的 122 倍瓶頸已由 NR-194 移除；任何更大的架構變更都要先以同一個 Release benchmark 證明它解決了新的瓶頸。[`NR-194` 量測交接](../work-items/NR-194-user-folder-drop-readability-probe.md#L129-L135)；[`NR-137` walker 邊界](../work-items/NR-137-directory-walker-module.md#L56-L74)

## 官方來源索引

- [FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)
- [FINDEX_INFO_LEVELS](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_info_levels)
- [FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)
- [FindClose](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findclose)
- [ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)
- [Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
- [Reparse points](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-points)
- [Reparse Point Tags](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-tags)
- [IShellFolder::EnumObjects](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-enumobjects)
- [IEnumIDList](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ienumidlist)
- [IShellFolder::GetDisplayNameOf](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-getdisplaynameof)
- [FSCTL_ENUM_USN_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_enum_usn_data)
- [FSCTL_READ_USN_JOURNAL](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_read_usn_journal)
- [USN_RECORD_V2](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-usn_record_v2)
- [Change Journal Records](https://learn.microsoft.com/en-us/windows/win32/fileio/change-journal-records)
- [Walking a Buffer of Change Journal Records](https://learn.microsoft.com/en-us/windows/win32/fileio/walking-a-buffer-of-change-journal-records)
- [ZwQueryDirectoryFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwquerydirectoryfile)
- [ZwQueryDirectoryFileEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwquerydirectoryfileex)
- [Using Nt and Zw Versions of the Native System Services Routines](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-nt-and-zw-versions-of-the-native-system-services-routines)
