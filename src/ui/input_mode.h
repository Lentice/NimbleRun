#pragma once

#include <windows.h>

namespace nimblerun {

namespace input_mode_detail {

// TSF's documented alphanumeric conversion value; kept visible to the
// focused contract check because zero is easy to confuse with the soft-keyboard flag.
inline constexpr LONG kTsfConversionModeAlphanumeric = 0x00000000;

} // namespace input_mode_detail

// NR-190: optional "switch the search box to English/alphanumeric on show".
// True only for a genuine hidden->visible panel show with the setting enabled,
// so a re-show while the panel is already visible never repeats the switch.
bool ShouldSetEnglishInputMode(bool enabled, bool was_visible);

// Best-effort switch of the search EDIT's IME input mode to alphanumeric
// (English). Tries the TSF thread-manager keyboard-input compartment first,
// falls back to IMM32. Returns false for a null/invalid HWND or when no usable
// IME context exists; never throws, never blocks, never touches settings, and
// never changes the keyboard layout.
bool SetEnglishInputMode(HWND edit);

} // namespace nimblerun
