#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>

#include "app_host/catalog_watcher.h"
#include "app_host/hotkey.h"
#include "app_host/panel_model.h"
#include "app_host/settings_dialog.h"
#include "catalog/appsfolder_catalog.h"
#include "catalog/catalog_cache.h"
#include "catalog/catalog_refresh.h"
#include "catalog/dedup.h"
#include "catalog/stable_id.h"
#include "catalog/start_menu_catalog.h"
#include "catalog/user_folder_catalog.h"
#include "diagnostics/diagnostic_log.h"
#include "icons/icon_cache.h"
#include "icons/icon_store.h"
#include "icons/icon_worker.h"
#include "icons/shell_icon_provider.h"
#include "launch/shell_launch.h"
#include "pins/pin_store.h"
#include "resources/resource.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "ui/panel_layout.h"
#include "ui/panel_palette.h"
#include "ui/quick_select.h"
#include "usage/usage_store.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"NimbleRun.Phase0Probe";
constexpr wchar_t kWindowTitle[] = L"NimbleRun";
constexpr wchar_t kInstanceMutex[] = L"Local\\NimbleRun.SingleInstance";
constexpr wchar_t kShowPanelMessageName[] = L"NimbleRun.ShowPanel";

// Tray icon callback message used by Shell_NotifyIcon.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
// Tray command messages posted to the main window.
constexpr UINT kRefreshMessage = WM_APP + 2;
constexpr UINT kSettingsMessage = WM_APP + 3;
constexpr UINT kAboutMessage = WM_APP + 4;
constexpr UINT kExitMessage = WM_APP + 5;
// NR-011: a watched directory changed. wParam = 1-based watch index, lParam = 1
// for a full rescan (buffer overflow / ERROR_NOTIFY_ENUM_DIR), 0 for a change.
constexpr UINT kWatchChangedMessage = WM_APP + 7;
// NR-011: a background enumeration finished. wParam = generation, lParam =
// pointer to a heap RebuildResult the UI thread takes ownership of.
constexpr UINT kRebuildDoneMessage = WM_APP + 8;
// NR-032: one decoded icon finished on the worker thread. lParam = pointer to a
// heap IconResult the UI thread takes ownership of (empty bitmap = failure).
constexpr UINT kIconReadyMessage = WM_APP + 9;
// NR-011: debounce timer id (500 ms, see FR-008).
constexpr UINT_PTR kRebuildTimerId = 2;

constexpr UINT kTrayIconId = 1;
constexpr UINT kCmdOpen = 1;
constexpr UINT kCmdRefresh = 2;
constexpr UINT kCmdSettings = 3;
constexpr UINT kCmdAbout = 4;
constexpr UINT kCmdExit = 5;
// NR-018: panel row context menu commands (design-spec §4.8).
constexpr UINT kCmdPin = 11;
constexpr UINT kCmdUnpin = 12;
constexpr UINT kCmdOpenLocation = 13;
// NR-040: "Properties" and "Remove from recent" context menu commands.
constexpr UINT kCmdProperties = 14;
constexpr UINT kCmdForgetRecent = 15;

constexpr int kSearchId = 100;

// NR-018: centralized English strings for the panel row context menu
// (design-spec §NFR-006); one table so text never scatters across the host.
namespace context_menu_strings {
constexpr wchar_t kPin[] = L"Pin";
constexpr wchar_t kUnpin[] = L"Unpin";
constexpr wchar_t kOpenFileLocation[] = L"Open file location";
// NR-040: "Remove from recent" (recent region rows only) and "Properties"
// (valid filesystem paths only, same gate as Open file location).
constexpr wchar_t kProperties[] = L"Properties";
constexpr wchar_t kRemoveFromRecent[] = L"Remove from recent";
} // namespace context_menu_strings

// NR-020: centralized English row/hint strings (design-spec §4.2/§4.3).
namespace list_strings {
constexpr wchar_t kWindowsApp[] = L"Windows app";
constexpr wchar_t kBuildingCatalog[] = L"Building app catalog\u2026";
constexpr wchar_t kNoMatchingApps[] = L"No matching apps";
} // namespace list_strings

// NR-021: centralized footer key-hint strings (design-spec §4.9). Fixed
// content, right-aligned in the reserved band; no status/version text.
namespace footer_strings {
constexpr wchar_t kScroll[] = L"Scroll";
constexpr wchar_t kPageUp[] = L"PgUp";
constexpr wchar_t kPageDown[] = L"PgDn";
// NR-024: the Alt+digit quick-select hint group (design-spec §4.9).
constexpr wchar_t kLaunch[] = L"Launch";
constexpr wchar_t kAltOnePrefix[] = L"Alt+1~";
} // namespace footer_strings

// NR-022: centralized English strings for the launch-failure / open-location
// dialog (design-spec §11). One dialog per user-triggered action; never a
// chained sequence of message boxes.
namespace dialog_strings {
constexpr wchar_t kTitle[] = L"NimbleRun";
constexpr wchar_t kOpenLocationFailed[] = L"Failed to open file location.";
constexpr wchar_t kLaunchFailedPrefix[] = L"Failed to launch \"";
constexpr wchar_t kLaunchFailedSuffix[] = L"\". ";
constexpr wchar_t kReasonNotInstalled[] = L"The app may have been removed or moved.";
constexpr wchar_t kReasonInvalid[] = L"The app entry is invalid.";
constexpr wchar_t kReasonAccessDenied[] = L"Access was denied.";
// NR-040: shown when the Shell's properties dialog cannot be opened.
constexpr wchar_t kPropertiesFailed[] = L"Failed to open properties.";
} // namespace dialog_strings

UINT g_show_panel_message = 0;

// Owns the single global hotkey (NR-003). Swap keeps register-new-first
// semantics; the active combo alternates between two ids (see ActiveId()).
nimblerun::GlobalHotkey g_hotkey;

// Last hotkey registration failure code, kept for NR-017 diagnostics.
DWORD g_last_hotkey_error = ERROR_SUCCESS;

// Panel state (NR-010). The model is pure; the window translates input.
nimblerun::PanelModel* g_model = nullptr;
nimblerun::UsageStore* g_usage = nullptr;
// NR-018: pure pin store (favorites.txt). The host reloads it when the panel
// opens and reconciles it against the catalog snapshot; the model only mirrors
// OrderedPins().
nimblerun::PinStore* g_pins = nullptr;
// True while the row context menu's modal loop is running, so the panel does
// not hide itself via WM_KILLFOCUS before a Pin/Unpin/Open-location choice.
bool g_context_menu_active = false;
// NR-022: true while the launch-failure dialog's modal loop is running, so the
// panel does not hide itself via WM_KILLFOCUS when the dialog takes focus
// (design-spec §11; same pattern as the NR-018 context menu flag).
bool g_dialog_active = false;
// NR-022: pure one-shot gate for the background refresh triggered by a launch
// failure. A failure schedules at most one full rebuild; a rebuild already
// running is merged instead.
nimblerun::LaunchFailureRefreshGate g_launch_failure_refresh;
// Live settings store, owned by wWinMain; the tray Settings dialog persists
// through it (NR-013).
nimblerun::SettingsStore* g_settings_store = nullptr;
// Current settings mirror, refreshed after the dialog applies (NR-013/NR-011).
nimblerun::Settings g_settings;
bool g_hide_after_launch = true;

// NR-015 theme state. The palette resolver (ui/panel_palette.h) is pure; this
// global is the host's current Theme setting, refreshed on every panel show.
nimblerun::Theme g_theme = nimblerun::Theme::System;
// Colors resolved for the current frame and the colors the live brushes were
// built with; when they differ the device resources are rebuilt (theme or high
// contrast changed between paints). No timers, purely event-driven.
nimblerun::palette::PanelColors g_colors{};
nimblerun::palette::PanelColors g_brush_colors{};

// NR-012/NR-032 icon state. The cache is the pure core; the Shell provider
// lives behind the worker, so the UI thread never calls Shell
// (design-spec §FR-009).
nimblerun::IconCache* g_icon_cache = nullptr;
// NR-032: one background thread owns Shell COM; the UI thread only posts
// requests and receives decoded bitmaps through kIconReadyMessage.
nimblerun::IconWorker* g_icon_worker = nullptr;
// Encoded keys with a request already in flight; a Render() miss on one of
// these does not re-post. UI-thread owned, never cleared on show (those
// requests are still flying).
std::set<std::wstring> g_pending_icon_keys;
// Encoded keys already handed to the provider this panel session, so a failed
// icon is not re-requested on every paint. Cleared on each show so a
// transient failure is retried the next time the panel opens.
std::set<std::wstring> g_requested_icon_keys;

HWND g_search_edit = nullptr;
WNDPROC g_search_original_proc = nullptr;
// NR-023: the search EDIT's 24-DIP message-font HFONT and its theme-colored
// background brush. Each GDI object exists exactly once; both are rebuilt on a
// palette change and released on WM_DESTROY (no per-message allocation).
HFONT g_search_font = nullptr;
HBRUSH g_search_bg_brush = nullptr;

// NR-011 refresh state. The coordinator is pure; the watcher posts change
// messages, and enumeration runs on short-lived background threads whose
// results come back through kRebuildDoneMessage.
nimblerun::CatalogRefreshCoordinator* g_refresh = nullptr;
nimblerun::CatalogWatcher* g_watcher = nullptr;
// 1-based watcher watch index -> CatalogSource, aligned with the watcher's
// root order (Start Menu folders first, then each user-folder root).
std::vector<nimblerun::CatalogSource> g_watch_sources;

// NR-017: bounded local diagnostic log under the per-user data dir. Only
// sanitized stage names, error codes and short details are written; never
// search text, usernames, personal paths or command lines (design-spec §FR-014).
nimblerun::DiagnosticLog* g_diag = nullptr;

// One finished background enumeration, transferred to the UI thread.
struct RebuildResult {
    std::uint64_t generation = 0;
    nimblerun::CatalogSource source = nimblerun::CatalogSource::StartMenu;
    bool failed = false;
    std::vector<nimblerun::AppEntry> entries;
};

std::int64_t MonotonicMs() {
    return static_cast<std::int64_t>(GetTickCount64());
}

ID2D1Factory* g_d2d_factory = nullptr;
ID2D1HwndRenderTarget* g_render_target = nullptr;
ID2D1SolidColorBrush* g_text_brush = nullptr;
ID2D1SolidColorBrush* g_dim_brush = nullptr;
ID2D1SolidColorBrush* g_card_brush = nullptr;
ID2D1SolidColorBrush* g_selected_brush = nullptr;
ID2D1SolidColorBrush* g_selected_border_brush = nullptr;
ID2D1SolidColorBrush* g_hover_brush = nullptr;  // NR-029: grid hover cell fill
ID2D1SolidColorBrush* g_search_fill_brush = nullptr;
ID2D1SolidColorBrush* g_search_border_brush = nullptr;
IDWriteFactory* g_write_factory = nullptr;
IDWriteTextFormat* g_title_format = nullptr;
IDWriteTextFormat* g_text_format = nullptr;
IDWriteTextFormat* g_small_format = nullptr;
// NR-029: centered single-line format for grid cell names.
IDWriteTextFormat* g_grid_name_format = nullptr;
// NR-020: character-granularity ellipsis for single-line row text; kept alive
// as long as the text formats above (see SetTrimming lifetime requirements).
IDWriteInlineObject* g_ellipsis_sign = nullptr;

