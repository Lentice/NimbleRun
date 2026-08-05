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
// name or parsing name) or when the parsing name is not a program-like target
// (design-spec §FR-004a, shared app_filter module), so callers skip and count
// the skip without aborting the walk. launch_identity is the Shell-launchable
// "shell:AppsFolder\" + parsing name (§FR-006); source_path keeps the bare
// parsing name for display, and the stable id is hashed from the bare parsing
// name only (§10.3, zero migration).
bool BuildAppsFolderEntry(const std::wstring& display_name,
                          const std::wstring& parsing_name,
                          AppEntry& out);

} // namespace nimblerun
