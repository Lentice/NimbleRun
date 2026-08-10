#include "test_util.h"

#include "search/search_engine.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;

// NR-047: the fixtures use designated-initializer lists that deliberately omit
// members (every AppEntry field has a default member initializer), so the next
// field added to AppEntry needs no coordination with this file. Clang's -Wextra
// warns about each omission; this file's omission IS the point, so silence it.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"

namespace {

int g_failures = 0;

} // namespace

int wmain() {
    const std::vector<AppEntry> catalog{
        {.stable_id = L"notepad", .display_name = L"Notepad", .normalized_name = L"notepad", .usage_score = 100},
        {.stable_id = L"calculator", .display_name = L"Calculator", .normalized_name = L"calculator", .usage_score = 1},
        {.stable_id = L"calendar", .display_name = L"Calendar", .normalized_name = L"calendar", .is_pinned = true, .usage_score = 0},
        {.stable_id = L"paint", .display_name = L"Paint 3D", .normalized_name = L"paint 3d", .usage_score = 0},
    };

    const auto prefix_results = nimblerun::SearchApps(catalog, L"  CAL  ");
    Expect(prefix_results.size() == 2, "trimmed prefix search returns Calendar and Calculator");
    Expect(prefix_results[0].display_name == L"Calendar", "Calendar outranks Calculator on exact-prefix score");
    Expect(prefix_results[1].display_name == L"Calculator", "Calculator is second in the prefix search results");

    const auto word_prefix_results = nimblerun::SearchApps(catalog, L"3d");
    Expect(word_prefix_results.size() == 1, "word prefix search matches one entry");
    Expect(word_prefix_results[0].display_name == L"Paint 3D", "word prefix search hits Paint 3D");

    const auto empty_results = nimblerun::SearchApps(catalog, L"   ");
    Expect(empty_results.empty(), "whitespace-only query returns no results");

    // NR-047: the motivating case -- a localized display name the name tiers
    // cannot match is still reachable through the secondary key (the resolved
    // target's stem). Name matches outrank every alias match, so a name
    // subsequence still beats an alias exact match.
    {
        const std::vector<AppEntry> alias_catalog{
            {.stable_id = L"calculator", .display_name = L"Calculator", .normalized_name = L"calculator", .usage_score = 0},
            {.stable_id = L"calc", .display_name = L"計算機", .normalized_name = L"計算機", .usage_score = 0, .search_alias = L"calc"},
            {.stable_id = L"paint", .display_name = L"Paint 3D", .normalized_name = L"paint 3d", .usage_score = 0},
        };

        const auto hit = nimblerun::SearchApps(alias_catalog, L"calc");
        Expect(hit.size() == 2, "alias search matches Calculator and localized calc");
        Expect(hit[0].display_name == L"Calculator", "name match outranks alias exact match");
        Expect(hit[1].display_name == L"計算機", "localized display name reachable via alias fallback");

        // An empty search_alias is unaffected: Paint 3D is found by name only.
        const auto paint = nimblerun::SearchApps(alias_catalog, L"paint");
        Expect(paint.size() == 1 && paint[0].display_name == L"Paint 3D",
               "empty search_alias leaves name-only search intact");
    }

    // NR-047: the alias is compared as given. The catalog stores it
    // pre-normalized (lowercase, as SetSnapshot produces); an uppercase alias
    // is not folded at search time.
    {
        AppEntry upper;
        upper.stable_id = L"upper";
        upper.display_name = L"Upper App";
        upper.search_alias = L"CALC";
        const std::vector<AppEntry> single{upper};
        Expect(nimblerun::SearchApps(single, L"calc").empty(),
               "uppercase alias is not folded at search time");
    }

    // NR-038: NormalizeName collapses and trims whitespace, then lowercases.
    Expect(nimblerun::NormalizeName(L"  Paint   3D  ") == L"paint 3d",
           "NormalizeName collapses and trims whitespace, then lowercases");
    Expect(nimblerun::NormalizeName(L"   ").empty(),
           "NormalizeName trims whitespace-only input to empty");
    Expect(nimblerun::NormalizeName(L"ABC") == L"abc",
           "NormalizeName lowercases input");

    // NR-038: a prefilled normalized_name is adopted and display_name is ignored.
    {
        AppEntry zebra;
        zebra.stable_id = L"zebra";
        zebra.display_name = L"Zebra";
        zebra.normalized_name = L"notepad";
        const std::vector<AppEntry> single{zebra};

        const auto hit = nimblerun::SearchApps(single, L"note");
        Expect(hit.size() == 1 && hit[0].stable_id == L"zebra",
               "prefilled normalized_name is adopted over display_name");
        const auto miss = nimblerun::SearchApps(single, L"zeb");
        Expect(miss.empty(), "prefilled normalized_name replaces display name for search");
    }

    // NR-038: worst-path latency on 5000 pre-normalized entries. The threshold
    // is two orders of magnitude above the measured time so it only flags a
    // regression that re-introduces per-entry normalization per keystroke.
    {
        std::vector<AppEntry> big;
        big.reserve(5000);
        for (int i = 0; i < 5000; ++i) {
            AppEntry entry;
            entry.stable_id = L"id" + std::to_wstring(i);
            entry.display_name = L"App " + std::to_wstring(i) + L" edition";
            entry.normalized_name = entry.display_name;
            big.push_back(std::move(entry));
        }

        const auto start = steady_clock::now();
        const auto results = nimblerun::SearchApps(big, L"e");
        const auto elapsed_us =
            duration_cast<microseconds>(steady_clock::now() - start).count();

        std::wprintf(L"NR-038: SearchApps over 5000 pre-normalized entries took %lld us (%lld ms), matched %zu\n",
                     elapsed_us, elapsed_us / 1000, results.size());
        Expect(results.size() == 5000, "5000-entry search returns every entry");
        Expect(elapsed_us / 1000 < 50, "5000-entry search stays under 50 ms");
    }

    // NR-047: worst-path latency when every display name misses and every entry
    // pays the alias fallback. Same 5000-entry shape as the block above; the
    // query matches no name, and the result set is asserted empty so this keeps
    // measuring the fallback even if the fixture names ever change.
    {
        std::vector<AppEntry> big;
        big.reserve(5000);
        for (int i = 0; i < 5000; ++i) {
            AppEntry entry;
            entry.stable_id = L"id" + std::to_wstring(i);
            entry.display_name = L"App " + std::to_wstring(i) + L" edition";
            entry.normalized_name = entry.display_name;
            entry.search_alias = L"target" + std::to_wstring(i);
            big.push_back(std::move(entry));
        }

        const auto start = steady_clock::now();
        const auto results = nimblerun::SearchApps(big, L"zzqx");
        const auto elapsed_us =
            duration_cast<microseconds>(steady_clock::now() - start).count();

        std::wprintf(L"NR-047: SearchApps over 5000 alias-fallback entries took %lld us (%lld ms), matched %zu\n",
                     elapsed_us, elapsed_us / 1000, results.size());
        Expect(results.empty(), "no-name query yields an empty result set");
        Expect(elapsed_us / 1000 < 50, "5000-entry alias-fallback search stays under 50 ms");
    }

    return g_failures == 0 ? 0 : 1;
}

#pragma clang diagnostic pop
