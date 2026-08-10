#pragma once

#include "catalog/app_entry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nimblerun {

// NR-121: catalog.cache is untrusted on-disk input (NR-070); a hand-edited or
// stale file must not freeze the cold-start UI thread with an unbounded
// load + dedup (design-spec §11). A cache with more rows than this cap is
// treated as Malformed (quarantined via PreserveCorrupt, rebuilt). Rows, not
// bytes: the icon pack's 32 MiB is a byte budget, this is a row-count budget.
// 20,000 is a generous multiple of FR-003's <5k enumerator scale with headroom.
constexpr std::size_t kMaxCacheRows = 20000;

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
