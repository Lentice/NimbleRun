#include "test_util.h"

#include "icons/png_codec.h"

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using nimblerun::DecodeIconPng;
using nimblerun::EncodeIconPng;
using nimblerun::IconBitmap;

namespace {

std::uint32_t Alpha(std::uint32_t px) { return (px >> 24) & 0xFFu; }
std::uint32_t Red(std::uint32_t px) { return (px >> 16) & 0xFFu; }
std::uint32_t Green(std::uint32_t px) { return (px >> 8) & 0xFFu; }
std::uint32_t Blue(std::uint32_t px) { return px & 0xFFu; }

// Straight (b,g,r,a) -> premultiplied BGRA. Integer truncation matches the
// round trip the codec runs, so the test starts from valid premultiplied data.
std::uint32_t Premul(std::uint32_t a, std::uint32_t b, std::uint32_t g, std::uint32_t r) {
    const auto scale = [a](std::uint32_t c) { return (c * a) / 255u; };
    return (a << 24) | (scale(r) << 16) | (scale(g) << 8) | scale(b);
}

// Deterministic synthetic pattern: opaque fill, four unique opaque corner
// colors, a fully transparent block, and a semi-transparent gradient block.
IconBitmap MakePattern(std::uint32_t size) {
    IconBitmap out;
    out.width = size;
    out.height = size;
    out.pixels.assign(static_cast<std::size_t>(size) * size, 0xFF4080FFu);

    const auto put = [&](std::uint32_t x, std::uint32_t y, std::uint32_t value) {
        out.pixels[static_cast<std::size_t>(y) * size + x] = value;
    };
    put(0, 0, 0xFF112233u);
    put(size - 1, 0, 0xFF445566u);
    put(0, size - 1, 0xFF778899u);
    put(size - 1, size - 1, 0xFFAABBCCu);

    const std::uint32_t third = size / 3;
    for (std::uint32_t y = third; y < 2 * third; ++y) {
        for (std::uint32_t x = third; x < 2 * third; ++x) {
            out.pixels[static_cast<std::size_t>(y) * size + x] = 0;  // a == 0
        }
    }
    for (std::uint32_t y = 2 * third; y < size; ++y) {
        for (std::uint32_t x = 2 * third; x < size; ++x) {
            const std::uint32_t a = 1 + ((x * 7u + y * 3u) % 254u);  // 1..254
            out.pixels[static_cast<std::size_t>(y) * size + x] =
                Premul(a, (x * 13u) & 0xFFu, (y * 17u) & 0xFFu, (x * 29u + y * 5u) & 0xFFu);
        }
    }
    return out;
}

void CheckRoundTrip(std::uint32_t size) {
    const IconBitmap src = MakePattern(size);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    Expect(!png.empty(), "encode produces bytes");
    const IconBitmap decoded = DecodeIconPng(png.data(), png.size());
    Expect(!decoded.Empty(), "decode produces a bitmap");
    Expect(decoded.width == size && decoded.height == size, "decoded size matches");

    for (std::size_t i = 0; i < src.pixels.size(); ++i) {
        const std::uint32_t expected = src.pixels[i];
        const std::uint32_t actual = decoded.pixels[i];
        const std::uint32_t a = Alpha(expected);
        if (a == 255 || a == 0) {
            // Fully opaque and fully transparent pixels round-trip exactly:
            // a == 255 leaves channels unchanged through both unpremultiply
            // (c*255/255) and premultiply (c*255/255); a == 0 premultiplies
            // every channel back to 0 no matter what straight color WIC wrote
            // for the invisible pixel. So the decoded word must equal the
            // source exactly.
            Expect(actual == expected, "opaque/transparent pixel round-trips exactly");
        } else {
            // Semi-transparent pixels traverse straight -> unpremultiply ->
            // PNG -> premultiply. Each step rounds, so allow +-1 per channel.
            // It is not 0 because the integer division has two independent
            // rounding points (one per direction) that can land either side of
            // the source value. Alpha is a byte-for-byte copy in both
            // conversions, so it stays exact.
            Expect(Alpha(actual) == a, "alpha is exact through the round trip");
            const auto within = [](std::uint32_t lhs, std::uint32_t rhs) {
                const std::uint32_t hi = lhs > rhs ? lhs : rhs;
                const std::uint32_t lo = lhs < rhs ? lhs : rhs;
                return hi - lo <= 1u;
            };
            Expect(within(Blue(actual), Blue(expected)), "blue within +-1");
            Expect(within(Green(actual), Green(expected)), "green within +-1");
            Expect(within(Red(actual), Red(expected)), "red within +-1");
        }
    }
}

void TestZeroAlphaStaysZero() {
    const IconBitmap src = MakePattern(48);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    const IconBitmap decoded = DecodeIconPng(png.data(), png.size());
    Expect(!decoded.Empty(), "decode succeeds");
    for (std::size_t i = 0; i < src.pixels.size(); ++i) {
        if (Alpha(src.pixels[i]) == 0) {
            Expect(decoded.pixels[i] == 0, "a==0 pixel stays all-zero");
        }
    }
}

void TestPngSignature() {
    const IconBitmap src = MakePattern(96);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    Expect(png.size() >= 8, "encoded bytes at least the signature");
    const std::uint8_t signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) {
        Expect(png[i] == signature[i], "PNG signature byte");
    }
}

