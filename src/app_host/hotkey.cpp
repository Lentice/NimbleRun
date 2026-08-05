#include "app_host/hotkey.h"

#include <windows.h>

namespace nimblerun {
namespace {

HotkeyResult RegisterBinding(HWND window, int id, const HotkeyBinding& binding) {
    HotkeyResult result;
    result.success = RegisterHotKey(window, id, binding.modifiers, binding.virtual_key) != 0;
    if (!result.success) {
        result.error = GetLastError();
    }
    return result;
}

bool SameBinding(const HotkeyBinding& left, const HotkeyBinding& right) {
    return left.modifiers == right.modifiers && left.virtual_key == right.virtual_key;
}

} // namespace

HotkeyResult GlobalHotkey::Initialize(HWND window, const HotkeyBinding& binding) {
    window_ = window;
    active_id_ = kGlobalHotkeyId;
    const HotkeyResult result = RegisterBinding(window_, active_id_, binding);
    if (result.success) {
        current_ = binding;
        active_ = true;
    }
    return result;
}

HotkeyResult GlobalHotkey::Swap(const HotkeyBinding& proposed) {
    if (active_ && SameBinding(current_, proposed)) {
        return {true, ERROR_SUCCESS};
    }

    // Pick the id that is not currently in use. The OS rejects registering the
    // same combo twice even on the same thread, so the proposed binding is
    // registered exactly once, under the free id, before the old one is touched.
    const int next_id = active_
        ? (active_id_ == kGlobalHotkeyId ? kProbeHotkeyId : kGlobalHotkeyId)
        : kGlobalHotkeyId;

    // 1. Register the proposed combo first. A rejected combo leaves the
    //    current registration untouched (no rollback needed).
    const HotkeyResult result = RegisterBinding(window_, next_id, proposed);
    if (!result.success) {
        return result;
    }

    // 2. The proposed combo is accepted by the OS; only now release the old one.
    if (active_) {
        UnregisterHotKey(window_, active_id_);
    }

    active_id_ = next_id;
    current_ = proposed;
    active_ = true;
    return result;
}

void GlobalHotkey::Shutdown() {
    if (active_) {
        UnregisterHotKey(window_, active_id_);
        active_ = false;
    }
}

} // namespace nimblerun
