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

// NR-198: TSF installs the hooks it uses to track focus when a thread calls
// ITfThreadMgr::Activate. Calling SetEnglishInputMode for the first time only
// after SetFocus has already fired misses that focus change, so the very first
// hidden->visible show never switches. Call this once, early on the UI thread
// and before any real SetFocus.
//
// NR-199: Activate is a reference count. The original implementation paired it
// with an immediate Deactivate, which dropped the count back to zero and undid
// the very hooks it was installing. This now acquires an activation that is
// held for the life of the process, on the thread that calls it. Best-effort
// and silent: no return value, never touches settings, never blocks. NR-200:
// ShowPanel also calls this before showing/activating the window when the live
// setting was enabled after startup, so no child-focus notification can race
// ahead of TSF readiness.
void WarmUpInputMode();

} // namespace nimblerun
