# NR-030 — Spec amendment: persistent icon cache and relaxed resource budget

- Status: `done`
- Phase: 3
- Depends on: —
- Source: `docs/design-spec.md` §FR-009、§9 職責表（`icon_cache`）、§10.1、§10.2、§10.4、§NFR-001、§19.1

## Goal

本 item **只改文件，不改任何程式碼**。它把「decoded icon 持久化到磁碟」與「放寬待機資源預算」寫進 Spec，讓 NR-031～NR-037 有合法依據。Spec 是產品唯一真實來源；沒有這個 item，後續 item 會直接違反 §FR-009 與 §9 職責表明寫的禁止事項。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§9 職責表／§10.1／§10.2／§10.4／§NFR-001／§19.1、`docs/work-items.md`（使用方式與 Agent 交付規則）、`docs/work-items/NR-012-icons.md`、本文件。

## 背景（為什麼要改 Spec）

1. §9 職責表第 645 行對 `icon_cache` 明寫「不：**永久保存全部 bitmap**」；§FR-009 只允許「記憶體 LRU cache」。把 decoded 結果寫到磁碟因此是 Spec 變更，不是實作細節。
2. §FR-009 第 393 行允許「第一幀 fallback，再非同步更新」，但實務上每次登入後第一次開窗、以及每次出現新的一頁項目，都必然閃一次 fallback。grid 狀態（§4.2、一頁 24 格）同時顯示的格子比清單多，閃動面積更大。
3. 現況 `src/app_host/main.cpp` 的 `LoadVisibleIcons()` 在 **UI thread 同步**呼叫 Shell，違反 §19.1 第 1031 行「不能讓 UI 等待圖示」。這是既有落差，不是本次新增。
4. 冷成本量級：`SHCreateItemFromParsingName` ＋ `IShellItemImageFactory::GetImage` 每顆約 1–10 ms（packaged app 或有 shell extension 介入時更久），24 顆約 50–200 ms；讀一個約 4 KiB 的自家 PNG 約數十–數百 µs。差兩個數量級，這是持久化要買的東西。系統 icon cache 省掉的是「找到圖」，省不掉「建 COM、載 DLL、抽資源、縮放、解碼上傳」。

## 已確認的產品決策（由使用者逐題確認）

- 採用**磁碟持久化 decoded bitmap**（而非只做記憶體 LRU ＋ 預熱）。
- **不讀取 explorer 的 `iconcache_*.db`**：無公開 API、格式未文件化且隨 build 變動、explorer 持有檔案鎖。
- **不引入 SQLite 或任何 DB**（§10.2 明文禁止，且救不到磁碟往返）。
- 磁碟只存**標準尺寸**，畫面用 D2D 於繪製時降尺寸（降尺寸為 GPU 取樣，成本可忽略）。
- 尺寸階梯為 **48 / 96 / 256**，取三者中最小的 ≥ 需要的實體像素。96 不是 Shell 原生資源尺寸（由 256 縮得），保留它的理由是**磁碟與解碼成本**（96px PNG 約 3–8 KiB，256px 約 20–40 KiB，差約 5 倍），而 150% 縮放（多數筆電出廠預設）下 40 DIP grid 需要 60 px，正落在此層。
- 快取鍵**移除 DPI 與畫面尺寸**，改為 `stable ID + variant`。DPI 變更視為罕見事件，允許整批重新取得，換取簡單好維護。
- **待機工作集預算放寬**：由 20 MiB 提高到 60 MiB。
- **grid 圖示維持 40 DIP**，不改為 48 DIP（高 DPI 機器兩者都得縮放，1:1 只有 100% 機器享受得到，不值得動 §4.3 的版面常數）。
- 記憶體 LRU 上限由固定 64 改為推導值（見 NR-031）。
- 單一 pack 檔（非每顆圖一個檔），mmap 讀取、append 寫入；**必須有毀損偵測與逐筆復原**（見 NR-033）。

## Scope（全部為 `docs/design-spec.md` 的文字修改）

