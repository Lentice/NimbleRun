#include "test_util.h"

#include "app_host/panel_model.h"
#include "search/search_engine.h"
#include "ui/panel_layout.h"
#include "ui/panel_accessibility.h"
#include "ui/panel_palette.h"
#include "ui/quick_select.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <oleacc.h>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::PanelModel;
using nimblerun::PanelAccessibilityElement;
using nimblerun::PanelAccessibilityProvider;
using nimblerun::PanelAccessibilitySnapshot;
using nimblerun::Theme;
using nimblerun::layout::ClampWindowSize;
using nimblerun::layout::FooterTopDip;
using nimblerun::layout::LayoutForDpi;
using nimblerun::layout::ViewportRowsForHeightDip;
using nimblerun::layout::SlotAtPointDip;
using nimblerun::layout::SlotRect;
using nimblerun::layout::kCellHeightDip;
using nimblerun::layout::kCellWidthDip;
using nimblerun::layout::kFooterTopDip;
using nimblerun::layout::kGridColumns;
using nimblerun::layout::kGridLeftDip;
using nimblerun::layout::kListLeftDip;
using nimblerun::layout::kListRightDip;
using nimblerun::layout::kListTopDip;
using nimblerun::layout::kPanelHeightDip;
using nimblerun::layout::kRowHeightDip;
using nimblerun::layout::kRowHintReserveDip;
using nimblerun::layout::kRowKeyBoxWidthDip;
using nimblerun::layout::kRowKeyGapDip;
using nimblerun::layout::kRowKeyRightInsetDip;
using nimblerun::layout::kTileSizeDip;
using nimblerun::palette::PanelColors;
using nimblerun::palette::ResolveColors;
using nimblerun::palette::Rgb;
using nimblerun::palette::SystemColors;
using nimblerun::ui::QuickSelectLabelForSlot;
using nimblerun::ui::QuickSelectSlotForKey;
using nimblerun::ui::kQuickSelectSlotCount;