// NR-029: window-layer grid hover state (pure visual; not model state). -1
// means the pointer is not over a cell. Only re-invalidated when the hit cell
// changes, and cleared on WM_MOUSELEAVE and layout switches; no timers.
int g_grid_hover_index = -1;
// True between a successful TrackMouseEvent(TME_LEAVE) and its WM_MOUSELEAVE,
// so leave-tracking is re-armed only after the leave actually fires.
bool g_tracking_mouse_leave = false;

template <typename T>
void Release(T*& resource) {
    if (resource) {
        resource->Release();
        resource = nullptr;
    }
}

void DiscardDeviceResources() {
    Release(g_render_target);
    Release(g_text_brush);
    Release(g_dim_brush);
    Release(g_card_brush);
    Release(g_selected_brush);
    Release(g_selected_border_brush);
    Release(g_hover_brush);
    Release(g_search_fill_brush);
    Release(g_search_border_brush);
}

// NR-015 OS state readers. Kept here (the host touches the OS); the pure
// palette model only maps these inputs to colors.

// True when the OS theme is dark. Reads the same per-user "light theme" toggle
// Windows Settings writes; missing value defaults to light. Chosen over the
// winrt UISettings/IsAppsUsingDarkTheme path because it is a single plain
// registry read with no COM or winrt dependency, reliable on Win10 22H2/Win11.
bool SystemUsesDarkTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LONG status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) {
        return false;  // missing / unreadable -> light mode
    }
    return value == 0;
}

// True when Windows high-contrast mode is active (design-spec §NFR-006).
bool HighContrastActive() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
        return false;
    }
    return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// GetSysColor returns 0x00BBGGRR; the palette model uses 0xRRGGBB.
nimblerun::palette::Rgb ColorRefToRgb(COLORREF color) {
    return (static_cast<nimblerun::palette::Rgb>(GetRValue(color)) << 16) |
        (static_cast<nimblerun::palette::Rgb>(GetGValue(color)) << 8) |
        static_cast<nimblerun::palette::Rgb>(GetBValue(color));
}

// NR-023: reverse of ColorRefToRgb; GDI (WM_CTLCOLOREDIT, CreateSolidBrush)
// wants 0x00BBGGRR while the palette model stores 0xRRGGBB.
COLORREF RgbToColorRef(nimblerun::palette::Rgb rgb) {
    return RGB(static_cast<BYTE>((rgb >> 16) & 0xFF),
               static_cast<BYTE>((rgb >> 8) & 0xFF),
               static_cast<BYTE>(rgb & 0xFF));
}

nimblerun::palette::SystemColors ReadSystemColors() {
    nimblerun::palette::SystemColors system;
    system.window = ColorRefToRgb(GetSysColor(COLOR_WINDOW));
    system.window_text = ColorRefToRgb(GetSysColor(COLOR_WINDOWTEXT));
    system.highlight = ColorRefToRgb(GetSysColor(COLOR_HIGHLIGHT));
    system.highlight_text = ColorRefToRgb(GetSysColor(COLOR_HIGHLIGHTTEXT));
    system.gray_text = ColorRefToRgb(GetSysColor(COLOR_GRAYTEXT));
    return system;
}

nimblerun::palette::PanelColors ResolveCurrentColors() {
    return nimblerun::palette::ResolveColors(
        g_theme,
        SystemUsesDarkTheme(),
        HighContrastActive(),
        ReadSystemColors());
}

bool CreateDeviceResources(HWND window) {
    if (g_render_target && g_text_brush && g_dim_brush && g_card_brush &&
        g_selected_brush && g_selected_border_brush && g_hover_brush &&
        g_search_fill_brush && g_search_border_brush &&
        g_title_format && g_text_format && g_small_format &&
        g_grid_name_format) {
        return true;
    }
    if (g_render_target) {
        DiscardDeviceResources();
    }

    if (!g_d2d_factory && FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            nullptr,
            reinterpret_cast<void**>(&g_d2d_factory)))) {
        return false;
    }

    if (!g_write_factory && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&g_write_factory)))) {
        return false;
    }

    RECT client_rect{};
    GetClientRect(window, &client_rect);
    const auto size = D2D1::SizeU(
        static_cast<UINT>(std::max(1L, client_rect.right - client_rect.left)),
        static_cast<UINT>(std::max(1L, client_rect.bottom - client_rect.top)));
    const auto render_target_properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE));
    const auto hwnd_properties = D2D1::HwndRenderTargetProperties(
        window,
        size,
        D2D1_PRESENT_OPTIONS_NONE);

    if (FAILED(g_d2d_factory->CreateHwndRenderTarget(
            render_target_properties,
            hwnd_properties,
            &g_render_target))) {
        return false;
    }

    // NR-015: font sizes are DIPs; D2D/DWrite scale them with the render
    // target's DPI, so they are not re-scaled here.
    const HRESULT title = g_write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTitleFontDip, L"en-US", &g_title_format);
    const HRESULT text = g_write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTextFontDip, L"en-US", &g_text_format);
    const HRESULT small = g_write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kSmallFontDip, L"en-US", &g_small_format);
    // NR-029: grid cell names use the same face/size as list row names but are
    // centered in the cell; alignment is set below with the trimming.
    const HRESULT grid_name = g_write_factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTextFontDip, L"en-US", &g_grid_name_format);
    if (FAILED(title) || FAILED(text) || FAILED(small) || FAILED(grid_name)) {
        return false;
    }

    // NR-020: row name and source-path formats are single-line and truncate
    // with a character-granularity trailing ellipsis (design-spec §4.2); long
    // text never wraps or changes row height. The trimming sign is kept in a
    // file-scope pointer for the formats' lifetime.
    if (!g_ellipsis_sign) {
        g_write_factory->CreateEllipsisTrimmingSign(g_text_format, &g_ellipsis_sign);
    }
    if (g_ellipsis_sign) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        g_text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        g_text_format->SetTrimming(&trimming, g_ellipsis_sign);
        g_small_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        g_small_format->SetTrimming(&trimming, g_ellipsis_sign);
        // NR-029: the grid name format follows the same single-line + ellipsis
        // rule, centered inside its cell; name length never changes cell
        // geometry (design-spec §4.2/§4.9).
        g_grid_name_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        g_grid_name_format->SetTrimming(&trimming, g_ellipsis_sign);
        g_grid_name_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_grid_name_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Brushes are built from the palette resolved for this frame; when the
    // resolved colors change, Render() discards these first.
    const nimblerun::palette::PanelColors& c = g_colors;
    const bool brushes =
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.text), &g_text_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.dim), &g_dim_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.card), &g_card_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.selected_fill), &g_selected_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.selected_border), &g_selected_border_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.hover_fill), &g_hover_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.input_fill), &g_search_fill_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(c.input_border), &g_search_border_brush));
    if (brushes) {
        g_brush_colors = g_colors;
    }
    return brushes;
}

// NR-020/NR-029: model item index for a physical client point, or -1 when it
// is not on any visible row (list) or cell (grid). Grid cells map through
// FirstVisibleRow() + row_index * Columns() + col_index; cells past RowCount()
// are a miss (design-spec §4.8).
int CellAtPoint(HWND window, int x, int y) {
    if (!g_model) {
        return -1;
    }
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    if (y < layout.list_top) {
        return -1;
    }
    const int columns = g_model->Columns();
    if (columns > 1) {
        const int cell_width = static_cast<int>(std::lround(
            nimblerun::layout::kCellWidthDip * layout.scale));
        const int cell_height = static_cast<int>(std::lround(
            nimblerun::layout::kCellHeightDip * layout.scale));
        const int grid_left = static_cast<int>(std::lround(
            nimblerun::layout::kGridLeftDip * layout.scale));
        const int col = (x - grid_left) / cell_width;
        const int row = (y - layout.list_top) / cell_height;
        if (col < 0 || col >= columns || row < 0) {
            return -1;
        }
        const int index = g_model->FirstVisibleRow() + row * columns + col;
        return index < static_cast<int>(g_model->Rows().size()) ? index : -1;
    }
    if (x < layout.list_left) {
        return -1;
    }
    const int index =
        (y - layout.list_top) / layout.row_height + g_model->FirstVisibleRow();
    return index >= 0 && index < static_cast<int>(g_model->Rows().size()) ? index : -1;
}

// NR-020: recomputes the viewport row count from the current client rect and
// DPI and pushes it into the model (design-spec §4.2/§4.9). Called whenever the
// panel is shown or resized; no timers. NR-029: the row height differs per
// layout state (48 DIP list rows vs 96 DIP grid cells), so Columns() picks it.
void UpdateViewportRows(HWND window) {
    if (!g_model) {
        return;
    }
    RECT client{};
    GetClientRect(window, &client);
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    const int list_height =
        std::max(0, static_cast<int>(client.bottom - client.top) - layout.list_top);
    const int row_height_px = g_model->Columns() > 1
        ? static_cast<int>(std::lround(nimblerun::layout::kCellHeightDip * layout.scale))
        : layout.row_height;
    g_model->SetViewportRows(std::max(1, list_height / row_height_px));
}

// NR-022: maps the Win32 error code returned by shell_launch to a short
// English reason for the dialog (design-spec §11). The launch error mapping in
// shell_launch is unchanged; only the presentation lives here.
std::wstring LaunchErrorReason(DWORD error_code) {
    switch (error_code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return dialog_strings::kReasonNotInstalled;
    case ERROR_INVALID_PARAMETER:
        return dialog_strings::kReasonInvalid;
    case ERROR_ACCESS_DENIED:
        return dialog_strings::kReasonAccessDenied;
    default:
        return L"Error " + std::to_wstring(error_code) + L".";
    }
}

// NR-022: one modal dialog per user-triggered failure (design-spec §11; no
// chained message boxes). The dialog's modal loop steals focus, so the same
// flag-based suppression as the NR-018 context menu keeps the panel from
// hiding on WM_KILLFOCUS; focus returns to the search box afterwards.
void ShowErrorDialog(HWND window, const std::wstring& message) {
    g_dialog_active = true;
    MessageBoxW(window, message.c_str(), dialog_strings::kTitle,
                MB_OK | MB_ICONWARNING);
    g_dialog_active = false;
    if (g_search_edit) {
        SetFocus(g_search_edit);
    }
}

// NR-011: starts one background thread per source for a rebuild cycle; defined
// below, forward-declared for the NR-022 launch-failure refresh path.
void StartRebuild(HWND window, std::vector<nimblerun::CatalogSource> sources);

