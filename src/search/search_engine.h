#pragma once

#include "catalog/app_entry.h"

#include <string_view>
#include <vector>

namespace nimblerun {

// Collapses runs of whitespace to a single space, trims the ends, and maps to
// invariant lowercase. Catalog names are normalized once per snapshot with this
// and stored in AppEntry::normalized_name; the query is normalized with the same
// function on every keystroke, so the two can never drift apart.
std::wstring NormalizeName(std::wstring_view value);

// Searches only the supplied App Catalog. It never interprets query text as a command.
std::vector<AppEntry> SearchApps(const std::vector<AppEntry>& catalog,
                                 std::wstring_view query);

} // namespace nimblerun
