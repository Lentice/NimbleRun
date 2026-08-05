# NR-032 — Icon worker thread with single-ownership handoff

- Status: `done`
- Phase: 3
- Depends on: NR-031
- Source: `docs/design-spec.md` §FR-009、§9（第 658 行）、§19.1（第 1031 行）、§NFR-001

## Goal

把圖示取得從 UI thread 搬到一條常駐 worker thread，UI 永不等待 Shell。修掉 §19.1 第 1031 行「不能讓 UI 等待圖示」的既有落差：現況 `LoadVisibleIcons()` 在 UI thread 同步呼叫 Shell，24 顆冷載入約 50–200 ms 完全阻塞輸入。

本 item **不做磁碟持久化**。做完之後每次登入後第一次開窗仍會看到 fallback，但不再卡住；持久化由 NR-036 接上同一條 worker。

## 必讀

`AGENTS.md`、`docs/development.md`（全部五節）、`docs/design-spec.md` §FR-009／§9／§19.1／§NFR-001／§NFR-002、`docs/work-items.md`、`docs/work-items/NR-012-icons.md`、`docs/work-items/NR-030-icon-cache-spec-amendment.md`、`docs/work-items/NR-031-icon-variant-key.md`、本文件。

依賴檢查：若 NR-031 未 `done`（`IconKey` 仍有 `dpi` 欄位），**回報阻塞**。

## 與既有 item 的關係（重要）

- **本 item 覆寫 NR-012 的「fallback-first ＋ `LoadVisibleIcons` 同步載入可見列」與其 `ponytail:` 註解所述的取捨。** NR-012 的其餘決策（fallback 先畫、失敗不重試、不造成格位重排）不變。
- **不回頭修改** NR-012／NR-031 文件。

## 設計原則：不要有 lock

`ID2D1RenderTarget` 預設非 thread-safe，`ID2D1Bitmap` 只能在擁有 device context 的 thread 上建立。因此就算加鎖，worker 也不能碰 LRU 裡的東西——鎖只會買到一個可以死的地方。改用**單一擁有權 ＋ 訊息傳遞**，共享狀態為零：

| 狀態 | 擁有者 |
|---|---|
| `IconCache`（LRU）、`ID2D1Bitmap`、pending key 集合、`g_requested_icon_keys` | UI thread 獨佔 |
| Shell COM（`CoInitializeEx` STA）、`IShellItemImageFactory`、`GetImage`、HBITMAP→BGRA 轉換 | worker thread 獨佔 |

跨界只走純值。唯一的同步原語是請求佇列的 `std::mutex` ＋ `std::condition_variable`，且它只保護一個 `std::deque<IconRequest>`——內容全是可複製的值，沒有 COM 指標、沒有 HWND、沒有 handle。

## 硬約束

- **不得為每個圖示建立 thread**（§9 第 658 行）；固定**一條** worker。
- 不得使用 timer 或輪詢；worker 以 condition variable 等待，UI 以 `PostMessage` 收結果（§NFR-002 事件驅動）。
- worker 不得持有或回傳任何 COM 介面指標、`HBITMAP`、`HWND`。回傳型別必須是既有的純值 `IconBitmap`。
- UI thread 不得呼叫任何會阻塞的等待（無 `join` 於訊息處理路徑、無 `wait`、無 `WaitForSingleObject`）。
- 待機執行緒數上限為 5（NR-030 已放寬）；不得再多開。
- 不新增第三方依賴、網路、遙測、服務、driver、管理員權限、設定項。

## Scope

### 1. 請求佇列與 worker（新檔 `src/icons/icon_worker.{h,cpp}`，加入 `nimblerun_icons` 庫）

`icon_worker.h` 可包含 `<windows.h>`（它是 host 邊界的一部分），但**不得**包含 D2D 標頭。

