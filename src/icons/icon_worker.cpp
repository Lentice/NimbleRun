#include "icons/icon_worker.h"

#include "catalog/stable_id.h"
#include "icons/icon_pack_format.h"
#include "icons/icon_store.h"
#include "icons/png_codec.h"

#include <windows.h>
#include <objbase.h>

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
    thread_ = std::thread(&IconWorker::Run, this);
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

void IconWorker::Post(IconRequest request) {
    IconTask task;
    task.kind = IconTaskKind::Load;
    task.request = std::move(request);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;  // stopped or never started: drop
        }
        if (task.request.visible) {
            queue_.push_front(std::move(task));
        } else {
            queue_.push_back(std::move(task));
        }
    }
    cv_.notify_one();
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
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void IconWorker::Run() {
    // The worker owns Shell COM on its own thread; it never depends on the UI
    // thread's initialization.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // NR-036: the store is opened (or created, or classified as Disabled) on
    // the worker, never on the UI thread.
    if (store_ != nullptr) {
        store_->Open();
    }
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
            if (store_ != nullptr) {
                store_->Flush(task.pinned_ids, task.now_utc);
                pending_puts_ = 0;
            }
            continue;
        }

        const IconRequest& request = task.request;
        auto* result = new IconResult;
        result->encoded_key = request.key.Encode();

        if (store_ != nullptr) {
            // NR-036 fetch order (design-spec §FR-009): memory LRU (UI side),
            // then the disk pack, then Shell. The worker owns every store call.
            const std::uint64_t source_stamp = SourceStampFor(request.entry);
            const std::uint64_t now_utc = UtcNow();
            const std::vector<std::uint8_t> png =
                store_->Lookup(request.entry.stable_id, request.key.variant,
                               source_stamp, now_utc);
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
                    if (!encoded.empty()) {
                        store_->Put(request.entry.stable_id, request.key.variant,
                                    std::move(encoded), source_stamp, now_utc);
                        ++pending_puts_;
                    }
                }
            }
        } else {
            result->bitmap = provider_.Load(request.entry, request.key);
        }

        if (!PostMessageW(target_, result_message_, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;  // window gone: never leak the handoff
        }

        // NR-036 timing 2: when the request queue drains and there is buffered
        // data, flush before going idle. Event-driven, no timer.
        if (store_ != nullptr && pending_puts_ > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty() && !stop_) {
                lock.unlock();
                store_->Flush({}, UtcNow());
                pending_puts_ = 0;
            }
        }
    }
    // NR-036 timing 3: one final best-effort flush before the thread exits.
    // Bounded by the pending count; an oversized backlog is dropped rather than
    // holding up shutdown.
    // ponytail: the cap drops the whole backlog; a recent-only subset flush
    // would need an IconStore API and is deferred until measurement shows it
    // matters.
    if (store_ != nullptr && pending_puts_ > 0 && pending_puts_ <= kStopFlushMaxPending) {
        store_->Flush({}, UtcNow());
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
