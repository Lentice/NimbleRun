#pragma once

#include "catalog/app_entry.h"
#include "settings/settings_store.h"

#include <string>
#include <vector>

namespace nimblerun {

// Enumerates launchable apps from every user-configured local folder in
// `settings` (design-spec §FR-005). Each CatalogRoot's `recursive` flag
// controls subfolder scanning; only files whose extension is in
// `settings.catalog_extensions` (matched case-insensitively, falling back to
// the full allowlist when empty) enter the catalog. Returns plain copyable
// AppEntry values with source UserFolder, a display name of the file name
// without extension, source_path and launch_identity equal to the full file
// path (Shell-launchable), and a stable id hashed from that path (§10.3).
// .exe/.cmd/.bat must be readable regular files; .lnk/.appref-ms are kept for
// Shell validation at launch time. A missing, unreadable or non-local root, a
// bad subdirectory, or an anomalous file is skipped without aborting or
// clearing the other roots' results.
std::vector<AppEntry> EnumerateUserFolderCatalog(const Settings& settings);

} // namespace nimblerun
