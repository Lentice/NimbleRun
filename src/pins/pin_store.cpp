#include "pins/pin_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"favorites.txt";
constexpr int kSchemaVersion = 2;

} // namespace

PinStore::PinStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

PinLoadResult PinStore::Load() {
    pins_.clear();
    // NR-096: any non-NewerSchema outcome stays writable; only the NewerSchema
    // branch below sets write_protected_.
    write_protected_ = false;

    std::vector<std::wstring> lines;
    switch (ReadVersionedLines(directory_, kFileName, kSchemaVersion, lines)) {
    case VersionedReadStatus::Loaded:
        break;
    case VersionedReadStatus::Missing:
        return PinLoadResult::Missing;
    case VersionedReadStatus::NewerSchema:
        write_protected_ = true;
        return PinLoadResult::NewerSchema;  // original untouched (design-spec §10.4)
    case VersionedReadStatus::OlderSchema:
        break;  // NR-062: a schema=1 file has no name column; its 2-field lines
                // are still valid and are upgraded on the next Save().
    default:  // Unreadable / Malformed
        PreserveCorrupt(directory_, kFileName);
        pins_.clear();  // NR-072: honor "non-Loaded store is empty" (pin_store.h)
        return PinLoadResult::Corrupt;
    }

    // NR-122: the O(n²) dedup scan per row became an O(n) membership set. Line
    // order is pin order, so a duplicated stable id keeps its first position.
    // NR-169: the set owns its keys -- each insert copies the stable_id into
    // the set, so it does not rely on move preserving the local pin's buffer
    // (an SSO short string dangles once the local is destroyed).
    //
    // The row cap counts parsed (non-empty) rows, not raw lines: SplitLines
    // adds one trailing empty line for a file that ends in '\n' -- exactly how
    // Save() writes it -- and the empty lines are skipped here, so a raw-line
    // pre-check would quarantine our own cap-exact output. Reaching the cap
    // aborts mid-parse, which is the same corrupt path as any over-limit file.
    const std::size_t reserve_size = std::min(lines.size(), kMaxRows);
    std::unordered_set<std::wstring> seen;
    seen.reserve(reserve_size);
    pins_.reserve(reserve_size);
    std::size_t data_rows = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::wstring line = Trim(lines[i]);
        if (line.empty()) {
            continue;
        }
        if (++data_rows > kMaxRows) {
            PreserveCorrupt(directory_, kFileName);
            pins_.clear();
            return PinLoadResult::Corrupt;
        }
        const std::vector<std::wstring_view> fields = SplitFields(line);
        // NR-062: schema=1 lines have 2 fields (no display_name); schema=2
        // lines have 3. NR-087: anything with at least 2 fields loads -- a
        // same-schema file with a future trailing field is read, not quarantined.
        if (fields.size() < 2) {
            PreserveCorrupt(directory_, kFileName);
            pins_.clear();  // NR-072: a partial parse must not become the live file
            return PinLoadResult::Corrupt;
        }
        PinRecord pin;
        pin.stable_id = UnescapeText(fields[0]);
        if (pin.stable_id.empty() || !ParseInt64(fields[1], pin.last_seen_utc)) {
            PreserveCorrupt(directory_, kFileName);
            pins_.clear();  // NR-072: a partial parse must not become the live file
            return PinLoadResult::Corrupt;
        }
        if (fields.size() >= 3) {
            pin.display_name = UnescapeText(fields[2]);
        }
        if (!seen.insert(pin.stable_id).second) {
            continue;  // duplicated stable id keeps its first position
        }
        pins_.push_back(std::move(pin));
    }
    return PinLoadResult::Loaded;
}

bool PinStore::Save() const {
    // NR-096: a newer-schema file is another build's data (design-spec §10.4).
    // Refuse without touching the original or the tmp file; the caller's
    // save-failed handling runs.
    if (write_protected_) {
        return false;
    }
    std::wstring text;
    text += kSchemaPrefix;
    text += std::to_wstring(kSchemaVersion);
    text += L"\n";

    // Written in pin order so the file's line order always equals the order the
    // pins were created in.
    for (const PinRecord& pin : pins_) {
        text += EscapeText(pin.stable_id);
        text += L'\t';
        text += std::to_wstring(pin.last_seen_utc);
        text += L'\t';
        text += EscapeText(pin.display_name);
        text += L'\n';
    }

    return AtomicWriteUtf8Text(directory_, kFileName, text);
}

bool PinStore::Pin(std::wstring stable_id, std::wstring display_name, std::int64_t now) {
    if (stable_id.empty()) {
        return false;
    }
    for (PinRecord& pin : pins_) {
        if (pin.stable_id == stable_id) {
            pin.last_seen_utc = now;
            pin.display_name = std::move(display_name);
            return true;
        }
    }
    PinRecord pin;
    pin.stable_id = std::move(stable_id);
    pin.last_seen_utc = now;
    pin.display_name = std::move(display_name);
    pins_.push_back(std::move(pin));
    return true;
}