// NR-037: prewarms exactly one empty-state page (design-spec §4.3, 24 cells)
// on the worker, so the next panel show's first frame already has real icons
// instead of fallbacks (design-spec §FR-009). The one-page cap is the only
// thing keeping this compatible with §FR-009 "Catalog 不預解碼所有圖示";
// raising it predecodes more than the next shown page.
void PrewarmEmptyStatePage(HWND window) {
    if (!g_model || !g_icon_worker || !g_icon_cache || !g_refresh) {
        return;
    }
    const std::vector<std::wstring> ids =
        g_model->EmptyStatePrewarmIds(nimblerun::kIconCacheWorkingSetItems);
    if (ids.empty()) {
        return;
    }
    // Grid variant for the current monitor DPI: the next panel show always
    // opens in the empty-query grid state (40 DIP cell -> physical px via
    // IconVariantForPixels). The list-state variant is the same tier for
    // most DPIs, so no second prewarm is needed.
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    const int needed_px = static_cast<int>(std::lround(
        nimblerun::layout::kIconSizeDip * layout.scale));
    for (const std::wstring& id : ids) {
        // Resolve through the current catalog snapshot; a pin for an app
        // absent from the catalog is skipped (design-spec §FR-011).
        const nimblerun::AppEntry* entry = nullptr;
        for (const nimblerun::AppEntry& candidate : g_refresh->Snapshot()) {
            if (candidate.stable_id == id) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            continue;
        }
        const nimblerun::IconKey key{entry->stable_id,
                                     nimblerun::IconVariantForPixels(needed_px)};
        const std::wstring encoded = key.Encode();
        // Skip keys already cached, in flight, or failed this panel session so
        // repeated hide/show cycles never re-post the same 24 keys.
        if (g_icon_cache->Peek(encoded) != nullptr ||
            g_pending_icon_keys.count(encoded) != 0 ||
            g_requested_icon_keys.count(encoded) != 0) {
            continue;
        }
        g_icon_worker->Post({*entry, key, /*visible=*/false});
    }
}

// NR-036: the single hide path. Every way the panel disappears (Esc second
// stage, WM_KILLFOCUS auto-hide, hide-after-launch, hotkey/tray toggle) funnels
// through here so the freshly fetched icons are flushed exactly once per hide
// -- never once per call site. The pinned list is the in-memory copy ShowPanel
// already loaded (design-spec §10.2); it rides the flush task as a pure-value
// copy and the worker never re-reads favorites.txt.
void HidePanel(HWND window) {
    ShowWindow(window, SW_HIDE);
    if (g_icon_worker) {
        const std::vector<std::wstring> pins =
            g_pins ? g_pins->OrderedPins() : std::vector<std::wstring>{};
        g_icon_worker->PostFlush(pins, static_cast<std::uint64_t>(std::time(nullptr)));
        // NR-037: flush first (NR-036 timing 1), then prewarm the page that is
        // guaranteed to be shown next; the worker drains them in queue order.
        PrewarmEmptyStatePage(window);
    }
}

void ActivateRow(std::size_t index, HWND window) {
    if (!g_model || index >= g_model->Rows().size()) {
        return;
    }
    const nimblerun::AppEntry entry = g_model->Rows()[index];
    const nimblerun::LaunchResult result = nimblerun::LaunchEntry(entry, window);
    if (!result.ok) {
        // NR-022: on failure, first trigger one background catalog refresh
        // through the same Ctrl+R rebuild path (StartRebuild over every source);
        // the gate merges into a rebuild already running so clicking several
        // dead entries never queues several full scans. Then show a single
        // dialog; the panel stays visible and hide-after-launch does not run.
        if (g_refresh &&
            g_launch_failure_refresh.OnLaunchAttempt(false,
                                                     g_refresh->IsRebuildInProgress())) {
            const std::vector<nimblerun::CatalogSource> all = {
                nimblerun::CatalogSource::StartMenu,
                nimblerun::CatalogSource::AppsFolder,
                nimblerun::CatalogSource::UserFolder,
            };
            StartRebuild(window, all);
        }
        if (g_diag) {
            g_diag->Write(L"launch",
                          L"error " + std::to_wstring(result.error_code) +
                              L" source=" + entry.stable_id);
        }
        const std::wstring message =
            dialog_strings::kLaunchFailedPrefix + entry.display_name +
            dialog_strings::kLaunchFailedSuffix +
            LaunchErrorReason(result.error_code);
        ShowErrorDialog(window, message);
        return;  // keep the panel visible; no crash
    }
    g_launch_failure_refresh.OnLaunchAttempt(
        true, g_refresh ? g_refresh->IsRebuildInProgress() : false);
    // Success: update usage and hide per settings.
    if (g_usage) {
        g_usage->RecordLaunch(entry.stable_id, static_cast<std::int64_t>(std::time(nullptr)));
        g_usage->Save();
    }
    if (g_hide_after_launch) {
        HidePanel(window);
    }
}

void OpenFileLocation(HWND window, const nimblerun::AppEntry& entry) {
    // Only filesystem paths can be revealed; AppsFolder parsing names cannot.
    if (!nimblerun::IsPathIdentity(entry.launch_identity)) {
        return;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(entry.launch_identity.c_str(), nullptr, &pidl, 0, nullptr))) {
        return;
    }
    const HRESULT hr = SHOpenFolderAndSelectItems(
        pidl, 0, nullptr, OFASI_EDIT | OFASI_OPENDESKTOP);
    CoTaskMemFree(pidl);
    if (FAILED(hr)) {
        if (g_diag) {
            g_diag->Write(L"open-location",
                          L"error " + std::to_wstring(static_cast<unsigned long>(hr)));
        }
        ShowErrorDialog(window, dialog_strings::kOpenLocationFailed);
    }
}

// NR-040: the Shell's own properties dialog, the same one Explorer shows for
// the shortcut/exe. Only filesystem paths qualify -- AppsFolder parsing names
// have no properties sheet -- so the caller gates on IsPathIdentity() exactly
// as it does for OpenFileLocation().
void ShowItemProperties(HWND window, const nimblerun::AppEntry& entry) {
    if (!nimblerun::IsPathIdentity(entry.launch_identity)) {
        return;
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // SEE_MASK_INVOKEIDLIST is required for the "properties" verb: the Shell
    // has to build the item's context menu to find it. SEE_MASK_NOASYNC keeps
    // the call valid without needing the process to outlive an async handoff.
    info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOASYNC;
    info.hwnd = window;
    info.lpVerb = L"properties";
    info.lpFile = entry.launch_identity.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (g_diag) {
            g_diag->Write(L"properties", L"error " + std::to_wstring(error));
        }
        ShowErrorDialog(window, dialog_strings::kPropertiesFailed);
    }
}

// NR-012/NR-031: cache key for an entry at a physical-pixel need. The variant
// is the smallest standard size >= needed_px (IconVariantForPixels); neither
// DPI nor the exact on-screen size is part of the key, so one entry serves the
// grid cell and list row at every DPI within the same variant tier
// (design-spec §FR-009). needed_px comes from the caller's LayoutForDpi().
nimblerun::IconKey IconKeyFor(const nimblerun::AppEntry& entry, int needed_px) {
    return {entry.stable_id, nimblerun::IconVariantForPixels(needed_px)};
}

// NR-032: posts a request for a key the renderer just missed, at most once per
// key while a result is in flight (pending) or already failed this panel
// session (requested). Called from Render() only, so requests are visible=true
// and the work stays off the UI thread; the cache hit path never posts.
void RequestVisibleIcon(const nimblerun::AppEntry& entry, const nimblerun::IconKey& key,
                        const std::wstring& encoded) {
    if (!g_icon_worker || g_pending_icon_keys.count(encoded) != 0 ||
        g_requested_icon_keys.count(encoded) != 0) {
        return;
    }
    g_pending_icon_keys.insert(encoded);
    g_icon_worker->Post({entry, key, /*visible=*/true});
}

void DrawDecodedIcon(const nimblerun::IconBitmap& icon,
                     const D2D1_RECT_F& tile,
                     float dpi_x,
                     float dpi_y) {
    if (icon.Empty()) {
        return;
    }
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi_x,
        dpi_y);
    ID2D1Bitmap* bitmap = nullptr;
    if (FAILED(g_render_target->CreateBitmap(
            D2D1::SizeU(icon.width, icon.height),
            icon.pixels.data(),
            icon.width * sizeof(std::uint32_t),
            props,
            &bitmap))) {
        return;
    }
    g_render_target->DrawBitmap(
        bitmap, tile, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    bitmap->Release();
}

// NR-018: reload pins from the store, reconcile them against the current
// catalog snapshot (30-day retention, last_seen refresh; design-spec §FR-011),
// persist any change, and mirror the ordered pin list into the panel model.
// Called when the panel opens (restart / external edit) and whenever the
// catalog snapshot is swapped.
void RefreshPins() {
    if (!g_pins || !g_refresh || !g_model) {
        return;
    }
    g_pins->Load();
    g_pins->Reconcile(g_refresh->Snapshot(),
                      static_cast<std::int64_t>(std::time(nullptr)));
    g_pins->Save();
    g_model->SetPins(g_pins->OrderedPins());
}

// NR-011: repoints the panel model at the coordinator's current snapshot and
// refreshes the recent list, so a swapped-in catalog appears immediately.
void RefreshPanelSnapshot() {
    if (!g_model || !g_refresh || !g_usage) {
        return;
    }
    g_model->SetCatalog(&g_refresh->Snapshot());
    std::vector<nimblerun::UsageRecord> recent_records = g_usage->Recent(g_settings.recent_count);
    std::vector<nimblerun::AppEntry> recent_entries;
    recent_entries.reserve(recent_records.size());
    for (const nimblerun::UsageRecord& record : recent_records) {
        for (const nimblerun::AppEntry& entry : g_refresh->Snapshot()) {
            if (entry.stable_id == record.stable_id) {
                recent_entries.push_back(entry);
                break;
            }
        }
    }
    g_model->SetRecent(std::move(recent_entries));
    RefreshPins();
}

// NR-011: starts one background thread per source in `sources` for a rebuild
// cycle; results return through kRebuildDoneMessage on the UI thread.
void StartRebuild(HWND window, std::vector<nimblerun::CatalogSource> sources) {
    if (!g_refresh) {
        return;
    }
    const std::uint64_t generation = g_refresh->BeginGeneration(sources);
    for (const nimblerun::CatalogSource source : sources) {
        std::thread worker([window, generation, source]() {
            auto* result = new RebuildResult;
            result->generation = generation;
            result->source = source;
            switch (source) {
            case nimblerun::CatalogSource::StartMenu:
                result->entries = nimblerun::EnumerateStartMenuCatalog();
                break;
            case nimblerun::CatalogSource::AppsFolder:
                // NR-028: "Include Windows apps" off skips the enumeration
                // entirely (no COM walk) and the source reports empty, so the
                // merged snapshot clears old packaged-app entries via the same
                // ApplySourceResult path.
                result->entries = g_settings.include_windows_apps
                    ? nimblerun::EnumerateAppsFolderCatalog().entries
                    : std::vector<nimblerun::AppEntry>{};
                break;
            case nimblerun::CatalogSource::UserFolder:
                result->entries = nimblerun::EnumerateUserFolderCatalog(g_settings);
                break;
            }
            PostMessageW(window, kRebuildDoneMessage,
                         static_cast<WPARAM>(generation),
                         reinterpret_cast<LPARAM>(result));
        });
        worker.detach();
    }
}

// NR-011: coalesces dense file events behind a 500 ms debounce; a full-rescan
// marker bypasses the wait. One timer at a time; after it fires the due sources
// are rebuilt together.
void ScheduleDebouncedRebuild(HWND window) {
    if (KillTimer(window, kRebuildTimerId) != 0) {
        // A timer was already pending; restarting it extends the debounce window.
    }
    SetTimer(window, kRebuildTimerId, 500, nullptr);
}

// NR-011: maps a watcher watch index (1-based) back to its catalog source.
nimblerun::CatalogSource WatchIndexToSource(int index) {
    if (index >= 1 && index <= static_cast<int>(g_watch_sources.size())) {
        return g_watch_sources[static_cast<std::size_t>(index - 1)];
    }
    return nimblerun::CatalogSource::StartMenu;
}

