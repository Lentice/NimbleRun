#pragma once

#include "icons/icon_cache.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nimblerun {

// Encode a decoded icon as PNG bytes. Returns an empty vector on any failure
// (invalid bitmap, WIC error). The caller treats empty as "do not persist".
std::vector<std::uint8_t> EncodeIconPng(const IconBitmap& bitmap);

// Decode PNG bytes produced by EncodeIconPng (or any PNG WIC can read) into
// 32bpp premultiplied BGRA. Returns an empty IconBitmap on any failure,
// including truncated or corrupt input. expected_size, when > 0, rejects images
// whose width or height differs, so a mismatched cache entry cannot slip
// through as a wrong-sized icon.
IconBitmap DecodeIconPng(const std::uint8_t* data, std::size_t size, int expected_size = 0);

} // namespace nimblerun
