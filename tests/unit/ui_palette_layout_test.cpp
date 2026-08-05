#include "app_host/panel_model.h"
#include "ui/panel_layout.h"
#include "ui/panel_palette.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::PanelModel;
using nimblerun::Theme;
using nimblerun::layout::ClampWindowSize;
using nimblerun::layout::LayoutForDpi;
using nimblerun::palette::PanelColors;
using nimblerun::palette::ResolveColors;
using nimblerun::palette::Rgb;
using nimblerun::palette::SystemColors;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

AppEntry Entry(std::wstring id, std::wstring name) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = name;
    entry.normalized_name = name;
    entry.launch_identity = L"C:\\Apps\\" + name + L".exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserFolder;
    return entry;
}

SystemColors InjectedSystem() {
    SystemColors system;
    system.window = 0x000000;
    system.window_text = 0xFFFFFF;
    system.highlight = 0x0000FF;
    system.highlight_text = 0x00FF00;
    system.gray_text = 0x808080;
    return system;
}

void TestLayoutScalingAcrossDpi() {
    // 100% (96 DPI): every field equals the DIP constant.
    const auto d96 = LayoutForDpi(96.0f);
    Expect(d96.panel_width == 640, "100% panel width is 640px");
    Expect(d96.panel_height == 488, "100% panel height is 488px");
    Expect(d96.list_left == 16 && d96.list_right == 624, "100% list bounds");
    Expect(d96.list_top == 72, "100% list top");
    Expect(d96.row_height == 48, "100% row height");
    Expect(d96.tile_size == 30, "100% tile size");
    Expect(d96.search_right == 624 && d96.search_bottom == 64, "100% search box");

    // 150% (144 DPI): all pixel geometry is exactly 1.5x the DIP size.
    const auto d144 = LayoutForDpi(144.0f);
    Expect(d144.panel_width == 960, "150% panel width is 960px");
    Expect(d144.panel_height == 732, "150% panel height is 732px");
    Expect(d144.row_height == 72, "150% row height");
    Expect(d144.tile_size == 45, "150% tile size");
    Expect(d144.list_left == 24 && d144.list_right == 936, "150% list bounds");
    Expect(d144.search_bottom == 96, "150% search box");

    // 200% (192 DPI): doubling the scale doubles the pixel sizes.
    const auto d192 = LayoutForDpi(192.0f);
    Expect(d192.panel_width == 1280, "200% panel width is 1280px");
    Expect(d192.panel_height == 976, "200% panel height is 976px");
    Expect(d192.row_height == 96, "200% row height is 2x 100%");
    Expect(d192.tile_size == 60, "200% tile size is 2x 100%");
    Expect(d192.list_right == 1248, "200% list right is 2x 100%");
}

void TestLayoutMonotonicBounds() {
    // Larger DPI never shrinks any geometry; predictable monotonic bounds.
    const auto d96 = LayoutForDpi(96.0f);
    const auto d144 = LayoutForDpi(144.0f);
    const auto d192 = LayoutForDpi(192.0f);
    Expect(d96.panel_width < d144.panel_width && d144.panel_width < d192.panel_width,
           "panel width grows monotonically with DPI");
    Expect(d96.row_height < d144.row_height && d144.row_height < d192.row_height,
           "row height grows monotonically with DPI");
    Expect(d96.tile_size < d144.tile_size && d144.tile_size < d192.tile_size,
           "tile size grows monotonically with DPI");
    Expect(d192.tile_size == 2 * d96.tile_size, "doubling DPI doubles tile size");
    Expect(d192.row_height == 2 * d96.row_height, "doubling DPI doubles row height");
}

void TestClampWindowSize() {
    // Large work area: the panel keeps its DPI-scaled size.
    const auto big = ClampWindowSize(96.0f, 1920, 1080);
    Expect(big.width == 640 && big.height == 488, "large work area keeps panel size");
    const auto big_150 = ClampWindowSize(144.0f, 1920, 1080);
    Expect(big_150.width == 960 && big_150.height == 732, "150% keeps scaled size");

    // Small work area: clamped, preserving a 32px margin on each edge.
    const auto small = ClampWindowSize(96.0f, 400, 300);
    Expect(small.width == 368 && small.height == 268, "clamped to work area minus 32");
    const auto small_200 = ClampWindowSize(192.0f, 1280, 720);
    Expect(small_200.width == 1248 && small_200.height == 688,
           "200% panel clamped to small work area");
}