void PinStore::Unpin(std::wstring_view stable_id) {
    const auto it = std::find_if(pins_.begin(), pins_.end(),
        [&](const PinRecord& pin) { return pin.stable_id == stable_id; });
    if (it != pins_.end()) {
        pins_.erase(it);
    }
}

bool PinStore::IsPinned(std::wstring_view stable_id) const {
    return std::find_if(pins_.begin(), pins_.end(),
        [&](const PinRecord& pin) { return pin.stable_id == stable_id; }) != pins_.end();
}

std::vector<std::wstring> PinStore::OrderedPins() const {
    std::vector<std::wstring> ids;
    ids.reserve(pins_.size());
    for (const PinRecord& pin : pins_) {
        ids.push_back(pin.stable_id);
    }
    return ids;
}

// NR-046: reorders the pins named in `order` so their relative order matches
// `order` exactly, while every pin NOT named there keeps the absolute index it
// already has. The grid only knows the pins that resolved against the current
// catalog, so an index-based reorder would silently move absent pins; IDs keep
// that stable.
bool PinStore::ReorderPresent(const std::vector<std::wstring>& order) {
    // Absolute indices of the pinned records named in `order`, ascending.
    std::vector<std::size_t> slots;
    for (std::size_t i = 0; i < pins_.size(); ++i) {
        if (std::find(order.begin(), order.end(), pins_[i].stable_id) != order.end()) {
            slots.push_back(i);
        }
    }

    // The IDs from `order` that are actually pinned, in `order`'s order.
    // Deduplicated: a repeated ID would make wanted longer than slots (and
    // resize pins_ below), and unknown IDs are never pinned so they never
    // enter. With pins_ holding unique IDs this keeps wanted aligned with
    // slots, which is the intersection the drag commit actually wants.
    std::vector<std::wstring> wanted;
    for (const std::wstring& id : order) {
        if (IsPinned(id) &&
            std::find(wanted.begin(), wanted.end(), id) == wanted.end()) {
            wanted.push_back(id);
        }
    }

    // Lift the records for `wanted` out of pins_ in wanted order, then place
    // each onto the absolute slot it already occupies. Unlisted pins are never
    // lifted and never overwritten, so they keep their positions. last_seen_utc
    // travels with its record because it moves inside the record itself.
    // Capture the pre-move id order before the lifts mutate pins_.
    const std::vector<std::wstring> before_ids = OrderedPins();
    std::vector<PinRecord> moved;
    moved.reserve(wanted.size());
    for (const std::wstring& id : wanted) {
        const auto it = std::find_if(pins_.begin(), pins_.end(),
            [&](const PinRecord& pin) { return pin.stable_id == id; });
        moved.push_back(std::move(*it));
    }

    for (std::size_t k = 0; k < slots.size(); ++k) {
        pins_[slots[k]] = std::move(moved[k]);
    }
    // Compare the id order, not PinRecord (which has no operator==).
    return OrderedPins() != before_ids;
}

void PinStore::Reconcile(const std::vector<AppEntry>& catalog, std::int64_t now) {
    // An empty catalog means "no data yet" (first launch, failed scan, in-flight
    // rebuild), never "all apps are gone". Never drop pins against it: a single
    // failed scan must not delete a pin (design-spec §FR-011).
    if (catalog.empty()) {
        return;
    }

    // NR-122: build a membership set of the catalog's stable ids once, so each
    // pin is a single O(1) lookup instead of a full catalog scan -- the
    // reconcile drops from O(pins × catalog) to O(n + m). The views point into
    // `catalog`, which is immutable for the call, so they stay valid. The empty
    // catalog early-return above is unchanged (design-spec §FR-011).
    std::unordered_set<std::wstring_view> present;
    present.reserve(catalog.size());
    for (const AppEntry& entry : catalog) {
        present.insert(entry.stable_id);
    }

    std::vector<PinRecord> kept;
    kept.reserve(pins_.size());
    for (PinRecord& pin : pins_) {
        if (present.find(pin.stable_id) != present.end()) {
            pin.last_seen_utc = now;  // seen again: restart the retention clock
            kept.push_back(std::move(pin));
        } else if (pin.last_seen_utc == 0 ||
                   // NR-070: compare last_seen against (now - retention) instead
                   // of subtracting last_seen from now -- a hand-edited
                   // favorites.txt can carry INT64_MIN, and the subtraction would
                   // be signed overflow (UB). `now` is a real clock reading and
                   // the constant subtraction cannot overflow; an INT64_MIN pin
                   // compares as expired and is dropped, which is the sane
                   // disposal of an absurd timestamp.
                   pin.last_seen_utc >= now - kPinRetentionSeconds) {
            kept.push_back(std::move(pin));  // absent but recent (or unknown age): keep
        }
        // else: absent for more than the retention window -> dropped here.
    }
    pins_ = std::move(kept);
}

} // namespace nimblerun
