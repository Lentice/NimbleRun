#pragma once

#include "catalog/app_entry.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace nimblerun {

// Result of one Start Menu enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained. source_ok is false for a
// source-level failure (COM unavailable, neither Programs known folder could be
// resolved, or a Programs directory walk failed mid-enumeration); the caller
// keeps the source's old entries in that case (design-spec §FR-008).
// corrupt_links counts unloadable .lnk files skipped mid-walk (design-spec §11
// "記錄錯誤，繼續掃描"); NR-124: reported for diagnostics, never logged here.
struct StartMenuEnumerateResult {
    std::vector<AppEntry> entries;
    std::size_t corrupt_links = 0;
    bool source_ok = true;
};

// Enumerates launchable apps from the current user's and all users' Start Menu
// Programs Known Folders (FOLDERID_Programs, FOLDERID_CommonPrograms), resolved
// via the Shell Known Folder API. Returns plain copyable AppEntry values with
// no Shell COM pointer retained. `cancel` is an optional cooperative
// cancellation token (NR-098): when set the walk stops at the next safe
// iteration boundary, reports source_ok = false and commits nothing.
StartMenuEnumerateResult EnumerateStartMenuCatalog(std::atomic<bool>* cancel = nullptr);

// Core recursive enumeration of one Programs-style directory. Injectable so
// tests can drive the logic with a synthetic fixture instead of the real Start
// Menu. A missing or unreadable root yields nothing and is still a clean end
// (NR-063); a single corrupt shortcut is skipped, counted into `corrupt_links`
// when non-null, and never aborts the walk.
// Returns false only when the walk started but failed mid-enumeration (NR-091)
// or was cancelled (NR-098): the collected prefix must not be committed as a
// complete source. `cancel` is optional (nullptr = no cancellation).
bool EnumerateProgramsDirectory(const std::wstring& root,
                                AppSource source,
                                std::vector<AppEntry>& out,
                                std::atomic<bool>* cancel = nullptr,
                                std::size_t* corrupt_links = nullptr);

} // namespace nimblerun
