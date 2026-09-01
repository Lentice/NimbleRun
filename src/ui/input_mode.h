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

// NR-190: optional "switch the search box to English/alphanumeric on show".
// True only for a genuine hidden->visible panel show with the setting enabled,
// so a re-show while the panel is already visible never repeats the switch.
bool ShouldSetEnglishInputMode(bool enabled, bool was_visible);

// Best-effort switch of the search EDIT's IME input mode to alphanumeric
// (English). NR-199: runs BOTH the TSF thread-manager keyboard-input
// compartment and the IMM32 path -- a successful TSF SetValue is not proof the
// focused TIP changed mode, so it must not suppress IMM32. Returns true if
// either path reported success, false for a null/invalid HWND or when neither
// is usable; never throws, never blocks, never touches settings, and never
// changes the keyboard layout.
bool SetEnglishInputMode(HWND edit);

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
