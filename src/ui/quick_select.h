#pragma once

namespace nimblerun {
namespace ui {

// NR-024: fixed Alt+digit quick-select key sequence (design-spec §4.7). The
// digit order is the visible-row slot order: Alt+1 -> slot 0, ... Alt+9 ->
// slot 8, Alt+0 -> slot 9. Main-keyboard digit virtual key codes equal their
// ASCII codes, so this header stays pure (no <windows.h>).
inline constexpr int kQuickSelectSlotCount = 10;

// 0-based visible-row slot for a key code, or -1 when the key is not one of
// the quick-select digits (VK_NUMPAD digits are intentionally not bound).
inline int QuickSelectSlotForKey(int key_code) {
    if (key_code >= '1' && key_code <= '9') {
        return key_code - '1';
    }
    if (key_code == '0') {
        return 9;
    }
    return -1;
}

// Static single-character label for a visible-row slot (L"1" .. L"0"), or
// nullptr when the slot is outside [0, kQuickSelectSlotCount). Static-storage
// literals keep the pointer valid and never return a dangling temporary.
inline const wchar_t* QuickSelectLabelForSlot(int slot) {
    static constexpr wchar_t kLabels[kQuickSelectSlotCount][2] = {
        L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};
    return slot >= 0 && slot < kQuickSelectSlotCount ? kLabels[slot] : nullptr;
}

}  // namespace ui
}  // namespace nimblerun
