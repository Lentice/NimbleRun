#include "catalog/user_folder_catalog.h"

#include "catalog/app_filter.h"
#include "catalog/directory_walker.h"
#include "catalog/stable_id.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

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
        const bool ok = WalkDirectory(
            root.path, {root.recursive, cancel},
            [&](const std::wstring& path, DWORD attributes) {
                if (ExtensionAllowed(path, extensions)) {
                    ProcessFile(path, attributes, result.entries);
                }
            },
            [&result] {
                // NR-124: count the skip (design-spec §11 "記錄一次"); the caller
                // reports it as a diagnostic, never logs here.
                ++result.skipped_directories;
            });
        if (!ok) {
            result.source_ok = false;  // NR-092: mid-walk failure: keep old entries
            if (cancel && cancel->load()) {
                return result;  // NR-098: cancelled: stop scanning further roots
            }
        }
    }
    return result;
}

} // namespace nimblerun
