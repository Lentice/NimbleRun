#include "icons/icon_cache.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::IconBitmap;
using nimblerun::IconCache;
using nimblerun::IconKey;
using nimblerun::IconProvider;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

AppEntry Entry(const std::wstring& stable_id) {
    AppEntry entry;
    entry.stable_id = stable_id;
    entry.display_name = L"probe";
    entry.launch_identity = L"C:\\Apps\\probe.exe";
    entry.source_path = entry.launch_identity;
    return entry;
}

// Deterministic fake provider: every load produces a bitmap carrying a
// per-call payload and records the key it was asked for. Failures are
// scripted with a per-key deny list.
class FakeIconProvider : public IconProvider {
public:
    int calls = 0;
    std::vector<std::wstring> requested;
    std::vector<std::wstring> failing;

    IconBitmap Load(const AppEntry&, const IconKey& key) override {
        ++calls;
        requested.push_back(key.Encode());
        for (const std::wstring& fail_key : failing) {
            if (fail_key == key.Encode()) {
                return {};
            }
        }
        IconBitmap bitmap;
        bitmap.width = 4;
        bitmap.height = 4;
        bitmap.pixels.assign(16, static_cast<std::uint32_t>(calls));
        return bitmap;
    }
};

IconKey Key(const std::wstring& stable_id, int variant = 48) {
    return IconKey{stable_id, variant};
}

void TestMissThenInsertAndHit() {
    FakeIconProvider provider;
    IconCache cache;
    const std::wstring encoded = Key(L"app1").Encode();

    Expect(cache.Peek(encoded) == nullptr, "uncached key misses");
    Expect(cache.Size() == 0, "empty cache has size zero");

    const IconBitmap loaded = cache.Resolve(Entry(L"app1"), Key(L"app1"), provider);
    Expect(!loaded.Empty(), "first resolve loads through the provider");
    Expect(loaded.pixels[0] == 1, "provider payload round-trips");
    Expect(provider.calls == 1, "provider called exactly once on a miss");
    Expect(cache.Size() == 1, "insert grew the cache");

    const IconBitmap hit = cache.Resolve(Entry(L"app1"), Key(L"app1"), provider);
    Expect(!hit.Empty(), "second resolve is a cache hit");
    Expect(hit.pixels[0] == 1, "hit returns the stored bitmap, not a reload");
    Expect(provider.calls == 1, "cache hit does not touch the provider");

    Expect(cache.Peek(encoded) != nullptr, "peek finds the inserted key");
    Expect(cache.Peek(encoded)->pixels[0] == 1, "peek returns the stored bitmap");
}

void TestLruEviction() {
    FakeIconProvider provider;
    IconCache cache(2);

    cache.Resolve(Entry(L"a"), Key(L"a"), provider);
    cache.Resolve(Entry(L"b"), Key(L"b"), provider);
    cache.Resolve(Entry(L"c"), Key(L"c"), provider);

    Expect(cache.Size() == 2, "cap keeps the cache bounded");
    Expect(cache.Peek(Key(L"a").Encode()) == nullptr, "oldest entry is evicted");
    Expect(cache.Peek(Key(L"b").Encode()) != nullptr, "second entry survives");
    Expect(cache.Peek(Key(L"c").Encode()) != nullptr, "newest entry survives");
}

void TestReinsertRefreshesRecency() {
    FakeIconProvider provider;
    IconCache cache(2);

    cache.Resolve(Entry(L"a"), Key(L"a"), provider);
    cache.Resolve(Entry(L"b"), Key(L"b"), provider);
    cache.Resolve(Entry(L"a"), Key(L"a"), provider);  // touch a again
    cache.Resolve(Entry(L"c"), Key(L"c"), provider);

    Expect(cache.Peek(Key(L"a").Encode()) != nullptr, "recently re-read entry survives");
    Expect(cache.Peek(Key(L"b").Encode()) == nullptr, "least recently used entry is evicted");
    Expect(cache.Peek(Key(L"c").Encode()) != nullptr, "newest entry survives");
}

