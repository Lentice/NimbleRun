#pragma once

#include <windows.h>

namespace nimblerun {

namespace input_mode_detail {

// TSF's documented alphanumeric conversion value; kept visible to the
// focused contract check because zero is easy to confuse with the soft-keyboard flag.
inline constexpr LONG kTsfConversionModeAlphanumeric = 0x00000000;

} // namespace input_mode_detail

// NR-201: captures one hidden->visible show. Prepare warms TSF before any
// window show/activation call; Apply consumes the transition after the search
// EDIT has focus and never runs the native operation twice.
class EnglishInputModeTransition {
public:
    static EnglishInputModeTransition Prepare(bool enabled, bool was_visible);

    bool WillApply() const noexcept { return will_apply_; }
    bool Apply(HWND edit);

private:
    explicit EnglishInputModeTransition(bool will_apply)
        : will_apply_(will_apply) {}

    bool will_apply_ = false;
};

// NR-190's transition predicate (enabled && !was_visible) and the best-effort
// native switch are both internal to input_mode.cpp now: NR-201 made
// EnglishInputModeTransition the module's only entry point for a panel show,
// and after it landed nothing outside called either one. Keeping them exported
// made the interface wider than the behavior and left the predicate as the only
// unit-tested part of a file whose bugs were all in activation lifetime and
// call ordering -- the transition class is what the tests exercise instead.

// NR-198/NR-199/NR-200: the TSF warm-up (activating the thread manager so its
// focus-tracking hooks exist before any real SetFocus, held for the process
// lifetime because Activate is a reference count) is internal to
// input_mode.cpp. Prepare performs it, so callers own no ordering rule beyond
// "Prepare before showing the window, Apply after the EDIT has focus".

} // namespace nimblerun
