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

IconKey Key(const std::wstring& stable_id, int size = 32, float dpi = 96.0f) {
    return IconKey{stable_id, size, dpi};
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

void TestKeySeparatesSizeAndDpi() {
    FakeIconProvider provider;
    IconCache cache(8);

    cache.Resolve(Entry(L"app"), Key(L"app", 32, 96.0f), provider);
    Expect(cache.Peek(Key(L"app", 32, 96.0f).Encode()) != nullptr, "exact key is cached");
    Expect(cache.Peek(Key(L"app", 48, 96.0f).Encode()) == nullptr, "larger size is a distinct key");
    Expect(cache.Peek(Key(L"app", 32, 144.0f).Encode()) == nullptr, "different dpi is a distinct key");
    Expect(cache.Peek(Key(L"other", 32, 96.0f).Encode()) == nullptr, "different stable id is a distinct key");
}

void TestDefaultCap() {
    FakeIconProvider provider;
    IconCache cache;
    for (int i = 0; i < static_cast<int>(IconCache::kDefaultMaxItems) + 5; ++i) {
        cache.Resolve(Entry(std::to_wstring(i)), Key(std::to_wstring(i)), provider);
    }
    Expect(cache.Size() == IconCache::kDefaultMaxItems, "default cap bounds the cache");
}

void TestKeyEncodingIsStable() {
    Expect(Key(L"id", 32, 96.0f).Encode() == L"id|32|96", "encoded key is deterministic");
    Expect(Key(L"id", 32, 96.4f).Encode() == L"id|32|96", "dpi rounds for a stable key");
}

} // namespace

int wmain() {
    TestMissThenInsertAndHit();
    TestLruEviction();
    TestReinsertRefreshesRecency();
    TestProviderFailureNotCached();
    TestKeySeparatesSizeAndDpi();
    TestDefaultCap();
    TestKeyEncodingIsStable();
    std::printf("NR-012 icon cache check PASSED\n");
    return 0;
}