```cpp
struct IconRequest {
    AppEntry entry;    // plain value; worker needs launch_identity/source_path
    IconKey key;
    bool visible = false;  // true = user is looking at it now, jump the queue
};

struct IconResult {
    std::wstring encoded_key;
    IconBitmap bitmap;     // empty on failure
};

// One background thread that owns Shell COM. Start() spawns it; Stop() wakes it
// and joins. Post() never blocks the caller. Results arrive as
// PostMessageW(target, result_message, 0, (LPARAM)new IconResult{...}); the UI
// thread takes ownership of that pointer and must delete it.
class IconWorker {
public:
    IconWorker(HWND target, UINT result_message, IconProvider& provider);
    ~IconWorker();  // calls Stop()

    void Start();
    void Stop();
    void Post(IconRequest request);  // visible => push_front, else push_back

private:
    void Run();  // CoInitializeEx(COINIT_APARTMENTTHREADED) ... CoUninitialize
    ...
};
```

- 一個 `std::deque<IconRequest>`：`visible == true` 走 `push_front`，預熱請求走 `push_back`。**不要**寫成 priority queue。
- `Run()` 迴圈：`wait` 直到有請求或收到停止旗標 → 取出 front → 呼叫 `provider.Load(entry, key)` → `PostMessageW` 一個 heap 配置的 `IconResult`。
- **失敗也要回報**（`bitmap` 為空），否則 UI 端的 pending 集合會永遠掛著那個鍵。
- `Stop()`：設旗標、`notify_all`、`join`。已排隊但未處理的請求直接丟棄（結果只是圖示，丟棄無副作用）。
- worker 自行 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`／`CoUninitialize`，不依賴 UI thread 的 COM 初始化。

### 2. UI 端接線（`src/app_host/main.cpp`）

- 新增 `constexpr UINT kIconReadyMessage = WM_APP + n;`（沿用檔內既有自訂訊息的編號慣例，不與 `kIconRequestMessage`／`kRebuildDoneMessage` 衝突）。
- **刪除** `LoadVisibleIcons()` 與 NR-012 的 `kIconRequestMessage` 這條「post 給自己再同步載入」的路徑。
- `Render()` 遇到 `Peek()` miss：畫 fallback（不變），並在該鍵**不在** pending 集合且**不在** `g_requested_icon_keys`（失敗集合）時，`g_icon_worker->Post({entry, key, /*visible=*/true})` 並把鍵加入 pending。`Render()` 不得配置大物件或做字串重組以外的工作。
- `kIconReadyMessage` 處理：取得 `IconResult*`、`std::unique_ptr` 接管、從 pending 移除該鍵；`bitmap` 非空 → 存入 `IconCache`（`Insert`，見 Scope 3）並 `InvalidateRect(hwnd, nullptr, FALSE)`；空 → 加入 `g_requested_icon_keys` 不重試。
- **結果晚到不丟棄**：面板已隱藏或查詢已變更時，仍照樣存入 LRU（那就是預熱的效果），只是不需要 `InvalidateRect`。不要實作取消機制。
- 多筆結果在同一輪抵達時不需自行合併：`InvalidateRect` 由 Windows 自然合併為一次 `WM_PAINT`。
- worker 於視窗建立後 `Start()`，於 `WM_DESTROY` 中 `Stop()`（在釋放 D2D 資源之前，確保不再有 `PostMessage` 進來）；`Stop()` 之後把訊息佇列中殘餘的 `kIconReadyMessage` 以 `PeekMessageW` 取出並 `delete`，避免洩漏。
- `ShowPanel` 時清空 `g_requested_icon_keys`（沿用 NR-012 的「每次顯示重試暫時性失敗」語意）；pending 集合**不清**（那些請求還在飛）。

### 3. `IconCache` 補一個 insert 入口（`src/icons/icon_cache.{h,cpp}`）

現況只有 `Resolve(entry, key, provider)` 會插入，而 worker 模型下取得已在別處完成。新增：

```cpp
// Insert an already-decoded bitmap (produced off-thread). Empty bitmaps are
// rejected, matching Resolve's "failures are not cached" rule.
void Insert(const std::wstring& encoded_key, IconBitmap bitmap);
```

`Resolve` 保留（測試與可能的同步路徑仍用得到），內部改為呼叫 `Insert`，不留第二份淘汰邏輯。

## Non-goals

- 不做磁碟持久化（NR-033～NR-036）。
- 不做預熱（NR-037）；本 item 只在 `Render()` 發現缺圖時才請求。
- 不做請求取消、超時、重試退避。
- 不改 `ShellIconProvider` 的取得方式或 variant 階梯。
- 不改 grid／清單版面、幾何、面板尺寸、palette、footer。
- 不改 catalog、dedup、usage、pin、settings 的邏輯或格式。
- 不加執行緒池、work stealing、`std::async`、`ThreadPool` API。

## Acceptance

- 面板第一次顯示 24 格未快取圖示時，UI thread 不呼叫任何 Shell API：搜尋欄可立即輸入，連續輸入不掉字、不出現可觀察停頓。
- 圖示逐顆抵達並就地替換 fallback，格位不重排、名稱不移動（§FR-009）。
- 同一個鍵在結果回來前重複出現於多幀 `Render()`，只會被 `Post` 一次（pending 集合去重）。
- 取得失敗的鍵不重試，直到下次 `ShowPanel`。
- 面板顯示中按 `Esc` 隱藏、立即再顯示，不崩潰、不洩漏；此期間抵達的結果仍進入 LRU，再次顯示時直接命中（不再看到 fallback）。
- 程式結束時 worker 乾淨結束（無 `std::terminate`、無 join 死鎖），殘餘 `IconResult` 全部釋放（以 Debug 建置或 sanitizer 觀察無洩漏；至少確認 `Stop()` 後有排空迴圈）。
- 待機執行緒數比 NR-017 的基準 **+1**，且 worker 待機時 CPU 為 0（阻塞在 condition variable，非輪詢）。
- repo 內搜尋不到 `LoadVisibleIcons`；`Render()` 內無 `provider.Load`／`Resolve` 呼叫。
- 建置無新增警告；全套件 CTest 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "icons" --output-on-failure
ctest --test-dir build --output-on-failure
```

