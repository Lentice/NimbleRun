#include "search/search_engine.h"

#include <cassert>
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

int wmain() {
    const std::vector<AppEntry> catalog{
        {.stable_id = L"notepad", .display_name = L"Notepad", .usage_score = 100},
        {.stable_id = L"calculator", .display_name = L"Calculator", .usage_score = 1},
        {.stable_id = L"calendar", .display_name = L"Calendar", .is_pinned = true, .usage_score = 0},
        {.stable_id = L"paint", .display_name = L"Paint 3D", .usage_score = 0},
    };

    const auto prefix_results = nimblerun::SearchApps(catalog, L"  CAL  ");
    assert(prefix_results.size() == 2);
    assert(prefix_results[0].display_name == L"Calendar");
    assert(prefix_results[1].display_name == L"Calculator");

    const auto word_prefix_results = nimblerun::SearchApps(catalog, L"3d");
    assert(word_prefix_results.size() == 1);
    assert(word_prefix_results[0].display_name == L"Paint 3D");

    const auto empty_results = nimblerun::SearchApps(catalog, L"   ");
    assert(empty_results.empty());

    // NR-047: the motivating case -- a localized display name the name tiers
    // cannot match is still reachable through the secondary key (the resolved
    // target's stem). Name matches outrank every alias match, so a name
    // subsequence still beats an alias exact match.
    {
        const std::vector<AppEntry> alias_catalog{
            {.stable_id = L"calculator", .display_name = L"Calculator", .usage_score = 0},
            {.stable_id = L"calc", .display_name = L"計算機", .usage_score = 0, .search_alias = L"calc"},
            {.stable_id = L"paint", .display_name = L"Paint 3D", .usage_score = 0},
        };

        const auto hit = nimblerun::SearchApps(alias_catalog, L"calc");
        assert(hit.size() == 2);
        assert(hit[0].display_name == L"Calculator");
        assert(hit[1].display_name == L"計算機");

        // An empty search_alias is unaffected: Paint 3D is found by name only.
        const auto paint = nimblerun::SearchApps(alias_catalog, L"paint");
        assert(paint.size() == 1 && paint[0].display_name == L"Paint 3D");
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
        assert(nimblerun::SearchApps(single, L"calc").empty());
    }

    // NR-038: NormalizeName collapses and trims whitespace, then lowercases.
    assert(nimblerun::NormalizeName(L"  Paint   3D  ") == L"paint 3d");
    assert(nimblerun::NormalizeName(L"   ").empty());
    assert(nimblerun::NormalizeName(L"ABC") == L"abc");

    // NR-038: a prefilled normalized_name is adopted and display_name is ignored.
    {
        AppEntry zebra;
        zebra.stable_id = L"zebra";
        zebra.display_name = L"Zebra";
        zebra.normalized_name = L"notepad";
        const std::vector<AppEntry> single{zebra};

        const auto hit = nimblerun::SearchApps(single, L"note");
        assert(hit.size() == 1 && hit[0].stable_id == L"zebra");
        const auto miss = nimblerun::SearchApps(single, L"zeb");
        assert(miss.empty());
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
        assert(results.size() == 5000);
        assert(elapsed_us / 1000 < 50);
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
        assert(results.empty());
        assert(elapsed_us / 1000 < 50);
    }

    return 0;
}

#pragma clang diagnostic pop