### 1. §FR-009 圖示

- 「快取鍵由 stable ID、要求尺寸及 DPI 組成」→ 改為：「快取鍵由 stable ID 與標準尺寸 variant（48／96／256 實體像素）組成，**不含 DPI 與畫面版面尺寸**。取用時選三者中最小的 ≥ 需要的實體像素，並於繪製時由 renderer 降尺寸至目標矩形。DPI 或版面變更時允許重新取得，不做尺寸遷移。」
- 「記憶體 LRU cache 預設上限 64 個 decoded bitmap」→ 改為：「記憶體 LRU cache 上限由 `釘選項目數 + recent_count 設定值 + 一頁格數（24）` 推導，涵蓋預熱集與一次搜尋結果的工作集，避免搜尋把預熱好的釘選項目擠出快取。」
- 新增：「decoded 圖示額外持久化於本機單一 pack 檔（§10.1、§10.2），使登入後第一次顯示面板也能直接呈現真實圖示。該檔為**可完全重建的加速器**，任何毀損或版本不符都必須能在不損失使用者資料的前提下降級運作。」
- 新增：「取得圖示必須在背景 worker 執行；UI thread 不得等待 Shell、不得等待磁碟、不得等待解碼。」
- 保留不動：lazy loading、Catalog 不預解碼、fallback、不可造成格位重排。

### 2. §9 職責表（第 645 行）

`| \`icon_cache\` | 非同步取得、LRU、DPI key | 永久保存全部 bitmap |`
→ 改為
`| \`icon_cache\` | 背景取得、記憶體 LRU、標準尺寸 variant key、可重建的本機 pack 快取 | 保存全 Catalog 的圖示、以快取作為真實來源、原地覆寫使用者資料 |`

界線要寫清楚：允許持久化的是**曾經被顯示過**的項目（有界、受預算淘汰），不是整份 Catalog；§FR-009「Catalog 不預解碼所有圖示」不變。

### 3. §10.1 目錄

在 `catalog.cache` 之後新增一行 `├── icons.cache`（維持樹狀縮排正確）。

### 4. §10.2 格式選擇

在 `catalog.cache` 條目之後新增：

- `icons.cache`：版本化二進位 pack 檔，保存曾顯示過項目的 decoded 圖示（編碼為 PNG）。只用於加速，不是真實來源。採固定大小的雙份檔頭與固定容量索引區，payload 以 append 方式寫入；**每筆索引項與每筆 payload 各自帶 CRC32**，單筆毀損只丟棄該筆，不丟棄整檔。檔頭 magic／版本不符或雙份檔頭皆不可讀時整檔刪除重建。compaction 走 `.tmp` ＋ replace。

同時在「所有持久資料寫入應先寫 `.tmp`，flush 成功後以 replace 方式提交」之後補一句界定範圍：

> 此規則針對使用者資料（設定、pins、使用紀錄）。可完全重建的快取（`catalog.cache`、`icons.cache`）允許以 append 方式就地追加，但 compaction 與整檔重寫仍須走 `.tmp` ＋ replace。

### 5. §10.4 Migration

補一句：快取類檔案遇到較新且不支援的 schema version 時，**不覆寫原檔**、停用該快取（僅以記憶體 LRU 運作），且**不顯示錯誤提示**（快取降級對使用者不可見，不屬於需要通知的失敗）。使用者資料的既有規則不變。

### 6. §NFR-001 資源預算

| 指標 | 舊目標 | 新目標 | 舊阻擋 | 新阻擋 |
|---|---:|---:|---:|---:|
| 待機工作集 | 20 MiB | **60 MiB** | 35 MiB | **80 MiB** |
| 待機 Private Bytes | 15 MiB | **50 MiB** | 30 MiB | **70 MiB** |
| 面板顯示、20 個圖示完成後工作集 | 35 MiB | **75 MiB** | 55 MiB | **100 MiB** |
| 待機執行緒數 | 4 | **5** | 8 | 9 |

