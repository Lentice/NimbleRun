#pragma once

#include "catalog/app_entry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nimblerun {

// One app's usage record, keyed by stable_id (never display name). The clock
// value is injected by the caller (UTC epoch seconds), keeping the store pure,
// deterministic, and testable.
struct UsageRecord {
    std::wstring stable_id;
    std::uint64_t total_launches = 0;
    std::int64_t last_launch_utc = 0;
};

// Why Load returned. For anything other than Loaded the store is empty and the
// original file follows the settings-store conventions (design-spec §10.4).
enum class UsageLoadResult {
    Loaded,       // usage.tsv parsed and applied
    Missing,      // no usage.tsv yet
    Corrupt,      // unreadable/malformed; original renamed to usage.tsv.corrupt
    NewerSchema,  // schema version too new; original left untouched
};

// Pure per-user recent-usage store. No HWND, Shell, or COM dependencies.
// Persists to usage.tsv (versioned UTF-8 TSV) using the same tmp + flush +
// atomic replace scheme as settings.ini (design-spec §10.2). Records are keyed
// by stable ID and are kept even when the app is temporarily absent from the
// catalog (design-spec §FR-011); the caller decides which records are visible.
class UsageStore {
public:
    explicit UsageStore(std::wstring directory);

    // Loads usage.tsv, replacing any in-memory records.
    UsageLoadResult Load();

    // Persists the current records atomically. On any I/O failure the previous
    // file (if any) is left untouched.
    bool Save() const;

    // Clears all usage records and persists an empty file. On a persistence
    // failure the in-memory records are restored and false is returned, so a
    // failed clear never loses the previous history.
    bool Clear();

    // Records one successful launch at `last_launch_utc` (UTC epoch seconds,
    // injected by the caller). Only success paths should call this: a failed
    // launch never updates recent. Returns false for an empty stable_id.
    bool RecordLaunch(std::wstring stable_id, std::int64_t last_launch_utc);

    // Drops a single app's usage record (NR-040 "Remove from recent"). Returns
    // false when there is no record for stable_id, in which case nothing
    // changed and the caller should not Save(). Persistence is the caller's
    // job, exactly as it is for RecordLaunch().
    bool Forget(std::wstring_view stable_id);

    // NR-061: drops every record whose stable_id is absent from `catalog`, so
    // an uninstalled app cannot reappear in the recent region with its old
    // score after a reinstall. Mirrors PinStore::Reconcile's contract: the
    // caller must pass a real (non-empty) catalog snapshot -- reconciling
    // against an empty one during startup would wipe every record. Returns
    // false when nothing was dropped, in which case the caller must not Save().
    bool Reconcile(const std::vector<AppEntry>& catalog);

    // Recent list: newest last-launch first, ties broken by ascending
    // stable_id so the order is deterministic, capped at `cap`. Empty when
    // there are no records; never pads with other apps.
    std::vector<UsageRecord> Recent(int cap = 20) const;

    // All records, unordered. For scoring the whole catalog the caller needs
    // every record, not just the capped recent window.
    const std::vector<UsageRecord>& Records() const { return records_; }

private:
    std::wstring directory_;
    std::vector<UsageRecord> records_;
    // NR-096: true when the last Load() reported NewerSchema. The original file
    // is another build's data (design-spec §10.4); Save() must refuse to
    // overwrite it. Cleared by every non-NewerSchema Load outcome. mutable
    // because Save() is const.
    mutable bool write_protected_ = false;
};

// Usage score for one record at `now_utc` (design-spec §4.6): a launch-count
// term plus a recency bonus of 8 within 24h, 4 within 7 days, 1 within 30 days,
// 0 beyond. Pure, so the boundaries are testable without a clock.
//
// ponytail: §4.6 asks for launch_count_30d + 3 x launch_count_7d, but usage.tsv
// only keeps a lifetime total and the last launch time, so the total stands in
// for the 30-day count and the 7-day term is dropped. The recency bonus is what
// makes a just-launched app win a tie, which is the point. Upgrade path if the
// ordering ever feels wrong: bump the usage.tsv schema to keep per-window
// counters (or a bounded launch-timestamp log) and compute both terms here.
int UsageScore(const UsageRecord& record, std::int64_t now_utc);

} // namespace nimblerun
