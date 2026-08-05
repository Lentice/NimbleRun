#pragma once

#include <windows.h>

namespace nimblerun {

// Canonical id for NimbleRun's single global hotkey.
inline constexpr int kGlobalHotkeyId = 1;
// Temporary id used to prove a proposed combo is registerable before the
// previous combo is released during a swap.
inline constexpr int kProbeHotkeyId = 2;

inline constexpr UINT kDefaultHotkeyModifiers = MOD_ALT | MOD_NOREPEAT;
inline constexpr UINT kDefaultHotkeyVk = VK_SPACE;

struct HotkeyBinding {
    UINT modifiers;
    UINT virtual_key;
};

struct HotkeyResult {
    bool success = false;
    DWORD error = ERROR_SUCCESS;
};

// Owns the app's single global hotkey registration. Swap semantics register
// the proposed binding FIRST and only release the previous one after the new
// one succeeds, so a rejected or Windows-reserved combo never kills the
// working hotkey. The active combo alternates between two ids; WM_HOTKEY must
// be matched against ActiveId(). No low-level keyboard hook, no retry, no
// silent fallback.
class GlobalHotkey {
public:
    // Registers `binding` under kGlobalHotkeyId. On failure the instance stays
    // inactive with the Win32 error recorded; the caller keeps the tray alive.
    HotkeyResult Initialize(HWND window, const HotkeyBinding& binding);

    // Registers `proposed` first; unregisters the previous binding only after
    // the new one succeeds. On failure the current binding remains active.
    HotkeyResult Swap(const HotkeyBinding& proposed);

    // Releases the registration, if any. Safe to call repeatedly and after a
    // failed initialization.
    void Shutdown();

    bool IsActive() const { return active_; }
    int ActiveId() const { return active_id_; }
    const HotkeyBinding& Current() const { return current_; }

private:
    HWND window_ = nullptr;
    bool active_ = false;
    int active_id_ = kGlobalHotkeyId;
    HotkeyBinding current_{};
};

} // namespace nimblerun
