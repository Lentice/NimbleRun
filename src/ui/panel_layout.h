#pragma once

namespace nimblerun {
namespace layout {

// DIP constants (design-spec §4.9). D2D's HwndRenderTarget coordinate space is
// DIPs, so render geometry and DWrite font sizes use these directly; the host
// converts them to physical pixels only for Win32 geometry (window size, child
// controls, hit-testing) via LayoutForDpi().
constexpr float kDpi96 = 96.0f;
constexpr float kPanelWidthDip = 640.0f;
constexpr float kPanelHeightDip = 488.0f; // NR-023: taller search box (16~64) keeps 8 rows visible
constexpr float kListLeftDip = 16.0f;
constexpr float kListTopDip = 72.0f;      // NR-023: below the 64 DIP search box
constexpr float kListRightDip = 624.0f;
constexpr float kRowHeightDip = 48.0f;
constexpr float kTileSizeDip = 30.0f;   // NR-012 fixed tile
constexpr float kTileInsetDip = 8.0f;   // tile offset inside a row
constexpr float kFooterTopDip = 456.0f; // NR-023: footer band 456~488 (replaces NR-020's 400~432)
// NR-021: fixed footer key-hint band geometry (design-spec §4.9).
constexpr float kFooterDividerWidthDip = 1.0f;
constexpr float kFooterKeyBoxWidthDip = 44.0f;
constexpr float kFooterKeyBoxHeightDip = 20.0f;
constexpr float kFooterKeyRadiusDip = 3.0f;
constexpr float kFooterKeyGapDip = 8.0f;
constexpr float kFooterHintGapDip = 12.0f;  // "Scroll" label to the first key box
constexpr float kFooterTextInsetDip = 3.0f;  // text top padding inside a key box
// NR-024: per-row quick-select digit box geometry (design-spec §4.9). The name
// and second-line width unconditionally reserve kRowHintReserveDip so text
// width never jumps whether or not a row shows a digit.
constexpr float kRowKeyBoxWidthDip = 20.0f;
constexpr float kRowKeyRightInsetDip = 8.0f;   // box right edge from kListRightDip
constexpr float kRowKeyGapDip = 8.0f;          // box left edge from the text right edge
constexpr float kRowHintReserveDip = kRowKeyBoxWidthDip + kRowKeyRightInsetDip + kRowKeyGapDip;
// NR-024: the footer "Alt+1~N" box is wider than the short PgUp/PgDn boxes.
constexpr float kFooterWideKeyBoxWidthDip = 56.0f;
// NR-029: empty-query icon grid (design-spec §4.9). One page is kGridColumns x
// 4 rows = 24 cells (result area 72~456 DIP is 384 DIP tall -> 384/96 = 4 rows);
// the grid reuses the model's viewport/scroll/selection state with Columns()>1.
constexpr float kCellWidthDip = 101.0f;
constexpr float kCellHeightDip = 96.0f;
constexpr float kIconSizeDip = 40.0f;
constexpr int kGridColumns = 6;
// Left edge of the grid, horizontally centered in the list area (608 DIP wide
// vs 6 x 101 = 606 DIP of cells).
constexpr float kGridLeftDip =
    kListLeftDip + (kListRightDip - kListLeftDip - kGridColumns * kCellWidthDip) / 2.0f;
constexpr float kSearchLeftDip = 16.0f;
constexpr float kSearchTopDip = 16.0f;
constexpr float kSearchRightDip = 624.0f;
constexpr float kSearchBottomDip = 64.0f;  // NR-023: search box 16~64, height 48 DIP
// NR-023: rounded search box geometry (design-spec §4.9).
constexpr float kSearchCornerRadiusDip = 6.0f;
constexpr float kSearchTextInsetDip = 12.0f;   // EDIT inset left/right of the box
constexpr float kSearchEditInsetYDip = 6.0f;   // EDIT inset top/bottom of the box
constexpr float kSearchFontDip = 24.0f;
constexpr float kTitleFontDip = 16.0f;
constexpr float kTextFontDip = 14.0f;
constexpr float kSmallFontDip = 11.0f;

// Physical-pixel geometry for a monitor at `dpi`: every field is the
// corresponding DIP constant scaled by dpi / 96 (rounded). Pure value; no HWND
// or Windows dependencies.
struct LayoutPx {
    float scale = 1.0f;  // dpi / 96
    int panel_width = 0;
    int panel_height = 0;
    int list_left = 0;
    int list_top = 0;
    int list_right = 0;
    int row_height = 0;
    int tile_size = 0;
    int tile_inset = 0;
    int search_left = 0;
    int search_top = 0;
    int search_right = 0;
    int search_bottom = 0;
    // NR-023: the search EDIT child rect, inset inside the D2D search box so its
    // right angles never cover the rounded corners.
    int search_edit_left = 0;
    int search_edit_top = 0;
    int search_edit_right = 0;
    int search_edit_bottom = 0;
    // NR-023: LOGFONTW::lfHeight for the search font (negative = char height).
    int search_font_height = 0;
    int dpi = 0;
};

LayoutPx LayoutForDpi(float dpi);

struct WindowSize {
    int width = 0;
    int height = 0;
};

// DPI-scaled panel size capped to a monitor work area, keeping a 32px margin
// on each edge (design-spec §4.9).
WindowSize ClampWindowSize(float dpi, int work_width, int work_height);

// NR-120: the footer band (divider at kFooterTopDip .. panel bottom) keeps its
// height and hugs the client's bottom edge, so the path bar + key hints
// (design-spec §4.2/§4.9) stay visible even when ClampWindowSize shortens the
// panel below kPanelHeightDip (small screen + high DPI). `client_height_dip`
// is the client rect height in DIPs; returns the DIP y of the footer band's
// top edge, which is also the bottom edge of the row area. A full-height
// 488 DIP client lands exactly on kFooterTopDip; a shorter client moves the
// band up instead of clipping it. Pure value; no HWND dependency.
float FooterTopDip(float client_height_dip);

// NR-120: the row count the renderer paints for a client of the given DIP
// height, so the footer band always stays visible: rows end at the footer
// band's top edge (FooterTopDip), never the client bottom. `columns` is 1 for
// the list state and kGridColumns for the grid state; the row height follows
// the state (48 DIP list rows / 96 DIP grid cells), unchanged. Clamped to
// >= 1. Pure value; no HWND dependency.
int ViewportRowsForHeightDip(float client_height_dip, int columns);

// NR-133: a slot rect in DIPs. Slot = 0-based index within the visible area;
// columns == 1 is the single-column list, >1 the grid.
struct SlotRectDip {
    float left, top, right, bottom;
};

// NR-133: the rect of the slot-th visible cell (grid, columns > 1) or row
// (list, columns == 1) in DIPs, the reverse of SlotAtPointDip. The forward
// geometry is fixed (cells/rows advance from kGridLeftDip / kListTopDip), so
// `client_height_dip` exists only to mirror the inverse's footer bound; it is
// not read. Pure value; no HWND dependency.
SlotRectDip SlotRect(int slot, int columns, float client_height_dip);

// NR-133: the 0-based slot index of the visible cell/row containing the DIP
// point, or -1 on a miss. The footer band bound (NR-064/NR-120) uses the same
// clamped FooterTopDip as SlotRect's inverse and the renderer, and
// `viewport_rows` carries the model's actually-painted row count (NR-082), so
// footer and past-viewport points both miss. The inverse of SlotRect: any
// point inside a slot's rect maps back to that slot. This is the single
// definition of the slot hit-test geometry, previously four copies in the
// host. Pure value; no HWND dependency.
int SlotAtPointDip(float x, float y, int columns, int viewport_rows,
                   float client_height_dip);

}  // namespace layout
}  // namespace nimblerun
