# NR-159 — design-spec 措辭修正兩處：§3.1 排序描述、§10.2 cache 格式描述

Phase 0 · Docs · Depends on: —（文件同步；NR-125/126 同型，碼對 spec 錯）

- Source: `docs/design-spec.md` §3.1／§4.2／§10.2（碼對 spec 錯的兩處殘餘）、
  NR-094（spec 是唯一真相來源，措辭不得與碼漂移）
- Origin: 2026-08-10 第十四次稽核第 1 輪（spec 軸，LOW）與第 2 輪（確認為唯二
  措辭級 spec-bug）。主 Agent 已對照碼驗證。
- Priority: **LOW**——純文件兩行；不留著的理由是「下次稽核又要重報」。

## Why

兩處 spec 措辭與碼不符，兩邊都是**碼對 spec 錯**（改碼會破壞既有資料或無意義）：

1. **§3.1**（`design-spec.md:88`）：
   `- 依啟動次數與最近使用時間排列未釘選常用 App。`
   對照 §4.2（binding，`:142`）：「未釘選常用項目，依最後一次啟動時間排序，最近
   啟動者在最前」且「此處**不**使用 usage_score」。碼符合 §4.2
   （`usage_store.cpp:203-206` 依 `last_launch_utc` 排序）。修法：刪「啟動次數與」，
   改為 `- 依最近使用時間排列未釘選常用 App。`
2. **§10.2**（`design-spec.md:753`）：
   `- catalog.cache：可選的版本化二進位 cache，只用於加速，不是真實來源；讀取錯誤可直接刪除並重建。`
   實際是 schema=2 UTF-8 文字 TSV（`catalog_cache.cpp`：tab 分隔 7 個 escaping 欄位）。
   icons.cache 的「二進位」是對的（`:754`，不誤改）。修法：該行改為
   「版本化 UTF-8 文字 TSV cache」。

## Decisions already made — do not reopen

1. 只改這兩行字面；其餘條文不動。
2. §10.2 的「讀取錯誤可直接刪除並重建」維持不動（與 `.corrupt` 候選的決策正交：
   本 item 只改格式形容詞）。
3. 不開 code change；純文件。

## Binding constraints — quoted, do not go looking for them

`docs/work-items.md`（§候選）：

> 兩處都是改 spec 一行，與 NR-125/126 同型，下次文件同步批次一起做。

## Files to read and trace first

- `docs/design-spec.md`：`:88`（§3.1）、`:142-144`（§4.2 binding，不要誤改）、
  `:753`（§10.2）。
- `src/catalog/catalog_cache.cpp`（確認 TSV 描述與 kSchemaVersion=2 一致）。

## Scope

1. §3.1 那行改為「依最近使用時間排列」。
2. §10.2 catalog.cache 行改為「版本化 UTF-8 文字 TSV cache」。
3. 驗證：grep 改後措辭與 §4.2/碼一致。

## Non-goals

- 不動 §4.2、不動 icons.cache 行、不開 `.corrupt` 候選。
- 不更新 release-evidence/testing.md（無測試影響）。

## Acceptance

1. `rg "啟動次數與最近使用時間"` 在 spec 零命中。
2. `rg "版本化二進位"` 在 §10.2 只剩 icons.cache 行。
3. git diff 只含兩行。

## Agent checks

```powershell
rg -n "最近使用時間|版本化二進位" docs/design-spec.md
# expect: §3.1 為「依最近使用時間排列」；「版本化二進位」僅出現在 icons.cache 行
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄兩處改寫後全文、grep 結果、與 §4.2／碼的對應關係。

### 交接區（2026-08-11，實作完成）

本 item 為純文件改動，只改了 `docs/design-spec.md` 兩行與本交接區、tracker（NR-159 行
`ready` → `done`）。

**§3.1（`docs/design-spec.md:88`）改後全文：**

> - 依最近使用時間排列未釘選常用 App。

（刪「啟動次數與」；與 §4.2 binding「依最後一次啟動時間排序，不使用 usage_score」
及 `usage_store.cpp:203-206` 一致。）

**§10.2（`docs/design-spec.md:753`）改後全文：**

> - `catalog.cache`：可選的版本化 UTF-8 文字 TSV cache，只用於加速，不是真實來源；讀取錯誤可直接刪除並重建。

（格式形容詞改為「版本化 UTF-8 文字 TSV cache」，對應 `catalog_cache.cpp` 的
`kSchemaVersion=2` 7 欄 escaping TSV；「只用於加速…重建」維持不動。`icons.cache`
（`:754`）與 §4.2（`:142-144`）未動。）

**grep 驗證：** `rg "啟動次數與最近使用時間"` 在 `docs/design-spec.md` 零命中；
`rg "版本化二進位"` 在 §10.2 只剩 icons.cache 行（`:754`）。
