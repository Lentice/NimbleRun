#include "ui/panel_layout.h"

#include <algorithm>
#include <cmath>

namespace nimblerun {
namespace layout {

LayoutPx LayoutForDpi(float dpi) {
    const float scale = dpi / kDpi96;
    const auto px = [scale](float dip) {
        return static_cast<int>(std::lround(dip * scale));
    };
    LayoutPx out;
    out.scale = scale;
    out.panel_width = px(kPanelWidthDip);
    out.panel_height = px(kPanelHeightDip);
    out.list_left = px(kListLeftDip);
    out.list_top = px(kListTopDip);
    out.list_right = px(kListRightDip);
    out.row_height = px(kRowHeightDip);
    out.tile_size = px(kTileSizeDip);
    out.tile_inset = px(kTileInsetDip);
    out.search_left = px(kSearchLeftDip);
    out.search_top = px(kSearchTopDip);
    out.search_right = px(kSearchRightDip);
    out.search_bottom = px(kSearchBottomDip);
    out.search_edit_left = px(kSearchLeftDip + kSearchTextInsetDip);
    out.search_edit_top = px(kSearchTopDip + kSearchEditInsetYDip);
    out.search_edit_right = px(kSearchRightDip - kSearchTextInsetDip);
    out.search_edit_bottom = px(kSearchBottomDip - kSearchEditInsetYDip);
    out.search_font_height = -px(kSearchFontDip);
    out.dpi = static_cast<int>(std::lround(dpi));
    return out;
}

WindowSize ClampWindowSize(float dpi, int work_width, int work_height) {
    const LayoutPx layout = LayoutForDpi(dpi);
    WindowSize out;
    out.width = std::min(layout.panel_width, std::max(1, work_width - 32));
    out.height = std::min(layout.panel_height, std::max(1, work_height - 32));
    return out;
}

// NR-120: the footer band keeps its (kPanelHeightDip - kFooterTopDip) height
// and hugs the client bottom, so a clamped client moves the band up instead of
// clipping the path bar + key hints (design-spec §4.2/§4.9). Full height (488
// DIP) lands exactly on kFooterTopDip; the kListTopDip floor keeps the band out
// of the search box even on an absurdly small client.
float FooterTopDip(float client_height_dip) {
    const float band_height = kPanelHeightDip - kFooterTopDip;
    return std::max(kListTopDip, std::min(kFooterTopDip, client_height_dip - band_height));
}

// NR-120: rows end at the footer band's top edge, never the client bottom, so
// ViewportRows() shrinks when the panel is clamped below kPanelHeightDip and
// the footer stays visible. The row height follows the layout state and is
// unchanged (48 DIP list rows / 96 DIP grid cells).
int ViewportRowsForHeightDip(float client_height_dip, int columns) {
    const float row_height = columns > 1 ? kCellHeightDip : kRowHeightDip;
    const float result_height =
        std::max(0.0f, FooterTopDip(client_height_dip) - kListTopDip);
    return std::max(1, static_cast<int>(result_height / row_height));
}

}  // namespace layout
}  // namespace nimblerun
