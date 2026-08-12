#pragma once

#include <string>

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

namespace nimblerun {
namespace ui {

// NR-180: grid cell tooltip backed by the native TOOLTIPS_CLASS (design-spec
// §4.8/§4.9). The pure, testable geometry/measurement helpers below have no
// HWND; the CellTooltip class is a window-layer wrapper (one resident tooltip
// window, never touches PanelModel).

// Pure geometry: where the tooltip window sits relative to `cell_dip`, in the
// same DIP space as the cell rect. The tooltip prefers the space below the
// cell; when that bottom position would extend past `max_bottom_dip` (the
// panel's client height, so the last grid row's tooltip never covers the
// footer), it flips above the cell (the top position must not rise above
// `min_top_dip`, the grid area's top edge). Horizontally centered on the cell
// and clamped to [panel_left_dip, panel_right_dip - tip_width_dip]. When
// neither side fits, the side with more room wins. `above` tells the caller
// which side the tooltip ends up on.
struct TooltipGeometry {
    float left_dip = 0.0f;
    float top_dip = 0.0f;
    bool above = false;
};

TooltipGeometry ComputeTooltipGeometryDip(
    const D2D1_RECT_F& cell_dip, float tip_width_dip, float tip_height_dip,
    float gap_dip, float min_top_dip, float max_bottom_dip,
    float panel_left_dip, float panel_right_dip);

// True when `name` laid out with `format` needs more than `max_width_dip`.
// The layout is created with a huge width so the format's trimming (if any)
// never engages and GetMetrics reports the natural text width; the draw-time
// layout trims at exactly `max_width_dip`, so the comparison boundary matches
// what the grid actually paints (design-spec §4.2). Empty name -> false.
bool NameIsTruncated(IDWriteFactory& factory, IDWriteTextFormat& format,
                     const wchar_t* name, float max_width_dip);

// Window-layer wrapper (not unit-tested): one resident TOOLTIPS_CLASS track
// tooltip window, created lazily on first Show and kept for the process
// lifetime (design-spec §4.9: a few KB, not counted against NFR-001).
class CellTooltip {
public:
    CellTooltip() = default;
    ~CellTooltip() { Hide(); }
    CellTooltip(const CellTooltip&) = delete;
    CellTooltip& operator=(const CellTooltip&) = delete;

    // Creates the resident tooltip window (once) and registers the panel as
    // the tool (TTM_ADDTOOL). `tooltip_owner` owns the tooltip window; the
    // panel itself is a fine owner. No-op after the first call.
    void EnsureCreated(HWND panel, HWND tooltip_owner);

    // Shows `name` in a track tooltip positioned over `cell_dip`
    // (panel-client DIPs), converting to screen pixels via ClientToScreen +
    // `scale`. No-op (and hides any existing tooltip) when a parameter is
    // unusable.
    void Show(HWND panel, float scale, const D2D1_RECT_F& cell_dip,
              float min_top_dip, float max_bottom_dip, float panel_left_dip,
              float panel_right_dip, const wchar_t* name);
    void Hide();

private:
    HWND window_ = nullptr;      // the resident TOOLTIPS_CLASS window
    HWND tool_owner_ = nullptr;  // panel registered in the TOOLINFO (uId/hwnd)
    std::wstring name_;          // text buffer the tooltip control points at
    bool visible_ = false;       // TTM_TRACKACTIVATE state
};

}  // namespace ui
}  // namespace nimblerun
