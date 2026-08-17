#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nimblerun {

// MVP settings; keys and schema defined in design-spec §FR-013 and §10.
enum class Theme {
    System,
    Light,
    Dark,
};

// One user-configured local folder root. The path is validated to be an
// absolute local path on load; `recursive` controls subfolder scanning.
struct CatalogRoot {
    std::wstring path;
    bool recursive = true;
};

// Supported launchable extensions for user folders (design-spec §FR-005).
// Loaded/saved as lowercase, dot-prefixed.
std::vector<std::wstring> DefaultExtensions();

// Recent-apps count bounds (design-spec §FR-013). One definition shared by
// SettingsStore::Load (range check) and SettingsEditor::SetRecentCount.
inline constexpr int kMinRecentCount = 8;
inline constexpr int kMaxRecentCount = 40;

// True when value is a local drive-letter absolute path (e.g. C:\Tools).
// UNC, network, URI and device paths are rejected (design-spec §FR-005).
// Shared with the user-folder catalog enumerator for its defensive guard.
bool IsLocalAbsolutePath(std::wstring_view value);

// True except for DRIVE_REMOTE. Mapped network drives fail FR-005 (local
// paths only); disconnected local volumes (DRIVE_NO_ROOT_DIR / DRIVE_UNKNOWN)
// stay acceptable so a missing root is skipped by the enumerator (NR-092)
// rather than rejected here. Pure predicate over the GetDriveTypeW result, so
// the decision table is testable without creating a real network drive.
bool IsAcceptableDriveType(DWORD drive_type);

// NR-140: settings.ini is untrusted input (design-spec §10.4). These caps keep
// a crafted file from spawning a watcher thread per root at startup (each root
// becomes a CreateFileW + std::thread in main.cpp StartWatchers) and from
// pushing a huge value through ParseHotkey's per-'+' vector. Over-limit is
// whole-file Corrupt in Load; SettingsEditor::AddRoot enforces the same root
// cap on the write side (NR-152) so the UI cannot persist a file the next
// Load would quarantine.
inline constexpr std::size_t kMaxCatalogRoots = 32;
inline constexpr std::size_t kMaxHotkeyLength = 256;

struct Settings {
    std::wstring hotkey = L"Alt+Space";
    bool auto_start = false;
    Theme theme = Theme::System;
    int recent_count = 20;  // visible apps, validated to 8..40 on load
    bool hide_after_launch = true;
    bool include_windows_apps = true;  // AppsFolder source (design-spec §FR-013)
    // NR-190: switch the search box's IME to English/alphanumeric mode on a
    // hidden->visible panel show. Default off.
    bool english_input_on_show = false;
    std::vector<CatalogRoot> catalog_roots;
    std::vector<std::wstring> catalog_extensions = DefaultExtensions();
};

// Safe defaults used when the store is empty, corrupt, or from a newer schema.
Settings DefaultSettings();

// Per-user data root from design-spec §10.1: %LOCALAPPDATA%\NimbleRun.
// Returns an empty string when the Known Folder result is unavailable or not
// a bounded local absolute path. The test seam validates controlled failures
// without changing the process environment.
std::wstring UserDataDirFromLocalAppData(std::wstring_view local_app_data);
std::wstring DefaultSettingsDir();

// Why Load returned. For anything other than Loaded the out parameter holds
// DefaultSettings(), never a partial parse.
enum class SettingsLoadResult {
    Loaded,       // settings.ini parsed and applied
    Missing,      // no settings.ini yet
    Corrupt,      // unreadable/malformed; original renamed to settings.ini.corrupt
    NewerSchema,  // schema version too new; original left untouched
};

// Atomic per-user settings store. No HWND or Shell dependencies.
class SettingsStore {
public:
    explicit SettingsStore(std::wstring directory);

    SettingsLoadResult Load(Settings& out) const;
    bool Save(const Settings& settings) const;

private:
    std::wstring directory_;
    // NR-096: true when the last Load() reported NewerSchema. The original file
    // is another build's data (design-spec §10.4); Save() must refuse to
    // overwrite it. Cleared by every non-NewerSchema Load outcome. mutable
    // because Save() and Load() are const.
    mutable bool write_protected_ = false;
};

} // namespace nimblerun
