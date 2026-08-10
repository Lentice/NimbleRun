// Focused check for NR-089 (hotkey capture dialog state machine).
//
// The capture dialog's WH_KEYBOARD_LL callback is a thin adapter over
// HotkeyCaptureState; this test drives the pure state machine with synthetic
// (vk_code, is_down) sequences and pins the decisions: the combination
// consists of the modifiers held at the main-key-down moment, it completes
// only when the last related modifier is released (in any order), a main key
// without modifiers is invalid input, and pure-modifier presses produce
// nothing.

#include "test_util.h"

#include "settings/hotkey_capture.h"

#include "settings/settings_editor.h"  // FormatHotkey / ParseHotkey

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

using nimblerun::HotkeyBinding;
using nimblerun::HotkeyCaptureState;

// Feeds a full sequence and returns the captured binding (or asserts there
// was none).
HotkeyBinding CaptureAll(HotkeyCaptureState& state, const UINT* events,
                         std::size_t count, bool* captured_out = nullptr) {
    HotkeyCaptureState::Event captured{};
    for (std::size_t i = 0; i < count; ++i) {
        const bool down = (events[i] & 0x80000000u) != 0;
        captured = state.OnKey(events[i] & 0x7fffffffu, down);
        if (captured.captured || captured.invalid_press) {
            break;  // the sequence finished here
        }
    }
    if (captured_out) {
        *captured_out = captured.captured;
    }
    return captured.binding;
}

constexpr UINT DOWN_FLAG = 0x80000000u;
constexpr UINT KEY(UINT vk, bool down) {
    return vk | (down ? DOWN_FLAG : 0);
}

void TestCaptureWithReleaseOrderOne() {
    // Ctrl down -> Alt down -> E down -> Ctrl up -> Alt up.
    HotkeyCaptureState state;
    const UINT events[] = {KEY(VK_CONTROL, true), KEY(VK_MENU, true),
                           KEY(L'E', true),        KEY(VK_CONTROL, false),
                           KEY(VK_MENU, false)};
    bool captured = false;
    const HotkeyBinding binding = CaptureAll(state, events, 5, &captured);
    Expect(captured, "Ctrl+Alt+E is captured");
    Expect((binding.modifiers & MOD_CONTROL) != 0 &&
               (binding.modifiers & MOD_ALT) != 0,
           "captured combo carries Ctrl and Alt");
    Expect(binding.virtual_key == L'E', "captured combo's key is E");
    Expect((binding.modifiers & MOD_NOREPEAT) != 0,
           "captured combo carries MOD_NOREPEAT like ParseHotkey output");
    Expect(FormatHotkey(binding) == L"Ctrl+Alt+E", "captured combo formats canonically");
}

void TestCaptureWithReleaseOrderTwo() {
    // Same sequence, but Alt is released before Ctrl.
    HotkeyCaptureState state;
    const UINT events[] = {KEY(VK_CONTROL, true), KEY(VK_MENU, true),
                           KEY(L'E', true),        KEY(VK_MENU, false),
                           KEY(VK_CONTROL, false)};
    bool captured = false;
    const HotkeyBinding binding = CaptureAll(state, events, 5, &captured);
    Expect(captured, "release order does not matter");
    Expect(FormatHotkey(binding) == L"Ctrl+Alt+E",
           "Alt-first release still yields Ctrl+Alt+E");
}

void TestModifiersOnlyProducesNothing() {
    // Ctrl down -> Ctrl up: no main key ever lands.
    HotkeyCaptureState state;
    const UINT events[] = {KEY(VK_CONTROL, true), KEY(VK_CONTROL, false)};
    bool captured = false;
    const HotkeyBinding binding = CaptureAll(state, events, 2, &captured);
    Expect(!captured, "a modifier-only press captures nothing");
    Expect(binding.virtual_key == 0, "no binding for a modifier-only press");
}

