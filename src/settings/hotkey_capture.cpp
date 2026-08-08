#include "settings/hotkey_capture.h"

#include "settings/settings_editor.h"

#include <windows.h>

namespace nimblerun {
namespace {

// The MOD_* bit a modifier key represents; 0 for a non-modifier key. Both the
// left and right variants map to the same bit, because a held Alt is one
// modifier no matter which physical Alt is down.
UINT ModifierBit(UINT vk_code) {
    switch (vk_code) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return MOD_CONTROL;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return MOD_ALT;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return MOD_SHIFT;
    case VK_LWIN:
    case VK_RWIN:
        return MOD_WIN;
    default:
        return 0;
    }
}

} // namespace

bool HotkeyCaptureState::IsModifierKey(UINT vk_code) {
    return ModifierBit(vk_code) != 0;
}

HotkeyCaptureState::Event HotkeyCaptureState::OnKey(UINT vk_code, bool is_down) {
    Event event;
    const UINT bit = ModifierBit(vk_code);
    if (bit != 0) {
        if (is_down) {
            held_modifiers_ |= bit;
        } else {
            held_modifiers_ &= ~bit;
            // NR-089 decision 1: the sequence ends when the last related
            // modifier is released, whatever order they were released in.
            if (candidate_ && held_modifiers_ == 0) {
                event.captured = true;
                event.binding = *candidate_;
                candidate_.reset();
            }
        }
        return event;
    }
    if (is_down) {
        if (held_modifiers_ == 0) {
            // NR-089 decision 2: a main key without modifiers is invalid.
            event.invalid_press = true;
            return event;
        }
        // A later main key replaces an earlier one while modifiers are held.
        candidate_ = HotkeyBinding{held_modifiers_ | MOD_NOREPEAT, vk_code};
    }
    return event;
}

std::wstring HotkeyCaptureState::Preview() const {
    if (!candidate_) {
        // Only modifiers are held so far; MOD_NOREPEAT is not displayable, so
        // the binding below is just the held bits with a null key.
        HotkeyBinding partial{held_modifiers_, 0};
        return FormatHotkey(partial);
    }
    return FormatHotkey(*candidate_);
}

void HotkeyCaptureState::Reset() {
    held_modifiers_ = 0;
    candidate_.reset();
}

} // namespace nimblerun
