#include "test_util.h"

#include "icons/icon_cache.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using nimblerun::IconBitmap;
using nimblerun::IconCache;
using nimblerun::IconKey;

namespace {

IconKey Key(const std::wstring& stable_id, int variant = 48) {
    return IconKey{stable_id, variant};
}

IconBitmap SampleBitmap() {
    IconBitmap icon;
    icon.width = 2;
    icon.height = 2;
    icon.pixels.assign(4, 0xFF112233u);
    return icon;
}

// NR-128: the miss->provider->insert route (IconCache::Resolve) is gone, so the
// cache is exercised through its worker entry point Insert() plus Peek(), which
// is exactly the production path. TestLruEviction, TestReinsertRefreshesRecency
// and TestProviderFailureNotCached were Resolve-only routes whose semantics are
// covered verbatim by TestInsertAddsAndEvicts, TestInsertRefreshesRecency and
// TestInsertRejectsEmpty below (all share the same LRU/eviction code path).

void TestInsertThenPeekHit() {
    IconCache cache;
    const std::wstring encoded = Key(L"app1").Encode();

    Expect(cache.Peek(encoded) == nullptr, "uncached key misses");
    Expect(cache.Size() == 0, "empty cache has size zero");

    cache.Insert(encoded, SampleBitmap());
    Expect(cache.Size() == 1, "insert grew the cache");
    Expect(cache.Peek(encoded) != nullptr, "peek finds the inserted key");
    Expect(cache.Peek(encoded)->pixels[0] == 0xFF112233u,
           "peek returns the stored bitmap, not a reload");
}

void TestKeySeparatesVariant() {
    IconCache cache(8);

    cache.Insert(Key(L"app", 48).Encode(), SampleBitmap());
    Expect(cache.Peek(Key(L"app", 48).Encode()) != nullptr, "exact variant is cached");
    Expect(cache.Peek(Key(L"app", 96).Encode()) == nullptr, "next variant is a distinct key");
    Expect(cache.Peek(Key(L"other", 48).Encode()) == nullptr, "different stable id is a distinct key");
}

void TestDefaultCap() {
    IconCache cache;
    const std::size_t cap = nimblerun::IconCacheCapacityFor(0, 20);
    for (std::size_t i = 0; i < cap + 5; ++i) {
        cache.Insert(std::to_wstring(i) + L"|48", SampleBitmap());
    }
    Expect(cache.Size() == cap, "default cap bounds the cache");
}

void TestKeyEncodingIsStable() {
    Expect(Key(L"id", 48).Encode() == L"id|48", "encoded key is deterministic");
    Expect(Key(L"id", 96).Encode() == L"id|96", "variant encodes without any dpi");
}

void TestVariantForPixels() {
    const int small[] = {30, 40, 48};
    for (const int px : small) {
        Expect(nimblerun::IconVariantForPixels(px) == 48, "30/40/48 -> 48");
    }
    const int mid[] = {50, 60, 80, 96};
    for (const int px : mid) {
        Expect(nimblerun::IconVariantForPixels(px) == 96, "50/60/80/96 -> 96");
    }
    const int large[] = {97, 120, 256, 999};
    for (const int px : large) {
        Expect(nimblerun::IconVariantForPixels(px) == 256, "97/120/256/999 -> 256");
    }
    Expect(nimblerun::IconVariantForPixels(0) == 48, "0 -> 48");
    Expect(nimblerun::IconVariantForPixels(-1) == 48, "negative -> 48");
}

void TestSameStableIdSameVariant() {
    Expect(Key(L"app", nimblerun::IconVariantForPixels(30)).Encode() ==
           Key(L"app", nimblerun::IconVariantForPixels(40)).Encode(),
           "30px and 40px needs map to the same key");
    Expect(Key(L"app", nimblerun::IconVariantForPixels(40)).Encode() !=
           Key(L"app", nimblerun::IconVariantForPixels(60)).Encode(),
           "40px and 60px needs map to different keys");
}

void TestCapacityFor() {
    Expect(nimblerun::IconCacheCapacityFor(0, 20) == 44, "capacity (0,20) == 44");
    Expect(nimblerun::IconCacheCapacityFor(12, 20) == 56, "capacity (12,20) == 56");
    Expect(nimblerun::IconCacheCapacityFor(0, 8) == 32, "capacity (0,8) == 32");
    Expect(nimblerun::IconCacheCapacityFor(0, 8) <= nimblerun::IconCacheCapacityFor(0, 20),
           "capacity is monotonic in recent_count");
    Expect(nimblerun::IconCacheCapacityFor(0, 20) <= nimblerun::IconCacheCapacityFor(12, 20),
           "capacity is monotonic in pinned_count");
}

void TestSetMaxItemsShrink() {
    IconCache cache(8);
    cache.Insert(L"a|48", SampleBitmap());
    cache.Insert(L"b|48", SampleBitmap());
    cache.Insert(L"c|48", SampleBitmap());
    cache.Insert(L"d|48", SampleBitmap());

    cache.SetMaxItems(2);
    Expect(cache.Size() == 2, "shrink evicts down to the new cap");
    Expect(cache.Peek(L"a|48") == nullptr, "oldest entries are evicted first");
    Expect(cache.Peek(L"b|48") == nullptr, "second oldest entry is evicted");
    Expect(cache.Peek(L"c|48") != nullptr, "newer entries survive");
    Expect(cache.Peek(L"d|48") != nullptr, "newest entry survives");

    // Relative recency of the survivors is preserved: d is still more recent
    // than c, so inserting another key evicts c, never d.
    cache.Insert(L"e|48", SampleBitmap());
    Expect(cache.Peek(L"c|48") == nullptr, "shrink keeps recency order");
    Expect(cache.Peek(L"d|48") != nullptr, "newest of the survivors stays");
}

void TestSetMaxItemsZero() {
    IconCache cache(8);
    cache.Insert(L"a|48", SampleBitmap());
    cache.Insert(L"b|48", SampleBitmap());

    cache.SetMaxItems(0);
    Expect(cache.Size() <= 1, "zero cap degrades to a single item");
}

void TestSetMaxItemsGrow() {
    IconCache cache(2);
    cache.Insert(L"a|48", SampleBitmap());
    cache.Insert(L"b|48", SampleBitmap());

    cache.SetMaxItems(8);
    Expect(cache.Size() == 2, "growing the cap evicts nothing");
    Expect(cache.Peek(L"a|48") != nullptr, "grow keeps existing entries");
    Expect(cache.Peek(L"b|48") != nullptr, "grow keeps all existing entries");
}

// NR-032: IconCache::Insert is the worker-side entry point; empty bitmaps are
// rejected so a failed decode is never cached.
void TestInsertRejectsEmpty() {
    IconCache cache(2);
    cache.Insert(L"a|48", {});
    Expect(cache.Size() == 0, "empty bitmap insert is rejected");
    Expect(cache.Peek(L"a|48") == nullptr, "rejected insert leaves no entry");
}

void TestInsertAddsAndEvicts() {
    IconCache cache(2);
    IconBitmap icon;
    icon.width = 2;
    icon.height = 2;
    icon.pixels.assign(4, 0xFF112233u);

    cache.Insert(L"a|48", icon);
    Expect(cache.Size() == 1, "non-empty insert grows the cache");
    Expect(cache.Peek(L"a|48") != nullptr, "inserted key is peek-able");
    Expect(cache.Peek(L"a|48")->pixels[0] == 0xFF112233u, "inserted payload round-trips");

    cache.Insert(L"b|48", icon);
    cache.Insert(L"c|48", icon);
    Expect(cache.Size() == 2, "insert enforces the cap");
    Expect(cache.Peek(L"a|48") == nullptr, "oldest inserted key is evicted");
    Expect(cache.Peek(L"b|48") != nullptr, "newer key survives");
    Expect(cache.Peek(L"c|48") != nullptr, "newest key survives");
}

void TestInsertRefreshesRecency() {
    IconCache cache(2);
    IconBitmap icon;
    icon.width = 2;
    icon.height = 2;
    icon.pixels.assign(4, 0xFF112233u);

    cache.Insert(L"a|48", icon);
    cache.Insert(L"b|48", icon);
    cache.Insert(L"a|48", icon);  // re-insert refreshes recency
    cache.Insert(L"c|48", icon);
    Expect(cache.Size() == 2, "re-insert keeps the cache bounded");
    Expect(cache.Peek(L"a|48") != nullptr, "re-inserted key stays fresh");
    Expect(cache.Peek(L"b|48") == nullptr, "least recently used key is evicted");
    Expect(cache.Peek(L"c|48") != nullptr, "newest key survives");
}

} // namespace

int wmain() {
    TestInsertThenPeekHit();
    TestKeySeparatesVariant();
    TestDefaultCap();
    TestKeyEncodingIsStable();
    TestVariantForPixels();
    TestSameStableIdSameVariant();
    TestCapacityFor();
    TestSetMaxItemsShrink();
    TestSetMaxItemsZero();
    TestSetMaxItemsGrow();
    TestInsertRejectsEmpty();
    TestInsertAddsAndEvicts();
    TestInsertRefreshesRecency();
    std::printf("NR-032 icon cache check PASSED\n");
    return 0;
}
