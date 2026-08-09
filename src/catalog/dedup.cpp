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
// Never a reason to merge; only a diagnostic.
bool UnjudgeableNameCollision(const AppEntry& a, const AppEntry& b) {
    if (a.stable_id == b.stable_id) {
        return false;  // same app: merged, not ambiguous
    }
    if (ToLower(a.display_name) != ToLower(b.display_name)) {
        return false;  // not name-related: unrelated apps
    }
    const bool a_shell = a.source == AppSource::AppsFolder;
    const bool b_shell = b.source == AppSource::AppsFolder;
    return a_shell != b_shell;
}

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

    // ponytail: O(n^2) name-collision scan; catalog is bounded by design
    // (FR-003, <5k entries), switch to a name-keyed index if it ever matters.
    std::vector<bool> ambiguous(result.entries.size(), false);
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < result.entries.size(); ++j) {
            if (UnjudgeableNameCollision(result.entries[i], result.entries[j])) {
                ambiguous[i] = true;
                ambiguous[j] = true;
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
