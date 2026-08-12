#include "test_util.h"

#include "ui/cell_tooltip.h"

#include <d2d1.h>
#include <dwrite.h>
#include <objbase.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

using nimblerun::ui::ComputeTooltipGeometryDip;
using nimblerun::ui::NameIsTruncated;

namespace {

// Natural single-line width of `text` in `format`, measured with the same
// huge-layout technique NameIsTruncated uses.
float MeasureWidth(IDWriteFactory& factory, IDWriteTextFormat& format,
                   const wchar_t* text) {
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(factory.CreateTextLayout(
            text, static_cast<UINT32>(wcslen(text)), &format, 10000.0f,
            10000.0f, &layout))) {
        return -1.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    const HRESULT result = layout->GetMetrics(&metrics);
    layout->Release();
    return SUCCEEDED(result) ? metrics.width : -1.0f;
}

void TestGeometryBelowWhenSpacePermits() {
    // Middle grid row in the real layout (row 2 of 4: 168..264): the tooltip
    // fits below the cell and stays there (NR-179 below-first).
    const D2D1_RECT_F cell{100.0f, 168.0f, 200.0f, 264.0f};
    const auto geometry = ComputeTooltipGeometryDip(
        cell, 60.0f, 30.0f, 6.0f, 72.0f, 488.0f, 16.0f, 624.0f);
    Expect(!geometry.above, "tooltip below the cell when there is room");
    Expect(geometry.top_dip == 270.0f, "below = cell.bottom + gap");
    Expect(geometry.left_dip == 120.0f, "horizontally centered on the cell");
}

void TestGeometryLastRowFlipsAbove() {
    // Last grid row in the real layout (row 4 of 4: 360..456): below would
    // land at 456 + 6 + 30 = 492, past the 488 DIP panel, so it flips above.
    const D2D1_RECT_F cell{100.0f, 360.0f, 200.0f, 456.0f};
    const auto geometry = ComputeTooltipGeometryDip(
        cell, 60.0f, 30.0f, 6.0f, 72.0f, 488.0f, 16.0f, 624.0f);
    Expect(geometry.above, "last-row tooltip flips above the cell");
    Expect(geometry.top_dip == 324.0f, "above = cell.top - gap - height");
}

void TestGeometryTopRowStaysBelow() {
    // Top grid row in the real layout (row 1 of 4: 72..168): below always
    // fits, so even a top-row cell keeps the tooltip below it.
    const D2D1_RECT_F cell{100.0f, 72.0f, 201.0f, 168.0f};
    const auto geometry = ComputeTooltipGeometryDip(
        cell, 60.0f, 30.0f, 6.0f, 72.0f, 488.0f, 16.0f, 624.0f);
    Expect(!geometry.above, "top-row tooltip stays below the cell");
    Expect(geometry.top_dip == 174.0f, "below = cell.bottom + gap");
}

void TestGeometryClampsToPanelBounds() {
    // Cell hugging the right edge: the centered position would overflow the
    // panel, so the tooltip is clamped to the panel's right bound.
    const D2D1_RECT_F right_cell{500.0f, 100.0f, 600.0f, 150.0f};
    const auto right = ComputeTooltipGeometryDip(
        right_cell, 200.0f, 30.0f, 6.0f, 0.0f, 488.0f, 16.0f, 624.0f);
    Expect(right.left_dip == 424.0f, "clamped inside the panel's right edge");
    // Cell hugging the left edge: clamped to the panel's left bound.
    const D2D1_RECT_F left_cell{10.0f, 100.0f, 110.0f, 150.0f};
    const auto left = ComputeTooltipGeometryDip(
        left_cell, 200.0f, 30.0f, 6.0f, 0.0f, 488.0f, 16.0f, 624.0f);
    Expect(left.left_dip == 16.0f, "clamped inside the panel's left edge");
}

void TestGeometryGapRespected() {
    const D2D1_RECT_F cell{100.0f, 100.0f, 200.0f, 150.0f};
    const auto below = ComputeTooltipGeometryDip(
        cell, 60.0f, 30.0f, 10.0f, 0.0f, 400.0f, 0.0f, 400.0f);
    Expect(below.top_dip == 150.0f + 10.0f,
           "below keeps exactly the requested gap");
    // Shrunk max_bottom forces the above branch, which keeps its own gap.
    const D2D1_RECT_F top{100.0f, 100.0f, 200.0f, 150.0f};
    const auto above = ComputeTooltipGeometryDip(
        top, 60.0f, 30.0f, 10.0f, 0.0f, 180.0f, 0.0f, 400.0f);
    Expect(above.above, "flips above when below does not fit");
    Expect(above.top_dip == 100.0f - 10.0f - 30.0f,
           "above keeps exactly the requested gap");
}

void TestGeometryNeitherSideFits() {
    // Both sides overflow: the tooltip goes to the side with more room.
    // More room above -> above; more room below -> below.
    const D2D1_RECT_F cell{100.0f, 100.0f, 200.0f, 150.0f};
    const auto above = ComputeTooltipGeometryDip(
        cell, 60.0f, 200.0f, 6.0f, 0.0f, 200.0f, 0.0f, 400.0f);
    Expect(above.above, "picks above when it has more room");
    const D2D1_RECT_F low{100.0f, 30.0f, 200.0f, 80.0f};
    const auto below = ComputeTooltipGeometryDip(
        low, 60.0f, 200.0f, 6.0f, 0.0f, 200.0f, 0.0f, 400.0f);
    Expect(!below.above, "picks below when it has more room");
}

void TestNameTruncatedBasics(IDWriteFactory& factory,
                             IDWriteTextFormat& format) {
    Expect(!NameIsTruncated(factory, format, L"Short", 100.0f),
           "short name is not truncated");
    Expect(NameIsTruncated(factory, format,
                           L"A very long app name that cannot fit the cell",
                           30.0f),
           "long name is truncated");
    Expect(!NameIsTruncated(factory, format, L"", 100.0f),
           "empty name is not truncated");
    Expect(!NameIsTruncated(factory, format, nullptr, 100.0f),
           "null name is not truncated");
}

void TestNameTruncatedBoundary(IDWriteFactory& factory,
                               IDWriteTextFormat& format) {
    // Measure a name, then feed its own natural width back: exactly fitting
    // (and within the epsilon band) must not count as truncated, one DIP less
    // must.
    const wchar_t* name = L"ExactBoundaryName";
    const float width = MeasureWidth(factory, format, name);
    Expect(width > 0.0f, "fixture name measures");
    Expect(!NameIsTruncated(factory, format, name, width),
           "exactly fitting name is not truncated");
    Expect(!NameIsTruncated(factory, format, name, width + 0.005f),
           "within the epsilon band is not truncated");
    Expect(NameIsTruncated(factory, format, name, width - 1.0f),
           "one DIP narrower truncates");
}

}  // namespace

int wmain() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        std::fprintf(stderr, "FAILED: COM init\n");
        return 1;
    }

    IDWriteFactory* factory = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&factory)))) {
        std::fprintf(stderr, "FAILED: DWrite factory\n");
        CoUninitialize();
        return 1;
    }
    // Same parameters as the grid name format (Segoe UI, 14 DIP, no wrap).
    IDWriteTextFormat* format = nullptr;
    const HRESULT format_result = factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &format);
    if (FAILED(format_result)) {
        std::fprintf(stderr, "FAILED: text format\n");
        factory->Release();
        CoUninitialize();
        return 1;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    TestGeometryBelowWhenSpacePermits();
    TestGeometryLastRowFlipsAbove();
    TestGeometryTopRowStaysBelow();
    TestGeometryClampsToPanelBounds();
    TestGeometryGapRespected();
    TestGeometryNeitherSideFits();
    TestNameTruncatedBasics(*factory, *format);
    TestNameTruncatedBoundary(*factory, *format);

    format->Release();
    factory->Release();
    CoUninitialize();
    std::printf("NR-178 cell tooltip check PASSED\n");
    return 0;
}
