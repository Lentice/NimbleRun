#include "catalog/directory_walker.h"

namespace nimblerun {
namespace {

bool WalkDirectoryAtDepth(const std::wstring& directory, const WalkOptions& options,
                          const FileVisitor& on_file,
                          const DirectoryUnavailableHook& on_directory_unavailable,
                          int depth) {
    if (options.cancel && options.cancel->load()) {
        return false;  // NR-098: cancelled before this subtree: report failure
    }
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        if (on_directory_unavailable) {
            on_directory_unavailable();
        }
        return true;  // missing/unreadable directory: skip this subtree
    }

    bool failed = false;
    do {
        if (options.cancel && options.cancel->load()) {
            failed = true;  // NR-098: cancelled mid-walk: no partial commit
            break;
        }
        const std::wstring name = find_data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = directory + L"\\" + name;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (depth < options.max_depth &&
                (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever; reparse-point app dirs are not a real source.
                if (!WalkDirectoryAtDepth(full, options, on_file,
                                          on_directory_unavailable, depth + 1)) {
                    failed = true;  // NR-091/092: child failure reaches caller
                }
            }
            continue;
        }
        on_file(full, find_data.dwFileAttributes);
    } while (FindNextFileW(find, &find_data) != FALSE);

    // NR-091/092: FALSE is a clean end only when the list ran out; any other
    // error means this directory was not fully read.
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        failed = true;
    }
    FindClose(find);
    return !failed;
}

} // namespace

bool WalkDirectory(const std::wstring& directory, const WalkOptions& options,
                   const FileVisitor& on_file,
                   const DirectoryUnavailableHook& on_directory_unavailable) {
    return WalkDirectoryAtDepth(directory, options, on_file,
                                 on_directory_unavailable, 0);
}

} // namespace nimblerun
