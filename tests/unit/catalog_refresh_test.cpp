#include "catalog/catalog_cache.h"
#include "catalog/catalog_refresh.h"
#include "search/search_engine.h"
#include "settings/settings_store.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::CatalogRefreshCoordinator;
using nimblerun::CatalogSource;
using nimblerun::LoadCatalogCache;
using nimblerun::SaveCatalogCache;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

AppEntry Entry(std::wstring id, AppSource source) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = id;
    entry.normalized_name = id;
    entry.launch_identity = L"C:\\Apps\\" + entry.display_name + L".exe";
    entry.source_path = entry.launch_identity;
    entry.source = source;
    return entry;
}

std::wstring TempDir() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    (void)length;
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_catalog_refresh_test_" +
        std::to_wstring(GetCurrentProcessId());
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// Best-effort fixture cleanup so a transient OS/AV hold never fails the test.
void RemoveTreeBestEffort(const std::wstring& path) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::error_code ec;
        fs::remove_all(fs::path(path), ec);
        if (!ec || !fs::exists(fs::path(path), ec)) {
            return;
        }
        Sleep(150);
    }
}

// Dense events inside the 500 ms window coalesce into one rebuild: only one
// generation is due at the end of the window, not one per event.
void TestDebounceCoalescing() {
    CatalogRefreshCoordinator c;
    c.NotifySourceEvent(CatalogSource::StartMenu, 1000);
    c.NotifySourceEvent(CatalogSource::StartMenu, 1100);
    c.NotifySourceEvent(CatalogSource::StartMenu, 1300);
    Expect(!c.HasDueRebuild(1500), "events inside the debounce window are not due");
    Expect(c.HasDueRebuild(1900), "after 500 ms the merged rebuild is due");
    const auto due = c.DueSources(1900);
    Expect(due.size() == 1 && due[0] == CatalogSource::StartMenu,
           "one merged rebuild for the source, not one per event");
}

// Buffer overflow marks the source for an immediate full rescan.
void TestOverflowForcesFullRescan() {
    CatalogRefreshCoordinator c;
    c.MarkSourceFullRescan(CatalogSource::UserFolder);
    Expect(c.HasDueRebuild(0), "full-rescan marker is due immediately");
    const auto due = c.DueSources(0);
    Expect(due.size() == 1 && due[0] == CatalogSource::UserFolder,
           "overflowed source is due for a full rescan");
}

// NR-065: a scan with no event arriving during it clears the pending flag as
// before -- this round of results is the latest, so the debounce timer has
// nothing left to do.
void TestResultNoEventDuringScanClearsPending() {
    CatalogRefreshCoordinator c;
    c.NotifySourceEvent(CatalogSource::StartMenu, 1000);
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"a", AppSource::UserStartMenu)});
    Expect(!c.HasDueRebuild(1000 + CatalogRefreshCoordinator::kDebounceMs),
           "no event during the scan clears pending");
}

// NR-065: an event arriving after BeginGeneration must survive the apply that
// finishes the scan. The pending flag stays set, so the existing debounce timer
// triggers a second, fresher rebuild -- the mid-scan change is not dropped.
void TestResultEventDuringScanKeepsPending() {
    CatalogRefreshCoordinator c;
    c.NotifySourceEvent(CatalogSource::StartMenu, 1000);
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.NotifySourceEvent(CatalogSource::StartMenu, 1200);  // mid-scan event
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"a", AppSource::UserStartMenu)});
    const auto due = c.DueSources(1200 + CatalogRefreshCoordinator::kDebounceMs);
    Expect(due.size() == 1 && due[0] == CatalogSource::StartMenu,
           "event during the scan keeps pending for a second rebuild");
}

// NR-065: the failure wrap-up clears pending normally when nothing changed
// mid-scan.
void TestFailureNoEventDuringScanClearsPending() {
    CatalogRefreshCoordinator c;
    c.NotifySourceEvent(CatalogSource::StartMenu, 1000);
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceFailure(gen, CatalogSource::StartMenu);
    Expect(!c.HasDueRebuild(1000 + CatalogRefreshCoordinator::kDebounceMs),
           "failed scan with no new event clears pending");
}

