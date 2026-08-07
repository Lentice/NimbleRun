#pragma once

#include "catalog/app_entry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nimblerun {

// Result of one AppsFolder enumeration pass. entries are plain copyable
// AppEntry values with no Shell COM pointer retained; failed_items counts
// child items dropped for a Shell error so callers can record partial results
// without crashing (design-spec §11). source_ok is false for a source-level
// failure (COM unavailable, known-folder lookup or BindToHandler failure); the
// caller keeps the source's old entries in that case (design-spec §FR-008).
struct AppsFolderEnumerateResult {
    std::vector<AppEntry> entries;
    std::size_t failed_items = 0;
    bool source_ok = true;
};

// Enumerates packaged / Store apps through the FOLDERID_AppsFolder Shell
// namespace. Never touches WindowsApps or the packaged directory's EXEs. A
// source-level failure yields an empty result with source_ok = false so other
// sources are untouched; a single bad child is skipped and counted.
//
// A child whose parsing name is an AUMID gets its source_path from the Shell's
// link-target property when it has one, so the row shows the real program path
// instead of the "Windows app" label. Display only, never the identity key.
AppsFolderEnumerateResult EnumerateAppsFolderCatalog();

// An AppsFolder parsing name for a legacy app is often Known Folder relative,
// e.g. "{6D809377-...}\Notepad++\notepad++.exe" (design-spec §2.6). Expands the
// prefix to a real absolute path. Empty for an AUMID, an already-absolute path,
// a malformed prefix, or an unknown/unavailable folder id. SIGDN_FILESYSPATH is
// not an option here: the Shell returns E_INVALIDARG for AppsFolder children.
std::wstring ExpandKnownFolderPrefix(const std::wstring& parsing_name);

// Builds one AppEntry from a child's already-extracted Shell names. Returns
// false and does not modify out when the child data is unusable (empty display
// name or parsing name) or when the parsing name is not a program-like target
// (design-spec §FR-004a, shared app_filter module), so callers skip and count
// the skip without aborting the walk. launch_identity is always the
// Shell-launchable "shell:AppsFolder\" + bare parsing name (§FR-006), never the
// resolved path.
//
// `resolved_path` is the child's parsing name with a leading Known Folder GUID
// expanded to a real absolute path (the enumerator does that, it needs Shell).
// Pass it empty for an AUMID or when expansion failed. When present it becomes
// source_path (so the row shows a real path instead of the source label,
// §4.2/§4.9) and the identity key, which is what lets one physical EXE reached
// through AppsFolder, a Start Menu shortcut and a user folder collapse to a
// single row (§FR-007). AUMIDs keep hashing from the bare parsing name.
bool BuildAppsFolderEntry(const std::wstring& display_name,
                          const std::wstring& parsing_name,
                          AppEntry& out,
                          const std::wstring& resolved_path = {});

} // namespace nimblerun
