#include "icons/icon_cache.h"

#include <utility>

namespace nimblerun {

int IconVariantForPixels(int needed_px) {
    for (const int variant : kIconVariants) {
        if (variant >= needed_px) {
            return variant;
        }
    }
    return kIconVariants[std::size(kIconVariants) - 1];
}

std::wstring IconKey::Encode() const {
    return stable_id + L'|' + std::to_wstring(variant);
}

std::size_t IconCacheCapacityFor(std::size_t pinned_count, std::size_t recent_count) {
    return pinned_count + recent_count + kIconCacheWorkingSetItems;
}

IconCache::IconCache(std::size_t max_items) : max_items_(max_items > 0 ? max_items : 1) {}

const IconBitmap* IconCache::Peek(const std::wstring& encoded_key) const {
    const auto it = map_.find(encoded_key);
    return it == map_.end() ? nullptr : &it->second.first;
}

IconBitmap IconCache::Resolve(const AppEntry& entry, const IconKey& key, IconProvider& provider) {
    const std::wstring encoded = key.Encode();
    const auto it = map_.find(encoded);
    if (it != map_.end()) {
        order_.splice(order_.begin(), order_, it->second.second);  // refresh recency
        return it->second.first;
    }

    IconBitmap loaded = provider.Load(entry, key);
    if (loaded.Empty()) {
        return {};  // failure is not cached; caller keeps the fallback
    }

    order_.push_front(encoded);
    map_.emplace(encoded, std::make_pair(std::move(loaded), order_.begin()));
    while (order_.size() > max_items_) {
        map_.erase(order_.back());
        order_.pop_back();
    }
    return map_.at(encoded).first;
}

void IconCache::SetMaxItems(std::size_t max_items) {
    max_items_ = max_items > 0 ? max_items : 1;
    while (order_.size() > max_items_) {
        map_.erase(order_.back());
        order_.pop_back();
    }
}

void IconCache::Clear() {
    order_.clear();
    map_.clear();
}

} // namespace nimblerun