// NR-065: the failure wrap-up must not drop an event that arrived while the
// failed scan was in flight either.
void TestFailureEventDuringScanKeepsPending() {
    CatalogRefreshCoordinator c;
    c.NotifySourceEvent(CatalogSource::StartMenu, 1000);
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.NotifySourceEvent(CatalogSource::StartMenu, 1200);  // mid-scan event
    c.ApplySourceFailure(gen, CatalogSource::StartMenu);
    const auto due = c.DueSources(1200 + CatalogRefreshCoordinator::kDebounceMs);
    Expect(due.size() == 1 && due[0] == CatalogSource::StartMenu,
           "event during a failed scan keeps pending for a second rebuild");
}

// An older generation completing after a newer one never overwrites the newer
// snapshot.
void TestStaleGenerationDoesNotOverwrite() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen_old =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder});
    const std::uint64_t gen_new = c.BeginGeneration({CatalogSource::StartMenu});
    Expect(gen_new > gen_old, "generations are strictly increasing");

    c.ApplySourceResult(gen_new, CatalogSource::StartMenu,
                        {Entry(L"new", AppSource::UserStartMenu)});
    const std::vector<AppEntry> fresh = c.Snapshot();
    Expect(fresh.size() == 1 && fresh[0].stable_id == L"new", "newest generation applied");

    c.ApplySourceResult(gen_old, CatalogSource::StartMenu,
                        {Entry(L"old", AppSource::UserStartMenu)});
    const std::vector<AppEntry> after_stale = c.Snapshot();
    Expect(after_stale.size() == 1 && after_stale[0].stable_id == L"new",
           "stale generation does not overwrite the newer snapshot");
}

// A failed refresh keeps the last usable snapshot.
void TestFailureKeepsOldSnapshot() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"good", AppSource::UserStartMenu)});
    Expect(c.Snapshot().size() == 1, "initial snapshot present");

    const std::uint64_t gen_fail = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceFailure(gen_fail, CatalogSource::StartMenu);
    const std::vector<AppEntry> after_fail = c.Snapshot();
    Expect(after_fail.size() == 1 && after_fail[0].stable_id == L"good",
           "failed refresh keeps the last usable snapshot");
}

// One source failing keeps its old result while other sources' new results
// still apply (design-spec §FR-008).
void TestSingleSourceFailureIsolation() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"start", AppSource::UserStartMenu)});
    c.ApplySourceResult(gen, CatalogSource::AppsFolder,
                        {Entry(L"apps", AppSource::AppsFolder)});

    const std::uint64_t gen2 =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder});
    c.ApplySourceResult(gen2, CatalogSource::StartMenu,
                        {Entry(L"start2", AppSource::UserStartMenu)});
    c.ApplySourceFailure(gen2, CatalogSource::AppsFolder);

    const std::vector<AppEntry> after = c.Snapshot();
    Expect(after.size() == 2, "two sources represented after isolation");
    bool found_start = false;
    bool found_apps = false;
    for (const AppEntry& entry : after) {
        if (entry.stable_id == L"start2") {
            found_start = true;
        }
        if (entry.stable_id == L"apps") {
            found_apps = true;  // AppsFolder kept its old result
        }
    }
    Expect(found_start && found_apps,
           "failed source keeps old entries, others apply their new ones");
}

// The merged snapshot is not swapped until every source in a generation has
// reported, so the panel never sees a partial build.
void TestNoPartialSnapshotBeforeAllSourcesReport() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen = c.BeginGeneration(
        {CatalogSource::StartMenu, CatalogSource::AppsFolder, CatalogSource::UserFolder});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"start", AppSource::UserStartMenu)});
    c.ApplySourceResult(gen, CatalogSource::AppsFolder,
                        {Entry(L"apps", AppSource::AppsFolder)});
    // Only two of three sources have reported: the snapshot stays as it was.
    Expect(c.Snapshot().empty(), "no partial snapshot before all sources report");
    c.ApplySourceResult(gen, CatalogSource::UserFolder,
                        {Entry(L"user", AppSource::UserFolder)});
    Expect(c.Snapshot().size() == 3, "snapshot swaps only when the generation is complete");
}

