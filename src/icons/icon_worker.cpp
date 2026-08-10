#include "icons/icon_worker.h"

#include "catalog/stable_id.h"
#include "icons/icon_pack_format.h"
#include "icons/icon_store.h"
#include "icons/png_codec.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <ctime>
#include <utility>

namespace nimblerun {
namespace {

// NR-036: a final flush never holds up shutdown with a huge backlog; a batch
// larger than this is dropped (a lost cache write has no side effects).
constexpr std::size_t kStopFlushMaxPending = 64;

std::uint64_t UtcNow() {
    return static_cast<std::uint64_t>(std::time(nullptr));
}

void LogStoreFailure(IconStore* store, std::wstring_view detail) noexcept {
    if (store == nullptr) {
        return;
    }
    try {
        store->WriteLog(L"icon-worker", detail);
    } catch (...) {
        // The diagnostic path is part of the failure boundary too.
    }
}

void RememberDroppedRequest(const IconRequest& request, std::wstring encoded,
                            IconStore* store, HWND target, UINT result_message) noexcept {
    if (!request.visible) {
        return;
    }
    try {
        if (encoded.empty()) {
            encoded = request.key.Encode();
        }
        {
            std::lock_guard<std::mutex> lock(g_icon_dropped_keys_mutex);
            g_icon_dropped_keys.push_back(std::move(encoded));
        }
        // Reuse the normal result message as an event-driven drain signal. If
        // the target is already gone, the next ShowPanel drains the registry.
        PostMessageW(target, result_message, 1, 0);
    } catch (...) {
        LogStoreFailure(store, L"exception");
    }
}

} // namespace

IconWorker::IconWorker(HWND target, UINT result_message, IconProvider& provider,
                       IconStore* store)
    : target_(target), result_message_(result_message), provider_(provider),
      store_(store) {
}

IconWorker::~IconWorker() {
    Stop();
}

void IconWorker::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
        return;  // already running
    }
    stop_ = false;
    try {
        thread_ = std::thread(&IconWorker::Run, this);
    } catch (...) {
        // NR-097: thread creation failed (std::system_error). Stay stopped so
        // later Post() calls drop requests; icons degrade to the fallback and
        // the worker stays optional. Never propagate out of Start().
        stop_ = true;
        LogStoreFailure(store_, L"exception");
    }
}

void IconWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    thread_ = std::thread();
}

