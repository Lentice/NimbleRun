#pragma once

#include <windows.h>

#include "icons/icon_cache.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace nimblerun {

// Plain-value icon request. The worker only needs the entry to hand to the
// provider (launch_identity / source_path); no HWND, no COM, no handles.
struct IconRequest {
    AppEntry entry;
    IconKey key;
    bool visible = false;  // true = user is looking at it now, jump the queue
};

// Result handed back to the UI thread. owned by the receiving window; a
// failure is reported with an empty bitmap so a pending key is always
// acknowledged (design-spec §FR-009 lazy loading, NR-032).
struct IconResult {
    std::wstring encoded_key;
    IconBitmap bitmap;  // empty on failure
};

// One persistent background thread that owns Shell COM (design-spec §FR-009:
// "UI thread 不得等待 Shell"). The UI thread posts copyable requests; results
// arrive as PostMessageW(target, result_message, 0, (LPARAM)new IconResult).
// The only shared state is the request deque guarded by a single mutex +
// condition variable; the cache, D2D resources and pending sets stay UI-thread
// owned, and Shell COM stays worker-thread owned.
class IconWorker {
public:
    IconWorker(HWND target, UINT result_message, IconProvider& provider);
    ~IconWorker();  // calls Stop()

    IconWorker(const IconWorker&) = delete;
    IconWorker& operator=(const IconWorker&) = delete;

    // Spawns the thread (idempotent). Requests posted while stopped are
    // dropped.
    void Start();
    // Sets the stop flag, wakes and joins the thread. Queued-but-unprocessed
    // requests are discarded (a missing icon has no side effects).
    void Stop();
    // Never blocks. visible requests push_front, prewarm requests push_back.
    void Post(IconRequest request);

private:
    void Run();  // CoInitializeEx(COINIT_APARTMENTTHREADED) ... CoUninitialize

    HWND target_ = nullptr;
    UINT result_message_ = 0;
    IconProvider& provider_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<IconRequest> queue_;
    bool stop_ = false;
};

} // namespace nimblerun
