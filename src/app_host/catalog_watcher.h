#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nimblerun {

// Watches Start Menu and configured user-folder roots with ReadDirectoryChangesW
// (design-spec §FR-008). One background thread per watched root; each thread
// posts a message to `notify_window` on change. A buffer overflow or
// ERROR_NOTIFY_ENUM_DIR is reported as a full-rescan marker so the caller can
// rescan the whole source instead of trusting the event list. No fixed timer.
class CatalogWatcher {
public:
    // `message` is a registered/APP message whose wParam is the 1-based watch
    // index and whose lParam is 1 for full rescan, 0 for a normal change.
    CatalogWatcher(HWND notify_window, UINT message);
    ~CatalogWatcher();

    CatalogWatcher(const CatalogWatcher&) = delete;
    CatalogWatcher& operator=(const CatalogWatcher&) = delete;

    // Replaces the watched set. Any previous watches are stopped first.
    // `extensions` is the catalog's file-extension allowlist (".lnk", ".exe",
    // ...). It filters CONTENT-change events only: a watched tree usually holds
    // files the catalog never indexes (a clipboard manager's database inside a
    // program folder, a build tree), and rewriting one of those must not cost a
    // full source rebuild. Name changes (add/remove/rename) always notify, and
    // an empty list disables the filter.
    void SetRoots(const std::vector<std::wstring>& roots,
                  const std::vector<bool>& recursive,
                  const std::vector<std::wstring>& extensions = {});

    void Stop();

    struct Watch {
        std::wstring path;
        bool recursive = true;
        // Read by the watcher thread only, and never mutated after the thread
        // starts (SetRoots builds a whole new watch set).
        std::vector<std::wstring> extensions;
        HANDLE directory = INVALID_HANDLE_VALUE;
        std::atomic<bool> stop{false};
        std::thread thread;
        HWND window = nullptr;
        UINT message = 0;
        int index = 0;
        // NR-101: delivery intent retained when a PostMessageW fails. 0 = nothing
        // retained, 1 = a normal change, 2 = a full-rescan marker; a full rescan
        // dominates a normal change when coalescing. Only touched by the watcher
        // thread.
        std::atomic<int> pending_notify{0};
    };

private:
    HWND window_ = nullptr;
    UINT message_ = 0;
    std::vector<std::shared_ptr<Watch>> watches_;
};

} // namespace nimblerun
