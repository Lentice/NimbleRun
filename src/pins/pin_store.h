#pragma once

#include "catalog/app_entry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nimblerun {

// One app's pin record, keyed by stable_id. last_seen_utc is the UTC epoch
// seconds the pin was last confirmed present in the catalog; it drives the
// 30-day retention for temporarily absent apps (design-spec §FR-011).
struct PinRecord {
    std::wstring stable_id;
    std::int64_t last_seen_utc = 0;
    // NR-062: the display name last seen for this pin, recorded so a pin whose
    // app is missing from the catalog can still be shown by name. Empty for a
    // record loaded from a schema=1 file, which had no name column.
    std::wstring display_name;
};

// Why Load returned. For anything other than Loaded the store is empty and the
// original file follows the settings/usage conventions (design-spec §10.4).
enum class PinLoadResult {
    Loaded,       // favorites.txt parsed and applied
    Missing,      // no favorites.txt yet
    Corrupt,      // unreadable/malformed; original renamed to favorites.txt.corrupt
    NewerSchema,  // schema version too new; original left untouched
};

// Pin retention for apps missing from the catalog (design-spec §FR-011): pins
// for temporarily absent apps survive this long and are then dropped by an
// explicit Reconcile, never on the first failed scan.
inline constexpr std::int64_t kPinRetentionSeconds = 30LL * 24 * 60 * 60;

// Pure per-user pin store. No HWND, Shell, or COM dependencies.
//
// File format: design-spec §10.2 names `favorites.txt` (UTF-8, one pin per
// line, line order = pin order); §10.4 requires every data format's first line
// to carry the schema version; this item records a last-seen timestamp to
// implement the 30-day retention; and NR-062 adds a display name column so a
// pin whose app is missing from the catalog can still be shown by name. The
// documented choice is a versioned TSV:
//
//     schema=2
//     <escaped stable_id>\t<last_seen_utc epoch>\t<escaped display_name>
//     ...
//
// written with the shared tmp + flush + atomic replace scheme so a crash
// mid-write never corrupts the real file (design-spec §10.2).
//
// Schema 1 compatibility (NR-062): a schema=1 file has two fields per line
// (no display_name column). Load() accepts both 2- and 3-field lines --
// ReadVersionedLines reports OlderSchema for a schema=1 file, and that status
// is a valid load path here, not a corrupt one, or every existing user's pins
// would be wiped to a .corrupt file on this upgrade. A 2-field line loads with
// an empty display_name; the next Save() rewrites the file as schema=2.
//
// Pins are kept for apps temporarily absent from the catalog; only Reconcile,
// run against a real (non-empty) catalog snapshot, may drop an expired pin.
class PinStore {
public:
    explicit PinStore(std::wstring directory);

    // Loads favorites.txt, replacing any in-memory pins.
    PinLoadResult Load();

    // Persists the current pins atomically. On any I/O failure the previous
    // file (if any) is left untouched.
    bool Save() const;

    // Pins stable_id at `now` (UTC epoch seconds, injected by the caller).
    // Idempotent: re-pinning an already-pinned app keeps its original position
    // and refreshes last_seen and display_name. Returns false for an empty
    // stable_id.
    bool Pin(std::wstring stable_id, std::wstring display_name, std::int64_t now);

    // Removes the pin for stable_id; no-op when not pinned.
    void Unpin(std::wstring_view stable_id);

    bool IsPinned(std::wstring_view stable_id) const;

    // Stable IDs in pin order (creation/stable order).
    std::vector<std::wstring> OrderedPins() const;

    // Pin records in pin order, mirroring UsageStore::Records(). PanelModel
    // needs the display_name alongside the stable_id to synthesize a
    // placeholder row for a pin whose app is absent from the catalog
    // (NR-062).
    const std::vector<PinRecord>& Records() const { return pins_; }

    // NR-046: reorders the pins named in `order` so their relative order matches
    // `order` exactly, while every pin NOT named there (a pin whose app is absent
    // from the current catalog, design-spec §FR-011) keeps the absolute index it
    // already has. IDs in `order` that are not pinned are ignored. Returns true when
    // the stored order actually changed; call Save() to persist.
    bool ReorderPresent(const std::vector<std::wstring>& order);

    // Refreshes last_seen for pins present in `catalog` and drops pins absent
    // for more than kPinRetentionSeconds. A pin for an absent app is kept while
    // its age is within the retention window, and an empty catalog is never a
    // reason to drop anything, so a single failed scan never deletes a pin
    // (design-spec §FR-011). Mutates in-memory state; call Save() to persist.
    void Reconcile(const std::vector<AppEntry>& catalog, std::int64_t now);

private:
    std::wstring directory_;
    std::vector<PinRecord> pins_;
};

} // namespace nimblerun
