#pragma once

#include "catalog/app_entry.h"

#include <string_view>
#include <vector>

namespace nimblerun {

// Searches only the supplied App Catalog. It never interprets query text as a command.
std::vector<AppEntry> SearchApps(const std::vector<AppEntry>& catalog,
                                 std::wstring_view query);

} // namespace nimblerun
