# NR-096 — NewerSchema 載入後，所有 user-data Save 路徑仍須保護原檔

Phase 1 · Depends on: NR-004, NR-009, NR-013, NR-018, NR-058, NR-072, NR-080

- Source: `docs/design-spec.md` §10.4（較新 schema 不得覆寫）／§11
- Origin: 2026-08-09 全 repo 稽核（settings、pins、usage 的 Load→runtime mutation→Save 追蹤）
- Priority: HIGH（使用者資料可能被舊版本的正常操作降級覆寫）

## Why

三個 user-data store 都能回報 `NewerSchema`，並在初次載入時保留原檔，但保護只覆蓋了
部分 caller：

- `RefreshPins` 在初次 reconcile 時跳過 `Save`，但右鍵 Pin/Unpin 與拖曳排序仍直接呼叫
  `g_pins->Save()`。
- `UsageStore` 的 `usage_result` 只存在啟動區域變數；之後成功啟動、Remove from recent、
  reconcile 或 Clear usage 都可能呼叫 `Save()`。
- Settings dialog 讀到 `NewerSchema` 後使用 defaults；使用者只要修改任何設定，
  `SettingsEditor::Apply` 就會呼叫 `SettingsStore::Save()`，覆寫較新版本的檔案。

這違反「較新 schema 是別的版本／未來版本的資料，舊版本不得猜測並改寫」的契約。問題的
根源在 store 的 Save 入口沒有保留 load protection state，不是再為每個 UI caller 加 guard。

## Decisions already made — do not reopen

1. 保護必須落在三個 store 的共用 `Save` 入口；caller 不各自複製 `NewerSchema` 判斷。
2. `NewerSchema` 後的 `Save` 回傳失敗／拒絕，且不建立或替換 tmp/original；現有 UI 的
   save-failed／notice 行為沿用。
3. `Loaded`、`Missing` 與現有 `Corrupt` quarantine 語意維持可寫；不把正常 migration
   或同 schema 的 trailing fields 誤判為不可寫。
4. 不實作未來 schema 的 migration、merge、欄位猜測或 destructive cleanup。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10.4：

> 程式遇到較新的 schema：採安全預設值繼續執行、保留原檔不覆寫，記錄一次診斷。

`AGENTS.md`：

> Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.

> Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.

`docs/development.md`：

> `storage` | Settings, pins, usage, atomic writes | Catalog enumeration

> Reuse existing types and helpers before adding code.

## Files to read and trace first

- `src/settings/settings_store.{h,cpp}` — `Load`／`Save` 與 `SettingsLoadResult`。
- `src/pins/pin_store.{h,cpp}` — `Load`／`Save` 與 `PinLoadResult`。
- `src/usage/usage_store.{h,cpp}` — `Load`／`Save` 與 `UsageLoadResult`。
- `src/app_host/main.cpp` — `RefreshPins`、launch／forget／reconcile／clear 的所有 Save caller。
- `src/app_host/settings_dialog.cpp`、`src/settings/settings_editor.cpp` — settings dialog 的
  re-read 與 Apply。
- `tests/unit/settings_store_test.cpp`、`tests/unit/pin_store_test.cpp`、
  `tests/unit/recent_usage_test.cpp` — 既有 NewerSchema 與 atomic-write tests。
- `docs/work-items/NR-072-refreshpins-preserve-newer-schema.md`、NR-080 — 既有部分保護決策。

## Scope

1. 每個 store 在 `Load` 結果為 `NewerSchema` 時進入本次 store instance 的 write-protected
   狀態；其他既有可寫結果依原規則恢復／保持可寫。
2. 三個 `Save`（包含 `UsageStore::Clear` 間接路徑）在 write-protected 時不觸碰原檔與 tmp，
   並回傳現有 caller 可理解的失敗結果。
3. 保留目前 host 的 notice/log；只補齊 state，使下列操作不會繞過保護：pin/unpin、drag
   reorder、成功 launch、forget、clear、usage reconcile、settings Apply。
4. 各 store 至少新增一個 focused test：先寫入 marker 內容的較新 schema，再 mutate／Save，
   斷言回傳失敗且檔案 bytes 完全不變；另測正常 Missing/Loaded 仍可寫。

## Non-goals

- 不改檔案格式版本、不新增 migration、不解析未知欄位。
- 不改 corrupt file 的 quarantine 行為；不把 corrupt 與 newer 混為一談。
- 不新增通用 store framework、callback 或 UI abstraction。
- 不改 catalog.cache／icons.cache 的既有 NR-079／NR-036 保護。

## Acceptance

Automated：

1. Release build 無新增 warning；`ctest` 全綠。
2. settings、pins、usage 三個 store 的 NewerSchema Save tests 都證明原檔 bytes 不變。
3. 每個 store 的正常 Loaded/Missing Save regression 通過。
4. `rg` 追出的所有 UI Save caller 都只能經過受保護的 store Save 入口，不新增 caller-side
   例外繞道。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "settings|pinning|recent_usage" --output-on-failure
