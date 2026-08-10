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

// Every source, in the coordinator's stable iteration order (design-spec
// §FR-008). The "all sources" set for a full rebuild; shared by the
// coordinator and the host's four all-sources StartRebuild call sites.
inline constexpr CatalogSource kSources[] = {
    CatalogSource::StartMenu,
    CatalogSource::AppsFolder,
    CatalogSource::UserFolder,
};

// NR-124: one generation's diagnostic counters. corrupt_links (Start Menu) and
// skipped_directories (UserFolder) are folded in from the enumerator results by
// ApplySourceResult; ambiguous_kept / removed_duplicates are the dedup pass's
// own counters (design-spec §FR-007 item 3). Pure value: no HWND, no Shell.
struct GenerationDiagnostics {
    std::size_t corrupt_links = 0;        // StartMenu: unloadable .lnk skipped
    std::size_t skipped_directories = 0;  // UserFolder: unopenable dirs skipped
    std::size_t ambiguous_kept = 0;       // dedup: kept unjudgeable name peers
    std::size_t removed_duplicates = 0;   // dedup: collapsed exact duplicates
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

    // NR-118: true when a rebuild should start now -- at least one source is due
    // and no generation is running. Starting a partial rebuild while one runs
    // would supersede it (BeginGeneration drops the in-flight generation), which
    // at cold start leaves never-enumerated sources with only unverified cache
    // rows or nothing at all (design-spec §FR-008). Callers that always rebuild
    // every source (Ctrl+R, launch-failure, settings apply, startup) may bypass
    // this and still supersede.
    bool ShouldStartRebuild(std::int64_t now_ms) const;

    // Begins a rebuild cycle over the given sources. Any ApplySourceResult/
    // Failure carrying an older generation than the latest one is ignored, so a
    // stale worker never overwrites a newer snapshot. The merged snapshot is
    // recomputed only after every source in the cycle has reported (success or
    // failure), so the panel never sees a partial build.
    std::uint64_t BeginGeneration(std::vector<CatalogSource> sources);

    // Applies one source's fresh enumeration for `generation`. The source keeps
    // its old entries when the generation is stale. `diagnostics` carries the
    // enumerator's per-source counts and is folded into
    // LastGenerationDiagnostics() for this generation.
    bool ApplySourceResult(std::uint64_t generation, CatalogSource source,
                           std::vector<AppEntry> entries,
                           const GenerationDiagnostics& diagnostics = {});

    // Records one source's failure for `generation`: the source keeps its old
    // entries, other sources' results still apply (design-spec §FR-008).
    bool ApplySourceFailure(std::uint64_t generation, CatalogSource source);

    // Records a successful AppsFolder enumeration: the staleness clock restarts
    // from `now_ms` and the "never succeeded" state is cleared (NR-095).
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

    // NR-116: at startup the host loads catalog.cache into the merged snapshot
    // (SetSnapshot) and then calls this to seed the per-source "old entries" the
    // coordinator retains on failure. Splitting the cache snapshot by AppSource
    // means a source that FAILS the first rebuild keeps its cached rows (displayed,
    // still launch_verified == false per NR-113) instead of vanishing from the
    // panel and wiping its usage records (§FR-008). Entries keep their values
    // untouched, including launch_verified. Only the host's startup cache-load path
    // calls this; RebuildMerged's own SetSnapshot must NOT re-seed (it would
    // overwrite per-source losers with dedup winners).
    void SeedSourceEntriesFromSnapshot();

    // True when every source in the current generation has reported (success or
    // failure). NR-063: exposed for the host to reset its launch-failure gate
    // only when a whole rebuild cycle has finished, not on the first source.
    bool GenerationComplete(std::uint64_t generation) const;

    // NR-124: the current generation's diagnostic counters. Zeroed by
    // BeginGeneration, folded in by ApplySourceResult and the dedup pass, so
    // the host reads it once when the generation completes and writes at most
    // three sanitized log lines (design-spec §FR-014).
    const GenerationDiagnostics& LastGenerationDiagnostics() const {
        return generation_diagnostics_;
    }

private:
    void RebuildMerged();

    std::unordered_map<CatalogSource, std::vector<AppEntry>> source_entries_;
    std::unordered_map<CatalogSource, bool> pending_;
    std::unordered_map<CatalogSource, std::int64_t> last_event_ms_;
    // NR-065: per-source event timestamp as of BeginGeneration. ApplySourceResult
    // and ApplySourceFailure clear pending_ only when the timestamp is still the
    // snapshot, so an event that arrived while a scan was in flight survives to
    // trigger the existing debounce timer for a second, fresher rebuild.
    std::unordered_map<CatalogSource, std::int64_t> generation_event_snapshot_;
    std::vector<CatalogSource> active_sources_;
    std::unordered_map<CatalogSource, bool> received_;
    std::uint64_t generation_ = 0;
    // NR-095: timestamp of the last successful AppsFolder enumeration, or 0 when
    // it happened at monotonic time 0. The flag is what distinguishes "never
    // succeeded" from a genuine success at t=0.
    std::int64_t last_appsfolder_success_ms_ = 0;
    bool appsfolder_has_success_ = false;
    std::vector<AppEntry> merged_;
    // NR-124: diagnostic counters for the current generation (see
    // LastGenerationDiagnostics).
    GenerationDiagnostics generation_diagnostics_;
};

// NR-124: the up-to-three sanitized diagnostic detail strings for a completed
// generation ("<source-type> <count>", no paths, design-spec §FR-014). Empty
// when every count is zero, so a clean generation writes nothing (zero noise).
// The dedup pair shares one line, so a generation can emit at most three lines:
// startmenu corrupt-links N, userfolder skipped-directories N, dedup ambiguous
// N removed M.
std::vector<std::wstring> RebuildDiagnosticLines(const GenerationDiagnostics& diagnostics);

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
