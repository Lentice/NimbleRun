#include "launch/shell_launch.h"

#include <shellapi.h>

namespace nimblerun {

LaunchResult LaunchEntry(const AppEntry& entry, HWND owner) {
    if (entry.launch_identity.empty() || !entry.launch_verified) {
        return {false, ERROR_INVALID_PARAMETER};
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // fMask 0: no SEE_MASK_NOCLOSEPROCESS, so no process handle to leak; the
    // Shell owns the launched process (design-spec §FR-010).
    sei.fMask = 0;
    sei.hwnd = owner;
    sei.lpFile = entry.launch_identity.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        return {true, ERROR_SUCCESS};
    }
    return {false, GetLastError()};
}

} // namespace nimblerun
