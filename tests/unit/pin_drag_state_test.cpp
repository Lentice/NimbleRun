#include "test_util.h"

#include "ui/pin_drag_state.h"

#include <cstdio>

namespace {

using nimblerun::PinDragState;
using nimblerun::PointPx;

void TestClickDoesNotReorder() {
    PinDragState state;
    state.OnPress(1, {10, 10}, 3);
    state.OnMove({11, 10}, 2, 3, 4, 4);
    Expect(!state.OnRelease(3), "a move below the threshold stays a click");
}

void TestReleaseReturnsReorderedPins() {
    PinDragState state;
    state.OnPress(1, {10, 10}, 4);
    state.OnMove({20, 10}, 3, 4, 4, 4);
    const auto order = state.OnRelease(4);
    Expect(order.has_value(), "a drag to another cell produces an order");
    Expect(*order == std::vector<int>({0, 2, 3, -1}),
           "the dragged row is replaced by a gap at the destination");
}

void TestPinnedRegionShrinkCancelsPreviewAndRelease() {
    PinDragState state;
    state.OnPress(2, {10, 10}, 4);
    state.OnMove({20, 10}, 1, 2, 4, 4);
    Expect(state.PreviewOrder(2).empty(),
           "a shrunken pinned region cannot preview an out-of-range drag");
    Expect(!state.OnRelease(2),
           "a shrunken pinned region cannot produce an invalid reorder");
}

void TestReleaseUsesCurrentPinnedCount() {
    PinDragState state;
    state.OnPress(1, {10, 10}, 4);
    state.OnMove({20, 10}, 3, 4, 4, 4);
    Expect(!state.OnRelease(3),
           "release rechecks the current pinned count before building order");
}

void TestCancelClearsPress() {
    PinDragState state;
    state.OnPress(0, {10, 10}, 1);
    state.Cancel();
    Expect(!state.OnRelease(1), "cancelled drag has no release result");
}

void TestOutsidePinnedRegionClearsGap() {
    PinDragState state;
    state.OnPress(0, {10, 10}, 2);
    state.OnMove({20, 10}, 2, 2, 4, 4);
    Expect(state.PreviewOrder(2).empty(),
           "a hit outside the pinned region has no drop gap");
    Expect(!state.OnRelease(2), "an outside drop does not reorder pins");
}

} // namespace

int wmain() {
    TestClickDoesNotReorder();
    TestReleaseReturnsReorderedPins();
    TestPinnedRegionShrinkCancelsPreviewAndRelease();
    TestReleaseUsesCurrentPinnedCount();
    TestCancelClearsPress();
    TestOutsidePinnedRegionClearsGap();
    std::puts("NR-136 pin drag state check PASSED");
    return 0;
}
