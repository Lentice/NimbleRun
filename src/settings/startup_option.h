#pragma once

#include <windows.h>

#include <string>

namespace nimblerun {

// Result of reading NimbleRun's per-user Run entry (design-spec §FR-012).
enum class StartupStatus {
    Disabled,      // no "NimbleRun" value under the Run key
    Enabled,       // value points at this executable's current path
    EnabledMoved,  // value points at another path (EXE moved / not our entry)
    UnknownError,  // the Run key or value could not be read
};

// Injectable registry location for the per-user startup entry. The app uses
// the default HKCU Run key; tests point at an isolated
// HKCU\Software\NimbleRunTest\<pid> key so the real Run key is never touched.
// All registry access goes through `base`, so a HKCU base guarantees the
// change is per-user: this module has no HKLM code path at all.
struct StartupOptionRegistry {
    HKEY base = HKEY_CURRENT_USER;
    std::wstring subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
};

// Reads the "NimbleRun" value under the Run key and compares it with the
// current executable path. A missing value is Disabled.
StartupStatus GetStartupStatus(const StartupOptionRegistry& registry = {});

// Enables or disables NimbleRun at sign-in. Enable writes (or re-creates, e.g.
// after the EXE moved) a REG_SZ "NimbleRun" value pointing at the current
// executable; disable deletes only that value, never the Run key or any other
// entry. Returns false on a registry failure.
bool SetStartupEnabled(bool enabled, const StartupOptionRegistry& registry = {});

} // namespace nimblerun