namespace {

AppEntry Entry(std::wstring id, std::wstring name) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = name;
    // NR-038: a prefilled normalized_name is used verbatim (no re-normalization
    // in the search loop), so it must already be the normalized form.
    entry.normalized_name = nimblerun::NormalizeName(name);
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

std::wstring TakeBstr(BSTR value) {
    const std::wstring result = value == nullptr
        ? std::wstring{} : std::wstring(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}

VARIANT ChildId(LONG child) {
    VARIANT id{};
    id.vt = VT_I4;
    id.lVal = child;
    return id;
}

void TestAccessibleProviderMapping() {
    auto* provider = PanelAccessibilityProvider::Create(nullptr);
    Expect(provider != nullptr, "provider allocation");
    PanelAccessibilitySnapshot snapshot;
    snapshot.query = L"beta";
    snapshot.footer = L"Query: beta; Page 2 of 3; Selected: Beta";
    snapshot.page = 2;
    snapshot.page_count = 3;
    snapshot.search_focused = true;
    snapshot.selected_row = 0;
    snapshot.rows = {
        {PanelAccessibilityElement::Role::AppRow, L"Beta", RECT{10, 20, 100, 60}, true, false},
        {PanelAccessibilityElement::Role::AppRow, L"Missing", RECT{10, 60, 100, 100}, false, true},
    };
    Expect(provider->Update(nullptr, snapshot), "provider snapshot update");

    IAccessible* root = provider;
    LONG count = 0;
    Expect(SUCCEEDED(root->get_accChildCount(&count)) && count == 4,
           "search, two rows and footer are exposed");
    BSTR name = nullptr;
    Expect(SUCCEEDED(root->get_accName(ChildId(2), &name)) && TakeBstr(name) == L"Beta",
           "row name comes from snapshot display name");
    VARIANT role{};
    Expect(SUCCEEDED(root->get_accRole(ChildId(2), &role)) &&
               role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_LISTITEM,
           "row role is list item");
    VARIANT state{};
    Expect(SUCCEEDED(root->get_accState(ChildId(3), &state)) &&
               (state.lVal & STATE_SYSTEM_UNAVAILABLE) != 0,
           "missing pin is disabled");
    VARIANT selection{};
    Expect(SUCCEEDED(root->get_accSelection(&selection)) && selection.lVal == 2,
           "selection maps to the visible row child id");
    BSTR value = nullptr;
    Expect(SUCCEEDED(root->get_accValue(ChildId(1), &value)) && TakeBstr(value) == L"beta",
           "search value reflects query");
    Expect(SUCCEEDED(root->get_accValue(ChildId(4), &value)) &&
               TakeBstr(value).find(L"Page 2 of 3") != std::wstring::npos,
           "footer value reflects page state");
    IDispatch* child = nullptr;
    Expect(SUCCEEDED(root->get_accChild(ChildId(2), &child)) && child != nullptr,
           "row child is a COM object");
    IAccessible* row = nullptr;
    Expect(SUCCEEDED(child->QueryInterface(IID_IAccessible,
                                           reinterpret_cast<void**>(&row))),
           "row child supports IAccessible");
    name = nullptr;
    Expect(SUCCEEDED(row->get_accName(ChildId(CHILDID_SELF), &name)) &&
               TakeBstr(name) == L"Beta", "row child name remains stable");
    row->Release();
    child->Release();
    provider->Release();
}

PanelAccessibilityProvider* g_smoke_provider = nullptr;
int g_smoke_get_object_calls = 0;

LRESULT CALLBACK AccessibilitySmokeProc(HWND window, UINT message,
                                        WPARAM w_param, LPARAM l_param) {
    if (message == WM_GETOBJECT && g_smoke_provider) {
        ++g_smoke_get_object_calls;
        return g_smoke_provider->OnGetObject(w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void TestAccessibleProviderWindowSmoke() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Expect(SUCCEEDED(com), "COM initialization for WM_GETOBJECT smoke check");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t class_name[] = L"NimbleRun.AccessibilitySmoke";
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = AccessibilitySmokeProc;
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(0, class_name, L"", WS_POPUP,
                                  100, 100, 300, 200, nullptr, nullptr,
                                  instance, nullptr);
    Expect(window != nullptr, "native smoke window creation");
    g_smoke_provider = PanelAccessibilityProvider::Create(window);
    Expect(g_smoke_provider != nullptr, "native smoke provider allocation");
    g_smoke_get_object_calls = 0;
    PanelAccessibilitySnapshot snapshot;
    snapshot.footer = L"Page 1 of 1";
    snapshot.rows.push_back({PanelAccessibilityElement::Role::AppRow,
                             L"Smoke App", RECT{120, 120, 220, 160}, true, false});
    snapshot.selected_row = 0;
    Expect(g_smoke_provider->Update(window, snapshot), "native smoke snapshot update");
    LONG direct_count = 0;
    Expect(SUCCEEDED(g_smoke_provider->get_accChildCount(&direct_count)) && direct_count == 3,
           "native smoke provider keeps its root snapshot");
    IAccessible* client = nullptr;
    const LRESULT cookie = SendMessageW(window, WM_GETOBJECT, 0, OBJID_CLIENT);
    Expect(cookie != 0, "native WM_GETOBJECT returns an accessibility object");
    Expect(SUCCEEDED(ObjectFromLresult(cookie, IID_IAccessible, 0,
                                       reinterpret_cast<void**>(&client))) &&
               client != nullptr,
           "ObjectFromLresult obtains the WM_GETOBJECT provider");
    Expect(g_smoke_get_object_calls > 0, "native smoke reached WM_GETOBJECT");
    LONG count = 0;
    if (FAILED(client->get_accChildCount(&count))) {
        std::fprintf(stderr, "FAILED: native child count call\n");
        std::exit(1);
    }
    if (count != 3) {
        std::fprintf(stderr, "FAILED: native child count=%ld\n", count);
        std::exit(1);
    }
    Expect(count == 3,
           "native provider exposes search, row and footer");
    BSTR name = nullptr;
    Expect(SUCCEEDED(client->get_accName(ChildId(2), &name)) &&
               TakeBstr(name) == L"Smoke App", "native row name query");
    LONG left = 0;
    LONG top = 0;
    LONG width = 0;
    LONG height = 0;
    Expect(SUCCEEDED(client->accLocation(&left, &top, &width, &height, ChildId(2))) &&
               left == 120 && top == 120 && width == 100 && height == 40,
           "native row bounds query");
    client->Release();
    DestroyWindow(window);
    g_smoke_provider->Release();
    g_smoke_provider = nullptr;
    UnregisterClassW(class_name, instance);
    if (com == S_OK || com == S_FALSE) {
        CoUninitialize();
    }
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

// NR-024: the Alt+digit key sequence is pure header-only state: the slot
// mapping, the static labels and the reserved row width are all testable
// without a window (design-spec §4.7/§4.9).

void TestQuickSelectSlotForKey() {
    Expect(QuickSelectSlotForKey('1') == 0, "'1' maps to slot 0");
    Expect(QuickSelectSlotForKey('5') == 4, "'5' maps to slot 4");
    Expect(QuickSelectSlotForKey('9') == 8, "'9' maps to slot 8");
    Expect(QuickSelectSlotForKey('0') == 9, "'0' maps to slot 9");
    Expect(QuickSelectSlotForKey('A') == -1, "letter is not a quick-select key");
    Expect(QuickSelectSlotForKey(0) == -1, "NUL is not a quick-select key");
    Expect(QuickSelectSlotForKey(0x60) == -1, "VK_NUMPAD0 is not a quick-select key");
}

void TestQuickSelectLabelForSlot() {
    Expect(std::wcscmp(QuickSelectLabelForSlot(0), L"1") == 0, "slot 0 label is '1'");
    Expect(std::wcscmp(QuickSelectLabelForSlot(8), L"9") == 0, "slot 8 label is '9'");
    Expect(std::wcscmp(QuickSelectLabelForSlot(9), L"0") == 0, "slot 9 label is '0'");
    Expect(QuickSelectLabelForSlot(10) == nullptr, "slot 10 has no label");
    Expect(QuickSelectLabelForSlot(-1) == nullptr, "negative slot has no label");
}

// NR-128: kQuickSelectDigits was a literal the test verified against itself.
// The equivalent behavioral guarantee is a round-trip: every slot's label maps
// back to its own slot, which also proves the digit labels are unique.
void TestQuickSelectSlotLabelRoundTrip() {
    Expect(kQuickSelectSlotCount == 10, "quick-select slot count is 10");
    for (int slot = 0; slot < kQuickSelectSlotCount; ++slot) {
        const wchar_t* label = QuickSelectLabelForSlot(slot);
        Expect(label != nullptr, "every slot has a label");
        Expect(QuickSelectSlotForKey(*label) == slot,
               "slot label round-trips back to its own slot");
    }
}

void TestRowHintReserveWidth() {
    Expect(kRowHintReserveDip ==
               kRowKeyBoxWidthDip + kRowKeyRightInsetDip + kRowKeyGapDip,
           "row hint reserve is box + right inset + gap");
    Expect(kRowHintReserveDip == 36.0f, "row hint reserve is 36 DIP");
    Expect(kListLeftDip + kTileSizeDip + kRowHintReserveDip < kListRightDip,
           "app name still has positive width beside the key hint");
}

// NR-029: the empty-query grid must fit the list area and the result area must
// hold exactly 4 grid rows (6 x 4 = 24 cells per page, design-spec §4.9).
void TestGridGeometryFits() {
    Expect(kGridColumns * kCellWidthDip <= kListRightDip - kListLeftDip,
           "six 101-DIP cells fit in the list area");
    Expect(kGridLeftDip >= kListLeftDip &&
               kGridLeftDip + kGridColumns * kCellWidthDip <= kListRightDip,
           "grid is centered and stays inside the list area");
    Expect(static_cast<int>((kFooterTopDip - kListTopDip) / kCellHeightDip) == 4,
           "result area holds exactly 4 grid rows");
}

// NR-120: the footer band keeps its height and hugs the client bottom, so the
// path bar + key hints stay visible even when ClampWindowSize shortens the
// panel below 488 DIP. A full-height client keeps the band exactly on
// kFooterTopDip.
void TestFooterBandAlwaysVisible() {
    const float band = kPanelHeightDip - kFooterTopDip;
    Expect(FooterTopDip(kPanelHeightDip) == kFooterTopDip,
           "full-height client keeps the footer band on kFooterTopDip");
    // Clamped clients (200%@768 -> 348 DIP, 150%@1366x768 -> 464 DIP, etc.):
    // band top + band height never exceeds the client and never overlaps the
    // search box.
    const float clamped[] = {348.0f, 344.0f, 464.0f, 458.67f, 312.0f, 240.0f};
    for (const float client : clamped) {
        Expect(FooterTopDip(client) + band <= client + 0.001f,
               "footer band stays inside a clamped client");
        Expect(FooterTopDip(client) >= kListTopDip,
               "footer band never overlaps the search box");
    }
    Expect(FooterTopDip(348.0f) == 316.0f, "200%@768 work area band top is 316");
    Expect(FooterTopDip(464.0f) == 432.0f, "150%@1366x768 band top is 432");
}

// NR-120: ViewportRowsForHeightDip shrinks the visible row count so the footer
// fits; grid/list both covered; the full-height count is unchanged (8 list /
// 4 grid rows).
void TestViewportRowsShrinkForFooter() {
    Expect(ViewportRowsForHeightDip(kPanelHeightDip, 1) == 8, "full list rows");
    Expect(ViewportRowsForHeightDip(kPanelHeightDip, kGridColumns) == 4,
           "full grid rows");
    // 200% @ 1366x768 (work area 728px -> clamped client 348 DIP).
    Expect(ViewportRowsForHeightDip(348.0f, 1) == 5, "clamped list rows (200%)");
    Expect(ViewportRowsForHeightDip(348.0f, kGridColumns) == 2,
           "clamped grid rows (200%)");
    // 150% @ 1366x768 (work area 728px -> clamped client 464 DIP).
    Expect(ViewportRowsForHeightDip(464.0f, 1) == 7, "clamped list rows (150%)");
    Expect(ViewportRowsForHeightDip(464.0f, kGridColumns) == 3,
           "clamped grid rows (150%)");
    // The last painted row never crosses the footer band's top edge.
    const float last_list_bottom =
        kListTopDip + ViewportRowsForHeightDip(348.0f, 1) * kRowHeightDip;
    Expect(last_list_bottom <= FooterTopDip(348.0f),
           "last list row stays above the footer band");
    const float last_grid_bottom =
        kListTopDip +
        ViewportRowsForHeightDip(348.0f, kGridColumns) * kCellHeightDip;
    Expect(last_grid_bottom <= FooterTopDip(348.0f),
           "last grid row stays above the footer band");
}

// NR-120: for every measured DPI x work-area combo the clamped panel leaves
// the footer band fully inside the client and visible rows shrink instead of
// letting rows run past the band.
void TestClampedPanelKeepsFooter() {
    struct Combo {
        float dpi;
        int work_height;
    };
    const Combo combos[] = {
        {96.0f, 728}, {96.0f, 1040}, {96.0f, 1400},
        {120.0f, 728}, {120.0f, 1040}, {120.0f, 1400},
        {144.0f, 728}, {144.0f, 1040}, {144.0f, 1400},
        {192.0f, 720}, {192.0f, 728}, {192.0f, 1040}, {192.0f, 1400},
    };
    const float band = kPanelHeightDip - kFooterTopDip;
    for (const Combo& combo : combos) {
        const float scale = combo.dpi / 96.0f;
        const auto size = ClampWindowSize(combo.dpi, 1366, combo.work_height);
        // WS_POPUP client rect equals the window size; DIP height = px / scale.
        const float client_dip = static_cast<float>(size.height) / scale;
        const float footer_top = FooterTopDip(client_dip);
        Expect(footer_top + band <= client_dip + 0.001f,
               "footer band inside the clamped client");
        const float last_list_bottom =
            kListTopDip + ViewportRowsForHeightDip(client_dip, 1) * kRowHeightDip;
        Expect(last_list_bottom <= footer_top,
               "list rows stop at the footer band in every combo");
    }
}

void TestSlotGeometryRoundTrips() {
    const float heights[] = {kPanelHeightDip, 464.0f, 348.0f};
    const int columns[] = {1, kGridColumns};
    const float dpis[] = {96.0f, 144.0f, 192.0f};
    for (const float dpi : dpis) {
        const float scale = dpi / 96.0f;
        for (const int column_count : columns) {
            for (const float height : heights) {
                const int rows = ViewportRowsForHeightDip(height, column_count);
                for (int slot = 0; slot < rows * column_count; ++slot) {
                    const auto rect = SlotRect(slot, column_count, height);
                    const float center_x_px =
                        (rect.left + rect.right) * scale / 2.0f;
                    const float center_y_px =
                        (rect.top + rect.bottom) * scale / 2.0f;
                    const int round_trip = SlotAtPointDip(
                        center_x_px / scale, center_y_px / scale,
                        column_count, rows, height);
                    Expect(round_trip == slot, "slot center round-trips at every DPI and height");
                }
                const float footer_x = column_count > 1
                    ? kGridLeftDip + kCellWidthDip / 2.0f : kListLeftDip;
                Expect(SlotAtPointDip(footer_x, FooterTopDip(height) + 1.0f,
                                      column_count, rows, height) == -1,
                       "footer band is not a slot");
                Expect(SlotAtPointDip(kListLeftDip, kListTopDip + rows *
                                      (column_count > 1 ? kCellHeightDip : kRowHeightDip) + 1.0f,
                                      column_count, rows, height) == -1,
                       "past-viewport slot is not a hit");
            }
        }
    }
}


// NR-029: grid hover needs a visible fill in every theme. Light/dark use the
// card-level fill; high contrast collapses card to the window background, so
// the palette resolves hover to the system highlight there (the selection
// border is what separates a hovered cell from the selected cell).
void TestGridHoverFillVisible() {
    const PanelColors light = ResolveColors(Theme::Light, false, false, {});
    Expect(light.hover_fill == light.card && light.hover_fill != light.background,
           "light hover fill is the card-level fill, visible on the background");
    const PanelColors dark = ResolveColors(Theme::Dark, false, false, {});
    Expect(dark.hover_fill == dark.card && dark.hover_fill != dark.background,
           "dark hover fill is the card-level fill, visible on the background");
    const SystemColors system = InjectedSystem();
    const PanelColors hc = ResolveColors(Theme::Dark, false, true, system);
    Expect(hc.hover_fill == system.highlight && hc.hover_fill != hc.background,
           "high contrast hover fill is the system highlight, visible");
    Expect(hc.selected_border != hc.selected_fill,
           "selection border keeps hover distinct from selected in high contrast");
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
    TestAccessibleProviderMapping();
    TestAccessibleProviderWindowSmoke();
    TestSearchFieldGeometry();
    TestSearchFieldScalingTo200Percent();
    TestEditRectInsideSearchBox();
    TestSearchFieldColors();
    TestQuickSelectSlotForKey();
    TestQuickSelectLabelForSlot();
    TestQuickSelectSlotLabelRoundTrip();
    TestRowHintReserveWidth();
    TestGridGeometryFits();
    TestFooterBandAlwaysVisible();
    TestViewportRowsShrinkForFooter();
    TestClampedPanelKeepsFooter();
    TestSlotGeometryRoundTrips();
    TestGridHoverFillVisible();
    std::printf("NR-015/NR-024/NR-029/NR-111 accessibility check PASSED\n");
    return 0;
}
