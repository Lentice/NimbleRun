#pragma once

#include <windows.h>

#include <string>

namespace nimblerun {

// Injectable registry location for the per-user startup entry. The app uses
// the default HKCU Run key; tests point at an isolated
// HKCU\Software\NimbleRunTest\<pid> key so the real Run key is never touched.
// All registry access goes through `base`, so a HKCU base guarantees the
// change is per-user: this module has no HKLM code path at all.
struct StartupOptionRegistry {
    HKEY base = HKEY_CURRENT_USER;
    std::wstring subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
};

// Enables or disables NimbleRun at sign-in. Enable writes (or re-creates, e.g.
// after the EXE moved) a REG_SZ "NimbleRun" value pointing at the current
// executable; disable deletes only that value, never the Run key or any other
// entry. Returns false on a registry failure.
bool SetStartupEnabled(bool enabled, const StartupOptionRegistry& registry = {});

} // namespace nimblerun
