#include "pins/pin_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"favorites.txt";
constexpr std::wstring_view kSchemaPrefix = L"schema=";
constexpr int kSchemaVersion = 1;

} // namespace

PinStore::PinStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

PinLoadResult PinStore::Load() {
    pins_.clear();

    std::vector<std::wstring> lines;
    switch (ReadVersionedLines(directory_, kFileName, kSchemaVersion, lines)) {
    case VersionedReadStatus::Loaded:
        break;
    case VersionedReadStatus::Missing:
        return PinLoadResult::Missing;
    case VersionedReadStatus::NewerSchema:
        return PinLoadResult::NewerSchema;  // original untouched (design-spec §10.4)
    default:  // Unreadable / Malformed / OlderSchema
        PreserveCorrupt(directory_, kFileName);
        return PinLoadResult::Corrupt;
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::wstring line = Trim(lines[i]);
        if (line.empty()) {
            continue;
        }
        const std::vector<std::wstring_view> fields = SplitFields(line);
        if (fields.size() != 2) {
            PreserveCorrupt(directory_, kFileName);
            return PinLoadResult::Corrupt;
        }
        PinRecord pin;
        pin.stable_id = UnescapeText(fields[0]);
        if (pin.stable_id.empty() || !ParseInt64(fields[1], pin.last_seen_utc)) {
            PreserveCorrupt(directory_, kFileName);
            return PinLoadResult::Corrupt;
        }
        // Line order is pin order, so a duplicated stable id keeps its first
        // position.
        if (IsPinned(pin.stable_id)) {
            continue;
        }
        pins_.push_back(std::move(pin));
    }
    return PinLoadResult::Loaded;
}

bool PinStore::Save() const {
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
        text += L'\n';
    }

    return AtomicWriteUtf8Text(directory_, kFileName, text);
}

bool PinStore::Pin(std::wstring stable_id, std::int64_t now) {
    if (stable_id.empty()) {
        return false;
    }
    for (PinRecord& pin : pins_) {
        if (pin.stable_id == stable_id) {
            pin.last_seen_utc = now;
            return true;
        }
    }
    PinRecord pin;
    pin.stable_id = std::move(stable_id);
    pin.last_seen_utc = now;
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

    std::vector<PinRecord> kept;
    kept.reserve(pins_.size());
    for (PinRecord& pin : pins_) {
        const bool present = std::any_of(catalog.begin(), catalog.end(),
            [&](const AppEntry& entry) { return entry.stable_id == pin.stable_id; });
        if (present) {
            pin.last_seen_utc = now;  // seen again: restart the retention clock
            kept.push_back(std::move(pin));
        } else if (pin.last_seen_utc == 0 ||
                   now - pin.last_seen_utc <= kPinRetentionSeconds) {
            kept.push_back(std::move(pin));  // absent but recent (or unknown age): keep
        }
        // else: absent for more than the retention window -> dropped here.
    }
    pins_ = std::move(kept);
}

} // namespace nimblerun
