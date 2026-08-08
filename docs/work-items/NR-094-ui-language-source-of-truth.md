# NR-094 — 統一 MVP UI 語言規格，消除 English／雙語衝突

Phase 0 · Depends on: —

- Source: `docs/design-spec.md` §NFR-006、`docs/development.md` §UI language、`AGENTS.md` §Language rules
- Origin: 2026-08-08 第八次全 repo 稽核（規格與 agent contract 互相矛盾）
- Priority: MEDIUM（會讓後續 UI／字串工作在沒有產品決策時選錯方向）

## Why

產品規格 `docs/design-spec.md` §NFR-006 寫著 MVP UI「至少提供英文與繁體中文」，
但 repository 的兩個執行規則明定所有 NimbleRun application UI text 必須是 English：
`AGENTS.md` §Language rules 與 `docs/development.md` §UI language 都如此描述。現有
resource、dialog string 與面板文字也都是英文，且目前沒有 locale selector、翻譯資源
格式或本地化測試。

這不是要求現在新增翻譯；它是三份 binding-looking 文件對同一個 MVP 行為給出不同
答案。未先解決 authority，下一個 UI item 可能違反 repository contract，或反過來
無視產品 source of truth。

## Decisions needed

推薦先採 **MVP English-only**：目前 `AGENTS.md`、`docs/development.md`、程式現況
一致，能避免在沒有 locale 設計與翻譯資產時擴大產品範圍；若產品確實需要雙語，再
由本 item 明確決定 locale／fallback／測試邊界後另開實作 item。這是建議，不替產品
Owner 代做不可逆的需求決策。

## Binding constraints — quoted

`docs/design-spec.md` §NFR-006：

> MVP UI 至少提供英文與繁體中文，若時程不足，程式碼仍須集中管理字串，不可散落硬編碼。

`AGENTS.md`：

> NimbleRun application UI text must be English.

`docs/development.md`：

> All user-visible NimbleRun UI text is English.

## Files to read and trace first

- `docs/design-spec.md:542-549` — NFR-006 localization requirement。
- `docs/development.md:31-43` — development UI language contract。
- `AGENTS.md:9-14, 28` — repository language rules and centralized strings rule。
- `src/resources/`、`src/app_host/dialog_strings.h`、`src/app_host/settings_dialog.cpp`、
  `src/app_host/main.cpp` — current string ownership and user-visible UI scope；只盤點，
  不在本 item 修改 code。
- `docs/work-items.md` — confirm future UI items cite the resolved policy。

## Scope

1. 由產品 Owner 決定 MVP 的唯一 UI language policy；採 English-only 時，在
   `docs/design-spec.md` NFR-006 加上明確 MVP exception／改寫，並保留
   `docs/development.md`／`AGENTS.md` 一致文字。
2. 若決定雙語，只在文件中補齊最小 contract：locale 來源、預設／fallback、字串資產
   位置、驗收語言範圍，並另開實作 item；本 item 不實作翻譯。
3. 在 tracker 的決策紀錄留下選擇與日期，讓後續 work item 只引用一個 authority。

## Non-goals

- 不新增翻譯、locale picker、資源檔、字串抽取工具或 UI code。
- 不重寫現有英文 UI，也不在未決策前批量替換文件語言。
- 不把對話語言（Traditional Chinese）與 application UI language 混為一談。

## Acceptance criteria

1. `docs/design-spec.md`、`docs/development.md`、`AGENTS.md` 對 MVP application UI
   language 不再互相矛盾。
2. 文件明確指出後續 UI item 的唯一 authority、字串集中規則與若需雙語時的另案
   邊界；沒有新增 code 或 runtime dependency。
3. `docs/work-items.md` 的決策紀錄包含產品選擇、日期與對後續工作的影響。

## Agent checks

```powershell
rg -n "MVP UI|UI language|application UI text|user-visible NimbleRun UI|繁體中文" AGENTS.md docs/design-spec.md docs/development.md
# expect: one resolved MVP policy, not contradictory English-only and bilingual mandates

git diff --check
git diff --name-only
# expect: documentation files only
```

## 交接區

採用推薦的 **MVP application UI English-only** 政策。`docs/design-spec.md` §NFR-006
已改寫為唯一 authority，補上集中管理字串、對話／文件語言分離，以及未來雙語需另開
規格／實作 item 的邊界；`AGENTS.md` 與 `docs/development.md` 維持原 English-only
規則。`docs/work-items.md` 已記錄 2026-08-09 決策與後續影響。

Agent checks：rg 顯示三份文件不再互相矛盾，`git diff --check` 通過；變更僅文件，
未執行 build／CTest。未完成事項：無。