// NR-100: a source whose result could not be delivered (PostMessageW failed)
// still completes the generation when the UI drains it as a source failure.
// ApplySourceFailure keeps the delivered StartMenu entries, marks the source
// received, and the generation finishes instead of stalling in
// IsRebuildInProgress() forever (design-spec §FR-008).
void TestDeliveryFailureCompletesGeneration() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen = c.BeginGeneration(
        {CatalogSource::StartMenu, CatalogSource::AppsFolder, CatalogSource::UserFolder});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"start", AppSource::UserStartMenu)});
    Expect(c.IsRebuildInProgress(), "generation is in progress after one source delivers");

    c.ApplySourceFailure(gen, CatalogSource::AppsFolder);
    Expect(c.IsRebuildInProgress(), "still in progress with a source still pending");

    c.ApplySourceFailure(gen, CatalogSource::UserFolder);
    Expect(c.GenerationComplete(gen), "all sources reported: generation complete");
    Expect(!c.IsRebuildInProgress(), "no rebuild in progress after the drain completes");

    const std::vector<AppEntry> after = c.Snapshot();
    Expect(after.size() == 1 && after[0].stable_id == L"start",
           "delivered StartMenu entry is kept; no partial wipe");
}

// NR-106: models a worker that never reached result allocation, handoff
// registration, or thread start. The UI-owned failure signal still marks that
// source received, preserves its old entries, and lets healthy sources finish
// the generation.
void TestSetupFailureCompletesGeneration() {
    CatalogRefreshCoordinator c;
    const std::uint64_t initial =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder});
    c.ApplySourceResult(initial, CatalogSource::StartMenu,
                        {Entry(L"old-start", AppSource::UserStartMenu)});
    c.ApplySourceResult(initial, CatalogSource::AppsFolder,
                        {Entry(L"old-app", AppSource::AppsFolder)});

    const std::uint64_t gen =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder});
    Expect(c.ApplySourceFailure(gen, CatalogSource::StartMenu),
           "setup failure completes the source through the coordinator");
    Expect(c.IsRebuildInProgress(), "healthy source is still allowed to finish");
    c.ApplySourceResult(gen, CatalogSource::AppsFolder,
                        {Entry(L"new-app", AppSource::AppsFolder)});

    Expect(c.GenerationComplete(gen), "setup failure does not strand the generation");
    Expect(!c.IsRebuildInProgress(), "setup failure generation completes");
    const std::vector<AppEntry> after = c.Snapshot();
    Expect(after.size() == 2, "old failed-source and new healthy-source entries remain");
    bool found_old_start = false;
    bool found_new_app = false;
    for (const AppEntry& entry : after) {
        found_old_start = found_old_start || entry.stable_id == L"old-start";
        found_new_app = found_new_app || entry.stable_id == L"new-app";
    }
    Expect(found_old_start && found_new_app,
           "setup failure preserves the old source while healthy source publishes");
}

