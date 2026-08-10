#pragma once

#include <optional>
#include <vector>

namespace nimblerun {

struct PointPx {
    int x = 0;
    int y = 0;
};

class PinDragState {
public:
    void OnPress(int cell, PointPx point, int pinned_count);
    void OnMove(PointPx point, int hit_cell, int pinned_count,
                int threshold_x, int threshold_y);
    std::optional<std::vector<int>> OnRelease(int pinned_count);
    void Cancel();
    std::vector<int> PreviewOrder(int pinned_count) const;

    bool Active() const { return drag_row_ >= 0; }
    bool Dragging() const { return dragging_; }
    int PressedRow() const { return drag_row_; }
    PointPx Cursor() const { return cursor_; }

private:
    std::vector<int> BuildOrder(int pinned_count) const;

    int drag_row_ = -1;
    int drag_gap_ = -1;
    bool dragging_ = false;
    PointPx origin_{};
    PointPx cursor_{};
};

} // namespace nimblerun
