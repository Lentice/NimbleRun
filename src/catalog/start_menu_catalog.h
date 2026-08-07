#pragma once

#include "catalog/app_entry.h"

#include <string>
#include <vector>

namespace nimblerun {

// Result of one Start Menu enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained. source_ok is false for a
// source-level failure (COM unavailable, or neither Programs known folder could
// be resolved); the caller keeps the source's old entries in that case
// (design-spec §FR-008).
struct StartMenuEnumerateResult {
    std::vector<AppEntry> entries;
    bool source_ok = true;
};

// Enumerates launchable apps from the current user's and all users' Start Menu
// Programs Known Folders (FOLDERID_Programs, FOLDERID_CommonPrograms), resolved
// via the Shell Known Folder API. Returns plain copyable AppEntry values with
// no Shell COM pointer retained.
StartMenuEnumerateResult EnumerateStartMenuCatalog();

// Core recursive enumeration of one Programs-style directory. Injectable so
// tests can drive the logic with a synthetic fixture instead of the real Start
// Menu. A missing or unreadable root yields nothing; a single corrupt shortcut
// is skipped and never aborts the walk.
void EnumerateProgramsDirectory(const std::wstring& root,
                                AppSource source,
                                std::vector<AppEntry>& out);

} // namespace nimblerun