void TestEncodedSizeBelowBudget() {
    const IconBitmap src = MakePattern(96);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    Expect(!png.empty(), "encode succeeds");
    // Raw 96x96 BGRA is 36 KiB; the pattern must compress well below that,
    // proving the payload is PNG and not a raw fallback.
    Expect(png.size() < 20 * 1024, "96x96 pattern encodes under 20 KiB");
}

void TestEncodeRejections() {
    Expect(EncodeIconPng(IconBitmap{}).empty(), "empty bitmap rejected");

    IconBitmap size_mismatch;
    size_mismatch.width = 4;
    size_mismatch.height = 4;
    size_mismatch.pixels.assign(15, 0);  // 15 != 4 * 4
    Expect(EncodeIconPng(size_mismatch).empty(), "pixels.size mismatch rejected");

    IconBitmap huge;
    huge.width = 2048;
    huge.height = 2048;
    huge.pixels.assign(2048ull * 2048, 0);
    Expect(EncodeIconPng(huge).empty(), "width 2048 rejected");

    IconBitmap zero_dim;
    zero_dim.width = 0;
    zero_dim.height = 0;
    zero_dim.pixels.assign(1, 0);
    Expect(EncodeIconPng(zero_dim).empty(), "zero dimensions rejected");
}

void TestDecodeRejections() {
    Expect(DecodeIconPng(nullptr, 0).Empty(), "null data rejected");
    Expect(DecodeIconPng(nullptr, 8).Empty(), "null data rejected regardless of size");

    std::uint8_t one_byte = 0;
    Expect(DecodeIconPng(&one_byte, 0).Empty(), "size 0 rejected");

    const IconBitmap src = MakePattern(48);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    Expect(!png.empty(), "valid png exists");

    Expect(DecodeIconPng(png.data(), 8).Empty(), "signature-only truncated png rejected");

    std::vector<std::uint8_t> flipped = png;
    flipped[flipped.size() / 2] ^= 0x01;
    Expect(DecodeIconPng(flipped.data(), flipped.size()).Empty(), "mid-byte flip rejected");

    std::mt19937 rng(20260805u);
    std::vector<std::uint8_t> random(4096);
    for (std::uint8_t& byte : random) {
        byte = static_cast<std::uint8_t>(rng());
    }
    Expect(DecodeIconPng(random.data(), random.size()).Empty(), "random 4 KiB rejected");
}

void TestExpectedSize() {
    const IconBitmap src = MakePattern(48);
    const std::vector<std::uint8_t> png = EncodeIconPng(src);
    Expect(!png.empty(), "valid png exists");

    Expect(DecodeIconPng(png.data(), png.size(), 96).Empty(),
           "expected_size 96 rejects a 48x48 png");
    const IconBitmap accepted = DecodeIconPng(png.data(), png.size(), 0);
    Expect(!accepted.Empty() && accepted.width == 48 && accepted.height == 48,
           "expected_size 0 accepts the 48x48 png");
}

} // namespace

int wmain() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAILED: COM init\n");
        return 1;
    }

    CheckRoundTrip(48);
    CheckRoundTrip(96);
    CheckRoundTrip(256);
    TestZeroAlphaStaysZero();
    TestPngSignature();
    TestEncodedSizeBelowBudget();
    TestEncodeRejections();
    TestDecodeRejections();
    TestExpectedSize();

    CoUninitialize();
    std::printf("NR-034 png codec check PASSED\n");
    return 0;
}
