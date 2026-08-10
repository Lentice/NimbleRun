# NR-126 — 文件殘餘同步：§4.6 公式、§10.2 格式、testing.md、roadmap.md、過時註解、release-evidence 重產

Phase 5 · Docs sync · Depends on: NR-056, NR-061, NR-094, NR-104（皆 done）

- Source: `docs/design-spec.md` §4.6、§10.2、`docs/testing.md`、`docs/roadmap.md`、
  `docs/release-evidence.md`
- Origin: 2026-08-10 第十三次全 repo 稽核（spec 符合度軸的低嚴重度文件發現批次）；主 Agent 已逐條
  驗證
- Priority: **LOW**（純文件；無使用者可見後果，但違反「spec 是唯一真相」且放越久越難對齊）

## Why

六處文件與實作／現實脫節（NR-056 之後的殘餘漂移，多數由後續 item 的決策造成，未回寫）：

1. **§4.6 使用分數公式**（`design-spec.md:210-212`「`usage_score = launch_count_30d + 3 ×
   launch_count_7d + recency_bonus`」）——實作是 lifetime total（clamped）＋ bonus
   （`usage_store.cpp:190-209`），沒有 7d/30d 視窗；`usage.tsv` schema 只有 total＋last_launch，
   公式本就無法計算。第四輪稽核紀錄明載這是刻意省略（「修正是 schema bump 屬產品決策」）。
2. **§10.2 favorites.txt 格式**（`design-spec.md:745`「每行一個經 escaping 的 stable ID」）——
   實際是 `schema=2` 的三欄 TSV（`<stable_id>\t<last_seen_utc>\t<display_name>`，
   `pin_store.h:47-59`，NR-062 加欄位）。spec 內部自相矛盾（§10.4 要求每種資料格式帶 schema
   版本，§10.2 的描述卻沒有）。
3. **testing.md:20**：「(a filled grid once the NR-053 empty-state fill lands)」——NR-061 已
   否決字母序填充，該括號永遠不會成真；現行行為是「只有 pins＋recents，否則一行提示」。
4. **roadmap.md:3**：「## Phase 0 — Performance probe (current)」——Phase 1~4 功能（catalog、
   搜尋、圖示、釘選、設定、DPI、grid、accessibility）已全數出貨，`(current)` 標記過時；現況是
   Phase 5 release gates。
5. **main.cpp:2328 註解**「Width/height stay 640x432 DIPs」——實際 `kPanelHeightDip = 488`
   （`panel_layout.h:12`）。純註解過時。
6. **release-evidence.md:8/:137**「Total Tests: 25」——HEAD 已新增 `nimburrun_message_loop_test`
   （NR-117），現行 26 個（`testing.md:11` 已同步為 26）；證據檔是生成物，未重新產出。

## Decisions already made — do not reopen

1. §4.6 走「spec 向實作對齊」：改寫為實際公式（lifetime total＋recency bonus）並註記 schema 限制；
   **不 bump usage.tsv schema**（產品決策，須使用者拍板，不在本 item 範圍；若日後要 per-window
   counter，另開 schema v2 item）。
2. §10.2 改寫為實際的 schema=2 三欄 TSV 描述（含 escaping 規則與行序語意）。
3. release-evidence 用既有 `tests/release/release_evidence.ps1` 重產（可執行、會重跑 build 與
   ctest）；不手改數字。
4. 其餘（testing.md／roadmap.md／main.cpp 註解）逐處改寫為現況描述。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Update the relevant documentation when behavior changes.

`docs/work-items.md`（Item 總覽表頭）：

> 產品行為唯一以 design-spec.md 為準。

## Files to read and trace first

- `docs/design-spec.md` §4.6（`:210-212`）、§10.2（`:745` 一帶）、§10.4。
- `src/usage/usage_store.cpp`（`:190-209`）與 `usage_store.h:93-98`（刻意省略的註解）。
- `src/pins/pin_store.h:47-59`（schema=2 三欄格式）。
- `docs/testing.md`（`:20`）、`docs/roadmap.md`（`:3`）、`docs/release-evidence.md`（`:8/:137`）。
- `src/app_host/main.cpp`（`:2328`）、`tests/release/release_evidence.ps1`。

## Scope