// NR-011: (re)starts the directory watchers from the current settings. The
// two Programs known folders watch recursively; each user-folder root uses its
// own recursive flag. g_watch_sources stays aligned with the watch order.
void StartWatchers() {
    if (!g_watcher) {
        return;
    }
    std::vector<std::wstring> roots;
    std::vector<bool> recursive;
    g_watch_sources.clear();

    auto add_root = [&](const std::wstring& path, bool sub,
                        nimblerun::CatalogSource source) {
        if (path.empty()) {
            return;
        }
        roots.push_back(path);
        recursive.push_back(sub);
        g_watch_sources.push_back(source);
    };

    wchar_t* user_programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr,
                                       &user_programs)) &&
        user_programs) {
        add_root(user_programs, true, nimblerun::CatalogSource::StartMenu);
    }
    if (user_programs) {
        CoTaskMemFree(user_programs);
    }
    wchar_t* common_programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_DEFAULT, nullptr,
                                       &common_programs)) &&
        common_programs) {
        add_root(common_programs, true, nimblerun::CatalogSource::StartMenu);
    }
    if (common_programs) {
        CoTaskMemFree(common_programs);
    }

    for (const nimblerun::CatalogRoot& root : g_settings.catalog_roots) {
        add_root(root.path, root.recursive, nimblerun::CatalogSource::UserFolder);
    }

    g_watcher->SetRoots(roots, recursive);
}

// NR-024: one shared rounded key-box draw for the footer hints and the
// per-row digit boxes (design-spec §4.9). The caller supplies the full DIP
// rect; the footer keeps its right-to-left advance and the row loop passes
// the box centered on the row. There is exactly one key-box paint path in the
// repo.
void DrawKeyBox(const wchar_t* label, const D2D1_RECT_F& box_rect) {
    const auto box = D2D1::RoundedRect(
        box_rect,
        nimblerun::layout::kFooterKeyRadiusDip,
        nimblerun::layout::kFooterKeyRadiusDip);
    g_render_target->FillRoundedRectangle(box, g_card_brush);
    g_render_target->DrawRoundedRectangle(box, g_dim_brush,
                                          nimblerun::layout::kFooterDividerWidthDip);
    g_render_target->DrawText(
        label, static_cast<UINT32>(wcslen(label)), g_small_format,
        D2D1::RectF(box_rect.left,
                    box_rect.top + nimblerun::layout::kFooterTextInsetDip,
                    box_rect.right, box_rect.bottom),
        g_text_brush);
}

