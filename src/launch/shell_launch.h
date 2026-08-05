#pragma once

#include "catalog/app_entry.h"

#include <windows.h>

namespace nimblerun {

// Result of one Shell launch request. error_code is ERROR_SUCCESS on success;
// on failure it is the Win32 error from GetLastError, or
// ERROR_INVALID_PARAMETER when the entry was rejected without touching the
// Shell.
struct LaunchResult {
    bool ok = false;
    DWORD error_code = ERROR_SUCCESS;
};

// Launches a catalog entry's launch_identity through the Windows Shell with
// ShellExecuteExW (Unicode). The identity is always a catalog value: a full
// Start Menu shortcut / user-folder file path (.lnk/.exe/.cmd/.bat) or an
// AppsFolder Shell parsing name. No command line is ever built from search
// input; the Shell resolves the default verb for the identity. An entry with an
// empty launch_identity (invalid/unresolved) is rejected without any Shell
// call. Requires the calling thread to have initialized STA COM with
// COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE, as the UI thread does
// (design-spec §FR-010). No process handle is taken: the default path avoids
// SEE_MASK_NOCLOSEPROCESS, so the target process lifetime is the Shell's
// concern, not the caller's.
LaunchResult LaunchEntry(const AppEntry& entry, HWND owner = nullptr);

} // namespace nimblerun