void TestPaletteLightAndDarkDiffer() {
    const PanelColors light = ResolveColors(Theme::Light, false, false, {});
    const PanelColors dark = ResolveColors(Theme::Dark, false, false, {});
    Expect(light.background != dark.background, "background differs light vs dark");
    Expect(light.card != dark.card, "card differs light vs dark");
    Expect(light.text != dark.text, "text differs light vs dark");
    Expect(light.selected_fill != dark.selected_fill, "selection fill differs");

    // Light and dark palettes keep the current dark defaults intact.
    Expect(dark.background == 0x181818 && dark.card == 0x2B2B2B,
           "dark palette keeps existing defaults");
    Expect(dark.selected_fill == 0x3A5A8C, "dark selection fill kept");
}

void TestPaletteSystemFollowsOs() {
    const PanelColors light = ResolveColors(Theme::System, false, false, {});
    const PanelColors dark = ResolveColors(Theme::System, true, false, {});
    Expect(light.background == 0xF3F3F3, "system + light OS -> light palette");
    Expect(dark.background == 0x181818, "system + dark OS -> dark palette");
}

void TestHighContrastUsesSystemPalette() {
    const SystemColors system = InjectedSystem();
    const PanelColors hc = ResolveColors(Theme::Dark, false, true, system);
    Expect(hc.background == system.window, "high contrast uses system window");
    Expect(hc.card == system.window, "high contrast card collapses to window");
    Expect(hc.selected_fill == system.highlight, "high contrast uses system highlight");
    Expect(hc.text == system.window_text, "high contrast uses system window text");
    Expect(hc.dim == system.gray_text, "high contrast uses system gray text");

    // High contrast wins over both explicit light and explicit dark settings.
    const PanelColors hc_light = ResolveColors(Theme::Light, false, true, system);
    Expect(hc_light.background == system.window,
           "high contrast overrides explicit light theme");
    const PanelColors hc_dark = ResolveColors(Theme::Dark, false, true, system);
    Expect(hc_dark.selected_fill == system.highlight,
           "high contrast overrides explicit dark theme");
}

void TestSelectionBorderIsNonColorSignal() {
    // The selected row must carry a border color distinct from its fill in
    // every palette, so selection is not conveyed by color alone (NFR-006).
    const PanelColors light = ResolveColors(Theme::Light, false, false, {});
    Expect(light.selected_border != light.selected_fill,
           "light selection border distinct from fill");
    const PanelColors dark = ResolveColors(Theme::Dark, false, false, {});
    Expect(dark.selected_border != dark.selected_fill,
           "dark selection border distinct from fill");
    const PanelColors hc = ResolveColors(Theme::Dark, false, true, InjectedSystem());
    Expect(hc.selected_border != hc.selected_fill,
           "high contrast selection border distinct from fill");
}

// Theme/high-contrast state never changes the catalog or launch identity: the
// palette resolver takes no AppEntry and this whole test exercises colors and
// layout only, so there is no path from theme state into app data.
void TestThemeNeverTouchesCatalogIdentity() {
    const AppEntry entry = Entry(L"id", L"Name");
    Expect(entry.launch_identity == L"C:\\Apps\\Name.exe",
           "fixture launch identity intact");
    // Resolving palettes must not observe or mutate the entry; the signatures
    // below take colors and DPI only, never AppEntry.
    (void)ResolveColors(Theme::Dark, false, false, {});
    (void)LayoutForDpi(144.0f);
    Expect(entry.stable_id == L"id" && entry.display_name == L"Name",
           "entry data untouched by palette/layout resolution");
}

void TestAccessibleNamesPerRow() {
    const std::vector<AppEntry> catalog = {
        Entry(L"a", L"Alpha"), Entry(L"b", L"Beta"), Entry(L"c", L"Gamma")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"a");
    // Alpha (prefix) then Beta/Gamma (substring), name-ordered.
    Expect(model.Rows().size() == 3, "three rows match 'a'");
    Expect(model.AccessibleNameFor(0) == L"Alpha", "row 0 accessible name is display name");
    Expect(model.AccessibleNameFor(1) == L"Beta", "row 1 accessible name is display name");
    Expect(model.AccessibleNameFor(2) == L"Gamma", "row 2 accessible name is display name");
    Expect(model.AccessibleNameFor(5).empty(), "out-of-range row has empty name");
    Expect(model.SelectedAccessibleName() == L"Alpha", "first row selected by default");
    model.MoveSelection(1);
    Expect(model.SelectedAccessibleName() == L"Beta", "selection moves with the row");
}

void TestSelectedAccessibleNameEmptyState() {
    const std::vector<AppEntry> catalog = {Entry(L"a", L"Alpha")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"zzz-no-match");
    Expect(model.Rows().empty(), "no match -> empty rows");
    Expect(model.SelectedAccessibleName().empty(), "no selection -> empty name");
}

