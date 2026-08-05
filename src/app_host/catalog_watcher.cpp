#include "app_host/catalog_watcher.h"

#include <windows.h>

#include <thread>
#include <utility>

namespace nimblerun {
namespace {

constexpr DWORD kNotifyFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;
constexpr DWORD kBufferBytes = 64 * 1024;

void WatchLoop(std::shared_ptr<CatalogWatcher::Watch> watch) {
    std::vector<BYTE> buffer(kBufferBytes);
    for (;;) {
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
            // of busy-looping.
            if (watch->window && IsWindow(watch->window)) {
                PostMessageW(watch->window, watch->message,
                             static_cast<WPARAM>(watch->index), 1);
            }
            Sleep(1000);
            continue;
        }
        if (bytes_returned == 0) {
            // Buffer overflow: the event list is incomplete, rescan the source.
            if (watch->window && IsWindow(watch->window)) {
                PostMessageW(watch->window, watch->message,
                             static_cast<WPARAM>(watch->index), 1);
            }
            continue;
        }
        if (watch->window && IsWindow(watch->window)) {
            PostMessageW(watch->window, watch->message,
                         static_cast<WPARAM>(watch->index), 0);
        }
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
        shared->thread = std::thread(WatchLoop, shared);
        watches_.push_back(std::move(shared));    }
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
