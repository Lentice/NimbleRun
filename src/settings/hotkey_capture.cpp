#include "settings/hotkey_capture.h"

#include "settings/settings_editor.h"

#include <windows.h>

namespace nimblerun {
namespace {

// One bit per physical modifier vk_code (NR-093): the left and right variants
// of each category are distinct bits, so releasing one side never clears the
// other. The generic VK_SHIFT/VK_CONTROL/VK_MENU variants get their own bits
// too; real hook events use the left/right codes, the generics exist for the
// synthetic event tests.
UINT ModifierKeyBit(UINT vk_code) {
    switch (vk_code) {
    case VK_SHIFT:    return 1u << 0;
    case VK_LSHIFT:   return 1u << 1;
    case VK_RSHIFT:   return 1u << 2;
    case VK_CONTROL:  return 1u << 3;
    case VK_LCONTROL: return 1u << 4;
    case VK_RCONTROL: return 1u << 5;
    case VK_MENU:     return 1u << 6;
    case VK_LMENU:    return 1u << 7;
    case VK_RMENU:    return 1u << 8;
    case VK_LWIN:     return 1u << 9;
    case VK_RWIN:     return 1u << 10;
    default:          return 0;
    }
}

// Bit ranges above grouped per category, for deriving the MOD_* aggregate.
constexpr UINT kShiftKeyBits = 0x7u;    // bits 0-2
constexpr UINT kControlKeyBits = 0x38u; // bits 3-5
constexpr UINT kAltKeyBits = 0x1c0u;    // bits 6-8
constexpr UINT kWinKeyBits = 0x600u;    // bits 9-10

// Collapses the held physical-key mask into the MOD_* aggregate used by
// HotkeyBinding. Any held variant of a category contributes its MOD bit.
UINT AggregateModifiers(UINT held_keys) {
    UINT modifiers = 0;
    if (held_keys & kShiftKeyBits) modifiers |= MOD_SHIFT;
    if (held_keys & kControlKeyBits) modifiers |= MOD_CONTROL;
    if (held_keys & kAltKeyBits) modifiers |= MOD_ALT;
    if (held_keys & kWinKeyBits) modifiers |= MOD_WIN;
    return modifiers;
}

} // namespace

bool HotkeyCaptureState::IsModifierKey(UINT vk_code) {
    return ModifierKeyBit(vk_code) != 0;
}

UINT HotkeyCaptureState::ModifiersHeld() const {
    return AggregateModifiers(held_keys_);
}

HotkeyCaptureState::Event HotkeyCaptureState::OnKey(UINT vk_code, bool is_down) {
    Event event;
    const UINT key_bit = ModifierKeyBit(vk_code);
    if (key_bit != 0) {
        if (is_down) {
            // Idempotent: a key-repeat down event does not add a second hold.
            held_keys_ |= key_bit;
        } else {
            held_keys_ &= ~key_bit;
            // NR-089 decision 1: the sequence ends when the last related
            // modifier is released, whatever order they were released in.
            // NR-093: every held physical variant of a category must be up
            // before the aggregate drops to zero and the capture completes.
            if (candidate_ && ModifiersHeld() == 0) {
                event.captured = true;
                event.binding = *candidate_;
                candidate_.reset();
            }
        }
        return event;
    }
    if (is_down) {
        if (ModifiersHeld() == 0) {
            // NR-089 decision 2: a main key without modifiers is invalid.
            event.invalid_press = true;
            return event;
        }
        // A later main key replaces an earlier one while modifiers are held.
        candidate_ = HotkeyBinding{ModifiersHeld() | MOD_NOREPEAT, vk_code};
    }
    return event;
}

std::wstring HotkeyCaptureState::Preview() const {
    if (!candidate_) {
        // Only modifiers are held so far; MOD_NOREPEAT is not displayable, so
        // the binding below is just the held bits with a null key.
        HotkeyBinding partial{ModifiersHeld(), 0};
        return FormatHotkey(partial);
    }
    return FormatHotkey(*candidate_);
}

void HotkeyCaptureState::Reset() {
    held_keys_ = 0;
    candidate_.reset();
}

} // namespace nimblerun
