#pragma once

#include "catalog/app_entry.h"
#include "settings/settings_store.h"

#include <atomic>
#include <string>
#include <vector>

namespace nimblerun {

// Result of one UserFolder enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained. source_ok is false when
// an open directory's walk failed mid-enumeration (a FindNextFileW error other
// than the clean ERROR_NO_MORE_FILES end, including a recursive child's,
// NR-092); the caller keeps the source's old entries in that case
// (design-spec §FR-008). Missing, unreadable and non-local roots, bad
// subdirectories and anomalous files are still clean skips that never clear
// source_ok (NR-063).
struct UserFolderEnumerateResult {
    std::vector<AppEntry> entries;
    bool source_ok = true;
};

// Enumerates launchable apps from every user-configured local folder in
// `settings` (design-spec §FR-005). Each CatalogRoot's `recursive` flag
// controls subfolder scanning; only files whose extension is in
// `settings.catalog_extensions` (matched case-insensitively, falling back to
// the full allowlist when empty) enter the catalog. Returns plain copyable
// AppEntry values with source UserFolder, a display name of the file name
// without extension, source_path and launch_identity equal to the full file
// path (Shell-launchable), and a stable id hashed from that path (§10.3).
// .exe/.cmd/.bat must be readable regular files; .lnk/.appref-ms are kept for
// Shell validation at launch time. `cancel` is an optional cooperative
// cancellation token (NR-098): when set the walk stops at the next safe
// iteration boundary, reports source_ok = false and commits nothing.
UserFolderEnumerateResult EnumerateUserFolderCatalog(const Settings& settings,
                                                     std::atomic<bool>* cancel = nullptr);

} // namespace nimblerun
