# NR-185 — 冷啟動 cache 列不可啟動的誤導訊息：換成誠實的準備中提示

Phase 3 · Launch UX · Depends on: NR-113（已 done，安全 gate 不動）；NR-116（已 done）

- Source: `docs/design-spec.md` §FR-008（現文 `design-spec.md:403`：「啟動時先載入有效的 Catalog cache，立即提供舊結果」）；`docs/work-items/NR-113-catalog-cache-launch-provenance.md`
- Origin: 2026-08-12 第十七次全 repo 稽核（claude 報告 I-5；codex 報告 M4）
- Priority: **HIGH**（冷啟動後第一個動作就撞上，且錯誤訊息誣賴 App）

## Why

冷啟動時 `catalog_cache.cpp:166` 把每一列設成 `launch_verified = false`；`shell_launch.cpp:8-10` 對 `!launch_verified` 直接回 `{false, ERROR_INVALID_PARAMETER}`；`main.cpp:901-902` 把 `ERROR_INVALID_PARAMETER` 映射成 `"The app entry is invalid."`（`kReasonInvalid`）。

使用者實際看到的：開機後立刻按 Alt+Space（§FR-008 的賣點就是「立即提供舊結果」）、按 Enter → 面板不隱藏、跳出 `Failed to launch "Firefox". The app entry is invalid.`，同時觸發一次多餘的全來源 rebuild。等背景 rebuild 完成（Start Menu 走完整棵樹可達數秒）之後同一個 App 才能啟動。

NR-113 的安全立場（cache 不是真實來源，不該驅動 Shell）**保持不動**——本 item 不重開該決策，只修使用者體驗層：顯示得到、按不動、還被罵「entry 壞了」是冷啟動的預設體驗，錯誤訊息在說謊。

**Claude 報告的三選項**：本 item 採**選項 1**（最小改動、不動安全邊界、直接消掉誤導訊息）；選項 2（待啟動佇列，rebuild 完成後自動啟動）與選項 3（重新檢視 NR-113 的信任模型）不在此範圍，若要開另立 item。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-008（現文 `design-spec.md:403`）：

> 啟動時先載入有效的 Catalog cache，立即提供舊結果；再背景完整建立一次最新 Catalog。

`docs/work-items/NR-113-catalog-cache-launch-provenance.md`（決策保持）：

> cache-only／未經目前來源驗證的項目不得進入 Shell launch。

## Files to read and trace first

- `src/catalog/catalog_cache.cpp` — `launch_verified = false`（:166）與註解（schema 不序列化 launch_verified，§10.2）。
- `src/launch/shell_launch.cpp` — `!launch_verified → ERROR_INVALID_PARAMETER`（:8-9）。
- `src/app_host/main.cpp` — `ActivateRow`（:1052-1103，含 IsMissingPin 守門、launch-failure refresh gate `:1071-1076`、錯誤對話框 `:1082-1086`）、`kReasonInvalid`（:179）、`LaunchErrorReason`（:895-905）。
- `docs/work-items/NR-113-catalog-cache-launch-provenance.md` — 信任模型決策（不得改）。

## Scope

1. `ActivateRow` 對 `!entry.launch_verified` 走**專屬分支**（在 `LaunchEntry` 之前判斷，或讓 `LaunchResult` 帶可分辨的原因）：
   - 顯示誠實訊息（英文 UI）：例如 `"Still preparing apps — try again in a moment."`（沿用 `dialog_strings` 集中管理的既有形式，新增常數）。
   - **不**觸發 launch-failure refresh（背景 rebuild 本來就在跑；`g_launch_failure_refresh` 不應被此路徑消耗）。
   - 面板保持顯示（與失敗時行為一致）；不記錄 launch usage。
2. 正常 rebuild 完成後（`launch_verified` 的 snapshot）同一個 App 立即可以啟動——此路徑現況已正確，不需改。
3. 確認 `ERROR_INVALID_PARAMETER` 的既有映射仍只服務「真正的壞 entry」（若 cache 分支不再走它，該映射的文字維持現況即可；不要順手改）。
4. 測試：若「launch_verified=false → 專屬訊息且不觸發 refresh」可抽純函式，沿用 `nimblerun_shell_launch_test`／`nimblerun_panel_model_test` 的既有形狀加 focused 案例；否則以 sanity grep＋build 覆蓋（ActivateRow 是視窗層）。
5. 完成後更新本文件交接區與 `docs/work-items.md` 的 NR-185 列。

## Non-goals

- **不**重開 NR-113（不讓 cache 列驅動 Shell launch）。
- 不做選項 2（待啟動佇列）與選項 3（信任模型重估）——記入 `docs/work-items.md` §候選區（若有此節）或本文件交接區即可。
- 不改背景 rebuild 的時序／節流。
- 不改 IsMissingPin placeholder 的行為。

## Acceptance

- 冷啟動、rebuild 完成前按 Enter：面板保持顯示、出現「Still preparing apps」類訊息、**不**彈 `The app entry is invalid.`、不觸發多餘 refresh；rebuild 完成後同一 App 可正常啟動。
- 暖機路徑（verified entry）的行為逐位元不變。
- 訊息為英文且經既有字串集中管理。
- Release build 無 error／新增 warning；CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "launch_verified|kReasonInvalid|Still preparing" src/app_host/main.cpp src/launch/shell_launch.cpp src/catalog/catalog_cache.cpp
```

驗證：build 無 error／新增 warning；CTest 全 Passed；`ActivateRow` 對未驗證列走專屬分支（grep 可見），`kReasonInvalid` 不再從 cache 路徑到達。

## 交接區

（實作者填寫：分支位置與形狀（ActivateRow 或 LaunchResult 層）、訊息常數名稱與文字、refresh gate 不被消耗的確認、暖機路徑不變的證據、測試案例、build／CTest 證據）
