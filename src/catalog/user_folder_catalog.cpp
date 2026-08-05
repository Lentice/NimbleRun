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

void ScanDirectory(const std::wstring& directory, bool recursive,
                   const std::vector<std::wstring>& extensions, std::vector<AppEntry>& out) {
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        return;  // missing/unreadable directory: skip this subtree, keep others
    }
    do {
        const std::wstring name = find_data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = directory + L"\\" + name;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (recursive && (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever (design-spec §FR-005).
                ScanDirectory(full, recursive, extensions, out);
            }
            continue;
        }
        if (!ExtensionAllowed(full, extensions)) {
            continue;
        }
        ProcessFile(full, find_data.dwFileAttributes, out);
    } while (FindNextFileW(find, &find_data) != FALSE);
    FindClose(find);
}

} // namespace

std::vector<AppEntry> EnumerateUserFolderCatalog(const Settings& settings) {
    std::vector<AppEntry> out;
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
        ScanDirectory(root.path, root.recursive, extensions, out);
    }
    return out;
}

} // namespace nimblerun
