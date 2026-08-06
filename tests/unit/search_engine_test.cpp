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

int wmain() {
    const std::vector<AppEntry> catalog{
        {.stable_id = L"notepad", .display_name = L"Notepad", .normalized_name = L"", .launch_identity = L"", .source_path = L"", .source = {}, .is_pinned = false, .usage_score = 100},
        {.stable_id = L"calculator", .display_name = L"Calculator", .normalized_name = L"", .launch_identity = L"", .source_path = L"", .source = {}, .is_pinned = false, .usage_score = 1},
        {.stable_id = L"calendar", .display_name = L"Calendar", .normalized_name = L"", .launch_identity = L"", .source_path = L"", .source = {}, .is_pinned = true, .usage_score = 0},
        {.stable_id = L"paint", .display_name = L"Paint 3D", .normalized_name = L"", .launch_identity = L"", .source_path = L"", .source = {}, .is_pinned = false, .usage_score = 0},
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

    return 0;
}
