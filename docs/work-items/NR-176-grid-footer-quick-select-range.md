# NR-176 — 格狀狀態 footer 快選指引固定顯示完整鍵序 `Alt+0~9`

Phase 3 · Panel UI / footer key hints · Depends on: NR-024, NR-029, NR-045

- Source: `docs/design-spec.md` §4.7（鍵序固定 `1 2 3 4 5 6 7 8 9 0`，指派前 10 個可見項目）、
  §4.9（footer 按鍵指引帶）
- Origin: 2026-08-11 使用者回報：格狀（empty query）空矩陣狀態下，matrix 下方
  的提示顯示 `Alt+1~3`，與實際綁定不符——格狀狀態的數字 0~9 全都是快選鍵。
- Priority: **LOW**——純視覺不一致；work-items.md 2026-08-07 第三輪稽核已記
  備查：「grid 模式 footer 顯示 `Alt+1~4` 但實際綁定繪製 10 格（視覺不一致，LOW）」。

## Why

格狀狀態下 §4.7 把鍵序 `1..9,0` 指派給「當前可見項目的前 10 個」，與視窗
可見列數（4 列）無關；格子右上角的數字方塊也對前 10 格（slot 0-9）各畫一個
（`main.cpp:1500-1515`，僅 Alt 按住時）。但 footer 的 `Alt+1~N` 盒沿用
NR-024 的清單公式 `min(ViewportRows(), kQuickSelectSlotCount) - 1`
（`main.cpp:1758-1764`），格狀下依 4 列視窗組出 `Alt+1~3`——空矩陣時同樣
顯示 `Alt+1~3`，而實際可用的快選鍵是 0~9 全部。使用者判定此提示誤導。

## Decisions already made — do not reopen

1. **格狀狀態（`Columns() > 1`）footer 快選盒固定顯示完整鍵序 `Alt+0~9`**
   （直接使用 `footer_strings::kAltZeroNine` 靜態字串），不隨 `ViewportRows()`
   縮減。理由：格狀的快選鍵序是完整 10 鍵（§4.7 指派前 10 格），footer 指引是
   「能力提示」而非狀態回報——與 `Scroll`／`PgUp` 常駐一致。範圍記法取
   `Alt+0~9`（表達 0~9 全部可用），不採 `Alt+1~0`（1 到 0 讀起來怪）。
2. **清單狀態維持 NR-024 公式**（`min(ViewportRows(), 10) - 1`）：清單只有
   可見列被綁定，現狀準確，不改。
3. **本 item 覆寫 NR-029 交接區的「footer 右側 `Alt+1~N` 指引沿用 NR-024
   公式（grid 下顯示 `Alt+1~4`）」**：格狀狀態改為完整鍵序，清單不變。
4. 不更動 per-cell 數字方塊（格子右上角、僅 Alt 按住時顯示）、不更動
   `kHoldAltHint` 分支、不更動任何版面常數。
5. 不新增測試：Render 內逐幀組字串，NR-029 明文無測試 seam；本改動為單一
   三元分支，屬 trivial 邏輯（AGENTS.md「New non-trivial logic needs one
   focused runnable test」不適用）。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.7：

> `Alt` ＋數字 | 直接啟動對應的可見項目。鍵序固定為 `1 2 3 4 5 6 7 8 9 0`，依序指派給當前可見項目的前 10 個；沒有對應項目的數字不綁定

`docs/design-spec.md` §4.9：

> 數字快選指引顯示於對應項目上：清單狀態在列最右方，寬度固定且常駐佔位…格狀狀態在格子右上角，僅在按住 `Alt` 時顯示；未按住時 footer 右側以一句灰色說明文字取代 `Alt+1~N` 指引群組。

`AGENTS.md`：

> UI strings are English and should be centralized when more than one screen needs them.
> Update the relevant documentation when behavior changes.
> Keep changes scoped to the requested task.

## Files to read and trace first

