#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"NimbleRun.Phase0Probe";
constexpr wchar_t kWindowTitle[] = L"NimbleRun";
constexpr wchar_t kInstanceMutex[] = L"Local\\NimbleRun.SingleInstance";
constexpr int kHotkeyId = 1;
constexpr UINT kShowWindowMessage = WM_APP + 1;

ID2D1Factory* g_d2d_factory = nullptr;
ID2D1HwndRenderTarget* g_render_target = nullptr;
ID2D1SolidColorBrush* g_title_brush = nullptr;
ID2D1SolidColorBrush* g_text_brush = nullptr;
ID2D1SolidColorBrush* g_card_brush = nullptr;
IDWriteFactory* g_write_factory = nullptr;
IDWriteTextFormat* g_title_format = nullptr;
IDWriteTextFormat* g_text_format = nullptr;

template <typename T>
void Release(T*& resource) {
    if (resource) {
        resource->Release();
        resource = nullptr;
    }
}

void DiscardDeviceResources() {
    Release(g_render_target);
    Release(g_title_brush);
    Release(g_text_brush);
    Release(g_card_brush);
}

bool CreateDeviceResources(HWND window) {
    if (g_render_target && g_title_brush && g_text_brush && g_card_brush &&
        g_title_format && g_text_format) {
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

    if (FAILED(g_write_factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            24.0f,
            L"en-US",
            &g_title_format))) {
        return false;
    }
    if (FAILED(g_write_factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-US",
            &g_text_format))) {
        return false;
    }

    return SUCCEEDED(g_render_target->CreateSolidColorBrush(
               D2D1::ColorF(D2D1::ColorF::White),
               &g_title_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(0xD0D0D0),
            &g_text_brush)) &&
        SUCCEEDED(g_render_target->CreateSolidColorBrush(
            D2D1::ColorF(0x2B2B2B),
            &g_card_brush));
}

void Render(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);

    if (!CreateDeviceResources(window)) {
        EndPaint(window, &paint);
        return;
    }

    g_render_target->BeginDraw();
    g_render_target->Clear(D2D1::ColorF(0x181818));

    const auto title_rect = D2D1::RectF(24.0f, 18.0f, 616.0f, 54.0f);
    g_render_target->DrawText(
        L"NimbleRun",
        9,
        g_title_format,
        title_rect,
        g_title_brush);

    const auto hint_rect = D2D1::RectF(24.0f, 56.0f, 616.0f, 78.0f);
    g_render_target->DrawText(
        L"Phase 0 probe - press Alt+Space to toggle",
        43,
        g_text_format,
        hint_rect,
        g_text_brush);

    for (int index = 0; index < 20; ++index) {
        const float left = 24.0f + static_cast<float>(index % 5) * 118.0f;
        const float top = 96.0f + static_cast<float>(index / 5) * 70.0f;
        g_render_target->FillRectangle(
            D2D1::RectF(left, top, left + 104.0f, top + 56.0f),
            g_card_brush);

        const std::wstring label = L"App " + std::to_wstring(index + 1);
        g_render_target->DrawText(
            label.c_str(),
            static_cast<UINT32>(label.size()),
            g_text_format,
            D2D1::RectF(left + 12.0f, top + 18.0f, left + 92.0f, top + 42.0f),
            g_text_brush);
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

    const RECT work_area = monitor_info.rcWork;
    const int width = std::min(640, static_cast<int>(work_area.right - work_area.left - 32));
    const int height = std::min(420, static_cast<int>(work_area.bottom - work_area.top - 32));
    const int left = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    const int top = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;

    SetWindowPos(window, HWND_TOPMOST, left, top, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    SetFocus(window);
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case kShowWindowMessage:
        ShowPanel(window);
        return 0;
    case WM_HOTKEY:
        if (w_param == kHotkeyId) {
            if (IsWindowVisible(window)) {
                ShowWindow(window, SW_HIDE);
            } else {
                ShowPanel(window);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            ShowWindow(window, SW_HIDE);
        }
        return 0;
    case WM_KILLFOCUS:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_PAINT:
        Render(window);
        return 0;
    case WM_SIZE:
        if (g_render_target) {
            g_render_target->Resize(D2D1::SizeU(LOWORD(l_param), HIWORD(l_param)));
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(window, kHotkeyId);
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
    return RegisterClassExW(&window_class) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND existing = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(existing, kShowWindowMessage, 0, 0);
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
        WS_POPUP | WS_BORDER,
        0,
        0,
        640,
        420,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window || !RegisterHotKey(window, kHotkeyId, MOD_ALT | MOD_NOREPEAT, VK_SPACE)) {
        if (window) {
            DestroyWindow(window);
        }
        CoUninitialize();
        CloseHandle(mutex);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DiscardDeviceResources();
    Release(g_title_format);
    Release(g_text_format);
    Release(g_write_factory);
    Release(g_d2d_factory);
    CoUninitialize();
    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
