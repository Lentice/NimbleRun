#include "catalog/directory_walker.h"

namespace nimblerun {
namespace {

bool Walk(const std::wstring& directory, const WalkOptions& options,
          const FileVisitor& on_file,
          const MissingDirectoryHook& on_missing_directory) {
    if (options.cancel && options.cancel->load()) {
        return false;  // NR-098: cancelled before this subtree: report failure
    }
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        if (on_missing_directory) {
            on_missing_directory();
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
            if (options.recursive &&
                (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever; reparse-point app dirs are not a real source.
                if (!Walk(full, options, on_file, on_missing_directory)) {
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
                   const MissingDirectoryHook& on_missing_directory) {
    return Walk(directory, options, on_file, on_missing_directory);
}

} // namespace nimblerun