void Render(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);

    // NR-015: resolve the palette for this frame; if it differs from the one
    // the live brushes were built with, drop the device resources so they are
    // rebuilt below (theme / high-contrast change). Cheap and event-driven.
    const nimblerun::palette::PanelColors colors = ResolveCurrentColors();
    if (!(colors == g_brush_colors)) {
        DiscardDeviceResources();
        // NR-023: the search EDIT's cached GDI background brush follows the
        // same palette; delete it so the new fill is picked up lazily below.
        if (g_search_bg_brush) {
            DeleteObject(g_search_bg_brush);
            g_search_bg_brush = nullptr;
        }
    }
    g_colors = colors;
    if (!g_search_bg_brush) {
        g_search_bg_brush = CreateSolidBrush(RgbToColorRef(g_colors.input_fill));
    }

    if (!CreateDeviceResources(window)) {
        EndPaint(window, &paint);
        return;
    }

    g_render_target->BeginDraw();
    g_render_target->Clear(D2D1::ColorF(colors.background));

    float dpi_x = 96.0f;
    float dpi_y = 96.0f;
    g_render_target->GetDpi(&dpi_x, &dpi_y);
    // NR-031: the icon need per layout state comes from the shared
    // LayoutForDpi() geometry (40 DIP grid cell / 30 DIP list tile in physical
    // pixels), never a scale inline in the render loop.
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(dpi_x);
    const int grid_icon_needed_px = static_cast<int>(std::lround(
        nimblerun::layout::kIconSizeDip * layout.scale));

    // NR-023: rounded search box behind the EDIT (design-spec §4.9). DIP
    // geometry; the EDIT child sits inset and covers the corners, so no text
    // is drawn here. The 1-DIP border width reuses the NR-015 scale rule.
    const auto search_box = D2D1::RoundedRect(
        D2D1::RectF(nimblerun::layout::kSearchLeftDip, nimblerun::layout::kSearchTopDip,
                    nimblerun::layout::kSearchRightDip, nimblerun::layout::kSearchBottomDip),
        nimblerun::layout::kSearchCornerRadiusDip,
        nimblerun::layout::kSearchCornerRadiusDip);
    g_render_target->FillRoundedRectangle(search_box, g_search_fill_brush);
    const float search_border_width = std::max(1.0f, dpi_x / nimblerun::layout::kDpi96);
    g_render_target->DrawRoundedRectangle(search_box, g_search_border_brush,
                                          search_border_width);

    if (g_model) {
        const auto& rows = g_model->Rows();
        if (g_model->Columns() > 1) {
            // NR-029: empty-query icon grid (design-spec §4.2/§4.9). Reuses the
            // model viewport state: visible cells are
            // FirstVisibleRow()..+ViewportRows()*Columns().
            const int first = g_model->FirstVisibleRow();
            const int columns = g_model->Columns();
            const int visible = g_model->ViewportRows() * columns;
            const int last = std::min(first + visible, static_cast<int>(rows.size()));
            for (int i = first; i < last; ++i) {
                const int slot = i - first;
                const int row_index = slot / columns;
                const int col_index = slot % columns;
                const float cell_left =
                    nimblerun::layout::kGridLeftDip +
                    col_index * nimblerun::layout::kCellWidthDip;
                const float cell_top =
                    nimblerun::layout::kListTopDip +
                    row_index * nimblerun::layout::kCellHeightDip;
                const auto cell = D2D1::RectF(
                    cell_left, cell_top,
                    cell_left + nimblerun::layout::kCellWidthDip,
                    cell_top + nimblerun::layout::kCellHeightDip);
                const bool selected =
                    g_model->HasSelection() &&
                    g_model->SelectionIndex() == static_cast<std::size_t>(i);
                const bool hovered = g_grid_hover_index == i;
                if (selected) {
                    g_render_target->FillRectangle(cell, g_selected_brush);
                } else if (hovered) {
                    // NR-029: hover is a card-level fill only; it never draws
                    // the selection border, so the two states stay distinct.
                    g_render_target->FillRectangle(cell, g_hover_brush);
                }
                if (selected) {
                    // NR-015: the selected cell also gets a border in a color
                    // distinct from the fill (design-spec §NFR-006).
                    const float border_width = std::max(1.0f, dpi_x / nimblerun::layout::kDpi96);
                    const float inset = border_width / 2.0f;
                    g_render_target->DrawRectangle(
                        D2D1::RectF(cell.left + inset, cell.top + inset,
                                    cell.right - inset, cell.bottom - inset),
                        g_selected_border_brush, border_width);
                }

                // NR-029: 40x40 icon horizontally centered in the cell's upper
                // half; fallback tile + first letter until the real icon loads
                // (NR-012), same no-reflow rule as the list rows.
                const float icon_left =
                    cell_left + (nimblerun::layout::kCellWidthDip - nimblerun::layout::kIconSizeDip) / 2.0f;
                const float icon_top = cell_top + 12.0f;
                const auto tile = D2D1::RectF(
                    icon_left, icon_top,
                    icon_left + nimblerun::layout::kIconSizeDip,
                    icon_top + nimblerun::layout::kIconSizeDip);
                const nimblerun::IconKey key = IconKeyFor(rows[i], grid_icon_needed_px);
                const std::wstring encoded = key.Encode();
                if (const nimblerun::IconBitmap* icon = g_icon_cache ? g_icon_cache->Peek(encoded) : nullptr) {
                    DrawDecodedIcon(*icon, tile, dpi_x, dpi_y);
                } else {
                    // NR-032: fallback-first (design-spec §FR-009). The real
                    // icon is requested from the worker right here; the paint
                    // never waits on Shell, and the key is deduplicated by the
                    // pending set until its result arrives.
                    g_render_target->FillRectangle(tile, g_dim_brush);
                    const std::wstring initial =
                        rows[i].display_name.empty()
                            ? std::wstring(L"?")
                            : std::wstring(1, rows[i].display_name.front());
                    g_render_target->DrawText(
                        initial.c_str(), static_cast<UINT32>(initial.size()), g_text_format,
                        tile, g_text_brush);
                    RequestVisibleIcon(rows[i], key, encoded);
                }

                // NR-029: single-line centered name in the lower half. The grid
                // name format is NO_WRAP + character ellipsis (see the
                // SetTrimming setup), so name length never changes cell
                // geometry (design-spec §4.2).
                const auto name_rect = D2D1::RectF(
                    cell_left + 4.0f, cell_top + 56.0f,
                    cell.right - 4.0f, cell.bottom - 8.0f);
                g_render_target->DrawText(
                    rows[i].display_name.c_str(),
                    static_cast<UINT32>(rows[i].display_name.size()),
                    g_grid_name_format, name_rect, g_text_brush);

                // NR-029: NR-024 digit box at the cell's top-right corner for
                // the first 10 cells; the shared key-box paint is reused.
                if (const wchar_t* key_label = nimblerun::ui::QuickSelectLabelForSlot(slot)) {
                    const float box_right = cell.right - 4.0f;
                    DrawKeyBox(
                        key_label,
                        D2D1::RectF(box_right - nimblerun::layout::kRowKeyBoxWidthDip,
                                    cell_top + 4.0f,
                                    box_right,
                                    cell_top + 4.0f + nimblerun::layout::kFooterKeyBoxHeightDip));
                }

                // NR-041: pinned marker -- a filled dot in the cell's top-left
                // corner. Drawn last so it sits above the selection border, and
                // placed on the left because the top-right corner is the NR-024
                // quick-select digit box. Shape, not color, carries the state
                // (design-spec §NFR-006); the border color is reused because the
                // palette already guarantees it contrasts with every fill and
                // follows the system colors under high contrast.
                if (g_pins && g_pins->IsPinned(rows[i].stable_id)) {
                    constexpr float kPinDotRadiusDip = 4.0f;
                    constexpr float kPinDotInsetDip = 8.0f;
                    g_render_target->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(cell.left + kPinDotInsetDip,
                                                    cell.top + kPinDotInsetDip),
                                      kPinDotRadiusDip, kPinDotRadiusDip),
                        g_selected_border_brush);
                }
            }

            // NR-029 empty state (design-spec §4.3): the grid is never blank.
            // Same strings as the list state, drawn in the first grid row.
            if (rows.empty()) {
                const wchar_t* hint = g_model->CatalogAvailable()
                    ? list_strings::kNoMatchingApps
                    : list_strings::kBuildingCatalog;
                g_render_target->DrawText(
                    hint,
                    static_cast<UINT32>(wcslen(hint)),
                    g_text_format,
                    D2D1::RectF(nimblerun::layout::kListLeftDip, nimblerun::layout::kListTopDip,
                                nimblerun::layout::kListRightDip,
                                nimblerun::layout::kListTopDip + nimblerun::layout::kCellHeightDip),
                    g_dim_brush);
            }
        } else {
            const int first = g_model->FirstVisibleRow();
            const int last =
                std::min(first + g_model->ViewportRows(), static_cast<int>(rows.size()));
            const std::wstring windows_app_label(list_strings::kWindowsApp);
            for (int i = first; i < last; ++i) {
                // NR-020: fixed single-column list geometry. D2D coordinates are
                // DIPs; the render target scales them to the monitor's DPI
                // (design-spec §4.9).
                const float row_top =
                    nimblerun::layout::kListTopDip +
                    static_cast<float>(i - first) * nimblerun::layout::kRowHeightDip;
                const auto row_rect = D2D1::RectF(
                    nimblerun::layout::kListLeftDip, row_top,
                    nimblerun::layout::kListRightDip,
                    row_top + nimblerun::layout::kRowHeightDip);
                const bool selected =
                    g_model->HasSelection() &&
                    g_model->SelectionIndex() == static_cast<std::size_t>(i);
                g_render_target->FillRectangle(
                    row_rect,
                    selected ? g_selected_brush : g_card_brush);
    
                // NR-015: the selected row also gets a border in a color distinct
                // from the fill, so selection is never conveyed by color alone
                // (design-spec §NFR-006).
                if (selected) {
                    const float border_width = std::max(1.0f, dpi_x / nimblerun::layout::kDpi96);
                    const float inset = border_width / 2.0f;
                    g_render_target->DrawRectangle(
                        D2D1::RectF(row_rect.left + inset, row_rect.top + inset,
                                    row_rect.right - inset, row_rect.bottom - inset),
                        g_selected_border_brush,
                        border_width);
                }

                // NR-041: pinned marker -- a stripe on the row's leading edge,
                // in the gap kTileInsetDip already leaves before the icon. Same
                // rule as the grid dot: shape, not color, and drawn after the
                // selection border so it stays visible on the selected row.
                if (g_pins && g_pins->IsPinned(rows[i].stable_id)) {
                    constexpr float kPinStripeWidthDip = 3.0f;
                    g_render_target->FillRectangle(
                        D2D1::RectF(row_rect.left, row_rect.top,
                                    row_rect.left + kPinStripeWidthDip,
                                    row_rect.bottom),
                        g_selected_border_brush);
                }
    
                // NR-012: fixed tile inside the row, vertically centered. The decoded
                // icon (when cached) is drawn into the same rect the placeholder
                // occupies, so geometry is constant and a late-arriving icon never
                // reflows.
                const float tile_left =
                    nimblerun::layout::kListLeftDip + nimblerun::layout::kTileInsetDip;
                const float tile_top =
                    row_top + (nimblerun::layout::kRowHeightDip - nimblerun::layout::kTileSizeDip) / 2.0f;
                const auto tile = D2D1::RectF(
                    tile_left, tile_top,
                    tile_left + nimblerun::layout::kTileSizeDip,
                    tile_top + nimblerun::layout::kTileSizeDip);
    
                const nimblerun::IconKey key = IconKeyFor(rows[i], layout.tile_size);
                const std::wstring encoded = key.Encode();
                if (const nimblerun::IconBitmap* icon = g_icon_cache ? g_icon_cache->Peek(encoded) : nullptr) {
                    DrawDecodedIcon(*icon, tile, dpi_x, dpi_y);
                } else {
                    // Fallback tile (design-spec §FR-009): drawn on the first frame
                    // and kept until the icon arrives or the request fails.
                    g_render_target->FillRectangle(tile, g_dim_brush);
                    const std::wstring initial =
                        rows[i].display_name.empty()
                            ? std::wstring(L"?")
                            : std::wstring(1, rows[i].display_name.front());
                    g_render_target->DrawText(
                        initial.c_str(), static_cast<UINT32>(initial.size()), g_text_format,
                        tile,
                        g_text_brush);
                    RequestVisibleIcon(rows[i], key, encoded);
                }
    
                // NR-020: name in the upper half of the row, source path in the
                // lower half (design-spec §4.2). Packaged apps show a fixed label
                // instead of a Shell parsing name. Single-line with trailing
                // ellipsis (see the SetTrimming setup); length never changes row
                // height.
                const float text_left =
                    tile_left + nimblerun::layout::kTileSizeDip + 8.0f;
                // NR-024: the name and second line unconditionally reserve the key
                // hint column, so text width never jumps whether or not the row
                // shows a digit (design-spec §4.9). Single-line + ellipsis below.
                const float text_right =
                    nimblerun::layout::kListRightDip - nimblerun::layout::kRowHintReserveDip;
                const float row_mid = row_top + nimblerun::layout::kRowHeightDip / 2.0f;
                g_render_target->DrawText(
                    rows[i].display_name.c_str(),
                    static_cast<UINT32>(rows[i].display_name.size()),
                    g_text_format,
                    D2D1::RectF(text_left, row_top, text_right, row_mid),
                    g_text_brush);
                const std::wstring& subtitle =
                    rows[i].source == nimblerun::AppSource::AppsFolder
                        ? windows_app_label
                        : rows[i].source_path;
                g_render_target->DrawText(
                    subtitle.c_str(),
                    static_cast<UINT32>(subtitle.size()),
                    g_small_format,
                    D2D1::RectF(text_left, row_mid, text_right,
                                row_top + nimblerun::layout::kRowHeightDip),
                    g_dim_brush);
    
                // NR-024: per-row quick-select digit (design-spec §4.7/§4.9). The
                // slot is the row's position in the viewport; rows beyond the
                // 10-digit sequence get no box but keep the reserved text width.
                const int slot = i - first;
                if (const wchar_t* key_label = nimblerun::ui::QuickSelectLabelForSlot(slot)) {
                    const float box_left =
                        nimblerun::layout::kListRightDip -
                        nimblerun::layout::kRowKeyRightInsetDip -
                        nimblerun::layout::kRowKeyBoxWidthDip;
                    DrawKeyBox(
                        key_label,
                        D2D1::RectF(
                            box_left,
                            row_mid - nimblerun::layout::kFooterKeyBoxHeightDip / 2.0f,
                            box_left + nimblerun::layout::kRowKeyBoxWidthDip,
                            row_mid + nimblerun::layout::kFooterKeyBoxHeightDip / 2.0f));
                }
            }
    
            // NR-020 empty state (design-spec §4.3): the panel is never blank. A
            // null/empty catalog snapshot means background enumeration is still
            // running.
            if (rows.empty()) {
                const wchar_t* hint = g_model->CatalogAvailable()
                    ? list_strings::kNoMatchingApps
                    : list_strings::kBuildingCatalog;
                g_render_target->DrawText(
                    hint,
                    static_cast<UINT32>(wcslen(hint)),
                    g_text_format,
                    D2D1::RectF(nimblerun::layout::kListLeftDip, nimblerun::layout::kListTopDip,
                                nimblerun::layout::kListRightDip,
                                nimblerun::layout::kListTopDip + nimblerun::layout::kRowHeightDip),
                    g_dim_brush);
            }
        }
    }

    // NR-021 footer key-hint band (design-spec §4.9). A 1 DIP divider then a
    // right-aligned "Launch" group + "Scroll"/PgUp/PgDn group. Only key hints
    // live here; no status, version or update text. NR-024 adds the Launch
    // group whose box text depends on the current viewport row count.
    const float footer_top = nimblerun::layout::kFooterTopDip;
    g_render_target->DrawLine(
        D2D1::Point2F(0.0f, footer_top),
        D2D1::Point2F(nimblerun::layout::kPanelWidthDip, footer_top),
        g_dim_brush,
        nimblerun::layout::kFooterDividerWidthDip);

    const float footer_mid =
        (footer_top + nimblerun::layout::kPanelHeightDip) / 2.0f;
    const float box_top =
        footer_mid - nimblerun::layout::kFooterKeyBoxHeightDip / 2.0f;
    const float box_bottom = box_top + nimblerun::layout::kFooterKeyBoxHeightDip;
    // NR-024: the footer keeps its right-to-left advance; the paint itself is
    // the file-scope DrawKeyBox shared with the per-row digit boxes.
    auto draw_key_box = [&](const wchar_t* label, float right, float width) -> float {
        const float left = right - width;
        DrawKeyBox(label, D2D1::RectF(left, box_top, right, box_bottom));
        return left;
    };

    float right = nimblerun::layout::kListRightDip;
    float hints_left = right;
    right = draw_key_box(footer_strings::kPageDown, right,
                         nimblerun::layout::kFooterKeyBoxWidthDip);
    hints_left = std::min(hints_left, right);
    right -= nimblerun::layout::kFooterKeyGapDip;
    right = draw_key_box(footer_strings::kPageUp, right,
                         nimblerun::layout::kFooterKeyBoxWidthDip);
    hints_left = std::min(hints_left, right);
    right -= nimblerun::layout::kFooterHintGapDip;

    // The "Scroll" and "Launch" labels are right-aligned to the key box that
    // follows them: measure the text once per frame and draw it ending at
    // `right` so the group hugs the right edge (the shared formats stay
    // left-aligned for the list rows). Returns the measured width so the path
    // bar can stop kFooterHintGapDip before the leftmost hint (NR-029).
    auto draw_right_label = [&](const wchar_t* label, float label_right) -> float {
        IDWriteTextLayout* label_layout = nullptr;
        if (SUCCEEDED(g_write_factory->CreateTextLayout(
                label, static_cast<UINT32>(wcslen(label)), g_small_format,
                1000.0f, 1000.0f, &label_layout))) {
            DWRITE_TEXT_METRICS label_metrics{};
            if (SUCCEEDED(label_layout->GetMetrics(&label_metrics))) {
                g_render_target->DrawText(
                    label, static_cast<UINT32>(wcslen(label)), g_small_format,
                    D2D1::RectF(label_right - label_metrics.width,
                                box_top + nimblerun::layout::kFooterTextInsetDip,
                                label_right, box_bottom),
                    g_dim_brush);
                label_layout->Release();
                return label_metrics.width;
            }
            label_layout->Release();
        }
        return 0.0f;
    };

    hints_left = std::min(hints_left,
                          right - draw_right_label(footer_strings::kScroll, right));

    // NR-024: "Launch" group to the left of "Scroll", separated by the hint
    // gap. The wide box content is "Alt+1~" followed by the last digit bound
    // to the current viewport (8 visible rows -> Alt+1~8, >=10 -> Alt+1~0);
    // built per frame since the viewport can change.
    right -= nimblerun::layout::kFooterHintGapDip;
    const int last_slot =
        std::min(g_model ? g_model->ViewportRows() : 0,
                 nimblerun::ui::kQuickSelectSlotCount) - 1;
    std::wstring alt_label(footer_strings::kAltOnePrefix);
    if (const wchar_t* last_label = nimblerun::ui::QuickSelectLabelForSlot(last_slot)) {
        alt_label += last_label;
    }
    right = draw_key_box(alt_label.c_str(), right,
                         nimblerun::layout::kFooterWideKeyBoxWidthDip);
    hints_left = std::min(hints_left, right);
    right -= nimblerun::layout::kFooterKeyGapDip;
    hints_left = std::min(hints_left,
                          right - draw_right_label(footer_strings::kLaunch, right));

    // NR-029: path bar on the left half of the footer band (grid state only).
    // Hover wins over the keyboard selection; packaged apps show the source
    // label instead of a Shell parsing name. The bar ends kFooterHintGapDip
    // before the leftmost hint, so any length truncates without covering the
    // key hints (design-spec §4.2/§4.9).
    if (g_model && g_model->Columns() > 1) {
        const nimblerun::AppEntry* path_entry = nullptr;
        if (g_grid_hover_index >= 0 &&
            g_grid_hover_index < static_cast<int>(g_model->Rows().size())) {
            path_entry = &g_model->Rows()[static_cast<std::size_t>(g_grid_hover_index)];
        } else if (g_model->HasSelection()) {
            path_entry = &g_model->Rows()[g_model->SelectionIndex()];
        }
        if (path_entry) {
            const wchar_t* path =
                path_entry->source == nimblerun::AppSource::AppsFolder
                    ? list_strings::kWindowsApp
                    : path_entry->source_path.c_str();
            g_render_target->DrawText(
                path, static_cast<UINT32>(wcslen(path)), g_small_format,
                D2D1::RectF(nimblerun::layout::kListLeftDip,
                            box_top + nimblerun::layout::kFooterTextInsetDip,
                            hints_left - nimblerun::layout::kFooterHintGapDip,
                            box_bottom),
                g_dim_brush);
        }
    }

    const HRESULT result = g_render_target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
    EndPaint(window, &paint);
}