bool IconWorker::Post(IconRequest request) {
    try {
        IconTask task;
        task.kind = IconTaskKind::Load;
        task.request = std::move(request);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!thread_.joinable()) {
                return false;  // stopped or never started: drop
            }
            if (task.request.visible) {
                // NR-099: visible always lands at the front; evict from the back,
                // which is always a prewarm or flush task, never this visible one,
                // so its fallback recovery (design-spec §FR-009) is never lost.
                queue_.push_front(std::move(task));
                while (queue_.size() > kMaxQueuedTasks) {
                    queue_.pop_back();
                }
                cv_.notify_one();
                return true;
            }
            if (queue_.size() < kMaxQueuedTasks) {
                // NR-099: an over-cap prewarm is dropped; a dropped prewarm has no
                // side effects, and the bound keeps the queue explainable (§9.2).
                queue_.push_back(std::move(task));
                cv_.notify_one();
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

void IconWorker::PostFlush(std::vector<std::wstring> pinned_ids, std::uint64_t now_utc) {
    IconTask task;
    task.kind = IconTaskKind::Flush;
    task.pinned_ids = std::move(pinned_ids);
    task.now_utc = now_utc;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        // NR-099: a flood of hide cycles keeps at most one flush task. Replace
        // an existing Flush in place so the latest pins/now always win; only
        // push when the queue is below the cap (drop when full).
        const auto existing = std::find_if(
            queue_.begin(), queue_.end(),
            [](const IconTask& t) { return t.kind == IconTaskKind::Flush; });
        if (existing != queue_.end()) {
            *existing = std::move(task);
        } else if (queue_.size() < kMaxQueuedTasks) {
            queue_.push_back(std::move(task));
        }
    }
    cv_.notify_one();
}

void IconWorker::CancelPrewarm() {
    std::lock_guard<std::mutex> lock(mutex_);
    // NR-099: drop every queued Load task that is not visible -- a stale
    // prewarm from an earlier hide cycle. Visible Load tasks and the Flush
    // task stay. The in-flight request is unaffected (it is not queued).
    std::erase_if(queue_, [](const IconTask& task) {
        return task.kind == IconTaskKind::Load && !task.request.visible;
    });
}

std::size_t IconWorker::QueueDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void IconWorker::Run() {
    // The worker owns Shell COM on its own thread; it never depends on the UI
    // thread's initialization.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool store_readable = store_ != nullptr;
    bool store_writable = store_readable;
    // NR-036: the store is opened (or created, or classified as Disabled) on
    // the worker, never on the UI thread.
    if (store_readable) {
        try {
            const IconStore::StoreState state = store_->Open();
            store_readable = state != IconStore::StoreState::Disabled;
            store_writable = state == IconStore::StoreState::Ready;
        } catch (...) {
            store_readable = false;
            store_writable = false;
            LogStoreFailure(store_, L"open-exception");
        }
    }
    const auto flush_store = [&](const std::vector<std::wstring>& pinned_ids,
                                 std::uint64_t now_utc) noexcept {
        if (!store_readable) {
            return;
        }
        try {
            const bool flushed = store_->Flush(pinned_ids, now_utc);
            pending_puts_ = 0;
            if (!flushed) {
                store_writable = false;
            }
        } catch (...) {
            pending_puts_ = 0;
            store_readable = false;
            store_writable = false;
            LogStoreFailure(store_, L"flush-exception");
        }
    };
    for (;;) {
        IconTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
            if (stop_) {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }

        if (task.kind == IconTaskKind::Flush) {
            flush_store(task.pinned_ids, task.now_utc);
            continue;
        }

        IconRequest& request = task.request;
        // NR-097: the result allocation and the encoded_key are inside the try
        // so a heap-exhaustion failure is caught, not an unguarded escape from
        // the worker entry point.
        IconResult* result = nullptr;
        try {
            result = new IconResult;
            result->encoded_key = std::move(request.encoded_key);
            if (result->encoded_key.empty()) {
                result->encoded_key = request.key.Encode();
            }

            if (store_readable) {
                // NR-036 fetch order (design-spec §FR-009): memory LRU (UI side),
                // then the disk pack, then Shell. The worker owns every store call.
                const std::uint64_t source_stamp = SourceStampFor(request.entry);
                const std::uint64_t now_utc = UtcNow();
                std::vector<std::uint8_t> png;
                try {
                    png = store_->Lookup(request.entry.stable_id, request.key.variant,
                                         source_stamp, now_utc);
                } catch (...) {
                    store_readable = false;
                    store_writable = false;
                    LogStoreFailure(store_, L"lookup-exception");
                }
                if (!png.empty()) {
                    result->bitmap =
                        DecodeIconPng(png.data(), png.size(), request.key.variant);
                }
                // A hit that fails to decode (or a miss / stale stamp / TTL)
                // falls through to Shell, exactly like a miss.
                if (result->bitmap.Empty()) {
                    result->bitmap = provider_.Load(request.entry, request.key);
                    if (!result->bitmap.Empty()) {
                        const std::vector<std::uint8_t> encoded =
                            EncodeIconPng(result->bitmap);
                        // An un-encodable bitmap is reported but not persisted.
                        if (!encoded.empty() && store_writable) {
                            try {
                                store_->Put(request.entry.stable_id, request.key.variant,
                                            std::move(encoded), source_stamp, now_utc);
                                ++pending_puts_;
                            } catch (...) {
                                store_readable = false;
                                store_writable = false;
                                LogStoreFailure(store_, L"put-exception");
                            }
                        }
                    }
                }
            } else {
                result->bitmap = provider_.Load(request.entry, request.key);
            }
        } catch (...) {
            if (result == nullptr) {
                // NR-097: the result could not be allocated at all (heap
                // exhaustion). Nothing to hand back; the pending key stays set
                // and the fallback keeps showing. Log and move on.
                RememberDroppedRequest(request, std::move(request.encoded_key), store_,
                                       target_, result_message_);
                continue;
            }
            // NR-076: a throwing Shell/WIC/alloc path must not terminate the
            // process (design-spec §11: catch, log, discard). Report an empty
            // bitmap so the UI clears the pending key and keeps the fallback.
            LogStoreFailure(store_, L"exception");
            result->bitmap = {};  // keep result allocated; fall through to the post
        }

        // NR-077: hand the payload to the UI thread by token, never by a raw
        // pointer in a WM_APP message -- an unvalidated lParam must never be
        // dereferenced. Register under the shared mutex before posting; a full
        // message queue (PostMessageW fails) erases the token, which deletes
        // the object -- the old "window gone" leak guard.
        std::unique_ptr<IconResult> owned(result);
        const std::uintptr_t token = g_icon_handoffs.Register(std::move(owned));
        const bool registered = token != 0;
        if (!registered) {
            // NR-097/109: a bad_alloc during registry insertion must not
            // terminate the process or leak the object. The owned guard still
            // owns an unregistered result, and this path is outside the mutex
            // before recording the visible request's completion.
            RememberDroppedRequest(request, std::move(result->encoded_key), store_,
                                   target_, result_message_);
            continue;
        }
        if (!PostMessageW(target_, result_message_, 0,
                          static_cast<LPARAM>(token))) {
            RememberDroppedRequest(request, std::move(result->encoded_key), store_,
                                   target_, result_message_);
            g_icon_handoffs.Erase(token);
        }

        // NR-036 timing 2: when the request queue drains and there is buffered
        // data, flush before going idle. Event-driven, no timer.
        if (store_readable && pending_puts_ > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty() && !stop_) {
                lock.unlock();
                flush_store({}, UtcNow());
            }
        }
    }
    // NR-036 timing 3: one final best-effort flush before the thread exits.
    // Bounded by the pending count; an oversized backlog is dropped rather than
    // holding up shutdown.
    // ponytail: the cap drops the whole backlog; a recent-only subset flush
    // would need an IconStore API and is deferred until measurement shows it
    // matters.
    if (store_readable && pending_puts_ > 0 && pending_puts_ <= kStopFlushMaxPending) {
        flush_store({}, UtcNow());
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

std::uint64_t IconWorker::SourceStampFor(const AppEntry& entry) const {
    // AppsFolder / AUMID entries have no stat-able source file (their parsing
    // name is not a filesystem path); they fall back to the store's TTL.
    if (entry.launch_identity.rfind(L"shell:AppsFolder\\", 0) == 0) {
        return 0;
    }
    std::wstring path = entry.source_path;
    if (path.empty() && IsPathIdentity(entry.launch_identity)) {
        path = entry.launch_identity;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;  // nothing to stat: no stamp, the store applies its TTL
    }
    ULARGE_INTEGER ft{};
    ft.HighPart = data.ftLastWriteTime.dwHighDateTime;
    ft.LowPart = data.ftLastWriteTime.dwLowDateTime;
    const std::uint64_t size =
        (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return MakeSourceStamp(ft.QuadPart, size);
}

} // namespace nimblerun