// NR-115: models several sources whose result AND wake-up posts both failed
// (PostMessageW queue full), so only the manual-reset event signal reaches the
// UI. The UI drains the recorded failures in ONE pass -- ApplySourceFailure per
// entry, exactly as DrainRebuildDeliveryFailures does -- and the generation
// completes with the healthy source's entry and the failed sources' old entries.
void TestFailureWakeupDrainCompletesGeneration() {
    CatalogRefreshCoordinator c;
    const std::uint64_t initial =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder,
                           CatalogSource::UserFolder});
    c.ApplySourceResult(initial, CatalogSource::AppsFolder,
                        {Entry(L"old-app", AppSource::AppsFolder)});
    c.ApplySourceResult(initial, CatalogSource::UserFolder,
                        {Entry(L"old-user", AppSource::UserFolder)});

    const std::uint64_t gen =
        c.BeginGeneration({CatalogSource::StartMenu, CatalogSource::AppsFolder,
                           CatalogSource::UserFolder});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"new-start", AppSource::UserStartMenu)});
    Expect(c.IsRebuildInProgress(), "two failed-wakeup sources still pending");

    Expect(c.ApplySourceFailure(gen, CatalogSource::AppsFolder),
           "recorded AppsFolder failure applies exactly once");
    Expect(c.ApplySourceFailure(gen, CatalogSource::UserFolder),
           "recorded UserFolder failure applies exactly once");

    Expect(c.GenerationComplete(gen), "all sources reported: generation complete");
    Expect(!c.IsRebuildInProgress(), "no rebuild in progress after the drain");
    const std::vector<AppEntry> after = c.Snapshot();
    bool found_new_start = false;
    bool found_old_app = false;
    bool found_old_user = false;
    for (const AppEntry& entry : after) {
        found_new_start = found_new_start || entry.stable_id == L"new-start";
        found_old_app = found_old_app || entry.stable_id == L"old-app";
        found_old_user = found_old_user || entry.stable_id == L"old-user";
    }
    Expect(found_new_start && found_old_app && found_old_user,
           "healthy new entry and both failed sources' old entries remain");
}

// AppsFolder on-demand rule: no refresh under 10 minutes, refresh when older.
void TestAppsFolderStaleness() {
    CatalogRefreshCoordinator c;
    c.RecordAppsFolderSuccess(0);
    Expect(!c.ShouldRefreshAppsFolder(1000),
           "recent AppsFolder enumeration is not refreshed");
    Expect(c.ShouldRefreshAppsFolder(CatalogRefreshCoordinator::kAppsFolderStaleMs + 1),
           "AppsFolder older than 10 minutes is refreshed on demand");
}

// NR-095: no successful AppsFolder enumeration yet is not "succeeded at
// monotonic t=0" -- the very first scan failed at low uptime, so the next panel
// show must retry immediately instead of waiting out the 10-minute clock.
void TestAppsFolderNeverSucceededIsDueAtLowUptime() {
    CatalogRefreshCoordinator c;
    Expect(c.ShouldRefreshAppsFolder(1000),
           "a never-succeeded AppsFolder is due at low uptime");
}

// NR-095: a failed AppsFolder-only generation must not clear the never-succeeded
// state. After the failure the next panel show still retries.
void TestAppsFolderFailureKeepsDue() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::AppsFolder});
    c.ApplySourceFailure(gen, CatalogSource::AppsFolder);
    Expect(c.ShouldRefreshAppsFolder(1000),
           "a failed AppsFolder generation keeps the source due at low uptime");
}

// NR-081: the on-demand rule must not fire while a rebuild cycle is running --
// BeginGeneration supersedes the in-flight generation, so a ShowPanel-triggered
// {AppsFolder} cycle would drop the running full rebuild's StartMenu/UserFolder
// results as stale. Once the cycle completes, the staleness check applies again.
void TestAppsFolderStalenessSkipsRunningRebuild() {
    CatalogRefreshCoordinator c;
    c.RecordAppsFolderSuccess(0);
    const std::int64_t stale = CatalogRefreshCoordinator::kAppsFolderStaleMs + 1;
    Expect(c.ShouldRefreshAppsFolder(stale),
           "no running rebuild: a stale AppsFolder is due for an on-demand refresh");

    const std::uint64_t gen = c.BeginGeneration(
        {CatalogSource::StartMenu, CatalogSource::AppsFolder});
    Expect(c.IsRebuildInProgress(), "an open generation is a rebuild in progress");
    Expect(!c.ShouldRefreshAppsFolder(stale),
           "a running rebuild suppresses the on-demand AppsFolder refresh");

    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"start", AppSource::UserStartMenu)});
    c.ApplySourceResult(gen, CatalogSource::AppsFolder,
                        {Entry(L"apps", AppSource::AppsFolder)});
    Expect(!c.IsRebuildInProgress(), "the completed cycle is no longer in progress");
    Expect(c.ShouldRefreshAppsFolder(stale),
           "after the cycle completes the staleness check applies again");
}