void TestMainKeyWithoutModifiersIsInvalid() {
    HotkeyCaptureState state;
    const HotkeyCaptureState::Event event = state.OnKey(L'E', true);
    Expect(event.invalid_press, "a bare main key is invalid input");
    Expect(!event.captured, "a bare main key never captures");
    Expect(!HotkeyCaptureState::IsModifierKey(L'E'), "E is not a modifier key");
    Expect(HotkeyCaptureState::IsModifierKey(VK_LWIN), "left Win is a modifier key");
    Expect(HotkeyCaptureState::IsModifierKey(VK_RMENU), "right Alt is a modifier key");
}

void TestWinKeyCaptures() {
    // Win down -> E down -> Win up: the NR-088 MOD_WIN combo flows through.
    HotkeyCaptureState state;
    const UINT events[] = {KEY(VK_LWIN, true), KEY(L'E', true),
                           KEY(VK_LWIN, false)};
    bool captured = false;
    const HotkeyBinding binding = CaptureAll(state, events, 3, &captured);
    Expect(captured, "Win+E is captured");
    Expect((binding.modifiers & MOD_WIN) != 0, "captured combo carries MOD_WIN");
    Expect(FormatHotkey(binding) == L"Win+E", "Win+E formats canonically");
}

void TestShellReservedComboRejectedByParse() {
    // Alt down -> Tab down -> Alt up captures Alt+Tab; ParseHotkey must reject
    // it (NR-086 shell-reserved list), which is what makes the dialog treat it
    // as invalid rather than confirmable.
    HotkeyCaptureState state;
    const UINT events[] = {KEY(VK_MENU, true), KEY(VK_TAB, true),
                           KEY(VK_MENU, false)};
    bool captured = false;
    HotkeyBinding binding = CaptureAll(state, events, 3, &captured);
    Expect(captured, "Alt+Tab is captured by the state machine");
    Expect(ParseHotkey(FormatHotkey(binding), binding) == false,
           "the shell-reserved combo is rejected at parse, not confirmable");
}

void TestSameCategoryLeftRightDoesNotCompleteEarly() {
    // NR-093: LControl down -> RControl down -> E down -> LControl up must not
    // complete (RControl is still held); only RControl up completes Ctrl+E.
    HotkeyCaptureState state;
    HotkeyCaptureState::Event event = state.OnKey(VK_LCONTROL, true);
    event = state.OnKey(VK_RCONTROL, true);
    event = state.OnKey(L'E', true);
    event = state.OnKey(VK_LCONTROL, false);
    Expect(!event.captured,
           "releasing one Ctrl side while the other is held does not complete");
    event = state.OnKey(VK_RCONTROL, false);
    Expect(event.captured, "releasing the last Ctrl side completes the capture");
    Expect(FormatHotkey(event.binding) == L"Ctrl+E",
           "Ctrl+E is captured once both sides are up");
}

void TestRepeatedKeyDownAndRightAltVariant() {
    // NR-093 decision 2: a repeated key-down must not add a phantom hold. Right
    // Alt with E, a repeat of the left Alt (idempotent), then left up (capture
    // must not complete) and right up (completes Alt+E).
    HotkeyCaptureState state;
    HotkeyCaptureState::Event event = state.OnKey(VK_LMENU, true);
    event = state.OnKey(VK_RMENU, true);
    event = state.OnKey(L'E', true);
    event = state.OnKey(VK_LMENU, true);  // key-repeat down event
    Expect(state.Preview() == L"Alt+E", "preview stays Alt+E after a repeat down");
    event = state.OnKey(VK_LMENU, false);
    Expect(!event.captured,
           "left Alt release while right Alt is still held does not complete");
    event = state.OnKey(VK_RMENU, false);
    Expect(event.captured, "last Alt release completes Alt+E");
    Expect(FormatHotkey(event.binding) == L"Alt+E",
           "no phantom modifier leaks into the captured binding");
}

} // namespace

int wmain() {
    TestCaptureWithReleaseOrderOne();
    TestCaptureWithReleaseOrderTwo();
    TestModifiersOnlyProducesNothing();
    TestMainKeyWithoutModifiersIsInvalid();
    TestWinKeyCaptures();
    TestShellReservedComboRejectedByParse();
    TestSameCategoryLeftRightDoesNotCompleteEarly();
    TestRepeatedKeyDownAndRightAltVariant();
    std::printf("NR-089/NR-093 hotkey capture state check PASSED\n");
    return 0;
}
