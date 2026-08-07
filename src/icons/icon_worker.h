#pragma once

#include <windows.h>

#include "icons/icon_cache.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nimblerun {

class IconStore;

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

// NR-077: worker threads hand result objects to the UI thread by token, never
// by a raw pointer in a WM_APP message -- any same-integrity process can post
// to our HWND, and dereferencing an unvalidated lParam is a crash vector
// (design-spec §NFR-004). The registry owns the posted objects; a message
// carries only the object's address as a token, and the receiver ignores
// tokens it cannot find. Senders register under the mutex before posting and
// erase on a failed post; the UI thread moves the object out and erases on
// receipt; WM_DESTROY clears whatever is still in flight.
inline std::mutex g_handoff_mutex;
inline std::unordered_map<std::uintptr_t, std::unique_ptr<IconResult>> g_icon_handoffs;

// NR-036: tagged queue element. A Load task carries an IconRequest; a Flush
// task carries the pinned-id list and the wall-clock time the UI handed over,
// so the worker never reads favorites.txt (design-spec §10.2). Both kinds share
// one queue; flush signals have lower priority than visible requests.
enum class IconTaskKind { Load, Flush };

struct IconTask {
    IconTaskKind kind = IconTaskKind::Load;
    IconRequest request;                              // Load
    std::vector<std::wstring> pinned_ids;             // Flush
    std::uint64_t now_utc = 0;                        // Flush
};

// One persistent background thread that owns Shell COM (design-spec §FR-009:
// "UI thread 不得等待 Shell"). The UI thread posts copyable requests; results
// arrive as PostMessageW(target, result_message, 0, (LPARAM)new IconResult).
// The only shared state is the request deque guarded by a single mutex +
// condition variable; the cache, D2D resources and pending sets stay UI-thread
// owned, and Shell COM stays worker-thread owned.
class IconWorker {
public:
    // `store` is optional (nullptr = no disk layer, the NR-032 behavior). The
    // store is owned by the caller and must outlive the worker; only the worker
    // thread calls into it.
    IconWorker(HWND target, UINT result_message, IconProvider& provider,
               IconStore* store = nullptr);
    ~IconWorker();  // calls Stop()

    IconWorker(const IconWorker&) = delete;
    IconWorker& operator=(const IconWorker&) = delete;

    // Spawns the thread (idempotent). Requests posted while stopped are
    // dropped.
    void Start();
    // Sets the stop flag, wakes and joins the thread. Queued-but-unprocessed
    // requests are discarded (a missing icon has no side effects); buffered
    // cache writes get one final best-effort flush before the thread exits.
    void Stop();
    // Never blocks. visible requests push_front, prewarm requests push_back.
    void Post(IconRequest request);
    // Never blocks. Lower priority than any visible request; the worker calls
    // IconStore::Flush(pinned_ids, now_utc) when it reaches this task.
    void PostFlush(std::vector<std::wstring> pinned_ids, std::uint64_t now_utc);

private:
    void Run();  // CoInitializeEx(COINIT_APARTMENTTHREADED) ... CoUninitialize
    // Current source stamp for a request entry: stat the real file, 0 for
    // AppsFolder/AUMID or anything that cannot be stat'ed. Worker thread only.
    std::uint64_t SourceStampFor(const AppEntry& entry) const;

    HWND target_ = nullptr;
    UINT result_message_ = 0;
    IconProvider& provider_;
    IconStore* store_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<IconTask> queue_;
    bool stop_ = false;
    // Buffered-but-unflushed puts since the last Flush, counted on the worker
    // thread only; bounds the final flush so shutdown never hangs.
    std::size_t pending_puts_ = 0;
};

} // namespace nimblerun
