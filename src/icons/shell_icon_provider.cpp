#include "icons/shell_icon_provider.h"

#include <windows.h>
#include <shobjidl.h>

namespace nimblerun {
namespace {

// Converts an HBITMAP into a 32bpp premultiplied BGRA IconBitmap at its natural
// size (GetImage already returned the requested size). Returns an empty bitmap
// on any conversion failure.
IconBitmap BitmapFromHBITMAP(HBITMAP bitmap) {
    IconBitmap out;
    BITMAP info{};
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0) {
        return out;
    }

    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = info.bmWidth;
    dib.bmiHeader.biHeight = -info.bmHeight;  // top-down rows
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;

    out.width = static_cast<std::uint32_t>(info.bmWidth);
    out.height = static_cast<std::uint32_t>(info.bmHeight);
    out.pixels.assign(static_cast<std::size_t>(out.width) * out.height, 0);

    const HDC dc = GetDC(nullptr);
    const int rows = GetDIBits(dc, bitmap, 0, info.bmHeight, out.pixels.data(), &dib, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (rows != info.bmHeight) {
        return {};
    }

    // GetDIBits from a device-dependent bitmap may leave the alpha byte zero
    // even when the image is meant to be opaque. Icons from GetImage carry
    // alpha; when no pixel has any, treat the whole image as opaque.
    bool has_alpha = false;
    for (const std::uint32_t value : out.pixels) {
        if ((value >> 24) != 0) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha) {
        for (std::uint32_t& value : out.pixels) {
            value |= 0xFF000000u;
        }
    }

    // Direct2D bitmaps expect premultiplied alpha.
    for (std::uint32_t& value : out.pixels) {
        const std::uint32_t a = (value >> 24) & 0xFF;
        if (a == 0) {
            value = 0;
        } else if (a != 255) {
            const std::uint32_t b = ((value & 0xFF) * a) / 255;
            const std::uint32_t g = (((value >> 8) & 0xFF) * a) / 255;
            const std::uint32_t r = (((value >> 16) & 0xFF) * a) / 255;
            value = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return out;
}

} // namespace

IconBitmap ShellIconProvider::Load(const AppEntry& entry, const IconKey& key) {
    if (entry.launch_identity.empty() || key.variant <= 0) {
        return {};
    }

    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(entry.launch_identity.c_str(), nullptr,
                                           IID_PPV_ARGS(&factory)))) {
        return {};
    }

    HBITMAP hbitmap = nullptr;
    const HRESULT hr =
        factory->GetImage(SIZE{key.variant, key.variant}, SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT, &hbitmap);
    factory->Release();
    if (FAILED(hr) || hbitmap == nullptr) {
        return {};
    }

    IconBitmap out = BitmapFromHBITMAP(hbitmap);
    DeleteObject(hbitmap);
    return out;
}

} // namespace nimblerun