void TestProviderFailureNotCached() {
    FakeIconProvider provider;
    IconCache cache(2);
    provider.failing.push_back(Key(L"bad").Encode());

    const IconBitmap failed = cache.Resolve(Entry(L"bad"), Key(L"bad"), provider);
    Expect(failed.Empty(), "provider failure returns an empty bitmap");
    Expect(cache.Size() == 0, "failure is not cached");
    Expect(cache.Peek(Key(L"bad").Encode()) == nullptr, "no entry after a failure");

    cache.Resolve(Entry(L"bad"), Key(L"bad"), provider);
    Expect(provider.calls == 2, "uncached failure is retried, not suppressed");
    Expect(cache.Size() == 0, "still nothing cached after repeated failure");
}

void TestKeySeparatesVariant() {
    FakeIconProvider provider;
    IconCache cache(8);

    cache.Resolve(Entry(L"app"), Key(L"app", 48), provider);
    Expect(cache.Peek(Key(L"app", 48).Encode()) != nullptr, "exact variant is cached");
    Expect(cache.Peek(Key(L"app", 96).Encode()) == nullptr, "next variant is a distinct key");
    Expect(cache.Peek(Key(L"other", 48).Encode()) == nullptr, "different stable id is a distinct key");
}

void TestDefaultCap() {
    FakeIconProvider provider;
    IconCache cache;
    const std::size_t cap = nimblerun::IconCacheCapacityFor(0, 20);
    for (std::size_t i = 0; i < cap + 5; ++i) {
        cache.Resolve(Entry(std::to_wstring(i)), Key(std::to_wstring(i)), provider);
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
    FakeIconProvider provider;
    IconCache cache(8);
    cache.Resolve(Entry(L"a"), Key(L"a"), provider);
    cache.Resolve(Entry(L"b"), Key(L"b"), provider);
    cache.Resolve(Entry(L"c"), Key(L"c"), provider);
    cache.Resolve(Entry(L"d"), Key(L"d"), provider);

    cache.SetMaxItems(2);
    Expect(cache.Size() == 2, "shrink evicts down to the new cap");
    Expect(cache.Peek(Key(L"a").Encode()) == nullptr, "oldest entries are evicted first");
    Expect(cache.Peek(Key(L"b").Encode()) == nullptr, "second oldest entry is evicted");
    Expect(cache.Peek(Key(L"c").Encode()) != nullptr, "newer entries survive");
    Expect(cache.Peek(Key(L"d").Encode()) != nullptr, "newest entry survives");

    // Relative recency of the survivors is preserved: d is still more recent
    // than c, so inserting another key evicts c, never d.
    cache.Resolve(Entry(L"e"), Key(L"e"), provider);
    Expect(cache.Peek(Key(L"c").Encode()) == nullptr, "shrink keeps recency order");
    Expect(cache.Peek(Key(L"d").Encode()) != nullptr, "newest of the survivors stays");
}

void TestSetMaxItemsZero() {
    FakeIconProvider provider;
    IconCache cache(8);
    cache.Resolve(Entry(L"a"), Key(L"a"), provider);
    cache.Resolve(Entry(L"b"), Key(L"b"), provider);

    cache.SetMaxItems(0);
    Expect(cache.Size() <= 1, "zero cap degrades to a single item");
}

void TestSetMaxItemsGrow() {
    FakeIconProvider provider;
    IconCache cache(2);
    cache.Resolve(Entry(L"a"), Key(L"a"), provider);
    cache.Resolve(Entry(L"b"), Key(L"b"), provider);

    cache.SetMaxItems(8);
    Expect(cache.Size() == 2, "growing the cap evicts nothing");
    Expect(cache.Peek(Key(L"a").Encode()) != nullptr, "grow keeps existing entries");
    Expect(cache.Peek(Key(L"b").Encode()) != nullptr, "grow keeps all existing entries");
}

} // namespace

int wmain() {
    TestMissThenInsertAndHit();
    TestLruEviction();
    TestReinsertRefreshesRecency();
    TestProviderFailureNotCached();
    TestKeySeparatesVariant();
    TestDefaultCap();
    TestKeyEncodingIsStable();
    TestVariantForPixels();
    TestSameStableIdSameVariant();
    TestCapacityFor();
    TestSetMaxItemsShrink();
    TestSetMaxItemsZero();
    TestSetMaxItemsGrow();
    std::printf("NR-031 icon cache check PASSED\n");
    return 0;
}
