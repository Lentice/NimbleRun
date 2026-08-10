#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>

#include "app_host/catalog_watcher.h"
#include "app_host/full_rescan_throttle.h"
#include "app_host/hotkey.h"
#include "app_host/message_loop.h"
#include "app_host/panel_model.h"
#include "app_host/settings_dialog.h"
#include "catalog/app_filter.h"
#include "catalog/appsfolder_catalog.h"
#include "catalog/catalog_cache.h"
#include "catalog/catalog_refresh.h"
#include "catalog/dedup.h"
#include "catalog/stable_id.h"
#include "catalog/start_menu_catalog.h"
#include "catalog/user_folder_catalog.h"
#include "diagnostics/diagnostic_log.h"
#include "diagnostics/load_notice.h"
#include "icons/icon_cache.h"
#include "icons/icon_store.h"
#include "icons/icon_worker.h"
#include "icons/shell_icon_provider.h"
#include "launch/shell_launch.h"
#include "pins/pin_store.h"
#include "resources/resource.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "storage/atomic_text_file.h"
#include "ui/panel_layout.h"
#include "ui/panel_accessibility.h"
#include "ui/panel_palette.h"
#include "ui/quick_select.h"
#include "usage/usage_store.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"NimbleRun.Phase0Probe";
constexpr wchar_t kWindowTitle[] = L"NimbleRun";
constexpr wchar_t kInstanceMutex[] = L"Local\\NimbleRun.SingleInstance";
constexpr wchar_t kStartupReadyEvent[] = L"Local\\NimbleRun.StartupReady";
constexpr wchar_t kShowPanelMessageName[] = L"NimbleRun.ShowPanel";
constexpr DWORD kStartupRendezvousTimeoutMs = 5000;
constexpr DWORD kStartupTestGateTimeoutMs = 30000;

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
// NR-100: a worker could not deliver its rebuild result (PostMessageW failed);
// a pure wake-up so the UI drains the pending delivery failures as source
// failures and the generation can still complete. The non-zero wParam/lParam
// form is a no-allocation fallback for recording a failure.
constexpr UINT kRebuildDeliveryFailedMessage = WM_APP + 10;
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
// NR-061: shown for the empty-query state now that the panel no longer fills
// with alphabetical catalog entries (overrides NR-053's fill behavior).
constexpr wchar_t kNoRecentApps[] = L"No pinned or recent apps yet";
// NR-062: footer path-bar label for a missing-pin placeholder, distinct from
// kWindowsApp (a packaged app with no filesystem path is not the same case as
// a pin whose app cannot be found at all).
constexpr wchar_t kMissingApp[] = L"App not found";
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
// NR-045: shown in the grid state while Alt is up, in place of the
// Alt+1~N / Launch group (design-spec §4.9).
constexpr wchar_t kHoldAltHint[] = L"Hold Alt to show shortcuts";
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
// NR-130: the single-instance mutex is held but no window could be reached
// (a stale mutex, or the primary hung before showing). Shown before exiting
// instead of exiting silently; the process never becomes primary (NR-110).
constexpr wchar_t kRendezvousTimeout[] =
    L"NimbleRun appears to be already running, but its window could not be "
    L"contacted.";
} // namespace dialog_strings

UINT g_show_panel_message = 0;

// Owns the single global hotkey (NR-003). Swap keeps register-new-first
// semantics; the active combo alternates between two ids (see ActiveId()).
nimblerun::GlobalHotkey g_hotkey;

// NR-058: startup store-load failures, aggregated for a single tray balloon
// (design-spec §10.4/§11). Cleared to None after the balloon is sent, so a
// process notifies at most once.
unsigned g_store_load_issues = 0;
// NR-058: pins already notified this process. The panel reloads pins on every
// show, so a corrupt favorites.txt must not balloon on each Alt+Space.
bool g_pins_notified = false;
// NR-058: the main window HWND (tray callback target), set in wWinMain once the
// window exists; the only UI-thread owner of the tray notices.
HWND g_main_window = nullptr;
HANDLE g_test_show_semaphore = nullptr;
// NR-058: true once Shell_NotifyIconW(NIM_ADD) succeeded, so RefreshPins knows
// whether a balloon can be shown now or must be deferred to the startup send
// point (the first pin load runs before the tray icon exists).
bool g_tray_icon_active = false;

// Panel state (NR-010). The model is pure; the window translates input.
nimblerun::PanelModel* g_model = nullptr;
nimblerun::PanelAccessibilityProvider* g_accessibility = nullptr;
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
// NR-054: the per-user log directory (design-spec §10.1 logs\nimblerun.log).
// Set once in wWinMain and shared by the DiagnosticLog construction and the
// Settings dialog's "Open log folder" button.
std::wstring g_log_directory;
// Set once in wWinMain. Empty means the Known Folder resolver failed; all
// app-owned persistence and caches stay disabled for the process lifetime.
std::wstring g_user_data_directory;
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
// NR-130: last accepted full-rescan marker timestamp per source, so a storm of
// posted markers (any same-session process can send them) does not force one
// immediate rebuild per message. kFullRescanNever = no accepted marker yet.
// UI-thread only, like the rest of the watch state.
std::unordered_map<nimblerun::CatalogSource, std::int64_t> g_last_full_rescan_ms;

// NR-049: rebuild threads are owned, not detached. Joined in WM_DESTROY so a
// scan can never outlive the window it posts to, nor the globals it reads.
// Only ever touched on the UI thread (StartRebuild and WM_DESTROY), so it needs
// no lock of its own.
std::vector<std::thread> g_rebuild_threads;

// NR-098: cooperative cancellation signal for in-flight rebuild scans. Set on
// the UI thread (StartRebuild and WM_DESTROY both run there) before the
// previous cycle's threads are joined, and reset only after they are all
// joined, so the enumerators on those threads read it while it is stable and
// the next generation starts clean. The atomic is the only synchronization the
// enumerators need; no lock is required around it.
std::atomic<bool> g_rebuild_cancel{false};

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
    if (copied == 0 || copied >= length) {
        return {};
    }
    value.resize(copied);
    return value;
}

// NR-110: test-only gate proves a second process can launch before the first
// CreateWindowExW. Both named events are created by the lifecycle test; the
// release wait is bounded so an interrupted test cannot leave startup hung.
void WaitForStartupTestGate() {
    const std::wstring base = EnvironmentValue(L"NIMBLERUN_TEST_STARTUP_GATE");
    if (base.empty()) {
        return;
    }
    const std::wstring ready_name = base + L".ready";
    const std::wstring release_name = base + L".release";
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name.c_str());
    HANDLE release = OpenEventW(SYNCHRONIZE, FALSE, release_name.c_str());
    if (ready && release) {
        SetEvent(ready);
        WaitForSingleObject(release, kStartupTestGateTimeoutMs);
    }
    if (ready) {
        CloseHandle(ready);
    }
    if (release) {
        CloseHandle(release);
    }
}

void OpenTestShowSemaphore() {
    const std::wstring name = EnvironmentValue(L"NIMBLERUN_TEST_SHOW_SEMAPHORE");
    if (!name.empty()) {
        g_test_show_semaphore = OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE,
                                                name.c_str());
    }
}

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
    // NR-124: the enumerator's per-source counts, folded into the coordinator's
    // generation diagnostics by ApplySourceResult. Only ever consumed on the
    // UI thread's completion handler.
    nimblerun::GenerationDiagnostics diagnostics;
};

// NR-077: rebuild results are registered in the shared handoff registry
// (nimblerun::g_handoff_mutex) by the StartRebuild workers before posting;
// the UI thread moves them out on kRebuildDoneMessage and WM_DESTROY clears
// whatever is still in flight. Tokens are object addresses.
std::unordered_map<std::uintptr_t, std::unique_ptr<RebuildResult>> g_rebuild_handoffs;

// NR-100: rebuild sources whose result could not be delivered (PostMessageW
// failed). The worker records "(generation, source)" before the UI thread
// drains it as a source failure, so every source still completes its
// generation exactly once. Only touched by the workers under
// g_delivery_failure_mutex and by the UI thread under the same mutex.
std::mutex g_delivery_failure_mutex;
std::vector<std::pair<std::uint64_t, nimblerun::CatalogSource>> g_rebuild_delivery_failures;

// NR-115: manual-reset event the main message loop also waits on. A worker
// that records a rebuild delivery failure signals it when PostMessageW cannot
// deliver the wake-up message (queue full), so a recorded failure is always
// drained instead of waiting for an unrelated later message.
HANDLE g_rebuild_failure_event = nullptr;

// NR-079: set once at startup when the on-disk catalog.cache carries a newer
// schema than this build reads. design-spec §10.4 forbids overwriting that
// file, so cache writes stay off for the run (the cache is a rebuildable
// snapshot; the in-memory one keeps working); a re-read next launch re-decides.
bool g_catalog_cache_disable_writes = false;

std::int64_t MonotonicMs() {
    return static_cast<std::int64_t>(GetTickCount64());
}

ID2D1Factory* g_d2d_factory = nullptr;
ID2D1HwndRenderTarget* g_render_target = nullptr;
// NR-046: dashed stroke for the drag placeholder. Created from the D2D factory
// (device-independent), so it is created with the factory and released with it,
// never in the render-target create/release pair.
ID2D1StrokeStyle* g_dash_style = nullptr;
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
// NR-043: semi-bold centered format for the key-hint boxes (footer and
// per-row digits); kept distinct from g_small_format, which is left-aligned
// on purpose for row subtitles and the path bar.
IDWriteTextFormat* g_key_format = nullptr;
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

// NR-046: pinned-cell drag-reorder state. Window-layer visual state like
// g_grid_hover_index, never model state: it is reset when the panel hides and
// when capture is lost, so there is nothing stale to reconcile.
int g_drag_row = -1;      // pressed pinned row index; -1 = no press captured
int g_drag_gap = -1;      // pinned row index the dragged item would take;
                          // -1 = cursor is outside the pinned region (= cancel)
bool g_dragging = false;  // true once the press passed the system drag threshold
POINT g_drag_origin{};    // client coords of the press, for the threshold test
POINT g_drag_cursor{};    // latest client coords, for the ghost icon

// NR-066: sub-notch wheel-delta carry for WM_MOUSEWHEEL. Precision touchpads
// and high-resolution wheels report deltas of 30-60 per message, which divided
// by WHEEL_DELTA (120) is always zero; the remainder is accumulated here so a
// full notch's worth of movement still scrolls (MSDN's recommended pattern).
// Message-layer state, never model state -- it follows the panel, not the list.
int g_wheel_delta_carry = 0;

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
        g_grid_name_format && g_key_format) {
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
    // NR-046: the dash stroke style lives on the factory, so it is created once
    // here (device-independent, survives DiscardDeviceResources) and released
    // where the factory is released below the message loop.
    if (g_d2d_factory && !g_dash_style) {
        g_d2d_factory->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                        D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                                        D2D1_DASH_STYLE_DASH, 0.0f),
            nullptr, 0, &g_dash_style);
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
    // NR-067: text formats are device-independent (they outlive the render
    // target and must not be released by DiscardDeviceResources), so each is
    // created once and guarded like g_dash_style. Without the guard, a theme
    // switch or D2DERR_RECREATE_TARGET re-entered this function with the five
    // globals still alive and overwrote them -- leaking five COM objects per
    // device-resource recreation. The guard also makes a partial-failure retry
    // create only the format that is still missing.
    if (!g_title_format) {
        const HRESULT title = g_write_factory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTitleFontDip, L"en-US", &g_title_format);
        if (FAILED(title)) {
            return false;
        }
    }
    if (!g_text_format) {
        const HRESULT text = g_write_factory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTextFontDip, L"en-US", &g_text_format);
        if (FAILED(text)) {
            return false;
        }
    }
    if (!g_small_format) {
        const HRESULT small = g_write_factory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kSmallFontDip, L"en-US", &g_small_format);
        if (FAILED(small)) {
            return false;
        }
    }
    // NR-029: grid cell names use the same face/size as list row names but are
    // centered in the cell; alignment is set below with the trimming.
    if (!g_grid_name_format) {
        const HRESULT grid_name = g_write_factory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kTextFontDip, L"en-US", &g_grid_name_format);
        if (FAILED(grid_name)) {
            return false;
        }
    }
    // NR-043: key-hint boxes get their own format. Semi-bold reads as a keycap
    // at 20 DIP and Segoe UI's digits are tabular, so single digits land in the
    // same place in every box; centered on both axes so the label no longer
    // depends on the kFooterTextInsetDip nudge. g_small_format cannot be reused
    // -- it is left-aligned on purpose for row subtitles and the path bar.
    if (!g_key_format) {
        const HRESULT key = g_write_factory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, nimblerun::layout::kSmallFontDip, L"en-US", &g_key_format);
        if (FAILED(key)) {
            return false;
        }
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
        // NR-043: the key-hint format centers the label on both axes inside the
        // full box rect; no trimming so an overflowing label stays visible at
        // acceptance instead of being hidden behind an ellipsis.
        g_key_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        g_key_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_key_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
    // NR-064: the footer band begins at kFooterTopDip for both layouts (grid 4
    // rows and list 8 rows both end at 456 DIP), so y >= footer top is a miss.
    // Without this lower bound a footer click computed row first+8 (the 9th,
    // unpainted result) and launched an app the user could not see; the point is
    // now a window-drag (NR-039), not a launch. NR-082 adds the painted-row
    // bound for the clamp-shrunk strip between the last row and the footer.
    // Both bounds, plus the grid/list margins, now live in the single DIP
    // hit-test SlotAtPointDip (NR-133), the same geometry the renderer paints;
    // the caller converts physical px to DIP via layout.scale and applies the
    // model's FirstVisibleRow() offset and Rows().size() bounds below.
    RECT client{};
    GetClientRect(window, &client);
    const float client_height_dip =
        static_cast<float>(std::max(0L, client.bottom - client.top)) / layout.scale;
    const int slot = nimblerun::layout::SlotAtPointDip(
        static_cast<float>(x) / layout.scale,
        static_cast<float>(y) / layout.scale,
        g_model->Columns(), g_model->ViewportRows(), client_height_dip);
    if (slot < 0) {
        return -1;
    }
    const int index = g_model->FirstVisibleRow() + slot;
    return index < static_cast<int>(g_model->Rows().size()) ? index : -1;
}

