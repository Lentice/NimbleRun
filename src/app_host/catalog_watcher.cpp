#include "app_host/catalog_watcher.h"

#include <cstddef>
#include <cstring>
#include <string_view>

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
// the next recovery wait or WatchLoop iteration.
constexpr int kNotifyChange = 1;
constexpr int kNotifyFullRescan = 2;
constexpr int kPostRetries = 2;
constexpr DWORD kPostRetrySleepMs = 250;
constexpr DWORD kPendingNotifyRetryInitialMs = 1000;
constexpr DWORD kPendingNotifyRetryMaxMs = 30000;

// NR-101: delivers a change/full-rescan intent to the host through the existing
// message path, retaining it when the post fails. Coalescing lets a full rescan
// dominate a normal change, so a full-rescan intent is never downgraded. Only
// the watcher thread touches pending_notify; a permanently invalid window
// (teardown) stops delivery quietly.
void PostNotification(CatalogWatcher::Watch& watch, int level) {
    if (!watch.window || !IsWindow(watch.window)) {
        watch.pending_notify.store(0);
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

// True when this batch of change records contains anything the catalog cares
// about. Every add/remove/rename counts; a content change counts only for a
// file whose extension is in the allowlist. Without this, a single unrelated
// file rewrite inside a watched tree (Ditto's clipboard database living under a
// watched program folder -- one rewrite per copy) drives a full rebuild of that
// source, several hundred ms of work plus its diagnostic writes, on a loop.
bool HasCatalogRelevantChange(const BYTE* buffer, DWORD bytes,
                              const std::vector<std::wstring>& extensions) {
    if (extensions.empty() || bytes < sizeof(FILE_NOTIFY_INFORMATION)) {
        return true;  // no allowlist (or nothing parseable): keep the old behavior
    }
    DWORD offset = 0;
    for (;;) {
        if (bytes - offset < offsetof(FILE_NOTIFY_INFORMATION, FileName)) {
            return true;  // truncated record: cannot classify, so do not drop it
        }
        FILE_NOTIFY_INFORMATION info{};
        std::memcpy(&info, buffer + offset, offsetof(FILE_NOTIFY_INFORMATION, FileName));
        const DWORD name_bytes = info.FileNameLength;
        if (name_bytes > bytes - offset - offsetof(FILE_NOTIFY_INFORMATION, FileName)) {
            return true;
        }
        if (info.Action != FILE_ACTION_MODIFIED) {
            return true;  // a name appeared, vanished or changed: always relevant
        }
        const std::wstring_view name(
            reinterpret_cast<const wchar_t*>(buffer + offset +
                                             offsetof(FILE_NOTIFY_INFORMATION, FileName)),
            name_bytes / sizeof(wchar_t));
        const std::size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring_view::npos) {
            const std::wstring_view extension = name.substr(dot);
            for (const std::wstring& allowed : extensions) {
                if (extension.size() == allowed.size() &&
                    CompareStringOrdinal(extension.data(),
                                         static_cast<int>(extension.size()),
                                         allowed.c_str(),
                                         static_cast<int>(allowed.size()),
                                         TRUE) == CSTR_EQUAL) {
                    return true;
                }
            }
        }
        if (info.NextEntryOffset == 0) {
            return false;  // every record was an unrelated content change
        }
        if (info.NextEntryOffset > bytes - offset) {
            return true;
        }
        offset += info.NextEntryOffset;
    }
}

void WatchLoop(std::shared_ptr<CatalogWatcher::Watch> watch) {
    // Identifies this thread to Win32 tooling as app-owned: the CRT's
    // _beginthreadex trampoline is the reported Win32 start address for every
    // std::thread on this toolchain, so start-address alone cannot distinguish
    // it from an OS-injected worker.
    SetThreadDescription(GetCurrentThread(), L"NimbleRun.Watcher");
    try {
        std::vector<BYTE> buffer(kBufferBytes);
        const HANDLE completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!completion) {
            return;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = completion;
        // NR-074: one full-rescan notice per failure episode. A persistent error
        // (root removed, access denied) must not post a marker every second --
        // that drives a 1 Hz rebuild loop in the host (§FR-008/NFR-002).
        bool reported = false;
        DWORD pending_retry_ms = kPendingNotifyRetryInitialMs;
        for (;;) {
            if (watch->stop.load()) {
                break;
            }
            // NR-101/105: re-deliver an intent a failed post retained earlier.
            // PostNotification guards an invalid window itself.
            if (watch->pending_notify.load() != 0) {
                PostNotification(*watch, watch->pending_notify.load());
            }
            ResetEvent(completion);
            const BOOL started = ReadDirectoryChangesW(
                watch->directory,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                watch->recursive ? TRUE : FALSE,
                kNotifyFilter,
                nullptr,
                &overlapped,
                nullptr);
            const DWORD start_error = started == FALSE ? GetLastError() : ERROR_SUCCESS;
            // Stop() can win the race immediately before this read starts, so its
            // first CancelIoEx may have seen no pending operation. Cancel again
            // after the call to close that gap before entering the wait.
            if (watch->stop.load()) {
                CancelIoEx(watch->directory, &overlapped);
            }
            if (started == FALSE && start_error != ERROR_IO_PENDING) {
                if (start_error == ERROR_OPERATION_ABORTED || watch->stop.load()) {
                    break;  // CancelIoEx from Stop(): normal shutdown
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

            for (;;) {
                const DWORD wait_ms = watch->pending_notify.load() != 0
                                          ? pending_retry_ms
                                          : INFINITE;
                const DWORD wait = WaitForSingleObject(completion, wait_ms);
                if (wait == WAIT_TIMEOUT) {
                    PostNotification(*watch, watch->pending_notify.load());
                    if (watch->pending_notify.load() != 0) {
                        // ponytail: conditional exponential wait, capped at 30s;
                        // no idle wakeups when there is no retained intent.
                        pending_retry_ms = pending_retry_ms < kPendingNotifyRetryMaxMs
                                               ? pending_retry_ms * 2
                                               : kPendingNotifyRetryMaxMs;
                    } else {
                        pending_retry_ms = kPendingNotifyRetryInitialMs;
                    }
                    continue;
                }
                if (wait != WAIT_OBJECT_0) {
                    CloseHandle(completion);
                    return;
                }
                break;
            }
            if (watch->stop.load()) {
                break;
            }

            DWORD bytes_returned = 0;
            if (GetOverlappedResult(watch->directory, &overlapped, &bytes_returned, FALSE) == FALSE) {
                const DWORD error = GetLastError();
                if (error == ERROR_OPERATION_ABORTED || watch->stop.load()) {
                    break;  // CancelIoEx from Stop(): normal shutdown
                }
                if (!reported) {
                    PostNotification(*watch, kNotifyFullRescan);
                }
                reported = true;
                Sleep(1000);
                continue;
            }
            pending_retry_ms = kPendingNotifyRetryInitialMs;
            reported = false;
            if (bytes_returned == 0) {
                // Buffer overflow: the event list is incomplete, rescan the source.
                PostNotification(*watch, kNotifyFullRescan);
                continue;
            }
            if (HasCatalogRelevantChange(buffer.data(), bytes_returned,
                                         watch->extensions)) {
                PostNotification(*watch, kNotifyChange);
            }
        }
        CloseHandle(completion);
    } catch (...) {
        // NR-175: an allocation failure inside the body (64 KiB buffer, STL
        // work) must not escape a std::thread entry to std::terminate. Return
        // quietly: this watcher ends and Stop()'s join completes normally.
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
                              const std::vector<bool>& recursive,
                              const std::vector<std::wstring>& extensions) {
    Stop();
    watches_.clear();
    const std::size_t count = roots.size();
    for (std::size_t i = 0; i < count; ++i) {
        auto watch = std::make_unique<Watch>();
        watch->path = roots[i];
        watch->recursive = i < recursive.size() ? recursive[i] : true;
        watch->extensions = extensions;
        watch->directory = CreateFileW(
            watch->path.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
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
