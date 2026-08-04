#include "search/search_engine.h"

#include <cassert>
#include <string>
#include <vector>

using nimblerun::AppEntry;

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
    return 0;
}
