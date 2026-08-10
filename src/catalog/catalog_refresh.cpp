#include "catalog/catalog_refresh.h"

#include "catalog/dedup.h"
#include "search/search_engine.h"

#include <algorithm>
#include <limits>

namespace nimblerun {
namespace {

constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::min();
// NR-065: snapshot value for a source that never had an event. Distinct from
// kNever (full-rescan marker) and from any real monotonic timestamp, so the
// "did the timestamp change during the scan?" comparison stays truthful.
constexpr std::int64_t kNoEventSentinel = std::numeric_limits<std::int64_t>::max();

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
    // NR-081: never recommend an on-demand AppsFolder refresh while a rebuild
    // cycle is running. BeginGeneration supersedes the in-flight generation, so
    // a ShowPanel-triggered {AppsFolder} cycle would drop the running full
    // rebuild's StartMenu/UserFolder results as stale (the merged snapshot
    // collapses to packaged apps and the usage reconcile in RefreshPanelSnapshot
    // wipes the dropped apps' records). The staleness check re-applies on the
    // next panel show once the cycle completes (design-spec §FR-008 "下次使用者
    // 叫出時再試"); a failed startup AppsFolder enumeration still retries then.
    if (IsRebuildInProgress()) {
        return false;
    }
    // NR-095: never succeeded is not "succeeded at monotonic t=0" -- a first
    // enumeration failure must retry on the next panel show instead of waiting
    // out the 10-minute clock.
    if (!appsfolder_has_success_) {
        return true;
    }
    return now_ms - last_appsfolder_success_ms_ >= kAppsFolderStaleMs;
}

bool CatalogRefreshCoordinator::IsRebuildInProgress() const {
    return !active_sources_.empty() && !GenerationComplete(generation_);
}

bool CatalogRefreshCoordinator::ShouldStartRebuild(std::int64_t now_ms) const {
    return !IsRebuildInProgress() && HasDueRebuild(now_ms);
}

std::uint64_t CatalogRefreshCoordinator::BeginGeneration(std::vector<CatalogSource> sources) {
    ++generation_;
    active_sources_ = std::move(sources);
    received_.clear();
    generation_event_snapshot_.clear();
    generation_diagnostics_ = {};  // NR-124: fresh counters per generation
    for (const CatalogSource source : active_sources_) {
        received_[source] = false;
        // NR-065: snapshot the event timestamp at scan start; a source that
        // never had an event stays the kNoEventSentinel, and the comparison in
        // ApplySourceResult/Failure reads the same way so an event arriving
        // mid-scan is always a visible change.
        const auto event = last_event_ms_.find(source);
        generation_event_snapshot_[source] =
            event == last_event_ms_.end() ? kNoEventSentinel : event->second;
    }
    return generation_;
}

bool CatalogRefreshCoordinator::ApplySourceResult(std::uint64_t generation,
                                                  CatalogSource source,
                                                  std::vector<AppEntry> entries,
                                                  const GenerationDiagnostics& diagnostics) {
    if (generation != generation_) {
        return false;  // a newer rebuild started; this worker is stale
    }
    // NR-139: the snapshot holds only active sources; a forged
    // kRebuildDeliveryFailedMessage can name a matching-generation source that
    // is not in the active set. Reject it before touching any state -- the
    // normal path guarantees membership, so find() never misses there.
    const auto snapshot = generation_event_snapshot_.find(source);
    if (snapshot == generation_event_snapshot_.end()) {
        return false;
    }
    // NR-124: fold the enumerator's counts into the generation diagnostics. A
    // source that failed never reaches this path, so a partial/failed walk's
    // counts are never reported as complete.
    generation_diagnostics_.corrupt_links += diagnostics.corrupt_links;
    generation_diagnostics_.skipped_directories += diagnostics.skipped_directories;
    source_entries_[source] = std::move(entries);
    // NR-065: clear pending only when no event arrived after the scan started
    // (the BeginGeneration timestamp snapshot is unchanged). An event mid-scan
    // keeps pending set, and the existing 500 ms debounce timer picks the
    // source up for a second, fresher rebuild instead of dropping the change.
    const auto event = last_event_ms_.find(source);
    const std::int64_t current =
        event == last_event_ms_.end() ? kNoEventSentinel : event->second;
    if (current == snapshot->second) {
        pending_[source] = false;
    }
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
    // NR-139: same forged-message guard as ApplySourceResult -- a non-active
    // source is not this generation's business, so reject it without throwing.
    const auto snapshot = generation_event_snapshot_.find(source);
    if (snapshot == generation_event_snapshot_.end()) {
        return false;
    }
    // NR-065: same conditional clear as ApplySourceResult -- a failure must not
    // drop an event that arrived while the failed scan was in flight.
    const auto event = last_event_ms_.find(source);
    const std::int64_t current =
        event == last_event_ms_.end() ? kNoEventSentinel : event->second;
    if (current == snapshot->second) {
        pending_[source] = false;
    }
    received_[source] = true;  // the source's old entries stay; others still apply
    if (GenerationComplete(generation)) {
        RebuildMerged();
    }
    return true;
}

void CatalogRefreshCoordinator::RecordAppsFolderSuccess(std::int64_t now_ms) {
    last_appsfolder_success_ms_ = now_ms;
    appsfolder_has_success_ = true;
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
        // NR-047: unconditional, unlike normalized_name -- an empty alias is a
        // legitimate value (UserFolder, unresolvable target), so there is no
        // "only when empty" test to make. NormalizeName is idempotent, so
        // re-normalizing the already-normalized value the disk cache carries is
        // a no-op.
        entry.search_alias = NormalizeName(entry.search_alias);
    }
    merged_ = std::move(merged);
}

