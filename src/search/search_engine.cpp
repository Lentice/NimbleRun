#include "search/search_engine.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
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

enum class MatchRank : int {
    Exact = 0,
    NamePrefix = 1,
    WordPrefix = 2,
    Substring = 3,
    Subsequence = 4,
    Alias = 5,    // NR-047: matched the target/AUMID, not the name
    NoMatch = 6,
};

MatchRank Rank(std::wstring_view name, std::wstring_view query) {
    if (name == query) {
        return MatchRank::Exact;
    }
    if (name.starts_with(query)) {
        return MatchRank::NamePrefix;
    }

    std::size_t word_start = 0;
    while (word_start < name.size()) {
        if (name.substr(word_start).starts_with(query)) {
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

std::wstring NormalizeName(std::wstring_view value) {
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

std::vector<AppEntry> SearchApps(const std::vector<AppEntry>& catalog,
                                 std::wstring_view query) {
    // ponytail: full O(catalog) scan per keystroke. Measured sub-millisecond for a
    // 5k catalog once names are pre-normalized (see search_engine_test). If a real
    // catalog ever makes this visible, the next step is incremental narrowing (a
    // longer query's match set is a subset of the shorter one's, for every tier),
    // not a debounce -- a debounce only makes the first keystroke slower.
    const std::wstring normalized_query = NormalizeName(query);
    if (normalized_query.empty()) {
        return {};
    }

    struct Ranked {
        MatchRank rank;
        std::uint32_t index;
    };

    std::vector<Ranked> ranked;
    ranked.reserve(catalog.size());
    for (std::uint32_t i = 0; i < catalog.size(); ++i) {
        const std::wstring_view name = NormalizedName(catalog[i]);
        MatchRank rank = Rank(name, normalized_query);
        // NR-047: the target name is a fallback, never a competitor. It is only
        // consulted when the display name does not match at all, and every hit
        // collapses to one tier below subsequence, so no target match can ever
        // outrank a name match and the §4.5 order among name matches is
        // untouched. How the alias matched is deliberately not preserved:
        // ranking target matches against each other by tier would promote a
        // vague name match to above a precise one.
        if (rank == MatchRank::NoMatch && !catalog[i].search_alias.empty() &&
            Rank(catalog[i].search_alias, normalized_query) != MatchRank::NoMatch) {
            rank = MatchRank::Alias;
        }
        if (rank != MatchRank::NoMatch) {
            ranked.push_back({rank, i});
        }
    }

    std::sort(ranked.begin(), ranked.end(),
              [&catalog](const Ranked& left, const Ranked& right) {
                  const AppEntry& left_entry = catalog[left.index];
                  const AppEntry& right_entry = catalog[right.index];
                  if (left.rank != right.rank) {
                      return left.rank < right.rank;
                  }
                  if (left_entry.is_pinned != right_entry.is_pinned) {
                      return left_entry.is_pinned > right_entry.is_pinned;
                  }
                  if (left_entry.usage_score != right_entry.usage_score) {
                      return left_entry.usage_score > right_entry.usage_score;
                  }
                  if (left_entry.display_name.size() != right_entry.display_name.size()) {
                      return left_entry.display_name.size() < right_entry.display_name.size();
                  }
                  const std::wstring_view left_name = NormalizedName(left_entry);
                  const std::wstring_view right_name = NormalizedName(right_entry);
                  if (left_name != right_name) {
                      return left_name < right_name;
                  }
                  return left_entry.stable_id < right_entry.stable_id;
              });

    std::vector<AppEntry> result;
    result.reserve(ranked.size());
    for (const Ranked& item : ranked) {
        result.push_back(catalog[item.index]);
    }
    return result;
}

} // namespace nimblerun