- `src/app_host/main.cpp:1745-1771`（footer 指引帶 NR-024 else 分支：`last_slot`
  公式與 `kAltOnePrefix` 組字串）、`:1500-1515`（格狀 per-cell 數字方塊，
  slot 0-9，證實綁定不隨列數縮減）、`:2255-2284`（`WM_SYSKEYDOWN` 的 Alt+digit
  綁定走 `RowForVisibleSlot`）。
- `src/ui/quick_select.h`（`kQuickSelectSlotCount`=10、`QuickSelectLabelForSlot`：
  slot 9 → `L"0"`）。
- `src/app_host/panel_model.h`（`Columns()`：query 空 → `grid_columns_`）。
- `docs/work-items/NR-029-empty-state-grid.md`（交接區「grid 下顯示 Alt+1~4」
  決策——本 item 覆寫）。

## Scope

1. `src/app_host/main.cpp` footer else 分支：格狀（`g_model && g_model->Columns() > 1`）
   直接使用 `footer_strings::kAltZeroNine`（`L"Alt+0~9"`）；清單維持既有
   `kAltOnePrefix` ＋ `std::min(ViewportRows(), kQuickSelectSlotCount) - 1`
   組字串。同步更新上方 NR-024 註解，指出格狀例外。
2. `docs/design-spec.md` §4.9 數字快選條補一句：格狀狀態 footer 快選指引顯示
   完整鍵序 `Alt+0~9`，不隨可見列數縮減。

## Non-goals

- 不改清單狀態的 `Alt+1~N` 公式與行為。
- 不改 per-cell 數字方塊、`kHoldAltHint` 分支、`WM_SYSKEYDOWN`／`WM_SYSCHAR`、
  `src/ui/quick_select.h`、任何版面常數、`footer_strings`（`kAltOnePrefix` 維持）。
- 不新增測試 seam／測試執行檔。

## Acceptance

1. 格狀狀態（含空矩陣、按住 Alt 時）footer 快選盒顯示 `Alt+0~9`，不隨視窗
   高度／列數縮減。
2. 清單狀態顯示內容與改動前相同（如 `Alt+1~8`）。
3. Release build 零新增 warning；CTest 全綠（數量不變）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -A10 "kAltOnePrefix" src/app_host/main.cpp
# expect: else 分支內格狀（Columns()>1）直接使用 kAltZeroNine，清單才用 kAltOnePrefix
```

完成後在文件底部補齊本 item 的 Handoff 交接備註。

## Handoff

實作者需記錄改動位置與 build／CTest 結果。

## 交接區

- 改動檔案：`src/app_host/main.cpp`（`footer_strings` 新增 `kAltZeroNine`；
  footer else 分支改分支：格狀 `Columns() > 1` → 直接顯示 `kAltZeroNine`
  （`Alt+0~9`），清單 → 既有 `kAltOnePrefix` ＋
  `min(ViewportRows(), 10) - 1` 組字串；NR-024 註解同步說明格狀例外）、
  `docs/design-spec.md` §4.9（數字快選條補「格狀 footer 固定顯示 `Alt+0~9`，
  不隨可見列數縮減；清單依當前可見列數組出」句）。
- 格式決策：使用者回饋「`Alt+1~0`（1 到 0）讀起來怪」，範圍記法改為
  `Alt+0~9`——與 §4.7「數字 0~9 都是快選鍵」的語意一致，字串長度不變
  （7 字元），`kFooterWideKeyBoxWidthDip` 無需調整。
- Build／CTest（Release x64, LLVM-MinGW + Ninja）：設定與 build 成功；
  全數 31/31 tests passed。唯一 warning 為 `main.cpp:1410` unused variable
  `target_size`，NR-174 交接區已記錄為改動前即存在，非本 item 新增。
- Sanity grep：`rg -n -A10 kAltOnePrefix src/app_host/main.cpp` 顯示
  格狀分支直接使用 `kAltZeroNine`，清單分支才用 `kAltOnePrefix`，
  符合 Acceptance 1；清單分支維持 `min(ViewportRows(), kQuickSelectSlotCount) - 1`。
- 偏差：無（使用者回報的「empty 空矩陣顯示 Alt+1~3」即格狀公式的縮減結果，
  本改動後格狀一律 `Alt+0~9`，含空矩陣）。
- 未 commit。
