#pragma once

#include "catalog/app_entry.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimblerun {

// Icon request key (design-spec §FR-009): stable ID + requested size (physical
// pixels) + DPI. Two rows sharing a stable ID but requested at different
// sizes/DPIs are distinct cache entries.
struct IconKey {
    std::wstring stable_id;
    int size = 0;
    float dpi = 0.0f;

    // Deterministic single-string key for map lookups. stable_id is a fixed
    // width hex hash, so '|' separators are unambiguous. DPI is rounded to the
    // nearest integer: per-monitor DPI values are integers in practice, and
    // rounding keeps repeated queries for the same monitor hitting one entry.
    std::wstring Encode() const {
        std::wstring out = stable_id;
        out.push_back(L'|');
        out += std::to_wstring(size);
        out.push_back(L'|');
        out += std::to_wstring(static_cast<int>(std::lround(dpi)));
        return out;
    }
};

// A decoded 32bpp premultiplied BGRA image, ready to hand to a renderer
// (D2D1Bitmap). Plain copyable data: the cache and everything above it never
// touch HWND, GDI, or Shell COM objects.
struct IconBitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;  // width * height, premultiplied BGRA

    bool Empty() const { return width == 0 || height == 0 || pixels.empty(); }
};

// Abstract icon source. The cache asks this for a key on a miss; production
// uses ShellIconProvider, tests inject a fake. Never owned by the cache.
class IconProvider {
public:
    virtual ~IconProvider() = default;
    virtual IconBitmap Load(const AppEntry& entry, const IconKey& key) = 0;
};

// Bounded LRU of decoded bitmaps (default cap 64, design-spec §FR-009). Keys
// are IconKey::Encode() strings. A cache hit refreshes recency; inserting a
// new key beyond the cap evicts the least recently used key. Provider failures
// are not cached: Resolve returns empty and leaves the cache unchanged, so the
// caller may keep a fallback without polluting eviction state.
class IconCache {
public:
    static constexpr std::size_t kDefaultMaxItems = 64;

    explicit IconCache(std::size_t max_items = kDefaultMaxItems);

    // Non-mutating lookup used by the renderer to decide fallback vs real icon.
    // Returns a pointer valid until the next mutation of this cache.
    const IconBitmap* Peek(const std::wstring& encoded_key) const;

    // Cached bitmap for the key, or ask the provider on a miss and insert it
    // on success. Returns empty only when the key is uncached and the provider
    // failed.
    IconBitmap Resolve(const AppEntry& entry, const IconKey& key, IconProvider& provider);

    std::size_t Size() const { return order_.size(); }
    void Clear();

private:
    std::size_t max_items_;
    // LRU order, front = most recently used. std::list iterators are stable, so
    // the map can hold them.
    std::list<std::wstring> order_;
    std::unordered_map<std::wstring, std::pair<IconBitmap, std::list<std::wstring>::iterator>> map_;
};

} // namespace nimblerun
