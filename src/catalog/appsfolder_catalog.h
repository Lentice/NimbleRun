#pragma once

#include "catalog/app_entry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nimblerun {

// Result of one AppsFolder enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained; failed_items counts
// child items dropped for a Shell error so callers can record partial results
// without crashing (design-spec §11).
struct AppsFolderEnumerateResult {
    std::vector<AppEntry> entries;
    std::size_t failed_items = 0;
};

// Enumerates packaged / Store apps through the FOLDERID_AppsFolder Shell
// namespace. Never touches WindowsApps or the packaged directory's EXEs. A
// source-level failure yields an empty result so other sources are untouched; a
// single bad child is skipped and counted.
AppsFolderEnumerateResult EnumerateAppsFolderCatalog();

// Builds one AppEntry from a child's already-extracted Shell names. Returns
// false and does not modify out when the child data is unusable (empty display
// name or parsing name), so callers skip and count the failure without aborting
// the walk. launch_identity and source_path carry the Shell parsing name, which
// is both the canonical launch identity (§FR-006, §10.3) and enough identity for
// a later icon query.
bool BuildAppsFolderEntry(const std::wstring& display_name,
                          const std::wstring& parsing_name,
                          AppEntry& out);

} // namespace nimblerun
