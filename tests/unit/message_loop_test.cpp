// Focused check for NR-117: the GetMessageW dispatch decision.
//
// GetMessageW returns a positive value for a retrieved message, 0 for WM_QUIT,
// and -1 on failure; only a positive result carries a valid MSG to translate
// and dispatch. The decision is isolated as the inline pure function
// ShouldDispatchMessage so the three outcomes are checked without driving the
// real Win32 message loop (an OS-level -1 cannot be injected safely).

#include "app_host/message_loop.h"

#include <cstdio>
#include <cstdlib>

using nimblerun::ShouldDispatchMessage;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

} // namespace

int wmain() {
    Expect(!ShouldDispatchMessage(-1), "GetMessageW -1 (retrieval error) must not dispatch");
    Expect(!ShouldDispatchMessage(0), "GetMessageW 0 (WM_QUIT) must not dispatch");
    Expect(ShouldDispatchMessage(1), "GetMessageW 1 (retrieved message) must dispatch");
    Expect(ShouldDispatchMessage(2), "GetMessageW 2 (retrieved message) must dispatch");
    std::printf("NR-117 message loop check PASSED\n");
    return 0;
}
