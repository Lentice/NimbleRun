#include "ui/pin_drag_state.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace nimblerun {

void PinDragState::OnPress(int cell, PointPx point, int pinned_count) {
    if (cell < 0 || cell >= pinned_count) {
        Cancel();
        return;
    }
    drag_row_ = cell;
    drag_gap_ = cell;
    dragging_ = false;
    origin_ = point;
    cursor_ = point;
}

void PinDragState::OnMove(PointPx point, int hit_cell, int pinned_count,
                          int threshold_x, int threshold_y) {
    if (!Active()) {
        return;
    }
    const int current_pinned_count = std::max(0, pinned_count);
    if (!dragging_ &&
        (std::abs(point.x - origin_.x) >= threshold_x ||
         std::abs(point.y - origin_.y) >= threshold_y)) {
        dragging_ = true;
    }
    if (dragging_) {
        cursor_ = point;
        drag_gap_ = hit_cell >= 0 && hit_cell < current_pinned_count ? hit_cell : -1;
    }
}

std::optional<std::vector<int>> PinDragState::OnRelease(int pinned_count) {
    std::optional<std::vector<int>> result;
    if (dragging_ && drag_gap_ >= 0 && drag_gap_ != drag_row_) {
        std::vector<int> order = BuildOrder(pinned_count);
        if (!order.empty()) {
            result = std::move(order);
        }
    }
    Cancel();
    return result;
}

void PinDragState::Cancel() {
    drag_row_ = -1;
    drag_gap_ = -1;
    dragging_ = false;
}

std::vector<int> PinDragState::PreviewOrder(int pinned_count) const {
    if (!dragging_) {
        return {};
    }
    return BuildOrder(pinned_count);
}

std::vector<int> PinDragState::BuildOrder(int pinned_count) const {
    if (!dragging_ || drag_row_ < 0 || drag_gap_ < 0 ||
        drag_row_ >= pinned_count || drag_gap_ >= pinned_count) {
        return {};
    }
    // NR-136: a catalog swap can shrink the pinned region during a drag.
    // Keep the preview and release result empty instead of indexing past it.
    std::vector<int> order(static_cast<std::size_t>(pinned_count));
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    order.erase(order.begin() + drag_row_);
    order.insert(order.begin() + drag_gap_, -1);
    return order;
}

} // namespace nimblerun
