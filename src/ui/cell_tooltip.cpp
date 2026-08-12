#include "ui/cell_tooltip.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

// TTM_UPDATETIPTEXTW (WM_USER + 57) is missing from MinGW's commctrl.h; the
// value matches the Windows SDK. Note: WM_USER + 52 is TTM_NEWTOOLRECTW, not
// a text message — TTM_SETTIPTEXTW does not exist in the SDK.
#ifndef TTM_UPDATETIPTEXTW
#define TTM_UPDATETIPTEXTW (WM_USER + 57)
#endif

namespace nimblerun {
namespace ui {

namespace {

// Measurement layout size: huge so the format's trimming (if set) never
// engages and GetMetrics reports the natural single-line width.
constexpr float kMeasureSizeDip = 10000.0f;
constexpr float kTruncationEpsilonDip = 0.01f;
// NR-180: the native tooltip sizes itself to the wrapped text, so the pure
// geometry helper only needs an estimate. Width = the panel content width
// (the wrap cap, design-spec §4.9), so the horizontal clamp is exact; height
// covers ~1-2 wrapped lines so the last grid row's tooltip flips above
// instead of covering the footer.
constexpr float kTipHeightEstimateDip = 40.0f;
constexpr float kTipGapDip = 6.0f;

}  // namespace

TooltipGeometry ComputeTooltipGeometryDip(
    const D2D1_RECT_F& cell_dip, float tip_width_dip, float tip_height_dip,
    float gap_dip, float min_top_dip, float max_bottom_dip,
    float panel_left_dip, float panel_right_dip) {
    TooltipGeometry geometry;
    const float center_x = (cell_dip.left + cell_dip.right) / 2.0f;
    // Clamp horizontally into the panel content area; the caller guarantees
    // tip_width_dip <= panel_right_dip - panel_left_dip.
    geometry.left_dip = std::clamp(
        center_x - tip_width_dip / 2.0f, panel_left_dip,
        panel_right_dip - tip_width_dip);
    // NR-179: below-first placement. Below (arrow up at the cell) wins as long
    // as the tooltip stays above `max_bottom_dip` (the panel's client height,
    // so the last row's tooltip never covers the footer); otherwise flip above
    // (arrow down) when `min_top_dip` allows. Guard: neither side fits, take
    // the one with more room.
    const float below_top = cell_dip.bottom + gap_dip;
    if (below_top + tip_height_dip <= max_bottom_dip) {
        geometry.above = false;
        geometry.top_dip = below_top;
    } else if (cell_dip.top - gap_dip - tip_height_dip >= min_top_dip) {
        geometry.above = true;
        geometry.top_dip = cell_dip.top - gap_dip - tip_height_dip;
    } else {
        geometry.above = cell_dip.top - gap_dip - min_top_dip >
                         max_bottom_dip - below_top;
        geometry.top_dip = geometry.above ? cell_dip.top - gap_dip - tip_height_dip
                                          : below_top;
    }
    return geometry;
}

bool NameIsTruncated(IDWriteFactory& factory, IDWriteTextFormat& format,
                     const wchar_t* name, float max_width_dip) {
    if (name == nullptr || *name == L'\0') {
        return false;
    }
    IDWriteTextLayout* layout = nullptr;
    const UINT32 length = static_cast<UINT32>(wcslen(name));
    if (FAILED(factory.CreateTextLayout(
            name, length, &format, kMeasureSizeDip, kMeasureSizeDip, &layout))) {
        return false;
    }
    DWRITE_TEXT_METRICS metrics{};
    const HRESULT result = layout->GetMetrics(&metrics);
    layout->Release();
    if (FAILED(result)) {
        return false;
    }
    return metrics.width > max_width_dip + kTruncationEpsilonDip;
}

void CellTooltip::EnsureCreated(HWND panel, HWND tooltip_owner) {
    if (window_) {
        return;
    }
    if (!panel || !tooltip_owner) {
        return;
    }
    // Resident track tooltip (design-spec §4.9): an owned popup whose owner is
    // the panel, so it stays above the panel and is destroyed with it.
    window_ = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, tooltip_owner, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
        return;
    }
    tool_owner_ = panel;
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    // TTF_TRACK + TTF_ABSOLUTE: TTM_TRACKPOSITION places the tooltip at the
    // exact screen point given, with no auto-flip (NR-180). TTF_TRANSPARENT:
    // clicks inside the tooltip pass through to the panel (design-spec §4.8).
    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    tool.hwnd = panel;
    tool.uId = reinterpret_cast<UINT_PTR>(panel);
    tool.lpszText = const_cast<wchar_t*>(name_.c_str());
    SendMessageW(window_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&tool));
}