新增一列磁碟預算：

| `icons.cache` 檔案大小 | ≤ 32 MiB | > 48 MiB |

執行緒數 +1 的理由要寫進表格下方說明：新增一條**常駐的**圖示 worker（負責取得、解碼、pack 檔讀寫與 idle flush），不為每個圖示建 thread（§9 第 658 行不變）。§NFR-001 下方既有的量測說明段落補一句：`icons.cache` 以 mmap 讀取，payload 不常駐，工作集只反映實際觸碰過的 page。

### 7. §19.1 驗收/約束清單

第 1031 行「圖示延遲載入並限制 cache；不能讓 UI 等待圖示」保留不動，並補一句「圖示取得與快取讀寫一律在背景 worker，UI thread 只接收純值結果」。

### 8. `docs/performance-baseline.md` 與 `docs/testing.md`

同步更新其中引用到舊 20／15／35 MiB 與執行緒數 4 的數字，避免出現第二份互相矛盾的預算來源。若這兩份文件只是引用 §NFR-001 而未複製數字，則不需改，但必須在交接區明確說明已檢查。

## Non-goals

- 不改任何 `src/` 或 `tests/` 檔案。
- 不改 §4.2／§4.3 的版面常數（grid 圖示維持 40 DIP、清單維持 30 DIP）。
- 不改 §10.3 Stable ID（icon 快取鍵沿用既有 stable ID，零遷移）。
- 不新增設定項（快取不可由使用者關閉或調整大小）。
- 不改 `catalog.cache` 的既有格式或行為。
- 不回頭修改 NR-012 或任何既有 item 文件。

## Acceptance

- 上述 8 節修改全部落在 `docs/design-spec.md`（第 8 節可能另涉兩份文件），且 repo 內搜尋不到殘留的「DPI key」「上限 64 個 decoded bitmap」「待機工作集 ≤ 20 MiB」等舊敘述。
- §FR-009 讀完後可獨立回答：快取鍵是什麼、有哪幾個尺寸、誰負責取得、毀損時怎麼辦。
- §9 職責表的「不」欄仍然禁止「保存全 Catalog 的圖示」，即持久化範圍有界這件事在 Spec 內可查證。
- §10.2 的 append 例外只涵蓋可重建快取，使用者資料的原子寫入規則字面上未被放寬。
- `docs/work-items.md` 的 Item 總覽、Dependency lanes 與計畫決策紀錄已加入 NR-030～NR-037。
- 不需建置；`ctest` 仍應維持既有全綠（本 item 未動程式碼，跑一次確認未誤改）。

## Agent checks

```powershell
git diff --stat
ctest --test-dir build --output-on-failure
```

`git diff --stat` 應只包含 `docs/` 下的檔案。

## 交接區

- Start: 2026-08-05。依「必讀」讀完 AGENTS.md、docs/development.md、design-spec §FR-009／§9／§10.1／§10.2／§10.4／§NFR-001／§19.1、work-items.md、NR-012。確認 NR-031～NR-037 文件均已存在。
- Subagent scope: 只改文件。依「必讀」讀完，逐節套用 Scope 1～8 的文字修改，不得順手實作程式碼或建立 `src/icons/` 新檔。回報實際修改的章節、行號與有無發現 Spec 內互相矛盾之處。
- Result: 已完成。修改檔案：`docs/design-spec.md`（§FR-009、§9.1 職責表＋表格下方界線句、§10.1、§10.2、§10.4、§NFR-001、§19.1）、`docs/performance-baseline.md`（四項舊數字＋新增 `icons.cache` 磁碟行）、`docs/work-items.md`（Item 總覽 NR-030～NR-037、Dependency lanes、計畫決策紀錄）、`docs/work-items/NR-030-*.md`（status `done`＋交接區）。`docs/testing.md` 只引用 `performance-baseline.md` 未複製數字，不需改。`git diff --stat` 僅含 docs/ 檔案；ctest 19/19 全綠。未動任何 `src/`、`tests/`。
