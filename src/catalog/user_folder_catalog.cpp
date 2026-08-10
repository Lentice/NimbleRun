#include "catalog/user_folder_catalog.h"

#include "catalog/stable_id.h"

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

std::wstring ToLower(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        out.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return out;
}

std::wstring_view FileName(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"/\\");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

// File name without the final extension, e.g. "Notepad.exe" -> "Notepad".
std::wstring FileStem(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    return std::wstring(dot == std::wstring_view::npos ? name : name.substr(0, dot));
}

// Lowercased extension including the dot, or empty when none. A trailing dot in
// a directory name is not treated as an extension.
std::wstring Extension(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos) {
        return {};
    }
    return ToLower(name.substr(dot));
}

bool ExtensionAllowed(std::wstring_view path, const std::vector<std::wstring>& extensions) {
    const std::wstring ext = Extension(path);
    for (const std::wstring& allowed : extensions) {
        if (ext == allowed) {
            return true;
        }
    }
    return false;
}

// FR-005: .exe/.cmd/.bat must be readable regular files; .lnk/.appref-ms are
// validated by the Shell at launch time and always kept.
bool IsReadableRegularFile(const std::wstring& path, DWORD find_attributes) {
    if ((find_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    if ((find_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;  // a symlink/junction is not a regular file
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;  // unreadable/locked file: skip this item, keep enumerating
    }
    CloseHandle(file);
    return true;
}

void ProcessFile(const std::wstring& path, DWORD find_attributes, std::vector<AppEntry>& out) {
    const std::wstring ext = Extension(path);
    const bool shell_validated = ext == L".lnk" || ext == L".appref-ms";
    if (!shell_validated && !IsReadableRegularFile(path, find_attributes)) {
        return;
    }
    AppEntry entry;
    entry.display_name = FileStem(path);
    entry.source = AppSource::UserFolder;
    entry.source_path = path;
    // The file path is the Shell-launchable identity (NR-008).
    entry.launch_identity = path;
    // ponytail: .lnk/.appref-ms are keyed by their own path, not the §10.3
    // resolved target (resolving needs Shell COM at scan time); cross-source
    // match with a Start Menu shortcut is therefore conservative and NR-007
    // keeps both when identities differ. Full resolution is a follow-up.
    entry.stable_id = HashStableId(NormalizePathKey(path));
    out.push_back(std::move(entry));
}

// Returns false only when the walk started but did not finish cleanly
// (NR-092): a mid-walk FindNextFileW failure, including a recursive child's,
// poisons the whole source so the caller keeps the old entries. A missing or
// unreadable directory is still a clean skip (NR-063 empty-walk success). A
// set `cancel` token also yields false at the next safe iteration boundary
// (NR-098), so the collected prefix is never committed as a complete source.
bool ScanDirectory(const std::wstring& directory, bool recursive,
                   const std::vector<std::wstring>& extensions,
                   std::vector<AppEntry>& out, std::atomic<bool>* cancel,
                   std::size_t& skipped_directories) {
    if (cancel && cancel->load()) {
        return false;  // NR-098: cancelled before this subtree: report failure
    }
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        // NR-124: count the skip (design-spec §11 "記錄一次"); the caller
        // reports it as a diagnostic, never logs here.
        ++skipped_directories;
        return true;  // missing/unreadable directory: skip this subtree, keep others
    }
    bool failed = false;
    do {
        if (cancel && cancel->load()) {
            failed = true;  // NR-098: cancelled mid-walk: no partial commit
            break;
        }
        const std::wstring name = find_data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = directory + L"\\" + name;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (recursive && (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever (design-spec §FR-005).
                if (!ScanDirectory(full, recursive, extensions, out, cancel,
                                   skipped_directories)) {
                    failed = true;  // NR-092: a child's failure must reach the caller
                }
            }
            continue;
        }
        if (!ExtensionAllowed(full, extensions)) {
            continue;
        }
        ProcessFile(full, find_data.dwFileAttributes, out);
    } while (FindNextFileW(find, &find_data) != FALSE);
    // NR-092: FALSE is a clean end only when it means the list ran out; any
    // other error (I/O, access) means this directory was not fully read, so the
    // collected prefix must not be committed as a complete source.
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        failed = true;
    }
    FindClose(find);
    return !failed;
}

} // namespace

UserFolderEnumerateResult EnumerateUserFolderCatalog(const Settings& settings,
                                                     std::atomic<bool>* cancel) {
    UserFolderEnumerateResult result;
    if (cancel && cancel->load()) {
        result.source_ok = false;  // NR-098: cancelled: keep old entries
        return result;
    }
    std::vector<std::wstring> extensions = settings.catalog_extensions;
    if (extensions.empty()) {
        extensions = DefaultExtensions();  // full allowlist (design-spec §FR-005)
    }
    for (std::wstring& ext : extensions) {
        ext = ToLower(ext);  // allowlist matching is case-insensitive
    }
    for (const CatalogRoot& root : settings.catalog_roots) {
        if (!IsLocalAbsolutePath(root.path)) {
            continue;  // defensive: settings validated at load; skip UNC/URI/device paths
        }
        // ponytail: duplicate roots are scanned once each, so a root listed
        // twice yields duplicate entries; NR-007 dedups across sources.
        if (!ScanDirectory(root.path, root.recursive, extensions, result.entries, cancel,
                           result.skipped_directories)) {
            result.source_ok = false;  // NR-092: mid-walk failure: keep old entries
            if (cancel && cancel->load()) {
                return result;  // NR-098: cancelled: stop scanning further roots
            }
        }
    }
    return result;
}

} // namespace nimblerun