// Snapshot is recomputed deterministically and atomic per swap: the caller sees
// either the old or the new merged list, never a partial build.
void TestSnapshotIsAtomicAndDeterministic() {
    CatalogRefreshCoordinator c;
    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceResult(gen, CatalogSource::StartMenu,
                        {Entry(L"a", AppSource::UserStartMenu)});
    const std::vector<AppEntry> first = c.Snapshot();
    const std::uint64_t gen2 = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceResult(gen2, CatalogSource::StartMenu,
                        {Entry(L"a", AppSource::UserStartMenu),
                         Entry(L"b", AppSource::UserStartMenu)});
    const std::vector<AppEntry> second = c.Snapshot();
    Expect(first.size() == 1 && second.size() == 2, "snapshot swaps as a whole");
    Expect(c.Snapshot().size() == second.size(), "later snapshot stays until the next swap");
}

// NR-038: entries without a prefilled normalized_name (the real catalog
// sources never set it) get it filled from display_name by the published
// snapshot, via a completed generation.
void TestSnapshotFillsNormalizedName() {
    CatalogRefreshCoordinator c;
    AppEntry entry;
    entry.stable_id = L"paint";
    entry.display_name = L"  Paint   3D  ";
    entry.launch_identity = L"C:\\Apps\\paint.exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserStartMenu;

    const std::uint64_t gen = c.BeginGeneration({CatalogSource::StartMenu});
    c.ApplySourceResult(gen, CatalogSource::StartMenu, {entry});

    const auto& snapshot = c.Snapshot();
    Expect(snapshot.size() == 1, "generation publishes one snapshot entry");
    Expect(!snapshot[0].normalized_name.empty(), "snapshot fills normalized_name");
    Expect(snapshot[0].normalized_name ==
               nimblerun::NormalizeName(snapshot[0].display_name),
           "snapshot normalized_name equals NormalizeName(display_name)");
}

// NR-038: direct SetSnapshot (the startup cache-load path) fills names the
// same way.
void TestSetSnapshotFillsNormalizedName() {
    CatalogRefreshCoordinator c;
    AppEntry entry;
    entry.stable_id = L"upper";
    entry.display_name = L"ABC";
    entry.launch_identity = L"C:\\Apps\\upper.exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserStartMenu;

    c.SetSnapshot({entry});

    const auto& snapshot = c.Snapshot();
    Expect(snapshot.size() == 1 && snapshot[0].normalized_name == L"abc",
           "SetSnapshot fills normalized_name from display_name");
}

// NR-038: a prefilled non-empty normalized_name is never overwritten.
void TestSetSnapshotRespectsPrefilledNormalizedName() {
    CatalogRefreshCoordinator c;
    AppEntry entry;
    entry.stable_id = L"zebra";
    entry.display_name = L"Zebra";
    entry.normalized_name = L"notepad";
    entry.launch_identity = L"C:\\Apps\\zebra.exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserStartMenu;

    c.SetSnapshot({entry});

    const auto& snapshot = c.Snapshot();
    Expect(snapshot.size() == 1 && snapshot[0].normalized_name == L"notepad",
           "SetSnapshot keeps a prefilled normalized_name");
}

// NR-022: a failed launch with no rebuild running triggers exactly one refresh.
void TestFailureNoRebuildTriggersOnce() {
    nimblerun::LaunchFailureRefreshGate gate;
    int triggers = 0;
    if (gate.OnLaunchAttempt(false, false)) {
        ++triggers;
    }
    Expect(triggers == 1, "failed launch with no rebuild in progress triggers once");
}

// NR-022: a failed launch while a rebuild is running merges instead.
void TestFailureWithRebuildMerges() {
    nimblerun::LaunchFailureRefreshGate gate;
    int triggers = 0;
    if (gate.OnLaunchAttempt(false, true)) {
        ++triggers;
    }
    Expect(triggers == 0, "failed launch during a rebuild does not trigger");
}

// NR-022: two consecutive failures (the first already scheduled) schedule only
// one refresh in total.
void TestConsecutiveFailuresTriggerOnce() {
    nimblerun::LaunchFailureRefreshGate gate;
    int triggers = 0;
    if (gate.OnLaunchAttempt(false, false)) {
        ++triggers;
    }
    if (gate.OnLaunchAttempt(false, false)) {
        ++triggers;
    }
    Expect(triggers == 1, "two consecutive failed launches schedule one refresh");
}

