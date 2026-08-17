// NR-190: focused check for the input-mode transition predicate and the
// failure-safe boundary of the native operation. The actual TSF/IMM provider
// behavior against a live Chinese IME is a manual smoke-matrix item
// (docs/testing.md), not a CI assertion.

#include "test_util.h"

#include "ui/input_mode.h"

#include <windows.h>

#include <cstdio>

namespace {

using nimblerun::SetEnglishInputMode;
using nimblerun::ShouldSetEnglishInputMode;

void TestTsfConversionModeContract() {
    Expect(nimblerun::input_mode_detail::kTsfConversionModeAlphanumeric == 0x00000000,
           "TSF alphanumeric mode must be zero, not the soft-keyboard flag");
}

void TestPredicateTruthTable() {
    Expect(!ShouldSetEnglishInputMode(false, false),
           "disabled + hidden: no switch (setting is the gate)");
    Expect(!ShouldSetEnglishInputMode(false, true),
           "disabled + visible: no switch");
    Expect(!ShouldSetEnglishInputMode(true, true),
           "enabled + already visible: no repeat switch");
    Expect(ShouldSetEnglishInputMode(true, false),
           "enabled + hidden->visible: switch exactly once");
}

void TestSetEnglishInputModeFailureSafe() {
    // NR-190: the null/invalid guards return false before any TSF/IMM/COM call,
    // so these are deterministic on any machine and never touch a live IME.
    Expect(!SetEnglishInputMode(nullptr), "nullptr HWND is a safe no-op");
    Expect(!SetEnglishInputMode(reinterpret_cast<HWND>(static_cast<uintptr_t>(1))),
           "invalid HWND is a safe no-op");
}

} // namespace

int wmain() {
    TestTsfConversionModeContract();
    TestPredicateTruthTable();
    TestSetEnglishInputModeFailureSafe();
    std::printf("NR-190 input mode check PASSED\n");
    return 0;
}
