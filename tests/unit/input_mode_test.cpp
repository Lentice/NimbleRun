// NR-190: focused check for the input-mode transition predicate and the
// failure-safe boundary of the native operation. The actual TSF/IMM provider
// behavior against a live Chinese IME is a manual smoke-matrix item
// (docs/testing.md), not a CI assertion.

#include "test_util.h"

#include "ui/input_mode.h"

#include <windows.h>

#include <cstdio>

namespace {

using nimblerun::EnglishInputModeTransition;

void TestTsfConversionModeContract() {
    Expect(nimblerun::input_mode_detail::kTsfConversionModeAlphanumeric == 0x00000000,
           "TSF alphanumeric mode must be zero, not the soft-keyboard flag");
}

void TestTransitionTruthTable() {
    const EnglishInputModeTransition disabled_hidden =
        EnglishInputModeTransition::Prepare(false, false);
    Expect(!disabled_hidden.WillApply(),
           "disabled + hidden: no switch (setting is the gate)");

    const EnglishInputModeTransition disabled_visible =
        EnglishInputModeTransition::Prepare(false, true);
    Expect(!disabled_visible.WillApply(),
           "disabled + visible: no switch");

    const EnglishInputModeTransition already_visible =
        EnglishInputModeTransition::Prepare(true, true);
    Expect(!already_visible.WillApply(),
           "enabled + already visible: no repeat switch");

    const EnglishInputModeTransition hidden_to_visible =
        EnglishInputModeTransition::Prepare(true, false);
    Expect(hidden_to_visible.WillApply(),
           "enabled + hidden->visible: switch exactly once");
}

void TestTransitionApplyFailureSafe() {
    // NR-190/NR-201: Apply's null/invalid guard runs before TSF/IMM, so these
    // checks are deterministic on any machine and never touch a live IME.
    EnglishInputModeTransition null_transition =
        EnglishInputModeTransition::Prepare(true, false);
    Expect(null_transition.WillApply(), "prepared transition starts active");
    Expect(!null_transition.Apply(nullptr), "nullptr HWND is a safe no-op");
    Expect(!null_transition.WillApply(),
           "a failed apply still consumes the transition");
    Expect(!null_transition.Apply(nullptr),
           "a consumed transition cannot run twice");

    EnglishInputModeTransition invalid_transition =
        EnglishInputModeTransition::Prepare(true, false);
    Expect(!invalid_transition.Apply(
               reinterpret_cast<HWND>(static_cast<uintptr_t>(1))),
           "invalid HWND is a safe no-op");
    Expect(!invalid_transition.WillApply(),
           "invalid HWND apply is still consumed exactly once");
}

void TestWarmUpInputModeDoesNotCrash() {
    // NR-198: this test process never calls CoInitializeEx, so the internal
    // CoCreateInstance is expected to fail -- the point is that a warm-up
    // call before COM/TSF is even usable stays a silent, safe no-op.
    // NR-199: the warm-up now caches a process-lifetime activated thread
    // manager, and Prepare is the only thing that triggers it. Prepare
    // repeatedly, and apply a transition across it, to cover the "acquire
    // failed, do not cache or double-release" path -- a cached-failure or
    // double-Release bug here crashes this process.
    EnglishInputModeTransition::Prepare(true, false);
    EnglishInputModeTransition::Prepare(true, false);
    EnglishInputModeTransition transition =
        EnglishInputModeTransition::Prepare(true, false);
    Expect(!transition.Apply(nullptr),
           "warm-up must not change the null-HWND guard");
    EnglishInputModeTransition::Prepare(true, false);
}

} // namespace

int wmain() {
    TestTsfConversionModeContract();
    TestTransitionTruthTable();
    TestTransitionApplyFailureSafe();
    TestWarmUpInputModeDoesNotCrash();
    std::printf("NR-201 input mode lifecycle check PASSED\n");
    return 0;
}
