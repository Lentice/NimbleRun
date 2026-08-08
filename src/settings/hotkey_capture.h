#pragma once

#include "app_host/hotkey.h"

#include <optional>
#include <string>

namespace nimblerun {

// NR-089: pure capture state machine for the hotkey capture dialog. Tracks
// which modifier keys are currently held and when a complete combination has
// been captured. No HHOOK/HWND dependency: the dialog's WH_KEYBOARD_LL
// callback feeds raw (vk_code, is_down) events in, this class decides, and the
// dialog renders the result. The capture completes only after the main key
// went down while at least one modifier was held AND every related modifier
// has since been released, in any order (NR-089 decisions 1-3).
class HotkeyCaptureState {
public:
    // One captured key event, valid only when captured (or invalid_press).
    struct Event {
        bool captured = false;       // a valid combination finished
        bool invalid_press = false;  // a main key went down with no modifiers
        HotkeyBinding binding{};     // the combination, when captured
    };

    // True for the four modifier keys, left and right variants.
    static bool IsModifierKey(UINT vk_code);

    // Feeds one raw keyboard event. captured is set exactly once, when the
    // sequence completes (the last held modifier is released). invalid_press
    // is set when a non-modifier key went down while no modifier was held;
    // the caller shows the error and calls Reset() so the user can start over.
    Event OnKey(UINT vk_code, bool is_down);

    // Live preview of what has been pressed so far ("Ctrl", "Ctrl+Alt",
    // "Ctrl+Alt+E", ...). Empty at the start of a sequence.
    std::wstring Preview() const;

    // True while at least one modifier key is held (used by the dialog to
    // distinguish a plain Esc cancel from a modifier+Esc combo).
    bool AnyModifiersDown() const { return held_modifiers_ != 0; }

    // True while a capture sequence is in progress: a modifier is held or a
    // main key has been pressed. While false, the dialog passes navigation
    // keys (Tab/Enter/Space/arrows) through to its normal button handling.
    bool Capturing() const { return held_modifiers_ != 0 || candidate_.has_value(); }

    // Starts a fresh capture sequence.
    void Reset();

private:
    UINT held_modifiers_ = 0;
    std::optional<HotkeyBinding> candidate_;
};

} // namespace nimblerun
