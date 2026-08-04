# NimbleRun Work-item Planning Requirements

## Goal Intention

將既有 `docs/design-spec.md` 拆成小型、可獨立交付與驗證的 Agile work items，讓低階 Agent 能依單一文件完成實作。`design-spec.md` 是唯一原始產品 Spec，本次不修改它。

## Scope Note

本文件只記錄拆分計畫所需的已確認需求與決策；產品行為仍以 `docs/design-spec.md` 為準。若後續要改變產品行為，直接修正原始 Spec，再同步調整受影響的 work items。

## Confirmed Requirements

### End-user outcomes

- 使用者可從 Portable ZIP 執行 NimbleRun，不需額外 Runtime、管理員權限或網路。
- 同一使用者工作階段只保留一個 NimbleRun；再次執行會喚醒既有面板後退出。
- 使用者可用 `Alt+Space` 喚起或隱藏面板；快捷鍵衝突時 NimbleRun 拒絕新設定並只提醒一次，不覆寫 OS 或其他程式。
- 空白搜尋時，使用者看到最多 20 個最近執行的 App，依最後執行時間由新到舊排序；不足時不以字母排序 App 填滿。
- 搜尋文字只會得到可啟動的 App，不混入檔案、網頁、設定、計算器、AI 或網路搜尋結果。
- 第一個搜尋結果可被選取但不會自動執行；使用者能以鍵盤選取並按 `Enter` 啟動。
- 第一個可交付的垂直切片可使用簡單列表，項目包含 Icon、App 名稱與有效路徑；matrix 延後為獨立視覺 item。
- 成功啟動後面板依設定隱藏並更新最近執行紀錄；啟動失敗時面板保持顯示並給出簡短錯誤。
- 封裝 App 沒有有效檔案路徑時不顯示「開啟檔案位置」；只有能提供有效路徑時才顯示。
- UI 文字採英文；規格、開發與測試文件可採繁體中文。
- 設定、釘選與使用紀錄在重新啟動後可恢復；損壞資料採安全預設，不使程式崩潰。
- UI 須保留鍵盤操作、不同 DPI、深淺色、高對比與離線使用的產品要求；人工畫面驗證不列入本次 Agent work-item 追蹤。

### Maintainer / manager outcomes

- 維護者可從一份總覽看到所有 item 的狀態、依賴、來源 Spec、驗收條件與測試證據連結。
- 每個 item 只有一個主要可觀察成果，且有明確的非目標與交接備註。
- 每個 item 都能由 Agent 透過建置、命令、自我檢查或測試完成驗證；不要求 Agent 操作 App 視窗。
- 維護者能識別 `planned`、`ready`、`in_progress`、`blocked`、`done`、`deferred` 的 item 與阻塞原因。
- 維護者能看到待機 CPU／記憶體、喚出速度、搜尋延遲與長時間穩定性的自動化證據；超過阻擋門檻的項目不得標成完成。
- 維護者能確認 work items 沒有引入檔案搜尋、網路、外掛、AI、雲端同步、管理員權限或第三方 Runtime。

## Resolved Decisions

- Work-item 粒度：一個可驗證成果，目標約半天至兩天；不按 class 或資料夾機械拆分。
- 開發方式：增量替換 Phase 0 probe；每個 item 完成後保持既有 build／CTest 可用。
- 空白面板：先做最近執行 App 的列表垂直切片，matrix 延後；不做拖曳排序。
- 最近清單：最多 20 個，依最後執行時間排序；不足時不補字母排序項目；無紀錄時顯示空狀態。
- 封裝 App 測試：Agent 驗證列舉與 Shell launch request；不可操控目標 App 的畫面。
- 狀態格式：使用六種簡單狀態，不建立額外審批流程。
- 文件位置：總覽為 `docs/work-items.md`，item SPEC 為 `docs/work-items/NR-xxx-*.md`。

## Remaining Uncertainties

沒有阻塞 work-item 拆分的未決事項。若發現實作需要改變產品行為，先回到 `docs/design-spec.md` 修正，再更新本文件與 item 文件。

## Brainstorm Coverage

需求腦暴涵蓋常駐／喚起、最近 App、搜尋、鍵盤與滑鼠、Catalog、Shell 啟動、封裝 App、設定、錯誤恢復、DPI／無障礙、效能與 work-item 追蹤。Manager role 的報告要求被限縮為狀態、依賴、測試證據與 release gate，沒有新增團隊流程。
