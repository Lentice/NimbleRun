#include "search/search_engine.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>

namespace nimblerun {
namespace {

std::wstring CollapseWhitespace(std::wstring_view value) {
    std::wstring result;
    result.reserve(value.size());

    bool pending_space = false;
    for (const wchar_t character : value) {
        if (std::iswspace(character)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(L' ');
            pending_space = false;
        }
        result.push_back(character);
    }
    return result;
}

std::wstring Normalize(std::wstring_view value) {
    const std::wstring collapsed = CollapseWhitespace(value);
    if (collapsed.empty()) {
        return {};
    }

    const int required = LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_LOWERCASE,
        collapsed.data(),
        static_cast<int>(collapsed.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    if (required <= 0) {
        return collapsed;
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_LOWERCASE,
        collapsed.data(),
        static_cast<int>(collapsed.size()),
        result.data(),
        required,
        nullptr,
        nullptr,
        0);
    return result;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

enum class MatchRank : int {
    Exact = 0,
    NamePrefix = 1,
    WordPrefix = 2,
    Substring = 3,
    Subsequence = 4,
    NoMatch = 5,
};

MatchRank Rank(std::wstring_view name, std::wstring_view query) {
    if (name == query) {
        return MatchRank::Exact;
    }
    if (StartsWith(name, query)) {
        return MatchRank::NamePrefix;
    }

    std::size_t word_start = 0;
    while (word_start < name.size()) {
        if (StartsWith(name.substr(word_start), query)) {
            return MatchRank::WordPrefix;
        }
        const std::size_t separator = name.find(L' ', word_start);
        if (separator == std::wstring_view::npos) {
            break;
        }
        word_start = separator + 1;
    }

    if (name.find(query) != std::wstring_view::npos) {
        return MatchRank::Substring;
    }

    std::size_t query_index = 0;
    for (const wchar_t character : name) {
        if (query_index < query.size() && character == query[query_index]) {
            ++query_index;
        }
    }
    return query_index == query.size() ? MatchRank::Subsequence : MatchRank::NoMatch;
}

std::wstring_view NormalizedName(const AppEntry& entry) {
    return entry.normalized_name.empty()
        ? std::wstring_view(entry.display_name)
        : std::wstring_view(entry.normalized_name);
}

} // namespace

std::vector<AppEntry> SearchApps(const std::vector<AppEntry>& catalog,
                                 std::wstring_view query) {
    const std::wstring normalized_query = Normalize(query);
    if (normalized_query.empty()) {
        return {};
    }

    struct RankedEntry {
        AppEntry entry;
        MatchRank rank;
        std::wstring normalized_name;
    };

    std::vector<RankedEntry> ranked;
    ranked.reserve(catalog.size());
    for (const AppEntry& entry : catalog) {
        const std::wstring name = Normalize(NormalizedName(entry));
        const MatchRank rank = Rank(name, normalized_query);
        if (rank != MatchRank::NoMatch) {
            ranked.push_back({entry, rank, name});
        }
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedEntry& left, const RankedEntry& right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        if (left.entry.is_pinned != right.entry.is_pinned) {
            return left.entry.is_pinned > right.entry.is_pinned;
        }
        if (left.entry.usage_score != right.entry.usage_score) {
            return left.entry.usage_score > right.entry.usage_score;
        }
        if (left.entry.display_name.size() != right.entry.display_name.size()) {
            return left.entry.display_name.size() < right.entry.display_name.size();
        }
        if (left.normalized_name != right.normalized_name) {
            return left.normalized_name < right.normalized_name;
        }
        return left.entry.stable_id < right.entry.stable_id;
    });

    std::vector<AppEntry> result;
    result.reserve(ranked.size());
    for (RankedEntry& item : ranked) {
        result.push_back(std::move(item.entry));
    }
    return result;
}

} // namespace nimblerun
