#include "icons/png_codec.h"

#include "icons/icon_pack_format.h"

#include "win/com.h"

#include <windows.h>
#include <ole2.h>
#include <wincodec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace nimblerun {
namespace {

// Bounds shared by encode and decode: larger than the largest variant (256)
// by a wide margin, yet small enough that a corrupted size field cannot ask
// for a huge allocation.
constexpr std::uint32_t kMaxIconSize = 1024;

// PNG layout: 8-byte signature, then chunks of 4-byte big-endian length,
// 4-byte type, length data bytes, 4-byte CRC over type + data. WIC's PNG
// decoder does not validate these CRCs -- a single corrupted byte in the
// middle of a payload decodes "successfully" to garbage -- so validate them
// here (reusing the pack format's Crc32, the same IEEE 802.3 CRC-32 PNG uses)
// to make the "empty on corrupt input" contract hold. The icons.cache pack
// layer also CRC-checks each payload (NR-033); this is defense in depth for
// payloads decoded without going through that check.
bool PngChunkCrcsValid(const std::uint8_t* data, std::size_t size) {
    static const std::uint8_t kSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < sizeof(kSignature) + 12 ||
        std::memcmp(data, kSignature, sizeof(kSignature)) != 0) {
        return false;
    }
    std::size_t pos = sizeof(kSignature);
    while (pos < size) {
        if (size - pos < 12) {
            return false;  // trailing bytes with no room for a chunk header
        }
        const std::uint32_t len =
            (static_cast<std::uint32_t>(data[pos]) << 24) |
            (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
            (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
            static_cast<std::uint32_t>(data[pos + 3]);
        const std::size_t chunk_end = pos + 12 + len;
        if (chunk_end > size) {
            return false;  // declared length runs past the end (truncation)
        }
        const std::uint32_t stored_crc =
            (static_cast<std::uint32_t>(data[chunk_end - 4]) << 24) |
            (static_cast<std::uint32_t>(data[chunk_end - 3]) << 16) |
            (static_cast<std::uint32_t>(data[chunk_end - 2]) << 8) |
            static_cast<std::uint32_t>(data[chunk_end - 1]);
        if (Crc32(data + pos + 4, 4 + len) != stored_crc) {
            return false;
        }
        pos = chunk_end;
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> EncodeIconPng(const IconBitmap& bitmap) {
    std::vector<std::uint8_t> out;
    if (bitmap.Empty() || bitmap.width > kMaxIconSize || bitmap.height > kMaxIconSize ||
        bitmap.pixels.size() != static_cast<std::size_t>(bitmap.width) * bitmap.height) {
        return out;
    }

    // ponytail: CoCreateInstance per call (tens of microseconds, on the worker
    // thread). If measurement ever shows it matters, hoist to a worker
    // thread-local factory; the caller's thread owns COM init.
    IWICImagingFactory* factory_raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory_raw)))) {
        return out;
    }
    std::unique_ptr<IWICImagingFactory, ComRelease> factory(factory_raw);