void ShowPanel(HWND window) {
    POINT cursor{};
    GetCursorPos(&cursor);

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &monitor_info);

    // NR-015: size the panel in DIPs scaled to the cursor monitor's DPI, then
    // clamp it to the work area. Width/height stay 640x432 DIPs at any DPI, so
    // the same layout math gives predictable bounds at 100/150/200%.
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
    const RECT work_area = monitor_info.rcWork;
    const nimblerun::layout::WindowSize size = nimblerun::layout::ClampWindowSize(
        static_cast<float>(dpi_x),
        work_area.right - work_area.left,
        work_area.bottom - work_area.top);
    const int left = work_area.left + ((work_area.right - work_area.left) - size.width) / 2;
    const int top = work_area.top + ((work_area.bottom - work_area.top) - size.height) / 2;

    SetWindowPos(window, HWND_TOPMOST, left, top, size.width, size.height, SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    // NR-015: the theme applies on the next panel show; reload so a settings
    // change is picked up without a restart (same "apply on next launch" rule
    // the existing code used for hide-after-launch).
    if (g_settings_store) {
        nimblerun::Settings current;
        g_settings_store->Load(current);
        g_theme = current.theme;
        g_hide_after_launch = current.hide_after_launch;
    }
    // NR-018: reload pins on every open so restarts and external edits to
    // favorites.txt are reflected.
    RefreshPins();
    // NR-031: derive the LRU cap from the live pin count + recent_count setting
    // + one grid page (design-spec §FR-009), so a search result never evicts
    // the prewarmed pins and forces a refetch on the next panel show.
    if (g_icon_cache) {
        g_icon_cache->SetMaxItems(nimblerun::IconCacheCapacityFor(
            g_pins ? g_pins->OrderedPins().size() : 0,
            g_settings.recent_count));
    }
    if (g_model) {
        g_model->Reset();
    }
    // NR-029: the grid hover index is a window-layer visual state; reset it for
    // this show and never leave a stale fill pointing at a previous session.
    // Leave-tracking is re-armed from the next mouse move (a hidden window may
    // swallow the WM_MOUSELEAVE that would otherwise clear the flag).
    g_grid_hover_index = -1;
    g_tracking_mouse_leave = false;
    // NR-012/NR-032: allow a retry of transient icon failures on this open.
    // The pending set is deliberately NOT cleared: those requests are still in
    // flight and their results keep landing in the LRU (that is the prewarm).
    g_requested_icon_keys.clear();
    // NR-011: AppsFolder is on-demand — when the panel is shown and the last
    // successful enumeration is older than 10 minutes, rebuild it in the
    // background; no polling and never a blocking scan on this path. NR-028:
    // "Include Windows apps" off schedules nothing here.
    if (g_refresh && g_settings.include_windows_apps &&
        g_refresh->ShouldRefreshAppsFolder(MonotonicMs())) {
        StartRebuild(window, {nimblerun::CatalogSource::AppsFolder});
    }
    if (g_search_edit) {
        SetWindowTextW(g_search_edit, L"");
        SetFocus(g_search_edit);
    }
    // NR-020/NR-029: the visible row count derives from the actual client rect
    // at this monitor's DPI and the active layout state (Columns() after the
    // Reset above, i.e. the empty-query grid); recompute on every show so a
    // monitor move is picked up.
    UpdateViewportRows(window);
    InvalidateRect(window, nullptr, FALSE);
}

void AddTrayIcon(HWND window) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = window;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr),
                          MAKEINTRESOURCEW(IDI_NIMBLERUN));
    wcsncpy(nid.szTip, L"NimbleRun", sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND window) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = window;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Non-blocking one-time reminder when the default hotkey cannot be registered.
// The tray stays fully functional; the Settings entry is the persistent fix path.
void ShowHotkeyConflictNotice(HWND window) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = window;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    wcsncpy(nid.szInfoTitle, L"NimbleRun", sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) - 1);
    wcsncpy(nid.szInfo,
        L"Alt+Space is already in use. Open Settings to choose another global hotkey.",
        sizeof(nid.szInfo) / sizeof(nid.szInfo[0]) - 1);
    nid.dwInfoFlags = NIIF_NONE;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void DispatchTrayCommand(HWND window, UINT command) {
    switch (command) {
    case kCmdOpen:
        PostMessageW(window, g_show_panel_message, 0, 0);
        break;
    case kCmdRefresh:
        PostMessageW(window, kRefreshMessage, 0, 0);
        break;
    case kCmdSettings:
        PostMessageW(window, kSettingsMessage, 0, 0);
        break;
    case kCmdAbout:
        PostMessageW(window, kAboutMessage, 0, 0);
        break;
    case kCmdExit:
        PostMessageW(window, kExitMessage, 0, 0);
        break;
    default:
        break;
    }
}

void ShowTrayMenu(HWND window) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kCmdOpen, L"Open NimbleRun");
    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh Apps");
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"Settings");
    AppendMenuW(menu, MF_STRING, kCmdAbout, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, 0, window, nullptr));
    PostMessageW(window, WM_NULL, 0, 0);

    DestroyMenu(menu);
    DispatchTrayCommand(window, command);
}

// NR-015: the search EDIT is a real Win32 child, so its rect is in physical
// pixels. Re-derive it from the window's current DPI whenever the panel is
// shown or moved to another monitor.
void RepositionSearchEdit(HWND window) {
    if (!g_search_edit) {
        return;
    }
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    SetWindowPos(
        g_search_edit, nullptr,
        layout.search_edit_left, layout.search_edit_top,
        layout.search_edit_right - layout.search_edit_left,
        layout.search_edit_bottom - layout.search_edit_top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

// NR-023: (re)builds the search EDIT font from the system message font at the
// window's current DPI, overriding only the height (24 DIP, negative lfHeight).
// The previous HFONT is deleted after the control switches to the new one, so
// exactly one font object lives at a time. Falls back to SystemParametersInfoW
// when the DPI-aware read is unavailable.
void UpdateSearchFont(HWND window) {
    if (!g_search_edit) {
        return;
    }
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                    GetDpiForWindow(window))) {
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            return;
        }
    }
    LOGFONTW font = ncm.lfMessageFont;
    font.lfHeight = nimblerun::layout::LayoutForDpi(GetDpiForWindow(window)).search_font_height;
    const HFONT new_font = CreateFontIndirectW(&font);
    if (!new_font) {
        return;
    }
    SendMessageW(g_search_edit, WM_SETFONT, reinterpret_cast<WPARAM>(new_font), TRUE);
    if (g_search_font) {
        DeleteObject(g_search_font);
    }
    g_search_font = new_font;
}

