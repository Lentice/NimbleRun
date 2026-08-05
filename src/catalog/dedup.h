#pragma once

#include "catalog/app_entry.h"

#include <cstddef>
#include <vector>

namespace nimblerun {

// Result of one deduplication pass over a merged catalog. entries keeps exactly
// one copy of every distinct app (the same physical app found by any source),
// chosen by precedence; the kept entry retains its own launch_identity /
// source_path so the Shell and icon cache see a real launchable item.
// removed_duplicates counts entries collapsed as exact duplicates.
// ambiguous_kept counts kept entries that could not be reliably judged equal to
// a same-display-name peer (e.g. a packaged app's Start Menu shortcut vs its
// AppsFolder item) and are therefore preserved rather than merged, with the
// case recorded for diagnostics (design-spec §FR-007 item 3). Entries are never
// merged on display name alone.
struct DedupResult {
    std::vector<AppEntry> entries;
    std::size_t removed_duplicates = 0;
    std::size_t ambiguous_kept = 0;
};

// Deduplicates one merged catalog. Entries with an equal stable id describe the
// same physical app and collapse to a single entry; among a collision the
// entry with the better launch/icon source precedence is kept (AppsFolder,
// then User Start Menu, then UserFolder, then Common Start Menu). Kept entries
// keep their input order, so the result is reproducible for a reproducible
// input. Pure value logic: no HWND, no Shell COM.
DedupResult DeduplicateCatalog(const std::vector<AppEntry>& entries);

} // namespace nimblerun
