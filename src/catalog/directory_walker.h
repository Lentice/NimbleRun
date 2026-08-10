#pragma once

#include <atomic>
#include <functional>
#include <string>

#include <windows.h>

namespace nimblerun {

struct WalkOptions {
    bool recursive = true;
    std::atomic<bool>* cancel = nullptr;
};

using FileVisitor = std::function<void(const std::wstring&, DWORD)>;
using MissingDirectoryHook = std::function<void()>;

// Returns false only when cancellation or an enumeration error prevents a
// started walk from finishing cleanly. Missing directories are clean skips.
bool WalkDirectory(const std::wstring& directory, const WalkOptions& options,
                   const FileVisitor& on_file,
                   const MissingDirectoryHook& on_missing_directory = {});

} // namespace nimblerun