// NR-023: the search box grew to 16~64 DIP and the list/footer moved down, so
// the footer band 456..488 keeps 8 visible rows at 96 DPI.
void TestSearchFieldGeometry() {
    const auto d96 = LayoutForDpi(96.0f);
    Expect(d96.panel_height == 488, "96 DPI panel height is 488");
    Expect(d96.list_top == 72, "96 DPI list top is 72");
    Expect(d96.search_bottom == 64, "96 DPI search bottom is 64");
    Expect((456 - 72) / 48 == 8, "footer band 456..488 leaves 8 visible rows");

    // The EDIT rect is the 12/6-DIP-inset box, rounded to physical px; the font
    // height is negative (character height) for LOGFONTW::lfHeight.
    Expect(d96.search_edit_left == 28, "96 DPI edit left is 28");
    Expect(d96.search_edit_top == 22, "96 DPI edit top is 22");
    Expect(d96.search_edit_right == 612, "96 DPI edit right is 612");
    Expect(d96.search_edit_bottom == 58, "96 DPI edit bottom is 58");
    Expect(d96.search_font_height == -24, "96 DPI search font height is -24");
}

// NR-023: doubling the DPI doubles every search-field value, and the EDIT keeps
// positive size.
void TestSearchFieldScalingTo200Percent() {
    const auto d96 = LayoutForDpi(96.0f);
    const auto d192 = LayoutForDpi(192.0f);
    Expect(d192.search_edit_left == 2 * d96.search_edit_left, "edit left doubles at 200%");
    Expect(d192.search_edit_top == 2 * d96.search_edit_top, "edit top doubles at 200%");
    Expect(d192.search_edit_right == 2 * d96.search_edit_right, "edit right doubles at 200%");
    Expect(d192.search_edit_bottom == 2 * d96.search_edit_bottom, "edit bottom doubles at 200%");
    Expect(d192.search_font_height == 2 * d96.search_font_height, "font height doubles at 200%");
    Expect(d192.search_edit_left < d192.search_edit_right, "edit has positive width");
    Expect(d192.search_edit_top < d192.search_edit_bottom, "edit has positive height");
}

// NR-023: the EDIT child is strictly inside the rounded search box at every
// DPI, so its right angles never cover the 6-DIP corners.
void TestEditRectInsideSearchBox() {
    const float dpis[] = {96.0f, 144.0f, 192.0f};
    for (const float dpi : dpis) {
        const auto l = LayoutForDpi(dpi);
        Expect(l.search_edit_left > l.search_left, "edit left is inside the box");
        Expect(l.search_edit_top > l.search_top, "edit top is inside the box");
        Expect(l.search_edit_right < l.search_right, "edit right is inside the box");
        Expect(l.search_edit_bottom < l.search_bottom, "edit bottom is inside the box");
    }
}

// NR-023 palette: input fill/border follow the theme and never equal the panel
// background in light/dark mode; high contrast uses solid system colors so the
// box stays visible (card collapses to window there).
void TestSearchFieldColors() {
    const PanelColors light = ResolveColors(Theme::Light, false, false, {});
    Expect(light.input_fill == 0xFFFFFF, "light input fill is white");
    Expect(light.input_border == 0xE0E0E0, "light input border is light gray");
    Expect(light.input_fill != light.background, "light input fill distinct from background");

    const PanelColors dark = ResolveColors(Theme::Dark, false, false, {});
    Expect(dark.input_fill == 0x2B2B2B, "dark input fill is dark gray");
    Expect(dark.input_border == 0x3C3C3C, "dark input border is lighter gray");
    Expect(dark.input_fill != dark.background, "dark input fill distinct from background");

    const SystemColors system = InjectedSystem();
    const PanelColors hc = ResolveColors(Theme::Dark, false, true, system);
    Expect(hc.input_fill == system.window, "high contrast input fill is system window");
    Expect(hc.input_border == system.window_text, "high contrast border is system window text");
    Expect(hc.input_fill != hc.input_border, "high contrast border distinct from fill");
}

}  // namespace

int wmain() {
    TestLayoutScalingAcrossDpi();
    TestLayoutMonotonicBounds();
    TestClampWindowSize();
    TestPaletteLightAndDarkDiffer();
    TestPaletteSystemFollowsOs();
    TestHighContrastUsesSystemPalette();
    TestSelectionBorderIsNonColorSignal();
    TestThemeNeverTouchesCatalogIdentity();
    TestAccessibleNamesPerRow();
    TestSelectedAccessibleNameEmptyState();
    TestSearchFieldGeometry();
    TestSearchFieldScalingTo200Percent();
    TestEditRectInsideSearchBox();
    TestSearchFieldColors();
    std::printf("NR-015 dpi/theme/accessibility check PASSED\n");
    return 0;
}
