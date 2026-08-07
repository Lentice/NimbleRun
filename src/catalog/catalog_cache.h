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
// silently. NR-079: when the file is from a *newer* schema, `newer_schema` (if
// non-null) is set to true so the caller can stop overwriting it (design-spec
// §10.4 forbids overwriting a newer schema's cache).
bool LoadCatalogCache(const std::wstring& directory, std::vector<AppEntry>& out,
                      bool* newer_schema = nullptr);

} // namespace nimblerun