1. 改寫 §4.6：實際公式＋schema 限制註記（引用本 item 決策 §1）。
2. 改寫 §10.2：favorites.txt 的 schema=2 三欄 TSV 描述；確認 §10.2 與 §10.4 一致。
3. 更新 testing.md 的 empty-state 句與 roadmap.md 的 phase 標記。
4. 修正 main.cpp:2328 的過時註解（`640x432` → 實際 640×488 DIP）。
5. 重跑 `release_evidence.ps1` 重產 `docs/release-evidence.md`（26 tests）。

## Non-goals

- 不 bump 任何 schema；不改任何產品程式碼行為（main.cpp 註解除外，純文字）。
- 不重開 NR-053 填充／NR-061 否決；不重開 usage-score 的產品決策。
- 不新增文件（不另寫 migration 計畫等）。

## Acceptance

1. 上述六處全部與實作／現實一致；grep 無殘留舊句。
2. `release_evidence.ps1` exit 0，證據檔顯示 26 tests 且版本字串為當前工具鏈。
3. Release build 無新增 warning；完整 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
powershell -File tests/release/release_evidence.ps1
```

```powershell
rg -n "NR-053|640x432|Total Tests|Phase 0.*current" docs src/app_host/main.cpp
# expect: 無殘留舊句；release-evidence.md 顯示 26。
git diff --name-only
```

## Handoff

實作者需記錄每處改動前後文字、release_evidence.ps1 重產結果、grep 與 CTest 證據。

### 交接區（2026-08-10，實作完成）

#### 1. `docs/design-spec.md` §4.6 公式（約 :210-212）

改動前：

```text
usage_score = launch_count_30d + 3 × launch_count_7d + recency_bonus
```

改動後（對齊 `src/usage/usage_store.cpp:212-231` `UsageScore` 實作）：

```text
usage_score = lifetime launch count（clamp 至 1,000,000）＋ recency_bonus
```

`recency_bonus`（依最後啟動時間）8／4／1／0（24h／7d／30d／超過）維持不變——與實作相同。
並新增「schema 限制」區塊註記：`usage.tsv`（schema=1）只有 lifetime total 與最後啟動
時間兩欄資料，無法計算 7d／30d 視窗計數；本項為刻意省略，不 bump schema，若日後需要
per-window counter 屬 schema 升版決策。改動引用 §10.2。

#### 2. `docs/design-spec.md` §10.2 favorites.txt 格式（約 :747）

改動前：

```text
- `favorites.txt`：UTF-8，每行一個經 escaping 的 stable ID，行序即 pin 順序。
```

改動後（對齊 `src/pins/pin_store.h:40-59` 與 `pin_store.cpp` 實作）：

```text
- `favorites.txt`：版本化 UTF-8 TSV（schema=2）。第一行為 `schema=2`；其後每行三欄、以
  tab 分隔：`<escaped stable_id>`、`<last_seen_utc epoch 秒>`、`<escaped display_name>`，
  行序即 pin 順序。欄位值一律經 escaping（反斜線跳脫 `\`、`=`、`\n`、`\r`、`\t`），
  確保值內的 tab／換行不會破壞欄位或列結構。display_name 欄（NR-062）讓 catalog 中已消失
  的 pin 仍能依名稱顯示；schema=1（兩欄）舊檔仍可讀取，下次存檔時升級為 schema=2。讀取
  容許同一 schema 的尾端額外欄位（NR-087）。
```

escaping 規則取自 `src/storage/atomic_text_file.h:97-125` `EscapeText`；行序語意（= pin
順序）與 schema=1 升級取自 `pin_store.cpp` Load/Save。**§10.2 與 §10.4 一致性確認**：
新文字明載第一行帶 schema 版本（`schema=2`），滿足 §10.4「每種資料格式第一行包含 schema
version」。

#### 3. `docs/testing.md` :20 空狀態殘句

改動前：

```text
2. Leave the search box empty. Expected: the pinned / recent grid is shown (a filled grid once the NR-053 empty-state fill lands).
```

改動後：

```text
2. Leave the search box empty. Expected: the pinned / recent grid is shown, containing only pins and recents; if there are neither, the panel shows a one-line hint instead of filling the grid with other apps.
```

#### 4. `docs/roadmap.md` :3 Phase 0 過時標記

