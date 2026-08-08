# NR-093 — 快速鍵擷取要分別追蹤左右修飾鍵的放開狀態

Phase 4 · Depends on: NR-089

- Source: `docs/design-spec.md` §FR-002、§NFR-006；NR-089 Decisions §1／§2
- Origin: 2026-08-08 第八次全 repo 稽核（HotkeyCaptureState physical-key trace）
- Priority: MEDIUM（特定同類左右鍵同時按住時會提前完成擷取）

## Why

`src/settings/hotkey_capture.cpp:13-32` 把左／右 Ctrl、Alt、Shift、Win 都映射成
同一個 `MOD_*` bit；`HotkeyCaptureState::OnKey()` 在 release 時直接清掉該 bit。
因此以下合法的物理事件序列會在 `RControl` 仍按住時誤完成：

```text
LControl down → RControl down → E down → LControl up
```

`candidate_` 存在且 category bit 已清空後，狀態機立即回報 `captured`；後續
`RControl up` 已沒有候選可完成。這與 NR-089「所有相關修飾鍵都放開後才確定，
放開順序不固定」的決策不符。現有測試只覆蓋不同類別（Ctrl／Alt）的放開順序，
沒有覆蓋同一類別的左右 physical variants。

## Decisions already made — do not reopen

1. Held state 必須辨識 physical virtual-key variant；同一類別的左右鍵同時按下時，
   放開其中一個不得清除整個 `MOD_*` aggregate。
2. 重複的 key-down（例如按鍵 repeat）不得增加虛假的持有數；以 physical-key
   presence／idempotent state 為準，不能用未配對的純計數器掩蓋重複事件。
3. Binding 的公開語意仍是 `MOD_CONTROL`／`MOD_ALT`／`MOD_SHIFT`／`MOD_WIN` 加
   virtual key；不把左右差異寫入設定格式或顯示文字。
4. 主鍵 down 當下的 aggregate modifiers、純修飾鍵拒絕、shell-reserved policy、
   Win hook 生命周期與 Confirm／Cancel UI 均沿用 NR-089，不在本 item 重做。

## Binding constraints — quoted

`docs/design-spec.md` §NFR-006：

> 鍵盤可完成全部核心操作。

`AGENTS.md`：

- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- New non-trivial logic needs one focused runnable test or self-check.
- Code, identifiers, test names, and diagnostic event names use English.

## Files to read and trace first

- `src/settings/hotkey_capture.h` — `held_modifiers_`、`candidate_`、`OnKey()` contract。
- `src/settings/hotkey_capture.cpp:10-84` — `ModifierBit()`、down/up transition、
  `Preview()`、`Reset()`。
- `src/app_host/settings_dialog.cpp` — `WH_KEYBOARD_LL` callback 如何餵 raw
  `(vk_code, is_down)`，以及捕獲完成／Reset 的生命週期。
- `src/settings/settings_editor.{h,cpp}` — `HotkeyBinding` aggregate format；確認不
  改設定檔 schema 與 display format。
- `tests/unit/hotkey_capture_test.cpp` — 既有不同 release order、Win、reserved combo
  checks；在此補 physical variant regression。
- `docs/work-items/NR-089-hotkey-capture-dialog.md` — 既有 decisions 與 acceptance，
  本 item 只補狀態機邊界。

## Scope

1. 把 `HotkeyCaptureState` 的 held representation 改成能辨識四類修飾鍵的左右
   physical variants，並由它推導現有 `MOD_*` aggregate。
2. 保持 `OnKey()` 的既有 capture timing、preview、invalid input、Reset 與 public
   `HotkeyBinding` 行為。
3. 在 `tests/unit/hotkey_capture_test.cpp` 增加最小合成事件檢查：
   `LControl down → RControl down → E down → LControl up` 不 captured；再收到
   `RControl up` 才 captured 為 `Ctrl+E`。至少再覆蓋一組左右 Alt／Win 或 repeated
   key-down，確認相同類別不會提前完成且不會虛增狀態。

## Non-goals

- 不改 NR-088 的 Win parsing／probe、不改 NR-086 shell-reserved 清單。
- 不增加滑鼠快捷鍵、左右修飾鍵獨立的設定 token、UI 字串或新的 hook。
- 不重寫 `settings_dialog.cpp` 的 dialog／hook adapter，除非 caller trace 證明
  interface 必須同步調整。
- 不處理 OS hook 被其他程式吞事件的未觀測情境；沿用 NR-089 以 hook event stream
  為準的決策。

## Acceptance criteria

1. 任一同類左右 modifier 同時 held 時，release 任一側都不會回報 captured；最後
   一側 release 才完成既有 candidate。
2. 重複 key-down 不會造成 release 一側後提前完成，也不會讓 preview／binding 多出
   修飾鍵。
3. 不同類別 Ctrl／Alt release order、Win+E、bare main key invalid、reserved combo
   與設定格式現有測試全數保持通過。
4. Release build 與完整 CTest 通過，沒有新增第三方依賴或 idle hook。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path tests/unit/hotkey_capture_test.cpp -Pattern 'VK_LCONTROL|VK_RCONTROL|VK_LMENU|VK_RMENU|repeated|repeat'
# expect: physical-variant regression coverage

git diff --check
```

## 交接區

`HotkeyCaptureState` 改以每個 physical modifier virtual key 的 bit 記錄 held state，
再聚合成既有 `MOD_*` binding；重複 key-down 為 idempotent，任一左右側 release 不會
提前完成 capture。`hotkey_capture_test.cpp` 新增左右 Ctrl、左右 Alt 與 repeated
down regression cases，公開設定格式與 NR-089 capture timing 未改。

Release build 成功，完整 CTest **24/24 通過**，physical variant grep 與
`git diff --check` 通過。手動驗收未執行；未完成事項：無。