void CatalogRefreshCoordinator::SeedSourceEntriesFromSnapshot() {
    // NR-116: split the startup cache snapshot back into per-source old entries.
    // RebuildMerged then keeps each entry under its own source, so a source that
    // fails the first rebuild retains its cached rows instead of dropping them
    // from the snapshot (§FR-008). Values are copied untouched (launch_verified
    // stays false for cache rows, NR-113). A switch over every source: adding an
    // AppSource without a CatalogSource mapping trips -Wswitch.
    for (const AppEntry& entry : merged_) {
        CatalogSource source = CatalogSource::UserFolder;
        switch (entry.source) {
            case AppSource::UserStartMenu:
            case AppSource::CommonStartMenu:
                source = CatalogSource::StartMenu;
                break;
            case AppSource::AppsFolder:
                source = CatalogSource::AppsFolder;
                break;
            case AppSource::UserFolder:
                source = CatalogSource::UserFolder;
                break;
        }
        source_entries_[source].push_back(entry);
    }
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
    // NR-124: keep the dedup pass's counts instead of discarding them; they are
    // the ambiguity diagnostic (design-spec §FR-007 item 3) for this generation.
    const DedupResult dedup = DeduplicateCatalog(merged);
    generation_diagnostics_.ambiguous_kept = dedup.ambiguous_kept;
    generation_diagnostics_.removed_duplicates = dedup.removed_duplicates;
    SetSnapshot(std::move(dedup.entries));
}

std::vector<std::wstring> RebuildDiagnosticLines(const GenerationDiagnostics& d) {
    std::vector<std::wstring> lines;
    if (d.corrupt_links != 0) {
        lines.push_back(L"startmenu corrupt-links " + std::to_wstring(d.corrupt_links));
    }
    if (d.skipped_directories != 0) {
        lines.push_back(L"userfolder skipped-directories " +
                        std::to_wstring(d.skipped_directories));
    }
    if (d.ambiguous_kept != 0 || d.removed_duplicates != 0) {
        lines.push_back(L"dedup ambiguous " + std::to_wstring(d.ambiguous_kept) +
                        L" removed " + std::to_wstring(d.removed_duplicates));
    }
    return lines;
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