// NR-022: a successful launch never triggers a refresh.
void TestSuccessNeverTriggers() {
    nimblerun::LaunchFailureRefreshGate gate;
    int triggers = 0;
    if (gate.OnLaunchAttempt(true, false)) {
        ++triggers;
    }
    if (gate.OnLaunchAttempt(true, true)) {
        ++triggers;
    }
    Expect(triggers == 0, "successful launch never triggers a refresh");
}

void TestCacheRoundTrip() {
    const std::wstring dir = TempDir();
    std::vector<AppEntry> entries = {
        Entry(L"id1", AppSource::UserStartMenu),
        Entry(L"id2", AppSource::AppsFolder),
        Entry(L"id3", AppSource::UserFolder),
    };
    entries[0].display_name = L"Notepad";
    entries[0].normalized_name = L"notepad";

    SaveCatalogCache(dir, entries);
    std::vector<AppEntry> loaded;
    Expect(LoadCatalogCache(dir, loaded), "valid cache loads");
    Expect(loaded.size() == 3, "cache round-trips the entry count");
    Expect(loaded[0].stable_id == L"id1" && loaded[0].display_name == L"Notepad",
           "cache preserves stable id and display name");
    Expect(loaded[1].source == AppSource::AppsFolder, "cache preserves source");
    Expect(loaded[2].launch_identity == entries[2].launch_identity,
           "cache preserves launch identity");
    RemoveTreeBestEffort(dir);
}

// NR-113: a cache entry is displayable but not launchable until a current
// source enumeration produces its identity again. SaveCatalogCache never writes
// the flag (schema stays at version 2), so every LoadCatalogCache entry comes
// back unverified while its identity still round-trips for display.
void TestCacheLoadEntriesAreUnverified() {
    const std::wstring dir = TempDir();
    std::vector<AppEntry> entries = {
        Entry(L"tampered", AppSource::UserFolder),
    };
    entries[0].launch_verified = true;  // the writer is not the trust boundary

    SaveCatalogCache(dir, entries);
    std::vector<AppEntry> loaded;
    Expect(LoadCatalogCache(dir, loaded), "valid cache loads");
    Expect(loaded.size() == 1, "one entry round-trips");
    Expect(loaded[0].stable_id == L"tampered", "stable id preserved for display");
    Expect(loaded[0].launch_identity == entries[0].launch_identity,
           "launch identity preserved for display");
    Expect(!loaded[0].launch_verified,
           "a cache-synthesized entry is not launchable until a source re-validates it");
    RemoveTreeBestEffort(dir);
}

void TestCorruptCacheRebuilds() {
    const std::wstring dir = TempDir();
    SaveCatalogCache(dir, {Entry(L"id1", AppSource::UserStartMenu)});

    std::vector<AppEntry> loaded;
    {
        std::ofstream corrupt(fs::path(dir) / L"catalog.cache",
                              std::ios::binary | std::ios::trunc);
        corrupt << "not a cache";
    }
    Expect(!LoadCatalogCache(dir, loaded), "corrupt cache fails to load");
    Expect(loaded.empty(), "no partial entries from a corrupt cache");
    Expect(fs::exists(fs::path(dir) / L"catalog.cache.corrupt"),
           "corrupt cache is preserved for diagnostics");
    RemoveTreeBestEffort(dir);
}

void TestNewerSchemaCacheRebuilds() {
    const std::wstring dir = TempDir();
    {
        std::ofstream cache(fs::path(dir) / L"catalog.cache", std::ios::binary | std::ios::trunc);
        cache << "schema=999\n";
    }  // closed and flushed before Load (NR-079)
    std::vector<AppEntry> loaded;
    bool newer_schema = false;
    Expect(!LoadCatalogCache(dir, loaded, &newer_schema), "newer schema cache is not loaded");
    Expect(newer_schema, "newer schema is reported through the out-param");
    Expect(fs::exists(fs::path(dir) / L"catalog.cache"),
           "newer schema file is left untouched");
    RemoveTreeBestEffort(dir);
}