// NR-046: pinned row count of the current view, 0 when there is no pinned
// region. RecentStartIndex() is the single pinned/recent boundary (NR-040).
int PinnedRowCount() {
    if (!g_model) {
        return 0;
    }
    const int recent_start = g_model->RecentStartIndex();
    return recent_start > 0 ? recent_start : 0;
}

// NR-046: paint order of the pinned region during a drag: the dragged row is
// lifted out and a gap (-1) is left where it would land, so the pinned region
// reflows and the recent region never moves -- the permutation is closed over
// the pinned region, so the total cell count is unchanged. Empty vector when no
// drag preview applies.
std::vector<int> DragPreviewOrder() {
    if (!(g_dragging && g_drag_row >= 0 && g_drag_gap >= 0)) {
        return {};
    }
    const int pinned = PinnedRowCount();
    // A catalog swap landing mid-drag can shrink the pinned region below the
    // pressed row; stay empty then rather than erase/insert out of range. The
    // ghost paint below guards its own row access the same way.
    if (g_drag_row >= pinned || g_drag_gap >= pinned) {
        return {};
    }
    std::vector<int> order(static_cast<std::size_t>(pinned));
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    order.erase(order.begin() + g_drag_row);
    order.insert(order.begin() + g_drag_gap, -1);
    return order;
}

void SyncAccessibility(HWND window);

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
    // NR-120: the row area ends at the footer band's top edge, never the client
    // bottom, so ViewportRows() shrinks when ClampWindowSize shortens the panel
    // below 488 DIP and the path bar + key hints stay visible (design-spec
    // §4.2/§4.9). Pure DIP geometry, matching the D2D renderer's coordinate
    // space; a full-height client yields the same 8 list / 4 grid rows.
    const float client_height_dip =
        static_cast<float>(std::max(0L, client.bottom - client.top)) / layout.scale;
    g_model->SetViewportRows(nimblerun::layout::ViewportRowsForHeightDip(
        client_height_dip, g_model->Columns()));
    SyncAccessibility(window);
}

void SyncAccessibility(HWND window) {
    if (!g_accessibility || !g_model || !window) {
        return;
    }
    nimblerun::PanelAccessibilitySnapshot snapshot;
    snapshot.query = g_model->Query();
    snapshot.search_focused = GetFocus() == g_search_edit;

    const auto dpi = static_cast<float>(GetDpiForWindow(window));
    const auto layout = nimblerun::layout::LayoutForDpi(dpi);
    const int columns = std::max(1, g_model->Columns());
    const int page_size = std::max(1, g_model->ViewportRows() * columns);
    const int first = g_model->FirstVisibleRow();
    const int visible = g_model->ViewportRows() * columns;
    const int selected_index = g_model->HasSelection()
        ? static_cast<int>(g_model->SelectionIndex()) : -1;
    snapshot.selected_row = selected_index >= first && selected_index < first + visible
        ? selected_index - first : -1;
    const int row_count = (static_cast<int>(g_model->Rows().size()) + page_size - 1) /
                          page_size;
    snapshot.page = first / page_size + 1;
    snapshot.page_count = std::max(1, row_count);
    snapshot.footer = L"Query: " + snapshot.query + L"; Page " +
                      std::to_wstring(snapshot.page) + L" of " +
                      std::to_wstring(snapshot.page_count);
    if (selected_index >= 0) {
        snapshot.footer += L"; Selected: " + g_model->SelectedAccessibleName();
    }

    RECT window_rect{};
    GetWindowRect(window, &window_rect);
    const auto screen_rect = [&](RECT client) {
        POINT top_left{client.left, client.top};
        POINT bottom_right{client.right, client.bottom};
        ClientToScreen(window, &top_left);
        ClientToScreen(window, &bottom_right);
        return RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    };
    RECT search{};
    if (!g_search_edit || !GetWindowRect(g_search_edit, &search)) {
        search = screen_rect(RECT{layout.search_left, layout.search_top,
                                  layout.search_right, layout.search_bottom});
    }
    snapshot.search_bounds = search;
    // NR-120: the footer band is pinned to the client bottom (same rule the
    // renderer and UpdateViewportRows use), so the reported bounds match where
    // the band actually paints when the panel is clamped below 488 DIP.
    RECT client_rect{};
    GetClientRect(window, &client_rect);
    const float client_height_dip =
        static_cast<float>(std::max(0L, client_rect.bottom - client_rect.top)) /
        layout.scale;
    const float footer_top_dip = nimblerun::layout::FooterTopDip(client_height_dip);
    const float footer_bottom_dip = footer_top_dip +
        (nimblerun::layout::kPanelHeightDip - nimblerun::layout::kFooterTopDip);
    snapshot.footer_bounds = screen_rect(RECT{0, static_cast<LONG>(
        std::lround(footer_top_dip * layout.scale)),
        window_rect.right - window_rect.left,
        static_cast<LONG>(std::lround(footer_bottom_dip * layout.scale))});

    snapshot.rows.reserve(static_cast<std::size_t>(std::max(0, visible)));
    for (int slot = 0; slot < visible; ++slot) {
        const int index = first + slot;
        if (index < 0 || index >= static_cast<int>(g_model->Rows().size())) {
            break;
        }
        // NR-133: the reported bounds come from the same SlotRect the renderer
        // paints, scaled to physical px -- one slot geometry for hit-test,
        // paint and accessibility instead of a second copy.
        const nimblerun::layout::SlotRectDip slot_rect =
            nimblerun::layout::SlotRect(slot, columns, client_height_dip);
        const RECT bounds = {
            static_cast<LONG>(std::lround(slot_rect.left * layout.scale)),
            static_cast<LONG>(std::lround(slot_rect.top * layout.scale)),
            static_cast<LONG>(std::lround(slot_rect.right * layout.scale)),
            static_cast<LONG>(std::lround(slot_rect.bottom * layout.scale))};
        nimblerun::PanelAccessibilityElement element;
        element.role = nimblerun::PanelAccessibilityElement::Role::AppRow;
        element.name = g_model->AccessibleNameFor(static_cast<std::size_t>(index));
        element.bounds = screen_rect(bounds);
        element.selected = slot == snapshot.selected_row;
        element.disabled = nimblerun::PanelModel::IsMissingPin(
            g_model->Rows()[static_cast<std::size_t>(index)]);
        snapshot.rows.push_back(std::move(element));
    }
    g_accessibility->Update(window, snapshot);
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

// NR-056: reads the file version out of the embedded VS_VERSION_INFO resource
// (src/resources/NimbleRun.rc), which is the single source of truth for the
// version number -- the About box must never carry a second hard-coded copy.
// Returns an empty string when the resource is missing or unreadable, in which
// case the caller simply omits the version line.
std::wstring ProductVersionString() {
    wchar_t module_path[MAX_PATH]{};
    const DWORD path_len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (path_len == 0 || path_len >= MAX_PATH) {
        return {};
    }
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(module_path, &handle);
    if (size == 0) {
        return {};
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(module_path, handle, size, data.data())) {
        return {};
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT info_len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info),
                        &info_len) ||
        !info || info_len < sizeof(VS_FIXEDFILEINFO)) {
        return {};
    }
    return L"Version " + std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
           std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
           std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." +
           std::to_wstring(LOWORD(info->dwFileVersionLS));
}

