#include "catalog/dedup.h"

#include "catalog/app_filter.h"

#include <cwctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nimblerun {
namespace {

// Lower wins (better launch/icon quality, design-spec §FR-007). The AppsFolder
// item is the Shell-canonical identity with high-quality icons; the user's own
// Start Menu shortcut precedes a user-folder file, which precedes an all-users
// (Common) Start Menu shortcut.
int SourcePriority(AppSource source) {
    switch (source) {
        case AppSource::AppsFolder:
            return 0;
        case AppSource::UserStartMenu:
            return 1;
        case AppSource::UserFolder:
            return 2;
        case AppSource::CommonStartMenu:
            return 3;
    }
    return 4;  // unreachable; keeps the function total
}

// A path-identity entry (Start Menu shortcut or user-folder file) and a
// shell-canonical AppsFolder entry cannot be reliably judged the same from
// their stored identity alone (design-spec §FR-007 item 3): a packaged app's
// Start Menu shortcut is a path, its AppsFolder item a parsing name. Same
// display name + different identity + one Shell side = keep both, record it.
// Never a reason to merge; only a diagnostic. The name-equality half of this
// predicate is the bucket key of the scan in DeduplicateCatalog (NR-121), so
// the residual test inside a bucket is: distinct stable ids, one Shell side.

// Between two entries for the same physical target, the target itself beats a
// shortcut to it: the body is what the user thinks of as the app, and its path
// is the useful one to show. Only then does source precedence decide.
bool IsShortcut(const AppEntry& entry) {
    const std::wstring ext = Extension(entry.source_path);
    return ext == L".lnk" || ext == L".appref-ms";
}

bool Beats(const AppEntry& candidate, const AppEntry& kept) {
    // NR-116: a fresh current-source entry (verified) must never lose dedup to a
    // retained cache row (unverified) with the same stable_id. This only changes
    // mixed-provenance merges (cold-start retention); enumerator-produced entries
    // are uniformly verified, so the shortcut/source-priority rules below keep
    // their existing order for homogeneous input.
    if (candidate.launch_verified != kept.launch_verified) {
        return candidate.launch_verified;
    }
    if (IsShortcut(candidate) != IsShortcut(kept)) {
        return !IsShortcut(candidate);
    }
    return SourcePriority(candidate.source) < SourcePriority(kept.source);
}

} // namespace

DedupResult DeduplicateCatalog(const std::vector<AppEntry>& entries) {
    DedupResult result;
    // stable_id -> index of the kept entry in result.entries; the kept slot
    // sits at the first occurrence's position, so output order is input order.
    std::unordered_map<std::wstring, std::size_t> best;
    best.reserve(entries.size());

    for (const AppEntry& entry : entries) {
        const auto found = best.find(entry.stable_id);
        if (found == best.end()) {
            best.emplace(entry.stable_id, result.entries.size());
            result.entries.push_back(entry);
            continue;
        }
        AppEntry& kept = result.entries[found->second];
        if (Beats(entry, kept)) {
            kept = entry;  // the body, or better source precedence, wins the slot
        }
        ++result.removed_duplicates;
    }

    // NR-121: name-keyed bucketing. UnjudgeableNameCollision can only be true
    // when the two lowercased display names are equal, so the old all-pairs
    // scan reduces to bucketing kept entries by ToLower(display_name) once each
    // and comparing only inside a bucket. Marked pairs, ambiguous_kept and
    // output order are bit-for-bit the old scan's: the same predicate on
    // exactly the subset of pairs it could ever fire on.
    std::vector<bool> ambiguous(result.entries.size(), false);
    std::unordered_map<std::wstring, std::vector<std::size_t>> buckets;
    buckets.reserve(result.entries.size());
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        buckets[ToLower(result.entries[i].display_name)].push_back(i);
    }
    // ponytail: a pathological bucket of n same-named kept entries still costs
    // O(n^2) trivial comparisons -- inherent, every such pair may be ambiguous.
    // The cache row cap (kMaxCacheRows) and FR-003 bound n.
    for (const auto& bucket : buckets) {
        const std::vector<std::size_t>& indices = bucket.second;
        for (std::size_t x = 0; x < indices.size(); ++x) {
            const AppEntry& a = result.entries[indices[x]];
            for (std::size_t y = x + 1; y < indices.size(); ++y) {
                const AppEntry& b = result.entries[indices[y]];
                if (a.stable_id != b.stable_id &&
                    (a.source == AppSource::AppsFolder) !=
                        (b.source == AppSource::AppsFolder)) {
                    ambiguous[indices[x]] = true;
                    ambiguous[indices[y]] = true;
                }
            }
        }
    }
    for (const bool is_ambiguous : ambiguous) {
        if (is_ambiguous) {
            ++result.ambiguous_kept;
        }
    }
    return result;
}

} // namespace nimblerun