    // Memory-backed stream the encoder writes into; read back after Commit.
    IStream* hglobal_raw = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &hglobal_raw)) || hglobal_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IStream, ComRelease> hglobal(hglobal_raw);

    IWICStream* wic_stream_raw = nullptr;
    if (FAILED(factory->CreateStream(&wic_stream_raw)) || wic_stream_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IWICStream, ComRelease> wic_stream(wic_stream_raw);
    if (FAILED(wic_stream->InitializeFromIStream(hglobal.get()))) {
        return out;
    }

    IWICBitmapEncoder* encoder_raw = nullptr;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder_raw)) ||
        encoder_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IWICBitmapEncoder, ComRelease> encoder(encoder_raw);
    if (FAILED(encoder->Initialize(wic_stream.get(), WICBitmapEncoderNoCache))) {
        return out;
    }

    IWICBitmapFrameEncode* frame_raw = nullptr;
    IPropertyBag2* props_raw = nullptr;
    if (FAILED(encoder->CreateNewFrame(&frame_raw, &props_raw)) || frame_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IWICBitmapFrameEncode, ComRelease> frame(frame_raw);
    std::unique_ptr<IPropertyBag2, ComRelease> props(props_raw);
    if (FAILED(frame->Initialize(props.get()))) {
        return out;
    }

    if (FAILED(frame->SetSize(bitmap.width, bitmap.height))) {
        return out;
    }
    // PNG stores straight alpha; the default encoder settings are fine.
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) {
        return out;
    }

    IWICBitmap* src_raw = nullptr;
    if (FAILED(factory->CreateBitmapFromMemory(
            bitmap.width, bitmap.height, GUID_WICPixelFormat32bppPBGRA,
            bitmap.width * 4, static_cast<UINT>(bitmap.pixels.size() * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(bitmap.pixels.data())),
            &src_raw)) || src_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IWICBitmap, ComRelease> src(src_raw);

    // Premultiplied -> straight is an unpremultiply. Let WIC do it rather than
    // reimplementing the rounding (NR-034: a hand-rolled loop disagrees with
    // WIC on a == 0 and rounding, so the round-trip tests would drift by one).
    IWICFormatConverter* converter_raw = nullptr;
    if (FAILED(factory->CreateFormatConverter(&converter_raw)) || converter_raw == nullptr) {
        return out;
    }
    std::unique_ptr<IWICFormatConverter, ComRelease> converter(converter_raw);
    if (FAILED(converter->Initialize(src.get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return out;
    }

    if (FAILED(frame->WriteSource(converter.get(), nullptr)) ||
        FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        return out;
    }

    // Read the encoded bytes back out of the memory stream.
    STATSTG stat{};
    if (FAILED(hglobal->Stat(&stat, STATFLAG_NONAME)) ||
        stat.cbSize.HighPart != 0 || stat.cbSize.LowPart == 0) {
        return out;
    }
    out.resize(stat.cbSize.LowPart);
    LARGE_INTEGER zero{};
    if (FAILED(hglobal->Seek(zero, STREAM_SEEK_SET, nullptr))) {
        out.clear();
        return out;
    }
    ULONG read = 0;
    if (FAILED(hglobal->Read(out.data(), stat.cbSize.LowPart, &read)) ||
        read != stat.cbSize.LowPart) {
        out.clear();
    }
    return out;
}

IconBitmap DecodeIconPng(const std::uint8_t* data, std::size_t size, int expected_size) {
    if (data == nullptr || size == 0 ||
        size > static_cast<std::size_t>(std::numeric_limits<UINT>::max()) ||
        !PngChunkCrcsValid(data, size)) {
        return {};
    }

    IWICImagingFactory* factory_raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory_raw)))) {
        return {};
    }
    std::unique_ptr<IWICImagingFactory, ComRelease> factory(factory_raw);

    IWICStream* stream_raw = nullptr;
    if (FAILED(factory->CreateStream(&stream_raw)) || stream_raw == nullptr) {
        return {};
    }
    std::unique_ptr<IWICStream, ComRelease> stream(stream_raw);
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data),
                                            static_cast<UINT>(size)))) {
        return {};
    }

    IWICBitmapDecoder* decoder_raw = nullptr;
    if (FAILED(factory->CreateDecoderFromStream(
            stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder_raw)) ||
        decoder_raw == nullptr) {
        return {};
    }
    std::unique_ptr<IWICBitmapDecoder, ComRelease> decoder(decoder_raw);

    UINT frame_count = 0;
    if (FAILED(decoder->GetFrameCount(&frame_count)) || frame_count == 0) {
        return {};
    }

    IWICBitmapFrameDecode* frame_raw = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame_raw)) || frame_raw == nullptr) {
        return {};
    }
    std::unique_ptr<IWICBitmapFrameDecode, ComRelease> frame(frame_raw);

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) ||
        width == 0 || height == 0 || width > kMaxIconSize || height > kMaxIconSize) {
        return {};
    }
    if (expected_size > 0 &&
        (static_cast<int>(width) != expected_size || static_cast<int>(height) != expected_size)) {
        return {};
    }

    // PNG holds straight alpha; converting back to PBGRA premultiplies, which
    // is what IconBitmap and D2D expect. Again let WIC do the math.
    IWICFormatConverter* converter_raw = nullptr;
    if (FAILED(factory->CreateFormatConverter(&converter_raw)) || converter_raw == nullptr) {
        return {};
    }
    std::unique_ptr<IWICFormatConverter, ComRelease> converter(converter_raw);
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return {};
    }

    IconBitmap out;
    out.width = width;
    out.height = height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    out.pixels.assign(count, 0);
    if (FAILED(converter->CopyPixels(
            nullptr, width * 4, static_cast<UINT>(count * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(out.pixels.data())))) {
        return {};
    }
    return out;
}

} // namespace nimblerun