// Subclassed EDIT control: forwards Up/Down/Enter/Esc to the panel model
// (NR-020 list navigation); Left/Right/Home/End stay with the edit control for
// caret movement (design-spec §4.7). All other keys keep default editing.
LRESULT CALLBACK SearchEditProc(HWND edit, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_SETFOCUS: {
        // NR-023: force a solid caret after the default focus path. The native
        // caret can blend into the theme-colored input fill on dark themes; a
        // solid caret is drawn inverted against the background, so it stays
        // visible in both light and dark mode. No timers: the caret only blinks
        // via the system's own mechanism.
        const LRESULT result =
            CallWindowProcW(g_search_original_proc, edit, message, w_param, l_param);
        CreateCaret(edit, reinterpret_cast<HBITMAP>(1), 0, 0);
        ShowCaret(edit);
        return result;
    }
    case WM_KILLFOCUS:
        // NR-023: mirror of WM_SETFOCUS; destroy the caret we created.
        CallWindowProcW(g_search_original_proc, edit, message, w_param, l_param);
        DestroyCaret();
        return 0;
    case WM_LBUTTONDOWN: {
        // NR-039: the search box doubles as the panel's drag handle. DragDetect
        // blocks until the user either moves past the system drag threshold
        // (SM_CXDRAG/SM_CYDRAG -> TRUE) or releases without dragging (FALSE), and
        // it consumes the mouse messages either way -- so the plain-click path has
        // to place the caret itself. Cost accepted in NR-039: drag-selecting text
        // with the mouse is gone; double-click, Shift+arrows and Ctrl+A are not.
        const POINT client{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        POINT screen = client;
        ClientToScreen(edit, &screen);  // DragDetect takes screen coordinates
        if (DragDetect(edit, screen)) {
            ReleaseCapture();
            SendMessageW(GetParent(edit), WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        const LRESULT hit = SendMessageW(edit, EM_CHARFROMPOS, 0,
                                         MAKELPARAM(client.x, client.y));
        const int index = static_cast<int>(LOWORD(hit));
        SendMessageW(edit, EM_SETSEL, index, index);
        SetFocus(edit);
        return 0;
    }
    case WM_SYSKEYDOWN:
        // NR-024: Alt+digit directly launches the corresponding visible row,
        // exactly like Enter on that row (design-spec §4.7; reuses the same
        // ActivateRow usage/hide-after-launch/NR-022 failure path). Bit 29 of
        // lParam is the ALT state; Ctrl+Alt and unbound combos like Alt+Space
        // keep their default processing.
        if ((l_param & (1 << 29)) != 0 && GetKeyState(VK_CONTROL) >= 0 &&
            g_model != nullptr) {
            const int slot = nimblerun::ui::QuickSelectSlotForKey(
                static_cast<int>(w_param));
            if (slot >= 0) {
                const int row = g_model->RowForVisibleSlot(slot);
                if (row >= 0) {
                    g_model->SelectRow(static_cast<std::size_t>(row));
                    ActivateRow(static_cast<std::size_t>(row), GetParent(edit));
                }
                // A bound digit never beeps, even when no row maps to it.
                return 0;
            }
        }
        break;  // unbound (e.g. Alt+Space) -> default processing
    case WM_SYSCHAR:
        // NR-024: swallow the system beep for the 10 bound digits; the
        // WM_SYSKEYDOWN above already handled the launch. Everything else
        // falls through to the default.
        if ((l_param & (1 << 29)) != 0 &&
            nimblerun::ui::QuickSelectSlotForKey(static_cast<int>(w_param)) >= 0) {
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (g_model) {
            switch (w_param) {
            case VK_UP:
                // NR-029: in the grid one step is a whole row (Columns() items);
                // in the list Columns() is 1, so this stays the original move.
                g_model->MoveSelection(-g_model->Columns());
                InvalidateRect(GetParent(edit), nullptr, FALSE);
                return 0;
            case VK_DOWN:
                g_model->MoveSelection(g_model->Columns());
                InvalidateRect(GetParent(edit), nullptr, FALSE);
                return 0;
            case VK_LEFT:
                // NR-029: only the grid consumes Left/Right (the search box is
                // empty there, so caret movement is moot); the list keeps
                // NR-020 behavior and hands them to the EDIT for text editing.
                if (g_model->Columns() > 1) {
                    g_model->MoveSelection(-1);
                    InvalidateRect(GetParent(edit), nullptr, FALSE);
                    return 0;
                }
                break;
            case VK_RIGHT:
                if (g_model->Columns() > 1) {
                    g_model->MoveSelection(1);
                    InvalidateRect(GetParent(edit), nullptr, FALSE);
                    return 0;
                }
                break;
            case VK_PRIOR:
                g_model->ScrollBy(-g_model->ViewportRows());
                InvalidateRect(GetParent(edit), nullptr, FALSE);
                return 0;
            case VK_NEXT:
                g_model->ScrollBy(g_model->ViewportRows());
                InvalidateRect(GetParent(edit), nullptr, FALSE);
                return 0;
            case VK_RETURN: {
                const nimblerun::PanelAction action = g_model->Activate();
                if (action.launch) {
                    ActivateRow(g_model->SelectionIndex(), GetParent(edit));
                }
                return 0;
            }
            case VK_ESCAPE:
                if (g_model->Esc()) {
                    HidePanel(GetParent(edit));
                }
                return 0;
            case 'R':
                if (GetKeyState(VK_CONTROL) < 0) {
                    // Ctrl+R forces a full catalog rebuild (design-spec §4.7).
                    PostMessageW(GetParent(edit), kRefreshMessage, 0, 0);
                    return 0;
                }
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
    return CallWindowProcW(g_search_original_proc, edit, message, w_param, l_param);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == g_show_panel_message) {
        ShowPanel(window);
        return 0;
    }
    switch (message) {
    case kTrayCallbackMessage:
        if (l_param == WM_LBUTTONUP) {
            PostMessageW(window, g_show_panel_message, 0, 0);
        } else if (l_param == WM_RBUTTONUP) {
            ShowTrayMenu(window);
        }
        return 0;
    case kRefreshMessage: {
        // NR-011: Ctrl+R / tray "Refresh Apps" forces a full rebuild of every
        // source. A successful launch never triggers this (see ActivateRow).
        if (g_refresh) {
            const std::vector<nimblerun::CatalogSource> all = {
                nimblerun::CatalogSource::StartMenu,
                nimblerun::CatalogSource::AppsFolder,
                nimblerun::CatalogSource::UserFolder,
            };
            StartRebuild(window, all);
        }
        return 0;
    }
    case kWatchChangedMessage: {
        // NR-011: a watched directory changed. Coalesce behind the 500 ms
        // debounce; a full-rescan marker is due immediately.
        const int index = static_cast<int>(w_param);
        const bool full_rescan = l_param != 0;
        const nimblerun::CatalogSource source = WatchIndexToSource(index);
        if (full_rescan) {
            g_refresh->MarkSourceFullRescan(source);
            const std::vector<nimblerun::CatalogSource> due = g_refresh->DueSources(MonotonicMs());
            if (!due.empty()) {
                StartRebuild(window, due);
            }
        } else {
            g_refresh->NotifySourceEvent(source, MonotonicMs());
            ScheduleDebouncedRebuild(window);
        }
        return 0;
    }
    case kRebuildDoneMessage: {
        // NR-011: one background enumeration finished. The coordinator swaps the
        // snapshot only when the whole generation has reported, so applying the
        // result here can never show a partial catalog.
        std::unique_ptr<RebuildResult> result(
            reinterpret_cast<RebuildResult*>(l_param));
        bool generation_complete = false;
        if (result->failed) {
            generation_complete =
                g_refresh->ApplySourceFailure(result->generation, result->source);
        } else {
            if (g_refresh->ApplySourceResult(result->generation, result->source,
                                             std::move(result->entries))) {
                generation_complete = true;
                if (result->source == nimblerun::CatalogSource::AppsFolder) {
                    g_refresh->RecordAppsFolderSuccess(MonotonicMs());
                }
            }
        }
        if (generation_complete) {
            // NR-022: the refresh the launch-failure dialog scheduled has run
            // to completion, so a future failure can schedule a fresh refresh.
            g_launch_failure_refresh.OnRefreshComplete();
        }
        RefreshPanelSnapshot();
        nimblerun::SaveCatalogCache(nimblerun::DefaultSettingsDir(),
                                    g_refresh->Snapshot());
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (w_param == kRebuildTimerId) {
            KillTimer(window, kRebuildTimerId);
            if (g_refresh) {
                const std::vector<nimblerun::CatalogSource> due =
                    g_refresh->DueSources(MonotonicMs());
                if (!due.empty()) {
                    StartRebuild(window, due);
                }
            }
        }
        return 0;
    case kSettingsMessage: {
        // NR-013: the tray "Settings" entry opens the modal settings dialog.
        // Apply() persists the accepted settings and swaps the global hotkey;
        // on failure it rolls back so the previous values survive. On success
        // NR-011 restarts the watchers and rebuilds (roots/extensions changed).
        if (g_settings_store && g_usage) {
            const bool applied = nimblerun::ShowSettingsDialog(window, *g_settings_store,
                                                               *g_usage, g_hotkey);
            // Reload so the live panel picks up hide-after-launch without
            // waiting for a restart (recent_count/theme apply on next launch).
            nimblerun::Settings reloaded;
            g_settings_store->Load(reloaded);
            g_settings = reloaded;
            g_hide_after_launch = reloaded.hide_after_launch;
            if (applied) {
                StartWatchers();
                RefreshPanelSnapshot();
                // NR-031: recent_count (and the pin list) changed the derived
                // LRU cap; re-derive it after settings are applied.
                if (g_icon_cache) {
                    g_icon_cache->SetMaxItems(nimblerun::IconCacheCapacityFor(
                        g_pins ? g_pins->OrderedPins().size() : 0,
                        g_settings.recent_count));
                }
                const std::vector<nimblerun::CatalogSource> all = {
                    nimblerun::CatalogSource::StartMenu,
                    nimblerun::CatalogSource::AppsFolder,
                    nimblerun::CatalogSource::UserFolder,
                };
                StartRebuild(window, all);
            }
        }
        return 0;
    }
    case kAboutMessage:
        // ponytail: about dialog is not specced; this item only provides the dispatch target.
        return 0;
    case kExitMessage:
        DestroyWindow(window);
        return 0;
    case kIconReadyMessage: {
        // NR-032: one decoded icon finished on the worker thread. The heap
        // IconResult is owned by this window and deleted here; a failed load
        // still reports (empty bitmap) so the pending set is cleared and the
        // key is never re-requested this panel session.
        std::unique_ptr<nimblerun::IconResult> result(
            reinterpret_cast<nimblerun::IconResult*>(l_param));
        g_pending_icon_keys.erase(result->encoded_key);
        if (!result->bitmap.Empty() && g_icon_cache) {
            // Late results still land in the LRU even when the panel is hidden
            // or the query changed (that is the prewarm effect); only a visible
            // window needs a repaint.
            g_icon_cache->Insert(result->encoded_key, std::move(result->bitmap));
            if (IsWindowVisible(window)) {
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (result->bitmap.Empty()) {
            // NR-012: remember the failure so the fallback stays and the Shell
            // is not re-queried every frame, until the next ShowPanel.
            g_requested_icon_keys.insert(result->encoded_key);
        }
        return 0;
    }
    case WM_HOTKEY:
        if (w_param == static_cast<WPARAM>(g_hotkey.ActiveId())) {
            if (IsWindowVisible(window)) {
                HidePanel(window);
            } else {
                ShowPanel(window);
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w_param) == kSearchId && HIWORD(w_param) == EN_UPDATE && g_model) {
            wchar_t buffer[1024];
            const int length = GetWindowTextW(g_search_edit, buffer, 1024);
            g_model->SetQuery(std::wstring(buffer, length));
            // NR-029: the grid/list row counts differ (96 vs 48 DIP rows), so
            // recompute the viewport on the empty<->non-empty layout switch.
            // Hover is a grid-only visual state and is cleared here too.
            UpdateViewportRows(window);
            if (g_grid_hover_index != -1) {
                g_grid_hover_index = -1;
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        // NR-023: the search EDIT takes its text/background colors from the
        // theme palette instead of the classic white/default. lParam is the
        // child; only handle our own search box and return the cached brush
        // (created once, rebuilt on palette change, released on destroy).
        if (reinterpret_cast<HWND>(l_param) == g_search_edit && g_search_bg_brush) {
            const HDC hdc = reinterpret_cast<HDC>(w_param);
            SetTextColor(hdc, RgbToColorRef(g_colors.text));
            SetBkColor(hdc, RgbToColorRef(g_colors.input_fill));
            return reinterpret_cast<LRESULT>(g_search_bg_brush);
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_MOUSEWHEEL: {
        // NR-021: the wheel scrolls the list by the OS "lines per wheel notch"
        // setting (design-spec §4.8). WHEEL_PAGESCROLL means one viewport;
        // a failed SPI read falls back to 3. The wheel delta sign picks the
        // direction: up (positive) scrolls toward the start of the list.
        UINT lines = 3;
        if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0) &&
            lines == WHEEL_PAGESCROLL) {
            lines = g_model ? static_cast<UINT>(g_model->ViewportRows()) : 0;
        }
        const int steps =
            static_cast<int>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA;
        if (g_model && lines > 0 && steps != 0) {
            g_model->ScrollBy(-steps * static_cast<int>(lines));
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        // NR-029: grid hover is a window-layer visual state. The hit index is
        // only recomputed and invalidated when the hit cell actually changes
        // (no per-pixel paint); TME_LEAVE clears it when the pointer exits the
        // panel. No timers.
        if (g_model && g_model->Columns() > 1) {
            const int cell =
                CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
            if (cell != g_grid_hover_index) {
                g_grid_hover_index = cell;
                InvalidateRect(window, nullptr, FALSE);
            }
            if (!g_tracking_mouse_leave) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);
                g_tracking_mouse_leave = true;
            }
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_MOUSELEAVE:
        g_tracking_mouse_leave = false;
        if (g_grid_hover_index != -1) {
            g_grid_hover_index = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        // NR-020/NR-029: a single click selects and launches the row (list) or
        // cell (grid) under the cursor (design-spec §4.8).
        const int cell = CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        if (cell < 0) {
            // NR-039: nothing under the cursor -> the panel itself is the drag
            // handle, so it can be moved off whatever it happens to be covering.
            // WM_NCLBUTTONDOWN/HTCAPTION hands the window to the shell's own move
            // loop; DefWindowProc takes the live cursor position from the system,
            // so lParam is unused here (client coords would be wrong anyway --
            // WM_NCLBUTTONDOWN's lParam is in screen coordinates).
            ReleaseCapture();
            SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        g_model->SelectRow(static_cast<std::size_t>(cell));
        ActivateRow(static_cast<std::size_t>(cell), window);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        // NR-018: right-click offers Pin/Unpin (per the item's current pinned
        // state) and "Open file location" for valid paths (design-spec §4.8).
        if (!g_model || !g_pins) {
            return 0;
        }
        const int cell = CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        if (cell < 0) {
            return 0;
        }
        const nimblerun::AppEntry entry = g_model->Rows()[static_cast<std::size_t>(cell)];
        const bool pinned = g_pins->IsPinned(entry.stable_id);

        const HMENU menu = CreatePopupMenu();
        if (!menu) {
            return 0;
        }
        AppendMenuW(menu, MF_STRING, pinned ? kCmdUnpin : kCmdPin,
                    pinned ? context_menu_strings::kUnpin : context_menu_strings::kPin);
        // NR-040: only offered for rows actually showing in the recent region;
        // on a pinned row the command would silently change nothing.
        const int recent_start = g_model->RecentStartIndex();
        const bool in_recent = recent_start >= 0 && cell >= recent_start;
        if (in_recent) {
            AppendMenuW(menu, MF_STRING, kCmdForgetRecent,
                        context_menu_strings::kRemoveFromRecent);
        }
        if (nimblerun::IsPathIdentity(entry.launch_identity)) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kCmdOpenLocation,
                        context_menu_strings::kOpenFileLocation);
            AppendMenuW(menu, MF_STRING, kCmdProperties,
                        context_menu_strings::kProperties);
        }

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(window);
        g_context_menu_active = true;
        const UINT command = static_cast<UINT>(TrackPopupMenu(
            menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, 0, window, nullptr));
        g_context_menu_active = false;
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(menu);

        if (command == kCmdPin || command == kCmdUnpin) {
            if (pinned) {
                g_pins->Unpin(entry.stable_id);
            } else {
                g_pins->Pin(entry.stable_id,
                            static_cast<std::int64_t>(std::time(nullptr)));
            }
            if (g_pins->Save()) {
                // Refresh just the pin region: SetPins rebuilds the empty-query
                // rows so the entry moves into/out of the pinned region.
                g_model->SetPins(g_pins->OrderedPins());
                // NR-031: the pin count drives the derived LRU cap; re-derive it.
                if (g_icon_cache) {
                    g_icon_cache->SetMaxItems(nimblerun::IconCacheCapacityFor(
                        g_pins->OrderedPins().size(), g_settings.recent_count));
                }
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (command == kCmdOpenLocation) {
            OpenFileLocation(window, entry);
        } else if (command == kCmdProperties) {
            ShowItemProperties(window, entry);
        } else if (command == kCmdForgetRecent) {
            // NR-040: drop one usage record, persist, then rebuild the recent
            // rows through the single existing path. Save() failing leaves the
            // previous file untouched, so the view is not refreshed either.
            if (g_usage && g_usage->Forget(entry.stable_id) && g_usage->Save()) {
                RefreshPanelSnapshot();
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_KILLFOCUS:
        // Clicking another window moves focus away from the panel; hide it. A
        // right-click context menu or the launch-failure dialog running its
        // modal loop is exempt so the panel stays up while a choice is made.
        if (!g_context_menu_active && !g_dialog_active && g_search_edit &&
            GetFocus() != g_search_edit) {
            HidePanel(window);
        }
        return 0;
    case WM_PAINT:
        Render(window);
        return 0;
    case WM_SIZE:
        if (g_render_target) {
            g_render_target->Resize(D2D1::SizeU(LOWORD(l_param), HIWORD(l_param)));
        }
        UpdateViewportRows(window);
        RepositionSearchEdit(window);
        return 0;
    case WM_DPICHANGED:
        // The suggested rect preserves the panel's DIP size at the new monitor
        // DPI (design-spec §4.9); apply it and rebuild geometry.
        if (const RECT* suggested = reinterpret_cast<const RECT*>(l_param)) {
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        UpdateViewportRows(window);
        RepositionSearchEdit(window);
        UpdateSearchFont(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon(window);
        g_hotkey.Shutdown();
        // NR-032/NR-036: stop the icon worker before the D2D resources and the
        // store are released (that happens below the message loop), so no
        // kIconReadyMessage can arrive after teardown. Stop() also performs the
        // final best-effort cache flush on the worker. Drain results already
        // queued so no heap IconResult leaks.
        if (g_icon_worker) {
            g_icon_worker->Stop();
            MSG leftover{};
            while (PeekMessageW(&leftover, window, kIconReadyMessage,
                                kIconReadyMessage, PM_REMOVE)) {
                delete reinterpret_cast<nimblerun::IconResult*>(leftover.lParam);
            }
        }
        if (g_search_font) {
            DeleteObject(g_search_font);
            g_search_font = nullptr;
        }
        if (g_search_bg_brush) {
            DeleteObject(g_search_bg_brush);
            g_search_bg_brush = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

bool RegisterMainWindow(HINSTANCE instance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_NIMBLERUN));
    window_class.hIconSm = window_class.hIcon;
    return RegisterClassExW(&window_class) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_show_panel_message = RegisterWindowMessageW(kShowPanelMessageName);
    if (g_show_panel_message == 0) {
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND existing = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(existing, g_show_panel_message, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    const HRESULT com_result = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com_result)) {
        CloseHandle(mutex);
        return 1;
    }

    if (!RegisterMainWindow(instance)) {
        CoUninitialize();
        CloseHandle(mutex);
        return 1;
    }

    const HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kWindowClass,
        kWindowTitle,
        // NR-042: WS_CLIPCHILDREN keeps the D2D present out of the search EDIT's
        // rect. Without it every keystroke's whole-window InvalidateRect
        // (EN_UPDATE) repainted over the child, and because that invalidation
        // does not reach children the EDIT never repainted -- erasing the caret,
        // which the system draws outside WM_PAINT and cannot restore. The rounded
        // search frame is unaffected: it is drawn outside the EDIT rect, which is
        // inset by kSearchTextInsetDip / kSearchEditInsetYDip.
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
        0,
        0,
        640,
        488,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) {
        CoUninitialize();
        CloseHandle(mutex);
        return 1;
    }

    // NR-010/NR-011: load settings + usage, then serve the catalog from a valid
    // cache immediately while the full build runs in the background. The panel
    // model points at the coordinator's live snapshot, which swaps atomically
    // only when every source in a generation has reported.
    nimblerun::Settings settings = nimblerun::DefaultSettings();
    nimblerun::SettingsStore settings_store(nimblerun::DefaultSettingsDir());
    settings_store.Load(settings);
    g_settings_store = &settings_store;
    g_settings = settings;
    g_hide_after_launch = settings.hide_after_launch;
    g_theme = settings.theme;

    nimblerun::CatalogRefreshCoordinator refresh;
    g_refresh = &refresh;
    std::vector<nimblerun::AppEntry> cached;
    if (nimblerun::LoadCatalogCache(nimblerun::DefaultSettingsDir(), cached)) {
        refresh.SetSnapshot(std::move(cached));
    }

    nimblerun::UsageStore usage(nimblerun::DefaultSettingsDir());
    usage.Load();
    g_usage = &usage;

    // NR-017: bounded diagnostic log in the per-user data dir.
    nimblerun::DiagnosticLog diag(nimblerun::DefaultSettingsDir(), L"nimblerun.log");
    g_diag = &diag;

    // NR-018: the pin store is reloaded and reconciled in RefreshPins(); only
    // the (non-owning) pointer is kept so the model and host share one store.
    nimblerun::PinStore pins(nimblerun::DefaultSettingsDir());
    g_pins = &pins;

    nimblerun::PanelModel model(&refresh.Snapshot(), {});
    g_model = &model;
    // NR-029: the empty-query grid is a fixed 6-column layout (design-spec
    // §4.9); the constant is set once and Columns() switches by query state.
    model.SetGridColumns(nimblerun::layout::kGridColumns);
    RefreshPanelSnapshot();

    // NR-012: bounded decoded-bitmap cache + Shell-backed provider. NR-032: the
    // provider is owned by a dedicated worker thread (which CoInitializeEx's
    // its own STA); the UI thread only posts requests and receives results
    // through kIconReadyMessage.
    nimblerun::IconCache icon_cache;
    nimblerun::ShellIconProvider shell_icon_provider;
    g_icon_cache = &icon_cache;

    // NR-036: file-backed decoded-icon cache (%LOCALAPPDATA%\NimbleRun\icons.cache,
    // design-spec §10.1). The store is opened and driven exclusively by the
    // icon worker; the UI thread only owns the pointer and never calls it.
    // Declared before the worker so it is destroyed after it.
    nimblerun::IconStore::IconStorePaths icon_store_paths;
    icon_store_paths.pack =
        std::filesystem::path(nimblerun::DefaultSettingsDir()) / L"icons.cache";
    nimblerun::IconStore icon_store(icon_store_paths,
                                    nimblerun::IconStore::kMaxPackBytes, &diag);

    // NR-032: one persistent icon worker. Both it and the provider are function
    // locals destroyed after the message loop exits; WM_DESTROY stops the worker
    // while both are still alive.
    nimblerun::IconWorker icon_worker(window, kIconReadyMessage, shell_icon_provider,
                                      &icon_store);
    g_icon_worker = &icon_worker;
    icon_worker.Start();

    // NR-011: directory watchers over Programs + configured user folders.
    nimblerun::CatalogWatcher watcher(window, kWatchChangedMessage);
    g_watcher = &watcher;
    StartWatchers();

    // Search input as a child EDIT control; subclassed to route keys to the model.
    // NR-023: created at the origin (geometry is set by RepositionSearchEdit
    // right after, so the fixed-size rect is gone) and styled without a border
    // — the rounded frame is drawn by D2D on the panel.
    g_search_edit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(kSearchId),
        instance,
        nullptr);
    if (g_search_edit) {
        g_search_original_proc =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_search_edit, GWLP_WNDPROC,
                                                       reinterpret_cast<LONG_PTR>(SearchEditProc)));
        RepositionSearchEdit(window);
        UpdateSearchFont(window);
    }

    // NR-003/NR-013: register the global hotkey from settings (default
    // Alt+Space) with MOD_NOREPEAT. Unparseable stored values fall back to the
    // default. On failure keep the tray resident process alive, record the
    // Win32 error, and show a single non-blocking reminder. No low-level hook,
    // no retry, no silent fallback to another key.
    nimblerun::HotkeyBinding startup_binding{
        nimblerun::kDefaultHotkeyModifiers,
        nimblerun::kDefaultHotkeyVk};
    nimblerun::HotkeyBinding settings_binding{};
    if (nimblerun::ParseHotkey(g_settings.hotkey, settings_binding)) {
        startup_binding = settings_binding;
    }
    const nimblerun::HotkeyResult hotkey_result = g_hotkey.Initialize(window, startup_binding);

    AddTrayIcon(window);
    if (!hotkey_result.success) {
        g_last_hotkey_error = hotkey_result.error;
        if (g_diag) {
            g_diag->Write(L"hotkey-register",
                          L"error " + std::to_wstring(hotkey_result.error));
        }
        ShowHotkeyConflictNotice(window);
    }

    // NR-011: kick off the background full rebuild now that the panel can serve
    // the cached snapshot; the results arrive through kRebuildDoneMessage.
    if (g_refresh) {
        const std::vector<nimblerun::CatalogSource> all = {
            nimblerun::CatalogSource::StartMenu,
            nimblerun::CatalogSource::AppsFolder,
            nimblerun::CatalogSource::UserFolder,
        };
        StartRebuild(window, all);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DiscardDeviceResources();
    Release(g_title_format);
    Release(g_text_format);
    Release(g_small_format);
    Release(g_grid_name_format);
    Release(g_ellipsis_sign);
    Release(g_write_factory);
    Release(g_d2d_factory);
    CoUninitialize();
    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