`tests/unit/icon_cache_test.cpp` 或新測試 `tests/unit/icon_worker_test.cpp`（可建 message-only window，`CreateWindowExW(HWND_MESSAGE)`，不需顯示 UI）：

- 注入一個**故意延遲**的 fake provider（`Load` 內 sleep 30 ms）：`Post` 立即返回（量測呼叫端耗時 < 5 ms），結果稍後以訊息抵達。
- 送入 3 個請求後 pump 訊息，收到 3 筆結果、`encoded_key` 與送入者一一對應。
- fake provider 回傳空 bitmap 時**仍**收到一筆結果（失敗必須回報）。
- `visible = true` 的請求先於先前排入的 `visible = false` 請求被處理（以 fake provider 記錄呼叫順序驗證）。
- `Stop()` 後不再有新結果抵達，且 `Stop()` 可在佇列非空時呼叫而不 hang。
- `IconCache::Insert`：空 bitmap 被拒（`Size()` 不變）；非空插入後 `Peek` 命中；超過上限時淘汰最久未使用者（與既有 `Resolve` 的淘汰行為一致）。

## 交接區

- Start: 2026-08-05。已依「必讀」讀完 AGENTS.md、docs/development.md、design-spec §FR-009／§9／§19.1／§NFR-001／§NFR-002、work-items.md、NR-012／NR-030／NR-031；確認 NR-031 為 `done`（`IconKey` 已無 `dpi` 欄位，僅 `stable_id`＋`variant`）。trace 完 `src/app_host/main.cpp`（`LoadVisibleIcons`、`kIconRequestMessage`、`Render()` 兩個 icon 區塊、`ShowPanel`、`WM_DESTROY`、既有自訂訊息編號）、`src/icons/icon_cache.{h,cpp}`、`src/icons/shell_icon_provider.{h,cpp}`、`src/app_host/catalog_watcher.{h,cpp}`（既有背景 thread＋`PostMessage` 回 UI 風格）、`tests/unit/icon_cache_test.cpp`、兩份 CMakeLists。
- Subagent scope: 依「必讀」讀完所有文件；trace `src/app_host/main.cpp`（`LoadVisibleIcons`、`kIconRequestMessage`、`Render()` 兩個分支的 icon 區塊、`ShowPanel`、`WM_DESTROY`、既有自訂訊息編號）、`src/icons/icon_cache.{h,cpp}`、`src/icons/shell_icon_provider.{h,cpp}`、`src/app_host/catalog_watcher.{h,cpp}`（既有背景 thread ＋ `PostMessage` 回 UI 的寫法，本 item 應沿用同一風格，不要發明第二套）。先確認 NR-031 已 `done`，否則回報阻塞。實作 Scope 1～3。回報修改檔案、測試命令、結果與未完成事項。
- Result: 已完成。修改檔案：`src/icons/icon_worker.{h,cpp}`（新檔，加入 `nimblerun_icons`；`IconRequest`／`IconResult` 純值、`IconWorker`＝單一 mutex＋condition_variable 保護一個 `std::deque`，`visible==true` push_front；`Run()` 自行 `CoInitializeEx(COINIT_APARTMENTTHREADED)`、front 取出後 `provider.Load(entry,key)`、heap `IconResult` 以 `PostMessageW` 回傳、失敗也回報（bitmap 空）、`PostMessageW` 失敗即 delete 防洩漏；`Stop()` 設旗標＋notify_all＋join＋清佇列，`Post()` 在 worker 未執行時直接丟棄）、`src/icons/icon_cache.{h,cpp}`（新增 `Insert(encoded_key, IconBitmap)`：空 bitmap 拒絕、既有鍵覆寫 payload＋刷新 recency、超限從 LRU 尾淘汰；`Resolve` 改呼叫 `Insert`，移除第二份淘汰邏輯）、`src/app_host/main.cpp`（新增 `kIconReadyMessage=WM_APP+9`；刪除 `kIconRequestMessage`、`LoadVisibleIcons`、`VisibleItemCount`；新增 `RequestVisibleIcon` helper，`Render()` grid／清單兩分支 miss 時 fallback 照畫並在鍵**不在** pending 且**不在** `g_requested_icon_keys` 時 `Post({entry,key,visible=true})` 並加入 pending；`kIconReadyMessage` 以 `unique_ptr` 接管、pending 移除、非空 bitmap → `Insert`＋可見時 `InvalidateRect`、空 → `g_requested_icon_keys` 不重試；`WM_DESTROY` 在 D2D 釋放前 `Stop()` 並以 `PeekMessageW` 排空殘餘結果 delete；`ShowPanel` 維持清空 `g_requested_icon_keys`、pending 不清；wWinMain 建 worker＋`Start()`，移除已無用途的 `g_icon_provider` global）、`CMakeLists.txt`（`nimblerun_icons` 加 `icon_worker.cpp`、PUBLIC 補 `user32`；`AppEntry` 為 header-only 故不需連結 `nimblerun_catalog`）、`tests/unit/icon_worker_test.cpp`（新檔，message-only window；fake provider 可延遲／gate 阻斷／失敗）、`tests/unit/icon_cache_test.cpp`（`Insert` 空 bitmap 拒絕、非空插入 Peek 命中、超限淘汰與 Resolve 一致、re-insert 刷新 recency）、`tests/CMakeLists.txt`（新 CTest 目標 `nimblerun_icon_worker_test`）。Agent checks：clean build 無新增 warning；`ctest -R icons` 1/1（regex `icons` 只命中 `nimblerun_icons_cache_test`，worker 測試另以 `ctest -R icon_worker` 驗證 1/1、連跑 10 次穩定）；全套件 22/22 通過（含 lifecycle_check 實跑程式、原 21/21＋1）。`LoadVisibleIcons`／`kIconRequestMessage` repo 內無殘留；`Render()` 內無 `provider.Load`／`Resolve` 呼叫。未完成：無（24 格冷載入不卡輸入、Esc 隱藏再顯示、執行緒數＋1 與 idle CPU 0 屬人工／release-evidence 驗證，worker 固定一條、blocking wait 只在 `WM_DESTROY` 的 `Stop()`）。