// NR-079: a newer schema is another build's data; the report lets the host stop
// overwriting it, and `out` must stay untouched so nothing partial leaks in.
void TestNewerSchemaCacheReportsAndLeavesOutUntouched() {
    const std::wstring dir = TempDir();
    {
        std::ofstream cache(fs::path(dir) / L"catalog.cache", std::ios::binary | std::ios::trunc);
        cache << "schema=3\n";
    }  // closed and flushed before Load (NR-079)
    std::vector<AppEntry> loaded = {Entry(L"seed", AppSource::UserStartMenu)};
    bool newer_schema = false;
    Expect(!LoadCatalogCache(dir, loaded, &newer_schema), "newer schema cache is not loaded");
    Expect(newer_schema, "newer schema is reported through the out-param");
    Expect(loaded.size() == 1 && loaded[0].stable_id == L"seed",
           "a failed newer-schema load leaves `out` untouched");
    Expect(fs::exists(fs::path(dir) / L"catalog.cache"),
           "newer schema file is left untouched");
    Expect(!fs::exists(fs::path(dir) / L"catalog.cache.corrupt"),
           "newer schema file is not quarantined as corrupt");
    RemoveTreeBestEffort(dir);
}

// NR-047: SetSnapshot normalizes search_alias the way it normalizes
// normalized_name, and leaves an empty alias empty (UserFolder entries).
void TestSetSnapshotNormalizesSearchAlias() {
    CatalogRefreshCoordinator c;
    AppEntry calc;
    calc.stable_id = L"calc";
    calc.display_name = L"計算機";
    calc.search_alias = L"  CALC  ";
    calc.launch_identity = L"C:\\Apps\\calc.exe";
    calc.source_path = calc.launch_identity;
    calc.source = AppSource::UserStartMenu;

    AppEntry user;
    user.stable_id = L"user";
    user.display_name = L"User Folder App";
    user.launch_identity = L"C:\\Apps\\user.exe";
    user.source_path = user.launch_identity;
    user.source = AppSource::UserFolder;

    c.SetSnapshot({calc, user});

    const auto& snapshot = c.Snapshot();
    Expect(snapshot.size() == 2 && snapshot[0].search_alias == L"calc",
           "SetSnapshot normalizes search_alias");
    Expect(snapshot[1].search_alias.empty(),
           "SetSnapshot leaves an empty search_alias empty");
}

// NR-047: SaveCatalogCache -> LoadCatalogCache preserves search_alias,
// including an empty one and one needing escape-text round-tripping.
void TestCacheRoundTripSearchAlias() {
    const std::wstring dir = TempDir();
    std::vector<AppEntry> entries = {
        Entry(L"id1", AppSource::UserStartMenu),
        Entry(L"id2", AppSource::AppsFolder),
        Entry(L"id3", AppSource::UserFolder),
    };
    entries[0].search_alias = L"calc";
    entries[1].search_alias = L"";
    entries[2].search_alias = L"foo\tbar\\baz";

    SaveCatalogCache(dir, entries);
    std::vector<AppEntry> loaded;
    Expect(LoadCatalogCache(dir, loaded), "alias cache loads");
    Expect(loaded.size() == 3, "alias cache round-trips the entry count");
    Expect(loaded[0].search_alias == L"calc", "plain alias preserved");
    Expect(loaded[1].search_alias.empty(), "empty alias preserved as empty");
    Expect(loaded[2].search_alias == L"foo\tbar\\baz",
           "tab and backslash in the alias survive the round trip");
    RemoveTreeBestEffort(dir);
}

