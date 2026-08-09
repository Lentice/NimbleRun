#include "app_host/catalog_watcher.h"

#include <windows.h>

#include <thread>
#include <utility>

namespace nimblerun {
namespace {

constexpr DWORD kNotifyFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;
constexpr DWORD kBufferBytes = 64 * 1024;
// NR-101: retained delivery intent levels and the bounded post-retry backoff.
// A failed PostMessageW (full queue, teardown race) must not silently lose a
// change; the intent is coalesced into watch.pending_notify and re-delivered on
// the next WatchLoop iteration.
constexpr int kNotifyChange = 1;
constexpr int kNotifyFullRescan = 2;
constexpr int kPostRetries = 2;
constexpr DWORD kPostRetrySleepMs = 250;

// NR-101: delivers a change/full-rescan intent to the host through the existing
// message path, retaining it when the post fails. Coalescing lets a full rescan
// dominate a normal change, so a full-rescan intent is never downgraded. Only
// the watcher thread touches pending_notify; a permanently invalid window
// (teardown) stops delivery quietly.
void PostNotification(CatalogWatcher::Watch& watch, int level) {
    if (!watch.window || !IsWindow(watch.window)) {
        return;  // teardown: never deliver to a destroyed HWND; Stop() joins
    }
    int pending = watch.pending_notify.load();
    if (level > pending) {
        pending = level;
        watch.pending_notify.store(pending);
    }
    const bool full_rescan = pending == kNotifyFullRescan;
    // Bounded backoff for a transiently full queue; never an endless retry
    // loop. On exhaustion the intent stays retained so the top-of-loop recovery
    // re-delivers at the next event / overflow / error cycle.
    for (int attempt = 0; attempt < kPostRetries; ++attempt) {
        if (PostMessageW(watch.window, watch.message,
                         static_cast<WPARAM>(watch.index),
                         full_rescan ? 1 : 0) != FALSE) {
            watch.pending_notify.store(0);
            return;
        }
        Sleep(kPostRetrySleepMs);
    }
}

void WatchLoop(std::shared_ptr<CatalogWatcher::Watch> watch) {
    std::vector<BYTE> buffer(kBufferBytes);
    // NR-074: one full-rescan notice per failure episode. A persistent error
    // (root removed, access denied) must not post a marker every second -- that
    // drives a 1 Hz rebuild loop in the host (§FR-008/NFR-002).
    bool reported = false;
    for (;;) {
        // NR-101: re-deliver an intent a failed post retained earlier. This runs
        // on the next event / overflow / error cycle, so recovery is event-driven
        // (no timer). PostNotification guards an invalid window itself.
        if (watch->pending_notify.load() != 0) {
            PostNotification(*watch, watch->pending_notify.load());
        }
        DWORD bytes_returned = 0;
        const BOOL ok = ReadDirectoryChangesW(
            watch->directory,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            watch->recursive ? TRUE : FALSE,
            kNotifyFilter,
            &bytes_returned,
            nullptr,
            nullptr);
        if (watch->stop.load()) {
            return;
        }
        if (ok == FALSE) {
            const DWORD error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED || watch->stop.load()) {
                return;  // CancelIoEx from Stop(): normal shutdown
            }
            // ERROR_INVALID_PARAMETER (root not a directory / too small buffer)
            // or a transient failure: report a full rescan and back off instead
            // of busy-looping. Report the first failure only; the backoff sleep
            // continues, and the next successful ReadDirectoryChangesW resets
            // the flag so a genuine later event is reported again.
            if (!reported) {
                PostNotification(*watch, kNotifyFullRescan);
            }
            reported = true;
            Sleep(1000);
            continue;
        }
        reported = false;
        if (bytes_returned == 0) {
            // Buffer overflow: the event list is incomplete, rescan the source.
            PostNotification(*watch, kNotifyFullRescan);
            continue;
        }
        PostNotification(*watch, kNotifyChange);
    }
}

} // namespace

CatalogWatcher::CatalogWatcher(HWND notify_window, UINT message)
    : window_(notify_window), message_(message) {
}

CatalogWatcher::~CatalogWatcher() {
    Stop();
}

void CatalogWatcher::SetRoots(const std::vector<std::wstring>& roots,
                              const std::vector<bool>& recursive) {
    Stop();
    watches_.clear();
    const std::size_t count = roots.size();
    for (std::size_t i = 0; i < count; ++i) {
        auto watch = std::make_unique<Watch>();
        watch->path = roots[i];
        watch->recursive = i < recursive.size() ? recursive[i] : true;
        watch->directory = CreateFileW(
            watch->path.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (watch->directory == INVALID_HANDLE_VALUE) {
            continue;  // root missing/unreadable: skip this watch, keep others
        }
        watch->window = window_;
        watch->message = message_;
        watch->index = static_cast<int>(i + 1);  // 1-based, 0 means none
        std::shared_ptr<CatalogWatcher::Watch> shared =
            std::shared_ptr<CatalogWatcher::Watch>(std::move(watch));
        try {
            shared->thread = std::thread(WatchLoop, shared);
        } catch (...) {
            // NR-097: thread creation failed (std::system_error). Close the
            // opened directory handle and skip this root; other watches keep
            // working. The watch is never added to watches_.
            CloseHandle(shared->directory);
            shared->directory = INVALID_HANDLE_VALUE;
            continue;
        }
        try {
            watches_.push_back(std::move(shared));
        } catch (...) {
            // NR-097: bad_alloc while growing watches_. Tear the watch down
            // cleanly -- stop, cancel the blocked read, join the thread, close
            // the handle -- so a joinable std::thread member is never destroyed
            // without a join and no partially-initialized watch is left behind.
            shared->stop.store(true);
            CancelIoEx(shared->directory, nullptr);
            if (shared->thread.joinable()) {
                shared->thread.join();
            }
            CloseHandle(shared->directory);
            shared->directory = INVALID_HANDLE_VALUE;
            continue;
        }
    }
}

void CatalogWatcher::Stop() {
    for (auto& watch : watches_) {
        watch->stop.store(true);
        // Cancel the blocked ReadDirectoryChangesW so the thread wakes and
        // exits promptly instead of hanging on shutdown.
        CancelIoEx(watch->directory, nullptr);
        if (watch->thread.joinable()) {
            watch->thread.join();
        }
        CloseHandle(watch->directory);
        watch->directory = INVALID_HANDLE_VALUE;
    }
    watches_.clear();
}

} // namespace nimblerun
