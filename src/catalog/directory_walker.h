#pragma once

#include <atomic>
#include <functional>
#include <string>

#include <windows.h>

namespace nimblerun {

struct WalkOptions {
    int max_depth = 20;
    std::atomic<bool>* cancel = nullptr;
};

using FileVisitor = std::function<void(const std::wstring&, DWORD)>;
using DirectoryUnavailableHook = std::function<void()>;

// Returns false only when cancellation or an enumeration error prevents a
// started walk from finishing cleanly. Missing directories are clean skips.
bool WalkDirectory(const std::wstring& directory, const WalkOptions& options,
                   const FileVisitor& on_file,
                   const DirectoryUnavailableHook& on_directory_unavailable = {});

} // namespace nimblerun