// NR-047: an older schema is a valid file this build cannot read; loading it
// fails and the file is left in place, not quarantined as corrupt. NR-079:
// unlike a newer schema, an older one must not set the newer-schema flag.
void TestOlderSchemaCacheRebuilds() {
    const std::wstring dir = TempDir();
    {
        std::ofstream cache(fs::path(dir) / L"catalog.cache", std::ios::binary | std::ios::trunc);
        cache << "schema=1\n";
    }  // closed and flushed before Load (NR-079)
    std::vector<AppEntry> loaded;
    bool newer_schema = false;
    Expect(!LoadCatalogCache(dir, loaded, &newer_schema), "older schema cache is not loaded");
    Expect(!newer_schema, "an older schema does not set the newer-schema flag");
    Expect(fs::exists(fs::path(dir) / L"catalog.cache"),
           "older schema file is left in place");
    Expect(!fs::exists(fs::path(dir) / L"catalog.cache.corrupt"),
           "older schema file is not quarantined as corrupt");
    RemoveTreeBestEffort(dir);
}

// NR-049: StartRebuild hands each rebuild thread a by-value Settings snapshot,
// so a later mutation of the original cannot reach into the running scan. This
// pins that Settings really is an ordinary copyable value with no shared
// buffers (AGENTS.md), which is the whole basis of the fix in main.cpp.
void TestSettingsCopyIsIndependent() {
    nimblerun::Settings original;
    original.catalog_roots.push_back({L"C:\\Tools", /*recursive=*/true});
    original.catalog_roots.push_back({L"D:\\Games", /*recursive=*/false});
    original.catalog_extensions = {L".exe", L".lnk"};
    original.include_windows_apps = true;

    const nimblerun::Settings copy = original;
    original.catalog_roots.clear();
    original.catalog_extensions.clear();
    original.include_windows_apps = false;

    Expect(copy.include_windows_apps, "copied Settings keeps include_windows_apps");
    Expect(copy.catalog_roots.size() == 2, "copied Settings keeps catalog_roots");
    Expect(copy.catalog_roots[0].path == L"C:\\Tools" &&
               copy.catalog_roots[0].recursive,
           "copied root path content survives a mutation of the original");
    Expect(copy.catalog_roots[1].path == L"D:\\Games" &&
               !copy.catalog_roots[1].recursive,
           "second copied root survives too");
    Expect(copy.catalog_extensions.size() == 2 &&
               copy.catalog_extensions[0] == L".exe" &&
               copy.catalog_extensions[1] == L".lnk",
           "copied catalog_extensions are untouched");
}

} // namespace

int wmain() {
    TestDebounceCoalescing();
    TestOverflowForcesFullRescan();
    TestResultNoEventDuringScanClearsPending();
    TestResultEventDuringScanKeepsPending();
    TestFailureNoEventDuringScanClearsPending();
    TestFailureEventDuringScanKeepsPending();
    TestStaleGenerationDoesNotOverwrite();
    TestFailureKeepsOldSnapshot();
    TestSingleSourceFailureIsolation();
    TestAppsFolderStaleness();
    TestAppsFolderNeverSucceededIsDueAtLowUptime();
    TestAppsFolderFailureKeepsDue();
    TestAppsFolderStalenessSkipsRunningRebuild();
    TestSnapshotIsAtomicAndDeterministic();
    TestNoPartialSnapshotBeforeAllSourcesReport();
    TestDeliveryFailureCompletesGeneration();
    TestSetupFailureCompletesGeneration();
    TestFailureWakeupDrainCompletesGeneration();
    TestSnapshotFillsNormalizedName();
    TestSetSnapshotFillsNormalizedName();
    TestSetSnapshotRespectsPrefilledNormalizedName();
    TestSetSnapshotNormalizesSearchAlias();
    TestCacheRoundTrip();
    TestCacheLoadEntriesAreUnverified();
    TestCacheRoundTripSearchAlias();
    TestNewerSchemaCacheRebuilds();
    TestNewerSchemaCacheReportsAndLeavesOutUntouched();
    TestCorruptCacheRebuilds();
    TestOlderSchemaCacheRebuilds();
    TestFailureNoRebuildTriggersOnce();
    TestFailureWithRebuildMerges();
    TestConsecutiveFailuresTriggerOnce();
    TestSuccessNeverTriggers();
    TestSettingsCopyIsIndependent();
    std::printf("NR-011/NR-022 catalog refresh check PASSED\n");
    return 0;
}
