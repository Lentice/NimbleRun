#pragma once

#include <cstdint>
#include <string>
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

    // Recent list: newest last-launch first, ties broken by ascending
    // stable_id so the order is deterministic, capped at `cap`. Empty when
    // there are no records; never pads with other apps.
    std::vector<UsageRecord> Recent(int cap = 20) const;

private:
    std::wstring directory_;
    std::vector<UsageRecord> records_;
};

} // namespace nimblerun