// NR-056: design-spec §4.10 requires an About entry in the tray menu. A menu
// item that does nothing is worse than no menu item: it reads as a bug every
// time it is clicked. A MessageBox with the product name and version satisfies
// the clause; anything more (icon, links, license text) is not specced.
void ShowAboutDialog(HWND window) {
    std::wstring message = dialog_strings::kTitle;
    const std::wstring version = ProductVersionString();
    if (!version.empty()) {
        message += L"\n\n" + version;
    }
    g_dialog_active = true;
    MessageBoxW(window, message.c_str(), dialog_strings::kTitle,
                MB_OK | MB_ICONINFORMATION);
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

void ClearDroppedIconRequests() {
    for (const std::wstring& key : nimblerun::TakeIconDroppedKeys()) {
        g_pending_icon_keys.erase(key);
    }
}

// Defined below, next to the panel-refresh path it is also part of; the launch
// path needs it before that point.
void StampRankingFields();

// NR-036: the single hide path. Every way the panel disappears (Esc second
// stage, WM_KILLFOCUS auto-hide, hide-after-launch, hotkey/tray toggle) funnels
// through here so the freshly fetched icons are flushed exactly once per hide
// -- never once per call site. The pinned list is the in-memory copy ShowPanel
// already loaded (design-spec §10.2); it rides the flush task as a pure-value
// copy and the worker never re-reads favorites.txt.
void HidePanel(HWND window) {
    ShowWindow(window, SW_HIDE);
    if (g_icon_worker) {
        // NR-099: drop the previous hide cycle's queued prewarm before the
        // fresh flush + prewarm for the new idle session is posted, so stale
        // prewarm work is cancelled on the new panel state (design-spec §9.2).
        g_icon_worker->CancelPrewarm();
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
    // NR-062: a missing-pin placeholder is not launchable (Enter, a click, and
    // Alt+digit quick-select all funnel through this one function, so this single
    // guard covers every launch entry point). No error dialog, no launch
    // failure flow -- nothing happens at all.
    if (nimblerun::PanelModel::IsMissingPin(entry)) {
        return;
    }
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
            StartRebuild(window, {std::cbegin(nimblerun::kSources),
                                  std::cend(nimblerun::kSources)});
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
        // The new score has to reach the snapshot the next search reads. With
        // hide-after-launch off the panel stays open, so waiting for the next
        // show would rank the app the user just launched on its old score.
        StampRankingFields();
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
    try {
        g_pending_icon_keys.insert(encoded);
        if (!g_icon_worker->Post({entry, key, /*visible=*/true, encoded})) {
            g_pending_icon_keys.erase(encoded);
        }
    } catch (...) {
        g_pending_icon_keys.erase(encoded);
    }
}

void DrawDecodedIcon(const nimblerun::IconBitmap& icon,
                     const D2D1_RECT_F& tile,
                     float dpi_x,
                     float dpi_y,
                     float opacity = 1.0f) {
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
        bitmap, tile, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    bitmap->Release();
}

// NR-059: the grid cell and the list row painted this identical block. It is
// not just drawing -- it also drives the NR-032 request de-duplication, so two
// copies meant the "one request per key" invariant had two homes. The only
// thing that ever differed is the rect and the pixel size the layout needs.
void DrawIconOrFallback(const nimblerun::AppEntry& entry,
                        const D2D1_RECT_F& tile,
                        int needed_px,
                        float dpi_x,
                        float dpi_y) {
    const nimblerun::IconKey key = IconKeyFor(entry, needed_px);
    const std::wstring encoded = key.Encode();
    if (const nimblerun::IconBitmap* icon =
            g_icon_cache ? g_icon_cache->Peek(encoded) : nullptr) {
        DrawDecodedIcon(*icon, tile, dpi_x, dpi_y);
        return;
    }
    // design-spec §FR-009: fallback-first, drawn into the same rect the real
    // icon will occupy, so a late icon never reflows anything.
    g_render_target->FillRectangle(tile, g_dim_brush);
    const std::wstring initial = entry.display_name.empty()
        ? std::wstring(L"?")
        : std::wstring(1, entry.display_name.front());
    g_render_target->DrawText(initial.c_str(), static_cast<UINT32>(initial.size()),
                              g_text_format, tile, g_text_brush);
    RequestVisibleIcon(entry, key, encoded);
}

// NR-062: a pinned row whose app is absent from the catalog (PanelModel::
// IsMissingPin) draws an X instead of the usual icon/fallback -- "this is not
// here", not a question mark or a generic gray tile (the user's explicit
// decision, see the work item). Uses the existing g_dim_brush; no new brush,
// image, or font resource. Inset ~25% of the icon rect on each side.
void DrawMissingPinTile(const D2D1_RECT_F& icon_rect, float dpi_x) {
    const float inset_x = (icon_rect.right - icon_rect.left) * 0.25f;
    const float inset_y = (icon_rect.bottom - icon_rect.top) * 0.25f;
    const float left = icon_rect.left + inset_x;
    const float top = icon_rect.top + inset_y;
    const float right = icon_rect.right - inset_x;
    const float bottom = icon_rect.bottom - inset_y;
    const float scale = dpi_x / nimblerun::layout::kDpi96;
    const float stroke_width = 2.0f * scale;
    g_render_target->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(right, bottom),
                              g_dim_brush, stroke_width);
    g_render_target->DrawLine(D2D1::Point2F(left, bottom), D2D1::Point2F(right, top),
                              g_dim_brush, stroke_width);
}

// NR-059: identical in both layouts except the row height (design-spec §4.3).
// NR-061: searching distinguishes "no results for this query" from "no pins
// or recent apps yet" -- kNoMatchingApps only makes sense for the former; the
// empty-query state no longer fills with catalog entries, so it needs its own
// hint text.
void DrawEmptyStateHint(float row_height, bool searching) {
    const wchar_t* hint = !g_model->CatalogAvailable()
        ? list_strings::kBuildingCatalog
        : (searching ? list_strings::kNoMatchingApps
                     : list_strings::kNoRecentApps);
    g_render_target->DrawText(
        hint, static_cast<UINT32>(wcslen(hint)), g_text_format,
        D2D1::RectF(nimblerun::layout::kListLeftDip, nimblerun::layout::kListTopDip,
                    nimblerun::layout::kListRightDip,
                    nimblerun::layout::kListTopDip + row_height),
        g_dim_brush);
}

// NR-058: single non-blocking tray balloon for a store-load failure; defined
// next to ShowHotkeyConflictNotice below.
void ShowLoadIssueNotice(HWND window, const std::wstring& text);

// NR-058: maps one store load result to its notification bit. Loaded and
// Missing never notify (first run with no files is normal). A switch over every
// enumerator: adding a load result without a UI decision here trips the
// compiler's -Wswitch reminder.
template <typename Result>
unsigned StoreLoadIssueFor(Result result) {
    switch (result) {
    case Result::Loaded:
    case Result::Missing:
        return 0;
    case Result::Corrupt:
        return static_cast<unsigned>(nimblerun::StoreLoadIssue::Corrupt);
    case Result::NewerSchema:
        return static_cast<unsigned>(nimblerun::StoreLoadIssue::TooNew);
    }
    return 0;
}

// NR-058: the one-word enum name for a diagnostic log line. Only the file name
// and the result enum name are ever logged (design-spec §FR-014).
template <typename Result>
const wchar_t* StoreLoadResultName(Result result) {
    switch (result) {
    case Result::Loaded:
        return L"Loaded";
    case Result::Missing:
        return L"Missing";
    case Result::Corrupt:
        return L"Corrupt";
    case Result::NewerSchema:
        return L"NewerSchema";
    }
    return L"?";
}

// NR-058: one diagnostic line per non-Loaded store load, shaped
// "settings_load\tresult=Corrupt". g_diag always exists at the call sites
// (settings/usage lines are written only after the log is created); the null
// check keeps the pattern consistent with the other Write callers.
void LogStoreLoad(const wchar_t* stage, const wchar_t* result_name) {
    if (g_diag) {
        g_diag->Write(stage, std::wstring(L"result=") + result_name);
    }
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
    const nimblerun::PinLoadResult result = g_pins->Load();
    if (result == nimblerun::PinLoadResult::Missing) {
        // First run: the file does not exist yet, which is normal. Still log
        // one line (decision #5), but never set a flag or show a balloon.
        LogStoreLoad(L"pins_load", StoreLoadResultName(result));
    } else if (result == nimblerun::PinLoadResult::Corrupt ||
               result == nimblerun::PinLoadResult::NewerSchema) {
        LogStoreLoad(L"pins_load", StoreLoadResultName(result));
        if (!g_pins_notified) {
            g_pins_notified = true;
            if (g_tray_icon_active && g_main_window) {
                const std::wstring text = nimblerun::StoreLoadNoticeText(
                    StoreLoadIssueFor(result));
                if (!text.empty()) {
                    ShowLoadIssueNotice(g_main_window, text);
                }
            } else {
                // The first pin load runs before the tray icon exists (startup
                // RefreshPanelSnapshot); the startup send point shows the
                // balloon once the icon is in place.
                g_store_load_issues |= StoreLoadIssueFor(result);
            }
        }
    }
    // NR-072: a newer-schema file is another build's data -- never touch it
    // (design-spec §10.4); a corrupt load must not let a partial parse become
    // the live file. Only a Loaded (or Missing) store may be reconciled and
    // persisted. This also stops an empty pins_ from clobbering favorites.txt.
    if (result == nimblerun::PinLoadResult::Loaded ||
        result == nimblerun::PinLoadResult::Missing) {
        g_pins->Reconcile(g_refresh->Snapshot(),
                          static_cast<std::int64_t>(std::time(nullptr)));
        g_pins->Save();
    }
    g_model->SetPins(g_pins->Records());
}

// design-spec §4.5: among equal text matches the order is pinned first, then
// higher usage score. Both fields live on AppEntry but are derived from the pin
// and usage stores, so they have to be stamped onto the merged snapshot after
// anything that changes either store -- and after every rebuild, which
// recomputes the snapshot from the source entries and drops them. Without this
// the two tie-breaks in SearchApps compare zero against zero and a
// just-launched app keeps sinking below alphabetically luckier ones.
//
// One linear pass over the snapshot with a hashed score lookup; the pin list is
// a handful of entries, so it is scanned directly.
void StampRankingFields() {
    if (!g_refresh || !g_usage) {
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::unordered_map<std::wstring_view, int> scores;
    scores.reserve(g_usage->Records().size());
    for (const nimblerun::UsageRecord& record : g_usage->Records()) {
        scores.emplace(record.stable_id, nimblerun::UsageScore(record, now));
    }
    const std::vector<std::wstring> pins =
        g_pins ? g_pins->OrderedPins() : std::vector<std::wstring>{};
    for (nimblerun::AppEntry& entry : g_refresh->MutableSnapshot()) {
        const auto score = scores.find(entry.stable_id);
        entry.usage_score = score == scores.end() ? 0 : score->second;
        entry.is_pinned =
            std::find(pins.begin(), pins.end(), entry.stable_id) != pins.end();
    }
}

// NR-011: repoints the panel model at the coordinator's current snapshot and
// refreshes the recent list, so a swapped-in catalog appears immediately.
void RefreshPanelSnapshot() {
    if (!g_model || !g_refresh || !g_usage) {
        return;
    }
    // NR-083: one stable_id -> snapshot index, built once for this refresh so
    // the recent-list resolution and the panel model's pin resolution do not
    // each linear-scan the whole catalog (O(recent_count x catalog) and
    // O(pin_count x catalog) on the ShowPanel hot path). The keys are views
    // into the current snapshot vector, which RebuildMerged replaces wholesale
    // on the next completed generation, so the index lives only for the
    // duration of this function: set before the first RefreshRows trigger
    // (RefreshPins -> SetPins), cleared after SetRecent.
    std::unordered_map<std::wstring_view, std::size_t> snapshot_index;
    snapshot_index.reserve(g_refresh->Snapshot().size());
    for (std::size_t i = 0; i < g_refresh->Snapshot().size(); ++i) {
        snapshot_index.emplace(g_refresh->Snapshot()[i].stable_id, i);
    }
    g_model->SetCatalogIndex(&snapshot_index);
    // Pins are loaded first because they feed the is_pinned stamp.
    RefreshPins();
    // NR-061: drops usage records for apps no longer in the catalog before
    // ranking runs, so an uninstalled app cannot reappear in the recent region
    // with its old score after a reinstall. Guarded on a non-empty snapshot:
    // reconciling against an empty one during startup would wipe every record.
    if (!g_refresh->Snapshot().empty() && g_usage->Reconcile(g_refresh->Snapshot())) {
        g_usage->Save();
    }
    StampRankingFields();
    g_model->SetCatalog(&g_refresh->MutableSnapshot());
    std::vector<nimblerun::UsageRecord> recent_records = g_usage->Recent(g_settings.recent_count);
    std::vector<nimblerun::AppEntry> recent_entries;
    recent_entries.reserve(recent_records.size());
    for (const nimblerun::UsageRecord& record : recent_records) {
        const auto found = snapshot_index.find(record.stable_id);
        if (found != snapshot_index.end()) {
            recent_entries.push_back(g_refresh->Snapshot()[found->second]);
        }
    }
    // SetRecent is last, so its RefreshRows is the one that decides the visible
    // rows; RefreshPins above already handed the pin list to the model.
    g_model->SetRecent(std::move(recent_entries));
    // NR-083: the index views die with this function's snapshot reference;
    // drop the hint so no later RefreshRows dereferences it.
    g_model->SetCatalogIndex(nullptr);
    SyncAccessibility(g_main_window);
}

// NR-100: the single post-generation-completion choke point. Runs only when the
// whole generation has reported (success or failure), so the launch-failure
// gate resets and the merged snapshot + cache are refreshed exactly once per
// completed generation (design-spec §FR-008). No InvalidateRect: the caller
// owns the single repaint per handled message.
void OnGenerationCompleteRefresh() {
    // NR-124: write the completed generation's diagnostics -- at most three
    // sanitized lines, zero-noise (a clean generation writes nothing). The
    // enumerators never touch DiagnosticLog; the counts travel back through the
    // coordinator and are written here, on the UI thread only.
    if (g_diag && g_refresh) {
        for (const std::wstring& line :
             nimblerun::RebuildDiagnosticLines(g_refresh->LastGenerationDiagnostics())) {
            g_diag->Write(L"rebuild", line);
        }
    }
    // NR-022: the refresh the launch-failure dialog scheduled has run
    // to completion, so a future failure can schedule a fresh refresh.
    g_launch_failure_refresh.OnRefreshComplete();
    // NR-073: the merged snapshot only changes when the whole generation
    // has reported; refreshing the panel and writing catalog.cache for
    // the first 1..n-1 results would reset the selection and re-persist
    // data the UI thread already holds, every cycle (§FR-008).
    RefreshPanelSnapshot();
    // NR-079: a newer-schema catalog.cache is another build's data --
    // never overwrite it (design-spec §10.4); the in-memory snapshot
    // keeps working and the flag stays set for the whole run.
    if (!g_catalog_cache_disable_writes) {
        nimblerun::SaveCatalogCache(g_user_data_directory,
                                    g_refresh->Snapshot());
    }
}

// NR-100: applies every recorded delivery failure as a source failure, so each
// source whose result could not be posted still completes its generation
// (ApplySourceFailure keeps the source's old entries). Entries for a stale
// generation are dropped harmlessly. Returns true when at least one entry was
// applied and the current generation completed as a result. UI thread only.
bool DrainRebuildDeliveryFailures() {
    if (!g_refresh) {
        return false;
    }
    std::vector<std::pair<std::uint64_t, nimblerun::CatalogSource>> failures;
    {
        std::lock_guard<std::mutex> lock(g_delivery_failure_mutex);
        failures.swap(g_rebuild_delivery_failures);
    }
    if (failures.empty()) {
        return false;
    }
    bool applied_any = false;
    for (const auto& [generation, source] : failures) {
        if (g_refresh->ApplySourceFailure(generation, source)) {
            applied_any = true;
        }
    }
    return applied_any && !g_refresh->IsRebuildInProgress();
}

// UI-owned completion for failures that happen before a worker can hand off a
// result. The worker never receives the coordinator pointer; it queues the
// same failure signal through QueueRebuildSourceFailure below.
void CompleteRebuildSourceFailure(std::uint64_t generation,
                                  nimblerun::CatalogSource source) {
    if (!g_refresh) {
        return;
    }
    if (g_refresh->ApplySourceFailure(generation, source) &&
        g_refresh->GenerationComplete(generation)) {
        OnGenerationCompleteRefresh();
    }
}

// NR-106: use the existing UI-owned delivery-failure handoff for a source
// that has no result payload (allocation or registry setup failed). StartRebuild
// reserves this vector for the current source batch, so the encoded message is
// only an OS-only fallback if an unexpected append failure still occurs.
void QueueRebuildSourceFailure(HWND window, std::uint64_t generation,
                               nimblerun::CatalogSource source) {
    bool recorded = false;
    try {
        std::lock_guard<std::mutex> lock(g_delivery_failure_mutex);
        g_rebuild_delivery_failures.emplace_back(generation, source);
        recorded = true;
    } catch (...) {
        if (g_diag) {
            g_diag->Write(L"rebuild", L"exception");
        }
    }
    if (recorded) {
        // NR-115: a queue-full PostMessageW failure must not strand the record --
        // the message loop's manual-reset event provides the reliable, event-driven
        // drain (no polling, no timer). SetEvent is thread-safe on a kernel object;
        // the UI drains under g_delivery_failure_mutex.
        if (!PostMessageW(window, kRebuildDeliveryFailedMessage, 0, 0) &&
            g_rebuild_failure_event != nullptr) {
            SetEvent(g_rebuild_failure_event);
        }
        return;
    }
    // No allocation is needed for this direct signal. The receiver validates
    // the small source enum before applying the UI-owned failure.
    if (!PostMessageW(window, kRebuildDeliveryFailedMessage,
                      static_cast<WPARAM>(generation),
                      static_cast<LPARAM>(source)) &&
        g_diag) {
        g_diag->Write(L"rebuild", L"exception");
    }
}

// A push_back failure is only handled after its just-started worker is joined.
// Remove any result/failure that worker may already have posted before the UI
// completes the source as a setup failure.
void DiscardPendingRebuildCompletion(std::uint64_t generation,
                                     nimblerun::CatalogSource source) {
    {
        std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
        for (auto it = g_rebuild_handoffs.begin(); it != g_rebuild_handoffs.end();) {
            if (it->second && it->second->generation == generation &&
                it->second->source == source) {
                it = g_rebuild_handoffs.erase(it);
            } else {
                ++it;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_delivery_failure_mutex);
        g_rebuild_delivery_failures.erase(
            std::remove_if(g_rebuild_delivery_failures.begin(),
                           g_rebuild_delivery_failures.end(),
                           [generation, source](const auto& failure) {
                               return failure.first == generation &&
                                      failure.second == source;
                           }),
            g_rebuild_delivery_failures.end());
    }
}

// NR-049: waits for every in-flight rebuild thread. The rebuild threads read
// g_settings and g_refresh, so this must run before anything that tears those
// down; WM_DESTROY is that point in practice, and each new rebuild cycle joins
// the previous one first. Rebuilds are debounced 500 ms apart (§FR-008), so a
// cycle's threads have almost always finished by the time this is called, and
// a finished thread returns from join() at once.
// NR-098: the cancellation flag is set BEFORE any join, so a scan still in
// flight (large or stuck directory, slow Shell extension) stops at its next
// safe iteration boundary instead of blocking the UI thread on join() until it
// finishes (design-spec §9.4). Reset only after every thread is joined so the
// next generation starts clean. StartRebuild and WM_DESTROY both use this one
// helper, so they share the same cancel→wait order.
// NR-123: the default INFINITE wait keeps StartRebuild's supersede semantics
// unchanged (a slow scan still stops at its next cancellation boundary,
// NR-098). WM_DESTROY passes a bounded timeout: a worker stuck inside a single
// uninterruptible Shell call (design-spec §16) must not hang shutdown forever.
// On timeout the survivors are detached and shutdown continues -- the process
// exits right after, so the OS reclaims them (NR-049's detach hazard only
// applies on a non-exit path), and a late kRebuildDoneMessage is ignored as an
// unknown token (NR-077).
// NR-123: how long WM_DESTROY waits for a worker stuck in an uninterruptible
// Shell call before giving up and continuing the shutdown.
constexpr DWORD kRebuildJoinTimeoutMs = 5000;
void JoinRebuildThreads(DWORD timeout_ms = INFINITE) {
    g_rebuild_cancel.store(true);
    bool all_finished = true;
    if (!g_rebuild_threads.empty()) {
        std::vector<HANDLE> handles;
        handles.reserve(g_rebuild_threads.size());
        for (std::thread& worker : g_rebuild_threads) {
            if (worker.joinable()) {
                handles.push_back(worker.native_handle());
            }
        }
        if (timeout_ms != INFINITE && !handles.empty()) {
            const DWORD wait_result = WaitForMultipleObjects(
                static_cast<DWORD>(handles.size()), handles.data(), TRUE, timeout_ms);
            all_finished = (wait_result == WAIT_OBJECT_0);
        }
    }
    if (all_finished) {
        for (std::thread& worker : g_rebuild_threads) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        g_rebuild_threads.clear();
        g_rebuild_cancel.store(false);
    } else {
        // NR-123: timeout (or a failed wait) -- at least one worker is still
        // inside an uninterruptible call. Detach it so destroying the vector
        // does not call std::terminate, then continue the shutdown; the OS
        // reclaims the thread when the process exits. Leave g_rebuild_cancel
        // set: the process is exiting, no next generation will run.
        for (std::thread& worker : g_rebuild_threads) {
            if (worker.joinable()) {
                worker.detach();
            }
        }
        g_rebuild_threads.clear();
    }
}

// NR-011: starts one background thread per source in `sources` for a rebuild
// cycle; results return through kRebuildDoneMessage on the UI thread.
void StartRebuild(HWND window, std::vector<nimblerun::CatalogSource> sources) {
    if (!g_refresh) {
        return;
    }
    // NR-049: join the previous cycle before starting a new one. Rebuilds are
    // debounced 500 ms apart (§FR-008) and a cycle's threads have almost
    // always finished by the time the next one starts, so this is normally a
    // no-op; when it is not, the join is bounded because JoinRebuildThreads
    // first signals the cooperative cancellation flag, so a slow scan stops at
    // its next safe iteration boundary instead of blocking the UI thread
    // (NR-098).
    JoinRebuildThreads();
    const std::uint64_t generation = g_refresh->BeginGeneration(sources);
    // NR-049: the rebuild threads must never touch g_settings. It is UI-thread
    // state, and the settings dialog can reassign it (freeing catalog_roots and
    // every root string) while a scan is halfway through a directory tree.
    // A Settings is an ordinary copyable value (AGENTS.md), so each thread gets
    // the snapshot that was current when the rebuild started -- which is also
    // the semantically correct input: a rebuild reflects the settings that
    // triggered it, not settings applied after it began.
    nimblerun::Settings settings_snapshot;
    try {
        settings_snapshot = g_settings;
        g_rebuild_threads.reserve(g_rebuild_threads.size() + sources.size());
        std::lock_guard<std::mutex> lock(g_delivery_failure_mutex);
        g_rebuild_delivery_failures.reserve(g_rebuild_delivery_failures.size() +
                                             sources.size());
    } catch (...) {
        // NR-106: no worker was started, so complete every source directly on
        // the UI-owned coordinator when setup storage itself cannot grow.
        if (g_diag) {
            g_diag->Write(L"rebuild", L"exception");
        }
        for (const nimblerun::CatalogSource source : sources) {
            CompleteRebuildSourceFailure(generation, source);
        }
        return;
    }
    for (const nimblerun::CatalogSource source : sources) {
        std::thread worker;
        try {
            worker = std::thread([window, generation, source, settings_snapshot]() {
            // NR-097: the allocation and the generation/source assignment are
            // inside the try so a heap-exhaustion failure is caught instead of
            // escaping the thread entry point.
            RebuildResult* result = nullptr;
            try {
                result = new RebuildResult;
                result->generation = generation;
                result->source = source;
                switch (source) {
                case nimblerun::CatalogSource::StartMenu: {
                    // NR-063: the enumerator reports source-level failure; the worker
                    // just forwards it so the coordinator keeps the old entries.
                    // NR-098: a cancelled scan also comes back as a source failure.
                    const auto res = nimblerun::EnumerateStartMenuCatalog(&g_rebuild_cancel);
                    result->failed = !res.source_ok;
                    result->entries = std::move(res.entries);
                    // NR-124: carry the corrupt-link count to the completion handler.
                    result->diagnostics.corrupt_links = res.corrupt_links;
                    break;
                }
                case nimblerun::CatalogSource::AppsFolder:
                    // NR-028: "Include Windows apps" off skips the enumeration
                    // entirely (no COM walk) and the source reports empty, so the
                    // merged snapshot clears old packaged-app entries via the same
                    // ApplySourceResult path.
                    if (settings_snapshot.include_windows_apps) {
                        const auto res = nimblerun::EnumerateAppsFolderCatalog(&g_rebuild_cancel);
                        result->failed = !res.source_ok;
                        result->entries = std::move(res.entries);
                    } else {
                        result->entries = std::vector<nimblerun::AppEntry>{};
                    }
                    break;
                case nimblerun::CatalogSource::UserFolder: {
                    // NR-092: the enumerator reports source-level failure when an
                    // open directory's walk fails mid-enumeration; the worker just
                    // forwards it so the coordinator keeps the old entries.
                    const auto res =
                        nimblerun::EnumerateUserFolderCatalog(settings_snapshot, &g_rebuild_cancel);
                    result->failed = !res.source_ok;
                    result->entries = std::move(res.entries);
                    // NR-124: carry the skipped-directory count to the completion handler.
                    result->diagnostics.skipped_directories = res.skipped_directories;
                    break;
                }
                }
            } catch (...) {
                if (result == nullptr) {
                    // NR-106: the result could not be allocated at all; queue
                    // the source failure without passing the coordinator to
                    // this worker.
                    if (g_diag) {
                        g_diag->Write(L"rebuild", L"exception");
                    }
                    QueueRebuildSourceFailure(window, generation, source);
                    return;
                }
                // NR-076: an allocation/enumeration exception must not terminate
                // the process (design-spec §11). Report a source failure so the
                // coordinator keeps this source's old entries (design-spec
                // §NFR-003); the enumerators' own failures already flow through
                // source_ok.
                result->failed = true;
                if (g_diag) {
                    g_diag->Write(L"rebuild", L"exception");
                }
            }
            // NR-077: hand the payload to the UI thread by token, never by a
            // raw pointer in a WM_APP message. The registry owns the object;
            // the receiver finds the token and moves it out. A full message
            // queue (PostMessageW fails) erases the token, which deletes the
            // object -- NR-063's leak guard.
            std::unique_ptr<RebuildResult> owned(result);
            bool registered = false;
            {
                std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
                try {
                    const auto [it, inserted] = g_rebuild_handoffs.emplace(
                        reinterpret_cast<std::uintptr_t>(result), std::move(owned));
                    registered = inserted;
                } catch (...) {
                }
            }
            if (!registered) {
                // NR-106: no registry token exists, so the owned guard releases
                // the result while the same UI-owned source failure is queued.
                if (g_diag) {
                    g_diag->Write(L"rebuild", L"exception");
                }
                QueueRebuildSourceFailure(window, generation, source);
                return;
            }
            if (!PostMessageW(window, kRebuildDoneMessage,
                              static_cast<WPARAM>(generation),
                              reinterpret_cast<LPARAM>(result))) {
                // NR-063's leak guard: the result never reaches the UI thread,
                // so erase the token, which deletes the payload.
                std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
                g_rebuild_handoffs.erase(reinterpret_cast<std::uintptr_t>(result));
                // NR-100: the UI thread must still complete this source for the
                // generation (ApplySourceFailure keeps its old entries). Record
                // it under its own mutex and send a payload-free wake-up.
                QueueRebuildSourceFailure(window, generation, source);
            }
            });
        } catch (...) {
            // NR-097: thread construction failed (std::system_error) on the UI
            // thread. No worker was started, so there is nothing to join; log
            // and skip this source for the generation.
            if (g_diag) {
                g_diag->Write(L"rebuild", L"exception");
            }
            CompleteRebuildSourceFailure(generation, source);
            continue;
        }
        try {
            g_rebuild_threads.push_back(std::move(worker));
        } catch (...) {
            // NR-097: a joinable std::thread that is destroyed = terminate.
            // push_back failed (bad_alloc), so join the just-started worker and
            // skip this source; the process survives.
            if (g_diag) {
                g_diag->Write(L"rebuild", L"exception");
            }
            worker.join();
            DiscardPendingRebuildCompletion(generation, source);
            CompleteRebuildSourceFailure(generation, source);
        }
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

// NR-045: the grid's per-cell digit boxes and the footer's Alt+1~N group are
// revealed only while Alt is physically down; the list state is unaffected.
// Queried per paint instead of tracked in a flag, so there is no stale state to
// clear on Alt+Tab, focus loss or panel hide.
bool AltHeld() { return GetKeyState(VK_MENU) < 0; }

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
    // NR-043: the label is centered in the full box rect (both axes come from
    // g_key_format) and drawn in the border color so the box reads as one
    // element. The box itself -- fill, border, position -- still carries the
    // hint, so this is not color-only signalling (design-spec §NFR-006).
    g_render_target->DrawText(
        label, static_cast<UINT32>(wcslen(label)), g_key_format, box_rect,
        g_dim_brush);
}

void Render(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);
    SyncAccessibility(window);

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
        // NR-133: the render target's DIP height is the client height that
        // drives the clamped FooterTopDip (NR-120) and, via SlotRect, the slot
        // geometry below.
        const D2D1_SIZE_F target_size = g_render_target->GetSize();
        if (g_model->Columns() > 1) {
            // NR-029: empty-query icon grid (design-spec §4.2/§4.9). Reuses the
            // model viewport state: visible cells are
            // FirstVisibleRow()..+ViewportRows()*Columns().
            const int first = g_model->FirstVisibleRow();
            const int columns = g_model->Columns();
            const int visible = g_model->ViewportRows() * columns;
            const int last = std::min(first + visible, static_cast<int>(rows.size()));
            // NR-046: take the drag permutation once, before the loop. Each slot
            // paints the row it maps to; the gap (-1) is the drop target.
            const std::vector<int> preview = DragPreviewOrder();
            const int pinned = PinnedRowCount();
            for (int i = first; i < last; ++i) {
                const int slot = i - first;
                const int row = (!preview.empty() && i < pinned)
                    ? preview[static_cast<std::size_t>(i)] : i;
                // NR-133: cell geometry is the shared SlotRect -- the single
                // definition of "where the Nth visible cell is".
                const nimblerun::layout::SlotRectDip cell_dip =
                    nimblerun::layout::SlotRect(slot, columns, target_size.height);
                const auto cell = D2D1::RectF(
                    cell_dip.left, cell_dip.top,
                    cell_dip.right, cell_dip.bottom);
                const float border_width = std::max(1.0f, dpi_x / nimblerun::layout::kDpi96);
                if (row == -1) {
                    // NR-046: the drop target -- a dashed rounded outline only,
                    // no fill, icon, name or digit box (the reflow leaves the
                    // surrounding cells in place, so geometry is untouched).
                    g_render_target->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(cell.left + 6.0f, cell.top + 6.0f,
                                                      cell.right - 6.0f, cell.bottom - 6.0f),
                                          nimblerun::layout::kSearchCornerRadiusDip,
                                          nimblerun::layout::kSearchCornerRadiusDip),
                        g_selected_border_brush, border_width, g_dash_style);
                    continue;
                }
                const bool selected =
                    g_model->HasSelection() &&
                    g_model->SelectionIndex() == static_cast<std::size_t>(row);
                // NR-046: while a drag is in progress the hover fill is frozen
                // (the hover index is cleared when the drag starts and is not
                // recomputed during it), so the reflow is not fighting a hover.
                const bool hovered = !g_dragging && g_grid_hover_index == row;
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
                    cell.left + (nimblerun::layout::kCellWidthDip - nimblerun::layout::kIconSizeDip) / 2.0f;
                const float icon_top = cell.top + 12.0f;
                const auto tile = D2D1::RectF(
                    icon_left, icon_top,
                    icon_left + nimblerun::layout::kIconSizeDip,
                    icon_top + nimblerun::layout::kIconSizeDip);
                if (nimblerun::PanelModel::IsMissingPin(rows[row])) {
                    DrawMissingPinTile(tile, dpi_x);
                } else {
                    DrawIconOrFallback(rows[row], tile, grid_icon_needed_px, dpi_x, dpi_y);
                }
                // NR-029: single-line centered name in the lower half. The grid
                // name format is NO_WRAP + character ellipsis (see the
                // SetTrimming setup), so name length never changes cell
                // geometry (design-spec §4.2).
                const auto name_rect = D2D1::RectF(
                    cell.left + 4.0f, cell.top + 56.0f,
                    cell.right - 4.0f, cell.bottom - 8.0f);
                g_render_target->DrawText(
                    rows[row].display_name.c_str(),
                    static_cast<UINT32>(rows[row].display_name.size()),
                    g_grid_name_format, name_rect, g_text_brush);

                // NR-029: NR-024 digit box at the cell's top-right corner for
                // the first 10 cells; the shared key-box paint is reused.
                // NR-045: in the grid state the box only paints while Alt is
                // down; it is an overlay that reserves no space, so hiding it
                // moves nothing (design-spec §4.9).
                if (AltHeld()) {
                    if (const wchar_t* key_label = nimblerun::ui::QuickSelectLabelForSlot(slot)) {
                        const float box_right = cell.right - 4.0f;
                        DrawKeyBox(
                            key_label,
                            D2D1::RectF(box_right - nimblerun::layout::kRowKeyBoxWidthDip,
                                        cell.top + 4.0f,
                                        box_right,
                                        cell.top + 4.0f + nimblerun::layout::kFooterKeyBoxHeightDip));
                    }
                }

                // NR-041: pinned marker -- a filled dot in the cell's top-left
                // corner. Drawn last so it sits above the selection border, and
                // placed on the left because the top-right corner is the NR-024
                // quick-select digit box. Shape, not color, carries the state
                // (design-spec §NFR-006); the border color is reused because the
                // palette already guarantees it contrasts with every fill and
                // follows the system colors under high contrast.
                if (g_pins && g_pins->IsPinned(rows[row].stable_id)) {
                    constexpr float kPinDotRadiusDip = 4.0f;
                    constexpr float kPinDotInsetDip = 8.0f;
                    g_render_target->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(cell.left + kPinDotInsetDip,
                                                    cell.top + kPinDotInsetDip),
                                      kPinDotRadiusDip, kPinDotRadiusDip),
                        g_selected_border_brush);
                }
            }

            // NR-046: the dragged item must remain visible under the cursor
            // (user requirement). Drawn after the cell loop so it sits above
            // every cell. Cache miss -> a dim square, so something always
            // follows the cursor; the icon request was already issued by the
            // cell paint. The row guard covers a catalog swap landing mid-drag.
            // NR-059: not a DrawIconOrFallback call site (dim square only on miss -- no initial, no re-request; the cell paint already asked).
            if (g_dragging && g_drag_row >= 0 &&
                static_cast<std::size_t>(g_drag_row) < rows.size()) {
                const float scale = dpi_x / nimblerun::layout::kDpi96;
                const auto ghost_rect = D2D1::RectF(
                    g_drag_cursor.x / scale - nimblerun::layout::kIconSizeDip / 2.0f,
                    g_drag_cursor.y / scale - nimblerun::layout::kIconSizeDip / 2.0f,
                    g_drag_cursor.x / scale + nimblerun::layout::kIconSizeDip / 2.0f,
                    g_drag_cursor.y / scale + nimblerun::layout::kIconSizeDip / 2.0f);
                const nimblerun::IconKey key = IconKeyFor(rows[g_drag_row], grid_icon_needed_px);
                const std::wstring encoded = key.Encode();
                if (const nimblerun::IconBitmap* icon = g_icon_cache ? g_icon_cache->Peek(encoded) : nullptr) {
                    DrawDecodedIcon(*icon, ghost_rect, dpi_x, dpi_y, 0.6f);
                } else {
                    g_render_target->FillRectangle(ghost_rect, g_dim_brush);
                }
            }

            if (rows.empty()) {
                DrawEmptyStateHint(nimblerun::layout::kCellHeightDip, /*searching=*/false);
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
                const nimblerun::layout::SlotRectDip row =
                    nimblerun::layout::SlotRect(i - first, 1, target_size.height);
                const auto row_rect = D2D1::RectF(
                    row.left, row.top, row.right, row.bottom);
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
                        D2D1::RectF(row.left, row.top,
                                    row.left + kPinStripeWidthDip, row.bottom),
                        g_selected_border_brush);
                }
                // NR-012: fixed tile inside the row, vertically centered. The decoded
                // icon (when cached) is drawn into the same rect the placeholder
                // occupies, so geometry is constant and a late-arriving icon never
                // reflows.
                const float tile_left =
                    nimblerun::layout::kListLeftDip + nimblerun::layout::kTileInsetDip;
                const float tile_top =
                    row.top + (row.bottom - row.top - nimblerun::layout::kTileSizeDip) / 2.0f;
                const auto tile = D2D1::RectF(
                    tile_left, tile_top,
                    tile_left + nimblerun::layout::kTileSizeDip,
                    tile_top + nimblerun::layout::kTileSizeDip);
                DrawIconOrFallback(rows[i], tile, layout.tile_size, dpi_x, dpi_y);
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
                const float row_mid = (row.top + row.bottom) / 2.0f;
                g_render_target->DrawText(
                    rows[i].display_name.c_str(),
                    static_cast<UINT32>(rows[i].display_name.size()),
                    g_text_format,
                    D2D1::RectF(text_left, row.top, text_right, row_mid),
                    g_text_brush);
                const std::wstring& subtitle =
                    nimblerun::IsDisplayablePath(rows[i].source_path)
                        ? rows[i].source_path
                        : windows_app_label;
                g_render_target->DrawText(
                    subtitle.c_str(),
                    static_cast<UINT32>(subtitle.size()),
                    g_small_format,
                    D2D1::RectF(text_left, row_mid, text_right, row.bottom),
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
            if (rows.empty()) {
                DrawEmptyStateHint(nimblerun::layout::kRowHeightDip, /*searching=*/true);
            }
        }
    }

    // NR-021 footer key-hint band (design-spec §4.9). A 1 DIP divider then a
    // right-aligned "Launch" group + "Scroll"/PgUp/PgDn group. Only key hints
    // live here; no status, version or update text. NR-024 adds the Launch
    // group whose box text depends on the current viewport row count.
    // NR-120: the band hugs the client bottom (FooterTopDip), so the path bar +
    // key hints stay visible when the panel is clamped below 488 DIP; a
    // full-height client keeps it exactly on kFooterTopDip as before.
    const D2D1_SIZE_F target_size = g_render_target->GetSize();
    const float footer_top = nimblerun::layout::FooterTopDip(target_size.height);
    g_render_target->DrawLine(
        D2D1::Point2F(0.0f, footer_top),
        D2D1::Point2F(nimblerun::layout::kPanelWidthDip, footer_top),
        g_dim_brush,
        nimblerun::layout::kFooterDividerWidthDip);

    const float footer_band_height =
        nimblerun::layout::kPanelHeightDip - nimblerun::layout::kFooterTopDip;
    const float footer_mid = footer_top + footer_band_height / 2.0f;
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

    // NR-043: draw_right_label measures the label and draws it ending at
    // `right`; `right` has to move past it too, or the next group to the left
    // (NR-024's Alt+1~N box) lands on top of the label -- which is what clipped
    // "Scroll" to "oll".
    right -= draw_right_label(footer_strings::kScroll, right);
    hints_left = std::min(hints_left, right);

    // NR-024: "Launch" group to the left of "Scroll", separated by the hint
    // gap. The wide box content is "Alt+1~" followed by the last digit bound
    // to the current viewport (8 visible rows -> Alt+1~8, >=10 -> Alt+1~0);
    // built per frame since the viewport can change.
    right -= nimblerun::layout::kFooterHintGapDip;
    if (g_model && g_model->Columns() > 1 && !AltHeld()) {
        // NR-045: in the grid state while Alt is up the per-cell digit boxes
        // are hidden, so the Alt+1~N group is replaced by one sentence that
        // says how to reveal them; drawn with the same draw_right_label the
        // rest of the group uses (design-spec §4.9).
        right -= draw_right_label(footer_strings::kHoldAltHint, right);
        hints_left = std::min(hints_left, right);
    } else {
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
        right -= draw_right_label(footer_strings::kLaunch, right);
        hints_left = std::min(hints_left, right);
    }

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
            const wchar_t* path = nimblerun::PanelModel::IsMissingPin(*path_entry)
                ? list_strings::kMissingApp
                : (nimblerun::IsDisplayablePath(path_entry->source_path)
                       ? path_entry->source_path.c_str()
                       : list_strings::kWindowsApp);
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
    // clamp it to the work area. Width/height stay 640x488 DIPs at any DPI, so
    // the same layout math gives predictable bounds at 100/150/200%.
    const RECT work_area = monitor_info.rcWork;
    // NR-103: park the (still hidden) window on the cursor monitor first so a
    // DPI change fires WM_DPICHANGED here and its suggested rect applies the
    // same DIP-size centering; GetDpiForWindow then reflects the cursor monitor
    // and stays the single per-window source that WM_DPICHANGED and every later
    // layout computation use, unlike the awareness-dependent GetDpiForMonitor.
    SetWindowPos(window, HWND_TOPMOST, work_area.left, work_area.top, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    const nimblerun::layout::WindowSize size = nimblerun::layout::ClampWindowSize(
        static_cast<float>(GetDpiForWindow(window)),
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
        // recent_count only drives the recent rows and the derived icon-cache
        // cap below, so it can follow the same "apply on next show" rule. The
        // catalog-source fields are deliberately not copied here: they are read
        // by rebuild workers and only change through a rebuild.
        g_settings.recent_count = current.recent_count;
    }
    // Clear the retained query first: the empty-query row build is a short pin
    // and recent walk, so the model refreshes below cost nothing, whereas with a
    // stale query each of them would re-run a full SearchApps on this warm-show
    // path (design-spec §11 p95 budget).
    if (g_model) {
        g_model->Reset();
    }
    // NR-018: reload pins on every open so restarts and external edits to
    // favorites.txt are reflected. Going through RefreshPanelSnapshot also
    // rebuilds the recent list, so a launch from this session's last open shows
    // up in Recent immediately instead of waiting for the next catalog rebuild.
    RefreshPanelSnapshot();
    // NR-031: derive the LRU cap from the live pin count + recent_count setting
    // + one grid page (design-spec §FR-009), so a search result never evicts
    // the prewarmed pins and forces a refetch on the next panel show.
    if (g_icon_cache) {
        g_icon_cache->SetMaxItems(nimblerun::IconCacheCapacityFor(
            g_pins ? g_pins->OrderedPins().size() : 0,
            g_settings.recent_count));
    }
    // NR-029: the grid hover index is a window-layer visual state; reset it for
    // this show and never leave a stale fill pointing at a previous session.
    // Leave-tracking is re-armed from the next mouse move (a hidden window may
    // swallow the WM_MOUSELEAVE that would otherwise clear the flag).
    g_grid_hover_index = -1;
    g_tracking_mouse_leave = false;
    // NR-046: same rule for the pinned-cell drag state; reset it for this show
    // so a drag that never got its mouse-up cannot leave a stale placeholder or
    // ghost behind (WM_CAPTURECHANGED covers the panel hiding mid-drag).
    g_drag_row = -1;
    g_drag_gap = -1;
    g_dragging = false;
    // NR-109: setup failures have no IconResult token to reach the normal
    // completion message; clear them before allowing this show to retry.
    ClearDroppedIconRequests();
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
    // NR-058: the tray icon's existence is what later NIM_MODIFY balloons key
    // off; record whether NIM_ADD actually landed.
    g_tray_icon_active = Shell_NotifyIconW(NIM_ADD, &nid);
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

// NR-058: single non-blocking balloon for startup store-load failures
// (design-spec §10.4/§11). Same NOTIFYICONDATAW info-balloon filling as
// ShowHotkeyConflictNotice. The caller clears the flags after sending, so a
// process shows at most one such balloon.
void ShowLoadIssueNotice(HWND window, const std::wstring& text) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = window;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    wcsncpy(nid.szInfoTitle, L"NimbleRun", sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) - 1);
    wcsncpy(nid.szInfo, text.c_str(),
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
// NR-078: shows the per-item context menu for the row at `cell` anchored at the
// given screen position (design-spec §4.8): Pin/Unpin, Remove from recent,
// Open file location and Properties. Shared by the right-click handler (hit
// cell) and the keyboard Context Menu / Shift+F10 path (selected cell) --
// §NFR-006 requires the keyboard to reach the same commands. The g_model/g_pins
// null guard lives here so both callers can call unconditionally.
void ShowItemMenu(HWND window, int cell, POINT screen_pos) {
    if (!g_model || !g_pins) {
        return;
    }
    const nimblerun::AppEntry entry = g_model->Rows()[static_cast<std::size_t>(cell)];
    const bool pinned = g_pins->IsPinned(entry.stable_id);

    const HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, pinned ? kCmdUnpin : kCmdPin,
                pinned ? context_menu_strings::kUnpin : context_menu_strings::kPin);
    // NR-062: a missing-pin placeholder's menu is Unpin only -- no Remove
    // from recent, no Open file location, no Properties. launch_identity is
    // empty on a placeholder, so IsPathIdentity below would already be
    // false, but an explicit early exit does not depend on that coincidence.
    if (!nimblerun::PanelModel::IsMissingPin(entry)) {
        // NR-040: only offered for rows actually showing in the recent region;
        // on a pinned row (which sits before RecentStartIndex) the command
        // would silently change nothing.
        const int recent_start = g_model->RecentStartIndex();
        const bool in_recent = recent_start >= 0 && cell >= recent_start
                               && cell < g_model->RecentEndIndex();
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
    }

    SetForegroundWindow(window);
    g_context_menu_active = true;
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screen_pos.x, screen_pos.y, 0, window, nullptr));
    g_context_menu_active = false;
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kCmdPin || command == kCmdUnpin) {
        if (pinned) {
            g_pins->Unpin(entry.stable_id);
        } else {
            g_pins->Pin(entry.stable_id, entry.display_name,
                        static_cast<std::int64_t>(std::time(nullptr)));
        }
        if (g_pins->Save()) {
            // The catalog's is_pinned stamp drives the §4.5 pinned-first
            // tie-break in search results, so it has to follow the store.
            StampRankingFields();
            // Refresh just the pin region: SetPins rebuilds the empty-query
            // rows so the entry moves into/out of the pinned region.
            g_model->SetPins(g_pins->Records());
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
}

// NR-078: opens the item menu for the currently selected row (Context Menu /
// Shift+F10; design-spec §4.7). No-op when the model is absent or the
// selection is out of range. The menu anchors at the selected row's top-left
// corner: the grid cell or list row rect is computed from the same
// LayoutForDpi geometry CellAtPoint uses, then converted to screen coordinates
// and clamped to the panel client rect.
void OpenKeyboardItemMenu(HWND window) {
    if (!g_model || g_model->Rows().empty()) {
        return;
    }
    const std::size_t sel = g_model->SelectionIndex();
    if (sel >= g_model->Rows().size()) {
        return;
    }
    const nimblerun::layout::LayoutPx layout =
        nimblerun::layout::LayoutForDpi(GetDpiForWindow(window));
    const int first = g_model->FirstVisibleRow();
    int rel = static_cast<int>(sel) - first;
    if (rel < 0) {
        rel = 0;
    }
    RECT client{};
    GetClientRect(window, &client);
    const float client_height_dip =
        static_cast<float>(std::max(0L, client.bottom - client.top)) / layout.scale;
    const nimblerun::layout::SlotRectDip slot = nimblerun::layout::SlotRect(
        rel, g_model->Columns(), client_height_dip);
    int left = static_cast<int>(std::lround(slot.left * layout.scale));
    int top = static_cast<int>(std::lround(slot.top * layout.scale));
    if (left < client.left) {
        left = client.left;
    }
    if (top < client.top) {
        top = client.top;
    }
    if (left > client.right) {
        left = client.right;
    }
    if (top > client.bottom) {
        top = client.bottom;
    }
    POINT screen_pos{left, top};
    ClientToScreen(window, &screen_pos);
    ShowItemMenu(window, static_cast<int>(sel), screen_pos);
}

LRESULT CALLBACK SearchEditProc(HWND edit, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_SETFOCUS: {
        // NR-023: force a solid caret after the default focus path. The native
        // caret can blend into the theme-colored input fill on dark themes; a
        // solid caret is drawn inverted against the background, so it stays
        // visible in both light and dark mode. No timers: the caret only blinks
        // via the system's own mechanism.
        //
        // Both CreateCaret arguments matter here: (HBITMAP)1 asks for a *gray*
        // caret -- the blending this override exists to avoid -- and a zero
        // height collapses the caret to the system window border height, which
        // is a 1-2 px stub. NULL gives the solid inverting caret, and the height
        // has to be stated: use the font's line height, not the client height,
        // which would also swallow the input box's vertical padding.
        //
        // CreateCaret destroys the caret the default handler just created and
        // positioned, and the replacement's position is undefined until it is
        // set, so carry the position across (the EDIT owns it from here on).
        const LRESULT result =
            CallWindowProcW(g_search_original_proc, edit, message, w_param, l_param);
        POINT caret{};
        GetCaretPos(&caret);
        TEXTMETRICW metrics{};
        int height = 0;
        if (const HDC dc = GetDC(edit)) {
            const HGDIOBJ previous =
                g_search_font ? SelectObject(dc, g_search_font) : nullptr;
            if (GetTextMetricsW(dc, &metrics)) {
                height = metrics.tmHeight;
            }
            if (previous) {
                SelectObject(dc, previous);
            }
            ReleaseDC(edit, dc);
        }
        if (height <= 0) {
            RECT client{};
            GetClientRect(edit, &client);
            height = client.bottom - client.top;
        }
        CreateCaret(edit, nullptr, 0, height);
        SetCaretPos(caret.x, caret.y);
        ShowCaret(edit);
        return result;
    }
    case WM_KILLFOCUS:
        // NR-023: mirror of WM_SETFOCUS; destroy the caret we created.
        CallWindowProcW(g_search_original_proc, edit, message, w_param, l_param);
        DestroyCaret();
        return 0;
    // NR-056: dragging the panel by its empty area is fine (see spec §4.1), but
    // the search box is a text field first. Forwarding its clicks to
    // HTCAPTION made mouse text selection impossible inside the one control the
    // user types into. Panel dragging stays available everywhere else.
    case WM_SYSKEYDOWN:
        // NR-045: repaint when Alt goes down so the grid digit boxes and the
        // footer sentence swap in; the auto-repeat bit (bit 30) guard keeps the
        // keyboard repeat rate from driving high-frequency repaints. Falls
        // through to the NR-024 digit handling below.
        if (w_param == VK_MENU && (l_param & (1 << 30)) == 0) {
            InvalidateRect(GetParent(edit), nullptr, FALSE);
            break;
        }
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
    case WM_SYSKEYUP:
    case WM_KEYUP:
        // NR-045: releasing Alt (WM_SYSKEYUP; a release that follows a
        // swallowed Alt+digit can arrive as WM_KEYUP) repaints the panel so the
        // grid hints and the footer group revert. No repeat guard needed: keyup
        // does not auto-repeat.
        if (w_param == VK_MENU) {
            InvalidateRect(GetParent(edit), nullptr, FALSE);
        }
        break;
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
                // NR-052: Esc's two-stage behavior lives in PanelModel::Esc()
                // (design-spec §4.7), but the model's query is a derived value
                // -- the EDIT control is what the user sees and types into.
                // Clearing only the model left the old text on screen under the
                // pinned/recent grid, and the next keystroke appended to it.
                // Clear the EDIT and let the existing EN_UPDATE path push the
                // empty query into the model, so there is still exactly one
                // route from typed text to query state.
                if (g_model->Esc()) {
                    HidePanel(GetParent(edit));
                } else {
                    SetWindowTextW(edit, L"");
                }
                return 0;
            case VK_APPS:
                // NR-078: the Context Menu key opens the item menu for the
                // selected row (design-spec §4.7). Swallowed so the EDIT never
                // shows its native clipboard menu; no-op when there is no
                // selection.
                OpenKeyboardItemMenu(GetParent(edit));
                return 0;
            case VK_F10:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    // NR-078: Shift+F10 is the keyboard alias for the Context
                    // Menu key. Plain F10 keeps its default (menu bar) handling.
                    OpenKeyboardItemMenu(GetParent(edit));
                    return 0;
                }
                break;
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
    if (message == WM_GETOBJECT && g_accessibility) {
        const LRESULT result = g_accessibility->OnGetObject(w_param, l_param);
        return result != 0 ? result : DefWindowProcW(window, message, w_param, l_param);
    }
    if (message == g_show_panel_message) {
        ShowPanel(window);
        if (g_test_show_semaphore) {
            ReleaseSemaphore(g_test_show_semaphore, 1, nullptr);
        }
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
            StartRebuild(window, {std::cbegin(nimblerun::kSources),
                                  std::cend(nimblerun::kSources)});
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
            const std::int64_t now = MonotonicMs();
            if (nimblerun::ShouldAcceptFullRescan(g_last_full_rescan_ms[source], now)) {
                // NR-130: the marker passed the storm throttle; it is due
                // immediately exactly as before.
                g_last_full_rescan_ms[source] = now;
                g_refresh->MarkSourceFullRescan(source);
                if (g_refresh->ShouldStartRebuild(now)) {
                    const std::vector<nimblerun::CatalogSource> due =
                        g_refresh->DueSources(now);
                    if (!due.empty()) {
                        StartRebuild(window, due);
                    }
                } else if (!g_refresh->DueSources(now).empty()) {
                    // NR-118: rebuild running; defer the full-rescan marker to the debounce
                    // timer so it is serviced once the current generation completes (the
                    // marker is always due per its kNever timestamp).
                    ScheduleDebouncedRebuild(window);
                }
            } else {
                // NR-130: throttled duplicate full-rescan marker from the same
                // source; merge into the existing debounce path instead of
                // forcing another immediate rebuild.
                g_refresh->NotifySourceEvent(source, now);
                ScheduleDebouncedRebuild(window);
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
        std::unique_ptr<RebuildResult> result;
        {
            // NR-077: lParam is an untrusted token, not a pointer. Move the
            // registered payload out; an unknown token (a same-integrity
            // process posting WM_APP+8) is ignored, never dereferenced.
            std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
            const auto it = g_rebuild_handoffs.find(static_cast<std::uintptr_t>(l_param));
            if (it == g_rebuild_handoffs.end()) {
                return 0;  // NR-077: not one of our handoffs
            }
            result = std::move(it->second);
            g_rebuild_handoffs.erase(it);
        }
        // NR-063: result_applied names what the boolean actually is -- this
        // source's outcome was accepted for the current generation -- not "the
        // whole generation finished". The launch-failure gate resets only when
        // every source in the cycle has reported.
        bool result_applied = false;
        if (result->failed) {
            result_applied =
                g_refresh->ApplySourceFailure(result->generation, result->source);
        } else {
            if (g_refresh->ApplySourceResult(result->generation, result->source,
                                             std::move(result->entries),
                                             result->diagnostics)) {
                result_applied = true;
                // NR-063: a successful source-ok enumeration refreshes the
                // AppsFolder staleness clock; a failure keeps it stale so the
                // 10-minute retry can run again (design-spec §FR-008).
                if (result->source == nimblerun::CatalogSource::AppsFolder) {
                    g_refresh->RecordAppsFolderSuccess(MonotonicMs());
                }
            }
        }
        if (result_applied && g_refresh->GenerationComplete(result->generation)) {
            OnGenerationCompleteRefresh();
        }
        // NR-100: a sibling source's result may have failed to post while this
        // one was delivered; drain those delivery failures. If that completes
        // the generation, run the same single completion refresh (NR-073 keeps
        // the snapshot swap at one choke point).
        if (DrainRebuildDeliveryFailures()) {
            OnGenerationCompleteRefresh();
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case kRebuildDeliveryFailedMessage:
        // NR-100: at least one rebuild source could not deliver its result via
        // kRebuildDoneMessage. Drain the recorded failures as source failures so
        // every source completes exactly once and the generation cannot stall
        // in IsRebuildInProgress() forever (payload-free wake-up).
        if (w_param != 0) {
            // NR-106: the failure queue's reserved capacity was unexpectedly
            // unavailable. The worker encoded only the generation and enum;
            // validate the enum before crossing into the coordinator.
            const auto source_value = static_cast<std::uintptr_t>(l_param);
            if (source_value <= static_cast<std::uintptr_t>(
                                   nimblerun::CatalogSource::UserFolder)) {
                CompleteRebuildSourceFailure(
                    static_cast<std::uint64_t>(w_param),
                    static_cast<nimblerun::CatalogSource>(source_value));
            }
        } else if (DrainRebuildDeliveryFailures()) {
            OnGenerationCompleteRefresh();
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (w_param == kRebuildTimerId) {
            KillTimer(window, kRebuildTimerId);
            if (g_refresh) {
                const std::int64_t now = MonotonicMs();
                if (g_refresh->ShouldStartRebuild(now)) {
                    StartRebuild(window, g_refresh->DueSources(now));
                } else if (!g_refresh->DueSources(now).empty()) {
                    // NR-118: due sources exist but a rebuild is running; a partial
                    // cycle must not supersede it (cold-start sources would be left
                    // unverified or dropped). Re-arm so the pending work is serviced
                    // by the next tick once the current generation completes.
                    SetTimer(window, kRebuildTimerId, 500, nullptr);
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
                                                       *g_usage, g_hotkey, g_log_directory,
                                                       g_diag);
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
                StartRebuild(window, {std::cbegin(nimblerun::kSources),
                                      std::cend(nimblerun::kSources)});
            }
        }
        return 0;
    }
    case kAboutMessage:
        // NR-056: design-spec §4.10 requires an About entry in the tray menu.
        // A menu item that does nothing is worse than no menu item: it reads as
        // a bug every time it is clicked. A MessageBox with the product name
        // and version satisfies the clause; anything more (icon, links, license
        // text) is not specced.
        ShowAboutDialog(window);
        return 0;
    case kExitMessage:
        DestroyWindow(window);
        return 0;
    case kIconReadyMessage: {
        if (w_param != 0) {
            ClearDroppedIconRequests();
            return 0;
        }
        // NR-032: one decoded icon finished on the worker thread. The heap
        // IconResult is owned by this window and deleted here; a failed load
        // still reports (empty bitmap) so the pending set is cleared and the
        // key is never re-requested this panel session.
        std::unique_ptr<nimblerun::IconResult> result;
        {
            // NR-077: lParam is an untrusted token, not a pointer. Move the
            // registered payload out; an unknown token (a same-integrity
            // process posting WM_APP+9) is ignored, never dereferenced.
            std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
            const auto it = nimblerun::g_icon_handoffs.find(static_cast<std::uintptr_t>(l_param));
            if (it == nimblerun::g_icon_handoffs.end()) {
                return 0;  // NR-077: not one of our handoffs
            }
            result = std::move(it->second);
            nimblerun::g_icon_handoffs.erase(it);
        }
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
        // NR-066: accumulate the raw delta and take whole notches out, keeping
        // the remainder for the next message. A precision touchpad sends a run
        // of 30-60-delta messages, so the panel stays scrollable on it while a
        // classic 120-multiple wheel behaves exactly as before.
        g_wheel_delta_carry += static_cast<int>(GET_WHEEL_DELTA_WPARAM(w_param));
        const int steps = g_wheel_delta_carry / WHEEL_DELTA;
        g_wheel_delta_carry %= WHEEL_DELTA;
        if (g_model && lines > 0 && steps != 0) {
            g_model->ScrollBy(-steps * static_cast<int>(lines));
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        // NR-046: a pinned-cell drag replaces the hover arm entirely -- the
        // ghost follows the cursor, the gap tracks the pinned region, and
        // nothing recomputes the hover fill until the gesture ends.
        if (g_drag_row >= 0) {
            const int x = GET_X_LPARAM(l_param);
            const int y = GET_Y_LPARAM(l_param);
            if (!g_dragging) {
                // Promote to a real drag once the press passes the system drag
                // threshold; the hover index is cleared so the pressed cell's
                // fill disappears with the reflow.
                if (std::abs(x - g_drag_origin.x) >= GetSystemMetrics(SM_CXDRAG) ||
                    std::abs(y - g_drag_origin.y) >= GetSystemMetrics(SM_CYDRAG)) {
                    g_dragging = true;
                    g_grid_hover_index = -1;
                }
            }
            if (g_dragging) {
                g_drag_cursor.x = x;
                g_drag_cursor.y = y;
                const int cell = CellAtPoint(window, x, y);
                g_drag_gap = (cell >= 0 && cell < PinnedRowCount()) ? cell : -1;
                // ponytail: the ghost follows the cursor, so every WM_MOUSEMOVE
                // during a drag invalidates the whole panel. The repaint rate is
                // bounded by the mouse message rate -- no timer, no busy loop --
                // and a drag is a brief, explicitly user-driven gesture. Narrow it
                // to the two dirty cell rects only if a drag ever measures as a
                // problem.
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
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
        if (g_model->Columns() > 1 && cell < PinnedRowCount()) {
            // NR-046: press on a pinned cell arms the drag -- capture, remember
            // the pressed row and its gap, and defer the launch until the
            // button is released without dragging. A drag threshold cannot
            // exist otherwise (design-spec §4.8).
            SetCapture(window);
            g_drag_row = cell;
            g_drag_gap = cell;
            g_dragging = false;
            g_drag_origin.x = GET_X_LPARAM(l_param);
            g_drag_origin.y = GET_Y_LPARAM(l_param);
            g_drag_cursor = g_drag_origin;
            return 0;
        }
        // Otherwise (recent cell, or list state): select and launch on press,
        // exactly as before.
        g_model->SelectRow(static_cast<std::size_t>(cell));
        ActivateRow(static_cast<std::size_t>(cell), window);
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_drag_row >= 0) {
            // NR-046: end the press. Copies of the drag state and the paint
            // permutation are taken BEFORE ReleaseCapture: the
            // WM_CAPTURECHANGED it triggers synchronously clears the globals,
            // so the arms below work from the copies and no arm can leave the
            // state half-set.
            const int row = g_drag_row;
            const int gap = g_drag_gap;
            const bool dragging = g_dragging;
            const std::vector<int> preview = DragPreviewOrder();
            ReleaseCapture();
            g_drag_row = -1;
            g_drag_gap = -1;
            g_dragging = false;
            if (!dragging) {
                // The press was a click (below the drag threshold): the launch
                // WM_LBUTTONDOWN deferred for the pinned region.
                g_model->SelectRow(static_cast<std::size_t>(row));
                ActivateRow(static_cast<std::size_t>(row), window);
            } else if (gap >= 0 && gap != row) {
                // Commit: rebuild the new pin order from the permutation, with
                // the gap replaced by the dragged row's own id, and persist it
                // through the store. A failed Save leaves the file untouched
                // and the view is simply not refreshed (NR-018's pattern).
                std::vector<std::wstring> order;
                order.reserve(preview.size());
                for (const int entry : preview) {
                    order.push_back(g_model->Rows()[static_cast<std::size_t>(
                        entry == -1 ? row : entry)].stable_id);
                }
                if (g_pins && g_pins->ReorderPresent(order) && g_pins->Save()) {
                    g_model->SetPins(g_pins->Records());
                }
            }
            // Else: dragging and dropped outside the pinned region (gap < 0)
            // or back on the same cell (gap == row) -> cancel, nothing is
            // written. The drop area below the grid is a miss, so no window
            // drag can start from here.
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_CAPTURECHANGED:
        // NR-046: capture lost (Alt+Tab, the panel hiding under the drag,
        // anything taking capture) ends the drag. This is the single escape
        // hatch; no WM_ACTIVATE or Esc bookkeeping is added.
        if (g_drag_row >= 0) {
            g_drag_row = -1;
            g_drag_gap = -1;
            g_dragging = false;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONDOWN: {
        // NR-018: right-click offers Pin/Unpin (per the item's current pinned
        // state) and "Open file location" for valid paths (design-spec §4.8).
        // CellAtPoint() returns -1 on its own when g_model is null, so it is
        // safe to compute before the g_model/g_pins check below.
        const int cell = CellAtPoint(window, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
        if (cell < 0) {
            // NR-060: right-clicking the panel's empty area (gaps, footer, the
            // area below the grid) offers the app-level commands. Settings
            // already exists behind the tray menu; before this the user had to
            // dismiss the panel to reach it. The search EDIT is deliberately not
            // covered -- design-spec §4.9 keeps its native clipboard menu.
            const HMENU menu = CreatePopupMenu();
            if (!menu) {
                return 0;
            }
            AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh Apps");
            AppendMenuW(menu, MF_STRING, kCmdSettings, L"Settings");
            AppendMenuW(menu, MF_STRING, kCmdAbout, L"About");

            POINT cursor{};
            GetCursorPos(&cursor);
            SetForegroundWindow(window);
            g_context_menu_active = true;
            const UINT command = static_cast<UINT>(TrackPopupMenu(
                menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, cursor.x, cursor.y, 0, window, nullptr));
            g_context_menu_active = false;
            PostMessageW(window, WM_NULL, 0, 0);
            DestroyMenu(menu);
            DispatchTrayCommand(window, command);
            return 0;
        }
        POINT cursor{};
        GetCursorPos(&cursor);
        // NR-078: the item branch lives in ShowItemMenu, shared with the
        // keyboard Context Menu / Shift+F10 path.
        ShowItemMenu(window, cell, cursor);
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
    case WM_ACTIVATE:
        // NR-085: ShowPanel puts focus on the search EDIT (SetFocus on the
        // child), so the panel itself never holds keyboard focus and the
        // WM_KILLFOCUS path above only fires for the EDIT -- which does not
        // tell its parent. "Click outside to hide" (design-spec §4.8) was
        // therefore dead on the two most common paths: show-then-click and
        // type-then-click. WM_ACTIVATE(WA_INACTIVE) is the single fact that
        // the panel was deactivated and covers every outside click while the
        // EDIT has focus; the same two modal flags as WM_KILLFOCUS exempt the
        // context menu and the launch-failure dialog.
        if (w_param == WA_INACTIVE && !g_context_menu_active && !g_dialog_active) {
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
            // NR-077: the drained payloads are owned by g_icon_handoffs; the
            // PeekMessageW loop only removes the queued messages, and the
            // registry clear below frees every in-flight object exactly once.
            MSG leftover{};
            while (PeekMessageW(&leftover, window, kIconReadyMessage,
                                kIconReadyMessage, PM_REMOVE)) {
            }
        }
        // NR-049: join the rebuild threads now, after the icon worker is
        // stopped but before anything that could tear down g_refresh or
        // g_settings (they live past the message loop, and the worker reads
        // both). Ordering matters: a scan that outlived the window would post
        // into a dead HWND and read globals mid-destruction.
        // NR-123: the wait is bounded. A worker stuck inside a single
        // uninterruptible Shell call must not hang shutdown (design-spec
        // §9.4); on timeout JoinRebuildThreads detaches it and we continue --
        // the process is exiting, so the OS reclaims the thread, and the
        // registry clear below frees every in-flight payload exactly once.
        JoinRebuildThreads(kRebuildJoinTimeoutMs);
        // NR-049: a thread can post its result microseconds before we join it,
        // so drain the queue and delete the payloads. Mirrors the existing
        // kIconReadyMessage drain directly above; without it every shutdown
        // during a rebuild leaks one RebuildResult per source.
        {
            // NR-077: as above, the rebuild payloads are owned by the registry;
            // the drain only removes the messages and the clear frees them.
            MSG leftover{};
            while (PeekMessageW(&leftover, window, kRebuildDoneMessage,
                                kRebuildDoneMessage, PM_REMOVE)) {
            }
        }
        // NR-077: everything still in flight (registered but not yet handled)
        // is released here, in the same UI thread that registered ownership
        // semantics; workers are all joined above so nothing can insert after.
        {
            std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
            nimblerun::g_icon_handoffs.clear();
            g_rebuild_handoffs.clear();
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

    HANDLE startup_ready = CreateEventW(nullptr, TRUE, FALSE, kStartupReadyEvent);
    if (!startup_ready) {
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!mutex) {
        CloseHandle(startup_ready);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (!existing &&
            WaitForSingleObject(startup_ready, kStartupRendezvousTimeoutMs) ==
                WAIT_OBJECT_0) {
            existing = FindWindowW(kWindowClass, nullptr);
        }
        if (existing) {
            PostMessageW(existing, g_show_panel_message, 0, 0);
        } else {
            // NR-130: the mutex is held but the window could not be reached
            // within the rendezvous window. Give the user feedback instead of
            // silently exiting; never steal the mutex and continue, which would
            // reopen NR-110's dual-instance startup race.
            MessageBoxW(nullptr, dialog_strings::kRendezvousTimeout,
                        dialog_strings::kTitle, MB_OK | MB_ICONWARNING);
        }
        CloseHandle(mutex);
        CloseHandle(startup_ready);
        return 0;
    }

    const HRESULT com_result = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com_result)) {
        CloseHandle(startup_ready);
        CloseHandle(mutex);
        return 1;
    }

    if (!RegisterMainWindow(instance)) {
        CoUninitialize();
        CloseHandle(startup_ready);
        CloseHandle(mutex);
        return 1;
    }

    WaitForStartupTestGate();
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
        CloseHandle(startup_ready);
        CloseHandle(mutex);
        return 1;
    }
    g_main_window = window;
    g_accessibility = nimblerun::PanelAccessibilityProvider::Create(window);
    SyncAccessibility(window);
    SetEvent(startup_ready);
    OpenTestShowSemaphore();

    // NR-044: let DWM round the panel's corners so it matches the Windows 11
    // flyouts and the panel's own 6 DIP search box (design-spec §4.9). The
    // attribute is composited by DWM -- no region, no layered window, no
    // per-frame cost, and it rounds the WS_BORDER frame and the system shadow
    // with it. Windows 10 does not know attribute 33: the call fails with
    // E_INVALIDARG and the panel stays square there, which NR-044 accepts. No
    // version probe and no fallback path, so the result is deliberately ignored.
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                          sizeof(corner));

    // NR-010/NR-011: load settings + usage, then serve the catalog from a valid
    // cache immediately while the full build runs in the background. The panel
    // model points at the coordinator's live snapshot, which swaps atomically
    // only when every source in a generation has reported.
    g_user_data_directory = nimblerun::DefaultSettingsDir();
    const bool persistence_available = !g_user_data_directory.empty();
    const std::wstring& data_directory = g_user_data_directory;
    nimblerun::Settings settings = nimblerun::DefaultSettings();
    nimblerun::SettingsStore settings_store(data_directory);
    // NR-058: the result is kept until the diagnostic log exists; the flags and
    // the log line are handled after g_diag is created below.
    const nimblerun::SettingsLoadResult settings_result =
        persistence_available ? settings_store.Load(settings)
                               : nimblerun::SettingsLoadResult::Missing;
    g_settings_store = &settings_store;
    g_settings = settings;
    g_hide_after_launch = settings.hide_after_launch;
    g_theme = settings.theme;

    nimblerun::CatalogRefreshCoordinator refresh;
    g_refresh = &refresh;
    std::vector<nimblerun::AppEntry> cached;
    bool cache_newer = false;
    if (persistence_available &&
        nimblerun::LoadCatalogCache(data_directory, cached, &cache_newer)) {
        refresh.SetSnapshot(std::move(cached));
        // NR-116: seed the per-source old entries from the startup cache, so a
        // source that fails the FIRST rebuild keeps its cached rows instead of
        // dropping them from the snapshot and wiping its usage records (§FR-008).
        refresh.SeedSourceEntriesFromSnapshot();
    }
    // NR-079: a newer-schema file on disk must not be overwritten by this build
    // (design-spec §10.4); the flag stays set for the whole run.
    g_catalog_cache_disable_writes = !persistence_available || cache_newer;

    nimblerun::UsageStore usage(data_directory);
    const nimblerun::UsageLoadResult usage_result =
        persistence_available ? usage.Load() : nimblerun::UsageLoadResult::Missing;
    g_usage = &usage;

    // NR-054: design-spec §10.1 puts the log at logs\nimblerun.log, not beside
    // settings.ini in the root. Keeps the diagnostics artifact out of the
    // user-data listing, so "delete my data" and "send me your log" are
    // different directories.
    g_log_directory = persistence_available
        ? nimblerun::JoinPath(data_directory, L"logs") : std::wstring{};
    nimblerun::DiagnosticLog diag(g_log_directory, L"nimblerun.log");
    g_diag = &diag;

    // NR-058: settings/usage load failures surface now that the log exists (the
    // log is created after those loads; the initialization order is unchanged).
    // Every non-Loaded result gets one line -- Missing included, since "this
    // file never existed" is exactly what triage wants to know. The balloon
    // itself waits until the tray icon exists.
    if (settings_result != nimblerun::SettingsLoadResult::Loaded) {
        g_store_load_issues |= StoreLoadIssueFor(settings_result);
        LogStoreLoad(L"settings_load", StoreLoadResultName(settings_result));
    }
    if (usage_result != nimblerun::UsageLoadResult::Loaded) {
        g_store_load_issues |= StoreLoadIssueFor(usage_result);
        LogStoreLoad(L"usage_load", StoreLoadResultName(usage_result));
    }

    // NR-018: the pin store is reloaded and reconciled in RefreshPins(); only
    // the (non-owning) pointer is kept so the model and host share one store.
    nimblerun::PinStore pins(data_directory);
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
    if (persistence_available) {
        icon_store_paths.pack = std::filesystem::path(data_directory) / L"icons.cache";
    }
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
        if (g_diag) {
            g_diag->Write(L"hotkey-register",
                          L"error " + std::to_wstring(hotkey_result.error));
        }
        ShowHotkeyConflictNotice(window);
    }
    // NR-058: the tray icon is in place (AddTrayIcon above ran NIM_ADD on this
    // HWND/uID), so any startup store-load failure -- settings, usage, or a pin
    // issue detected by the startup RefreshPanelSnapshot -- surfaces now as one
    // balloon. Sent after the hotkey notice so it is not the one clobbered if
    // both fire. Cleared afterwards: a process notifies at most once.
    if (g_store_load_issues != 0) {
        const std::wstring text = nimblerun::StoreLoadNoticeText(g_store_load_issues);
        if (!text.empty()) {
            ShowLoadIssueNotice(window, text);
        }
        g_store_load_issues = 0;
    }

    // NR-115: the main loop waits on a manual-reset event alongside the message
    // queue, so a recorded delivery failure is drained even when its
    // PostMessageW wake-up failed (queue full). Created before the first rebuild
    // can queue a failure; closed after WM_DESTROY joined every worker, so no
    // worker can SetEvent or append after the handle dies.
    g_rebuild_failure_event =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // A null event degrades gracefully: the loop below behaves exactly like the
    // previous plain GetMessageW loop and SetEvent stays guarded by the null check.

    // NR-011: kick off the background full rebuild now that the panel can serve
    // the cached snapshot; the results arrive through kRebuildDoneMessage.
    if (g_refresh) {
        StartRebuild(window, {std::cbegin(nimblerun::kSources),
                              std::cend(nimblerun::kSources)});
    }

    MSG message{};
    for (;;) {
        // NR-115: wait on the delivery-failure event as well as the message queue,
        // so a recorded failure is drained even when its PostMessageW wake-up
        // failed (queue full). Event-driven: no polling, no timer.
        DWORD wait_result = WAIT_FAILED;
        if (g_rebuild_failure_event != nullptr) {
            wait_result = MsgWaitForMultipleObjectsEx(
                1, &g_rebuild_failure_event, INFINITE, QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
        }
        if (g_rebuild_failure_event != nullptr &&
            wait_result == WAIT_OBJECT_0) {
            // A recorded delivery failure woke the loop. Drain under the UI-owned
            // coordinator; a complete generation refreshes the panel and cache
            // exactly once (OnGenerationCompleteRefresh). InvalidateRect mirrors
            // the kRebuildDeliveryFailedMessage case's single repaint.
            if (DrainRebuildDeliveryFailures()) {
                OnGenerationCompleteRefresh();
            }
            InvalidateRect(window, nullptr, FALSE);
            // The event is level-triggered: reset only when no records remain, so a
            // SetEvent racing the drain is never lost. New records after the reset
            // re-signal it and the next wait returns immediately.
            std::lock_guard<std::mutex> lock(g_delivery_failure_mutex);
            if (g_rebuild_delivery_failures.empty()) {
                ResetEvent(g_rebuild_failure_event);
            }
            continue;
        }
        const int get_result = GetMessageW(&message, nullptr, 0, 0);
        if (get_result == -1 && g_diag) {
            // NR-117: a message retrieval error (GetMessageW == -1) has no MSG to
            // dispatch; record it and shut down like the WM_QUIT path instead of
            // dispatching an undefined MSG.
            g_diag->Write(L"ui-loop", L"getmessage-error");
        }
        if (!nimblerun::ShouldDispatchMessage(get_result)) {
            break;  // 0 = WM_QUIT, -1 = retrieval error; neither has a dispatchable MSG
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DiscardDeviceResources();
    Release(g_title_format);
    Release(g_text_format);
    Release(g_small_format);
    Release(g_grid_name_format);
    Release(g_key_format);
    Release(g_ellipsis_sign);
    Release(g_write_factory);
    Release(g_dash_style);
    Release(g_d2d_factory);
    if (g_accessibility) {
        auto* provider = g_accessibility;
        g_accessibility = nullptr;
        provider->Release();
    }
    if (g_test_show_semaphore) {
        CloseHandle(g_test_show_semaphore);
        g_test_show_semaphore = nullptr;
    }
    CoUninitialize();
    if (g_rebuild_failure_event != nullptr) {
        CloseHandle(g_rebuild_failure_event);
        g_rebuild_failure_event = nullptr;
    }
    CloseHandle(startup_ready);
    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