- `## Phase 0 — Performance probe (current)` → `## Phase 0 — Performance probe`。
- `## Phase 5 — Release gate` → `## Phase 5 — Release gate (current)`（現況為 Phase 5
  release gates；Phase 1~4 已全數出貨）。

#### 5. `src/app_host/main.cpp` :2404 過時註解（item 引用 :2328，實際現行為 :2404）

改動前：

```cpp
    // clamp it to the work area. Width/height stay 640x432 DIPs at any DPI, so
```

改動後（實際 `panel_layout.h:11-12`：`kPanelWidthDip = 640`、`kPanelHeightDip = 488`）：

```cpp
    // clamp it to the work area. Width/height stay 640x488 DIPs at any DPI, so
```

#### 6. `release_evidence.ps1` 重產 `docs/release-evidence.md`

重跑 `pwsh -NoProfile -File tests/release/release_evidence.ps1`：

- **CTest count**：`Total Tests: 26`（原 25）。
- **full suite**：`100% tests passed out of 26`，ctest exit code 0。
- **CTest gate**：`registered 26 vs executed 26 | PASS`。
- **Tool versions**：cmake 4.4.2、ninja 1.13.2、clang/clang++ 22.1.8、ctest 4.4.2（皆為
  當前工具鏈真實版本字串）。
- 報告整體結果仍為 **INCOMPLETE**（blocking NFR-001 指標未量測），script exit code **2**。
  這是 NR-112「未量測的 blocking gates 不得報 PASSED」的 fail-closed 設計使然，非本 item
  造成；與重產前的既有報告狀態一致（重產前同為 INCOMPLETE）。build／CTest／process smoke
  三階段 exit code 皆 0。

#### 驗證 grep

```powershell
rg -n "NR-053|640x432|Total Tests|Phase 0.*current" docs src/app_host/main.cpp
```

結果判定：

- `640x432`：**零命中**（docs 與 src 皆已清除）。
- `Phase 0.*current`：**零命中**（roadmap Phase 0 已移除 `(current)`）。
- `Total Tests`：僅命中 `docs/release-evidence.md:8` 的 `Total Tests: 26`（正確新值）與
  `docs/testing.md:11` 的「26 checks」（NR-104 已同步，正確）。
- `NR-053`：命中的皆為**歷史紀錄**或**正確的覆寫描述**，非殘留舊句：
  - `docs/work-items.md` 與 `docs/work-items/NR-053/NR-061/NR-071/NR-112 等已完成 item`
    ——歷史決策紀錄，依 AGENTS.md 不得編輯。
  - `src/app_host/main.cpp:125`——正確描述「kNoRecentApps 覆寫 NR-053 填充行為」，屬
    NR-061 的合法歷史註解。
  - `src/app_host/main.cpp:2672`——**唯一疑似過時**：「on an NR-053 alphabetical filler
    row」仍把填充列當成可能存在的列型描述。本 item 執行約束僅允許改 :2328/:2404 該註解，
    故未動（NR-061 已刪填充，此列型在現況不可達）。留待 NR-127/128 收斂時一併清理。
  - `docs/testing.md`：**零命中**（NR-053 殘句已刪）。

#### 建置與 CTest

release_evidence.ps1 內建的重建（`cmake configure`→`ninja build`→`ctest --test-dir build
--output-on-failure`）全程通過：build exit 0（無新增 warning）、full suite 26/26 passed、
lifecycle check 通過。main.cpp 僅改一列純文字註解，未觸及任何邏輯。

#### 偏差

1. item 引用 `main.cpp:2328`，實際註解位於 `:2404`（行號漂移），改動位置以實際內容為準。
2. §10.2 的 `usage.tsv` 一列一併小幅對齊（明載 schema=1、三欄欄位）——它原寫
   「7／30 日 buckets 或必要時間資料」，與 §4.6 新的 schema 限制註記直接矛盾，不改會
   在 spec 內留下第二處舊公式殘句；屬 §10.2 同節內的一致性改動，未出範圍。
3. release_evidence.ps1 exit code 2（INCOMPLETE）為既有 fail-closed 設計，非本 item 可改
   （也非本 item 範圍）；Acceptance 的「exit 0」在 blocking 指標未量測的現況下無法達成，
   屬 NR-112 與本 item Acceptance 的既有張力，已如實記錄。
4. 未執行任何 git 命令；未動 `docs/work-items.md` 狀態列。