void CellTooltip::Show(HWND panel, float scale, const D2D1_RECT_F& cell_dip,
                       float min_top_dip, float max_bottom_dip,
                       float panel_left_dip, float panel_right_dip,
                       const wchar_t* name) {
    Hide();
    if (!panel || scale <= 0.0f || !name || *name == L'\0') {
        return;
    }
    if (!window_ || tool_owner_ != panel) {
        EnsureCreated(panel, panel);
    }
    if (!window_) {
        return;
    }

    name_ = name;
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    tool.hwnd = panel;
    tool.uId = reinterpret_cast<UINT_PTR>(panel);
    tool.lpszText = const_cast<wchar_t*>(name_.c_str());
    SendMessageW(window_, TTM_UPDATETIPTEXTW, 0,
                 reinterpret_cast<LPARAM>(&tool));
    // Wrap long names inside the panel content width (design-spec §4.9);
    // re-sent per show so a DPI change picks up the new scale lazily.
    const float content_width_dip = panel_right_dip - panel_left_dip;
    SendMessageW(window_, TTM_SETMAXTIPWIDTH, 0,
                 static_cast<LPARAM>(std::max(
                     1, static_cast<int>(std::lround(content_width_dip * scale)))));

    SendMessageW(window_, TTM_TRACKACTIVATE, TRUE,
                 reinterpret_cast<LPARAM>(&tool));

    // Measure the actual bubble after activation: TTM_GETBUBBLESIZE (valid
    // with TTF_TRACK | TTF_ABSOLUTE) reports the real wrapped size in pixels,
    // so the geometry below centers on the cell exactly and the last grid
    // row's flip decision uses the true height instead of an estimate. Falls
    // back to the content width / estimate when the control cannot report
    // (then the tooltip is still clamped inside the panel bounds).
    float tip_width_dip = content_width_dip;
    float tip_height_dip = kTipHeightEstimateDip;
    const LRESULT bubble = SendMessageW(
        window_, TTM_GETBUBBLESIZE, 0, reinterpret_cast<LPARAM>(&tool));
    if (bubble != FALSE) {
        tip_width_dip = static_cast<float>(LOWORD(bubble)) / scale;
        tip_height_dip = static_cast<float>(HIWORD(bubble)) / scale;
    }

    const TooltipGeometry geometry = ComputeTooltipGeometryDip(
        cell_dip, tip_width_dip, tip_height_dip, kTipGapDip,
        min_top_dip, max_bottom_dip, panel_left_dip, panel_right_dip);

    POINT origin{};
    ClientToScreen(panel, &origin);
    const int x = origin.x +
                  static_cast<int>(std::lround(geometry.left_dip * scale));
    const int y = origin.y +
                  static_cast<int>(std::lround(geometry.top_dip * scale));
    SendMessageW(window_, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    visible_ = true;
}

void CellTooltip::Hide() {
    if (window_ && visible_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
        tool.hwnd = tool_owner_;
        tool.uId = reinterpret_cast<UINT_PTR>(tool_owner_);
        tool.lpszText = const_cast<wchar_t*>(name_.c_str());
        SendMessageW(window_, TTM_TRACKACTIVATE, FALSE,
                     reinterpret_cast<LPARAM>(&tool));
        visible_ = false;
    }
}

}  // namespace ui
}  // namespace nimblerun
