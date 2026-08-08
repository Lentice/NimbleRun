#pragma once

#include "catalog/app_entry.h"

#include <string>
#include <vector>

namespace nimblerun {

// Result of one Start Menu enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained. source_ok is false for a
// source-level failure (COM unavailable, neither Programs known folder could be
// resolved, or a Programs directory walk failed mid-enumeration); the caller
// keeps the source's old entries in that case (design-spec §FR-008).
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
// Menu. A missing or unreadable root yields nothing and is still a clean end
// (NR-063); a single corrupt shortcut is skipped and never aborts the walk.
// Returns false only when the walk started but failed mid-enumeration (NR-091):
// the collected prefix must not be committed as a complete source.
bool EnumerateProgramsDirectory(const std::wstring& root,
                                AppSource source,
                                std::vector<AppEntry>& out);

} // namespace nimblerun
