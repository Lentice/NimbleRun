#pragma once

#include "catalog/app_entry.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimblerun {

// Standard on-disk / in-cache icon sizes in physical pixels. 48 is a native
// Windows icon resource size; 96 and 256 are the escalation tiers. 96 is not a
// native resource size (Shell derives it from 256) but keeps high-DPI machines
// from paying ~5x the bytes and decode cost for an image drawn at 60-80 px.
inline constexpr int kIconVariants[] = {48, 96, 256};

// Smallest variant >= needed_px, clamped to the largest variant. needed_px <= 0
// returns the smallest variant.
int IconVariantForPixels(int needed_px);

// Icon request key (design-spec §FR-009): stable ID + standard-size variant.
// Neither the on-screen size nor the DPI is part of the key: the renderer
// downscales at draw time, so one entry serves the 40 DIP grid cell and the
// 30 DIP list row at every DPI within the same variant tier.
struct IconKey {
    std::wstring stable_id;
    int variant = 0;   // one of kIconVariants

    std::wstring Encode() const;  // stable_id + L'|' + variant
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

// Grid page size (design-spec §4.3): one full page of cells, i.e. the working
// set of a single search result page.
inline constexpr std::size_t kIconCacheWorkingSetItems = 24;

// pinned_count + recent_count + one grid page. The first two terms are the
// prewarm set; the third stops a search from evicting the prewarmed pins and
// forcing a refetch on the next panel show.
std::size_t IconCacheCapacityFor(std::size_t pinned_count, std::size_t recent_count);

// Bounded LRU of decoded bitmaps (design-spec §FR-009). The default cap is
// IconCacheCapacityFor(0, 20) = 44, a start value before the live pin count and
// recent_count setting are known; ShowPanel re-derives it. Keys are
// IconKey::Encode() strings. A cache hit refreshes recency; inserting a
// new key beyond the cap evicts the least recently used key. Empty bitmaps
// are not cached (Insert rejects them), so a failed decode leaves the cache
// unchanged and the caller may keep a fallback without polluting eviction
// state.
class IconCache {
public:
    explicit IconCache(std::size_t max_items = IconCacheCapacityFor(0, 20));

    // Non-mutating lookup used by the renderer to decide fallback vs real icon.
    // Returns a pointer valid until the next mutation of this cache.
    const IconBitmap* Peek(const std::wstring& encoded_key) const;

    // Insert an already-decoded bitmap (produced off-thread). Empty bitmaps are
    // rejected so a failed decode is never cached. A non-empty insert for an
    // existing key replaces the payload and refreshes recency.
    void Insert(const std::wstring& encoded_key, IconBitmap bitmap);

    // Re-derives the LRU cap (design-spec §FR-009). Raising the cap never
    // evicts; lowering it evicts from the LRU tail (least recently used) until
    // the cache fits. max_items == 0 is treated as 1 so the cache is never
    // disabled. The surviving entries keep their relative recency order.
    void SetMaxItems(std::size_t max_items);

    std::size_t Size() const { return order_.size(); }

private:
    std::size_t max_items_;
    // LRU order, front = most recently used. std::list iterators are stable, so
    // the map can hold them.
    std::list<std::wstring> order_;
    std::unordered_map<std::wstring, std::pair<IconBitmap, std::list<std::wstring>::iterator>> map_;
};

} // namespace nimblerun
