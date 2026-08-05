#pragma once

#include "catalog/app_entry.h"

#include <string>
#include <vector>

namespace nimblerun {

// Versioned catalog cache under the per-user data dir (design-spec §10.2):
// a speed-only snapshot of the merged catalog, never a source of truth. Read
// errors are tolerated by simply rebuilding. Uses the same tmp + flush +
// atomic replace conventions as settings.ini / usage.tsv.
void SaveCatalogCache(const std::wstring& directory, const std::vector<AppEntry>& entries);

// Returns true when a valid cache was loaded into `out`; false when the file is
// missing, corrupt, or from a newer schema (the caller rebuilds in the
// background). A corrupt file is renamed aside for diagnostics, never deleted
// silently.
bool LoadCatalogCache(const std::wstring& directory, std::vector<AppEntry>& out);

} // namespace nimblerun
