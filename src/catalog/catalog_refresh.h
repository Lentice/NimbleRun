#pragma once

#include "catalog/app_entry.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nimblerun {

// The catalog sources the refresh coordinator tracks (design-spec §FR-008).
enum class CatalogSource {
    StartMenu,
    AppsFolder,
    UserFolder,
};

// Pure refresh coordinator (NR-011, design-spec §FR-008): owns per-source
// dirty/debounce state, generation counters, per-source best-known entries and
// the atomic merged snapshot. No HWND, no threads, no Shell/COM dependencies;
// the host feeds file events and applies worker enumeration results.
class CatalogRefreshCoordinator {
public:
    static constexpr std::int64_t kDebounceMs = 500;
    static constexpr std::int64_t kAppsFolderStaleMs = 10 * 60 * 1000;  // 10 minutes

    // A file event was observed for `source` at `now_ms` (monotonic).
    void NotifySourceEvent(CatalogSource source, std::int64_t now_ms);

    // Buffer overflow / ERROR_NOTIFY_ENUM_DIR: the source needs a full rescan;
    // it becomes due immediately, never waiting out the debounce.
    void MarkSourceFullRescan(CatalogSource source);

    // True when any pending source's debounce window has elapsed.
    bool HasDueRebuild(std::int64_t now_ms) const;

    // The pending sources whose debounce window has elapsed, in a stable order.
    std::vector<CatalogSource> DueSources(std::int64_t now_ms) const;

    // AppsFolder on-demand refresh rule: panel shown and the last successful
    // enumeration is older than 10 minutes (design-spec §FR-008).
    bool ShouldRefreshAppsFolder(std::int64_t now_ms) const;

    // True while a rebuild cycle is running: a generation was begun and not all
    // of its sources have reported yet. Used by the launch-failure dialog to
    // merge its one-shot refresh into the running rebuild.
    bool IsRebuildInProgress() const;

    // Begins a rebuild cycle over the given sources. Any ApplySourceResult/
    // Failure carrying an older generation than the latest one is ignored, so a
    // stale worker never overwrites a newer snapshot. The merged snapshot is
    // recomputed only after every source in the cycle has reported (success or
    // failure), so the panel never sees a partial build.
    std::uint64_t BeginGeneration(std::vector<CatalogSource> sources);

    // Applies one source's fresh enumeration for `generation`. The source keeps
    // its old entries when the generation is stale.
    bool ApplySourceResult(std::uint64_t generation, CatalogSource source,
                           std::vector<AppEntry> entries);

    // Records one source's failure for `generation`: the source keeps its old
    // entries, other sources' results still apply (design-spec §FR-008).
    bool ApplySourceFailure(std::uint64_t generation, CatalogSource source);

    void RecordAppsFolderSuccess(std::int64_t now_ms);

    // Current atomic merged snapshot (deduplicated across sources).
    const std::vector<AppEntry>& Snapshot() const { return merged_; }

    // Same snapshot, for stamping the host-derived ranking fields (is_pinned,
    // usage_score) onto it. Every rebuild recomputes merged_ from the source
    // entries and so drops those fields; the host restamps them at its single
    // post-rebuild choke point. Not for adding or removing entries.
    std::vector<AppEntry>& MutableSnapshot() { return merged_; }

    // Best-known entries per source.
    const std::vector<AppEntry>& SourceEntries(CatalogSource source) const;

    // Replaces the whole merged snapshot directly (startup cache load).
    void SetSnapshot(std::vector<AppEntry> merged);

private:
    void RebuildMerged();
    bool GenerationComplete(std::uint64_t generation) const;

    std::unordered_map<CatalogSource, std::vector<AppEntry>> source_entries_;
    std::unordered_map<CatalogSource, bool> pending_;
    std::unordered_map<CatalogSource, std::int64_t> last_event_ms_;
    std::vector<CatalogSource> active_sources_;
    std::unordered_map<CatalogSource, bool> received_;
    std::uint64_t generation_ = 0;
    std::int64_t last_appsfolder_success_ms_ = 0;
    std::vector<AppEntry> merged_;
};

// NR-022: one-shot gate for the launch-failure background refresh (design-spec
// §11: "先在背景觸發一次 Catalog refresh（已在進行則合併）"). Pure value state,
// no HWND/Shell; the host feeds each launch outcome together with the
// coordinator's rebuild state and the gate says whether to schedule. A failure
// triggers at most one refresh: a rebuild that is already running, or one
// already scheduled by an earlier failure, is merged instead, so clicking
// several dead entries cannot queue several full scans.
class LaunchFailureRefreshGate {
public:
    // `refresh_in_progress` is the coordinator's IsRebuildInProgress(). True
    // when this launch failure should schedule a refresh. A successful launch
    // resets the gate and never triggers.
    bool OnLaunchAttempt(bool launch_succeeded, bool refresh_in_progress);

    // Call when the scheduled refresh has completed so a future failure can
    // schedule a fresh refresh.
    void OnRefreshComplete();

private:
    bool scheduled_ = false;
};

} // namespace nimblerun
