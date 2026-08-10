// Focused check for NR-130: the full-rescan marker throttle decision.
//
// Any same-session process can PostMessage a full-rescan marker
// (kWatchChangedMessage with lParam != 0); ShouldAcceptFullRescan gates whether
// a marker for a source is accepted (and forces an immediate full rebuild) or
// merged into the existing debounce path. Isolated as an inline pure function
// so the interval edges are checked without driving the real Win32 message loop.

#include "test_util.h"

#include "app_host/full_rescan_throttle.h"

#include <cstdio>
#include <cstdlib>

using nimblerun::kFullRescanMinIntervalMs;
using nimblerun::kFullRescanNever;
using nimblerun::ShouldAcceptFullRescan;

namespace {

} // namespace

int wmain() {
    Expect(ShouldAcceptFullRescan(kFullRescanNever, 0),
           "a source with no accepted marker yet is accepted immediately");
    Expect(ShouldAcceptFullRescan(kFullRescanNever, 1 << 30),
           "never-accepted stays accepted at any time");
    Expect(!ShouldAcceptFullRescan(1000, 1000),
           "a marker at the same timestamp is within the interval (rejected)");
    Expect(!ShouldAcceptFullRescan(1000, 1500),
           "a marker 500 ms after the last accepted one is rejected");
    Expect(!ShouldAcceptFullRescan(1000, 1000 + kFullRescanMinIntervalMs - 1),
           "a marker one ms short of the interval is still throttled");
    Expect(ShouldAcceptFullRescan(1000, 1000 + kFullRescanMinIntervalMs),
           "a marker exactly one interval later is accepted");
    Expect(ShouldAcceptFullRescan(1000, 1000 + kFullRescanMinIntervalMs + 1),
           "a marker after the interval is accepted");
    std::printf("NR-130 full-rescan throttle check PASSED\n");
    return 0;
}
