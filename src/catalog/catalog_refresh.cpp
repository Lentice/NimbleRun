#include "catalog/catalog_refresh.h"

#include "catalog/dedup.h"
#include "search/search_engine.h"

#include <algorithm>
#include <limits>

namespace nimblerun {
namespace {

constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::min();

constexpr CatalogSource kSources[] = {
    CatalogSource::StartMenu,
    CatalogSource::AppsFolder,
    CatalogSource::UserFolder,
};

} // namespace

void CatalogRefreshCoordinator::NotifySourceEvent(CatalogSource source, std::int64_t now_ms) {
    pending_[source] = true;
    last_event_ms_[source] = now_ms;
}

void CatalogRefreshCoordinator::MarkSourceFullRescan(CatalogSource source) {
    pending_[source] = true;
    last_event_ms_[source] = kNever;  // always due, no debounce wait
}

bool CatalogRefreshCoordinator::HasDueRebuild(std::int64_t now_ms) const {
    for (const CatalogSource source : kSources) {
        const auto event = last_event_ms_.find(source);
        if (pending_.count(source) != 0 && pending_.at(source) &&
            event != last_event_ms_.end() &&
            (event->second == kNever || now_ms - event->second >= kDebounceMs)) {
            return true;
        }
    }
    return false;
}

std::vector<CatalogSource> CatalogRefreshCoordinator::DueSources(std::int64_t now_ms) const {
    std::vector<CatalogSource> due;
    for (const CatalogSource source : kSources) {
        const auto event = last_event_ms_.find(source);
        if (pending_.count(source) != 0 && pending_.at(source) &&
            event != last_event_ms_.end() &&
            (event->second == kNever || now_ms - event->second >= kDebounceMs)) {
            due.push_back(source);
        }
    }
    return due;
}

bool CatalogRefreshCoordinator::ShouldRefreshAppsFolder(std::int64_t now_ms) const {
    return now_ms - last_appsfolder_success_ms_ >= kAppsFolderStaleMs;
}

bool CatalogRefreshCoordinator::IsRebuildInProgress() const {
    return !active_sources_.empty() && !GenerationComplete(generation_);
}

std::uint64_t CatalogRefreshCoordinator::BeginGeneration(std::vector<CatalogSource> sources) {
    ++generation_;
    active_sources_ = std::move(sources);
    received_.clear();
    for (const CatalogSource source : active_sources_) {
        received_[source] = false;
    }
    return generation_;
}

bool CatalogRefreshCoordinator::ApplySourceResult(std::uint64_t generation,
                                                  CatalogSource source,
                                                  std::vector<AppEntry> entries) {
    if (generation != generation_) {
        return false;  // a newer rebuild started; this worker is stale
    }
    source_entries_[source] = std::move(entries);
    pending_[source] = false;
    received_[source] = true;
    if (GenerationComplete(generation)) {
        RebuildMerged();
    }
    return true;
}

bool CatalogRefreshCoordinator::ApplySourceFailure(std::uint64_t generation,
                                                   CatalogSource source) {
    if (generation != generation_) {
        return false;
    }
    pending_[source] = false;
    received_[source] = true;  // the source's old entries stay; others still apply
    if (GenerationComplete(generation)) {
        RebuildMerged();
    }
    return true;
}

void CatalogRefreshCoordinator::RecordAppsFolderSuccess(std::int64_t now_ms) {
    last_appsfolder_success_ms_ = now_ms;
}

const std::vector<AppEntry>& CatalogRefreshCoordinator::SourceEntries(
    CatalogSource source) const {
    static const std::vector<AppEntry> kEmpty;
    const auto it = source_entries_.find(source);
    return it == source_entries_.end() ? kEmpty : it->second;
}

void CatalogRefreshCoordinator::SetSnapshot(std::vector<AppEntry> merged) {
    // NR-038: the sole place a published snapshot gets its normalized names, so
    // SearchApps never re-normalizes per keystroke. Only fill when empty: the disk
    // cache already carries the value (catalog_cache field 3), and a test may
    // supply its own.
    for (AppEntry& entry : merged) {
        if (entry.normalized_name.empty()) {
            entry.normalized_name = NormalizeName(entry.display_name);
        }
    }
    merged_ = std::move(merged);
}

bool CatalogRefreshCoordinator::GenerationComplete(std::uint64_t generation) const {
    if (generation != generation_) {
        return false;
    }
    if (active_sources_.empty()) {
        return false;
    }
    for (const CatalogSource source : active_sources_) {
        const auto it = received_.find(source);
        if (it == received_.end() || !it->second) {
            return false;
        }
    }
    return true;
}

void CatalogRefreshCoordinator::RebuildMerged() {
    std::vector<AppEntry> merged;
    for (const CatalogSource source : kSources) {
        const auto it = source_entries_.find(source);
        if (it != source_entries_.end()) {
            merged.insert(merged.end(), it->second.begin(), it->second.end());
        }
    }
    SetSnapshot(DeduplicateCatalog(merged).entries);
}

bool LaunchFailureRefreshGate::OnLaunchAttempt(bool launch_succeeded,
                                              bool refresh_in_progress) {
    if (launch_succeeded) {
        scheduled_ = false;
        return false;
    }
    if (refresh_in_progress || scheduled_) {
        return false;  // merge into the running / already-scheduled refresh
    }
    scheduled_ = true;
    return true;
}

void LaunchFailureRefreshGate::OnRefreshComplete() {
    scheduled_ = false;
}

} // namespace nimblerun