```

```powershell
rg -n "NewerSchema|bool .*Save|Save\(" src/settings src/pins src/usage src/app_host/main.cpp src/app_host/settings_dialog.cpp
git diff --name-only
# expect: 僅三個 store、必要 tests、本 item 與 tracker；無 code path 直接寫 user-data。
```

## 交接區

實作完成，未 commit。改動檔案（`git diff --name-only`）：`src/settings/settings_store.{h,cpp}`、
`src/pins/pin_store.{h,cpp}`、`src/usage/usage_store.{h,cpp}`、`tests/unit/settings_store_test.cpp`、
`tests/unit/pin_store_test.cpp`、`tests/unit/recent_usage_test.cpp`、本 item。

### Protected state 表示

三個 store 各加一個 private member：`mutable bool write_protected_ = false`（`Save()` 為 const，
故必須 mutable；`SettingsStore` 的 `Load()` 亦為 const，同樣受 mutable 要求）。

- 每次 `Load()` 開頭一律 `write_protected_ = false`；僅在 switch 的 `NewerSchema` 分支設為 `true`
  才回傳。因此四個結果的狀態映射為：`Loaded`/`Missing`/`Corrupt`（含 mid-file corrupt 提早 return 的
  分支）→ 可寫；`NewerSchema` → 本次 instance 寫保護。
- 三個 `Save()` 的第一行 guard：`if (write_protected_) return false;`，在組任何 text 或呼叫
  `AtomicWriteUtf8Text` 之前，原檔與 tmp 都不會碰。回傳 false 沿用既有 caller 的 save-failed
  行為。
- `UsageStore::Clear()` 未改：內部 `Save()` 回 false 時回復 `records_` 並回傳 false（既有語意，
  settings_dialog.cpp:531 顯示 `SaveFailedNotice`）。

### 每個 Save/寫入 caller 的追蹤結果

`rg -n -- "Save\(|Clear\("` 全數命中下列路徑，都只經受保護的 store `Save()` 入口，無繞道：

- `src/app_host/main.cpp:912` launch 成功 → `UsageStore::Save()`；回傳值被忽略，但原檔已不覆寫，
  in-memory stamp 照常無害。
- `src/app_host/main.cpp:1175` `RefreshPins()` → `PinStore::Save()`；外部 NR-072 guard
  `result == Loaded || result == Missing` 保留，NewerSchema 時根本不會到 Save。
- `src/app_host/main.cpp:1237` usage reconcile → `UsageStore::Save()`；回傳值忽略，原檔不受影響。
- `src/app_host/main.cpp:2219` context menu Pin/Unpin → `PinStore::Save()`；false 則整個 view 更新
  （SetPins／StampRankingFields／InvalidateRect）被跳過。
- `src/app_host/main.cpp:2241` forget → `UsageStore::Save()`；false 則不刷新 view。
- `src/app_host/main.cpp:2878` drag reorder → `PinStore::Save()`；false 則不 SetPins。
- `src/settings/settings_editor.cpp:461` settings Apply → `SettingsStore::Save()`；false → `save_failed`
  ＋ hotkey 換回＋Rollback。settings dialog 用的就是 startup 的 `*g_settings_store`（main.cpp:2619），
  dialog 內 `store.Load()`（settings_dialog.cpp:584）與 Apply 同一 instance，故 NewerSchema 後
  Apply 一定被拒。
- `src/app_host/settings_dialog.cpp:531` Clear usage → `UsageStore::Clear()`（間接 Save）；false →
  `SaveFailedNotice`。

`main.cpp:1487` 是 `g_render_target->Clear(D2D1::ColorF(...))`，Direct2D 渲染清除，非 user-data
寫入，忽略。

### 測試 marker bytes

統一 `schema=99` 標記，皆驗證 `Save()` 回 false、原檔 byte-for-byte 不變、無 `.tmp` 殘留、
再 Load 仍 NewerSchema 且仍拒寫：

- settings：`"schema=99\nrecent_count=30\n"`（mutate：`recent_count=35` 後 Save）。
- pins：`"schema=99\npinned_app\t1000\n"`（mutate：`Pin(L"another_app", ...)` 後 Save）。
- usage：`"schema=99\nnot_in_catalog\t5\t1000\n"`（mutate：`RecordLaunch(L"another_app", 2000)`
  後 Save；再驗 `Clear()` 回 false 且 `Records()` 數量還原）。

Regression（Missing/Loaded/Corrupt 皆仍可寫；Corrupt 用獨立 temp dir，因原檔被改名 `.corrupt`）：
三個測試檔各新增一函式涵蓋三種正常結果的 Save 成功並寫出新檔。

### 新增測試函式名

- `settings_store_test.cpp`：`TestNewerSchemaSaveRefused`、`TestMissingLoadSaveWritesFile`、
  `TestLoadedLoadSaveWritesFile`、`TestCorruptLoadSaveWritesFile`。
- `pin_store_test.cpp`：`TestNewerSchemaSaveRefused`、`TestNormalLoadsRemainWritable`。
- `recent_usage_test.cpp`：`TestNewerSchemaSaveRefused`、`TestNormalLoadsRemainWritable`。

三個執行檔各自的 `wmain()` 已註冊新函式。

### 建置／CTest

- 全量 Release build 無新增 warning（對六個改動檔 touch 強制重編後 grep `warning|error|FAILED`
  為零）。
- `ctest --test-dir build --output-on-failure`：**24/24 全綠**。
- `ctest --test-dir build -R "settings|pinning|recent_usage" --output-on-failure`：4/4 全綠。

### 偏差

- 無功能偏差。三份 test 檔裡 regression 直接拆成三個小函式（settings）或單一多段函式
  （pins/usage）維持既有風格；未改任何既有測試、未動 catalog.cache/icons.cache 保護、未改
  `docs/work-items.md` 狀態。
