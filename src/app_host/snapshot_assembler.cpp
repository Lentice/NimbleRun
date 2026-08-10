#include "app_host/snapshot_assembler.h"

#include "app_host/panel_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimblerun {

CatalogSnapshotAssembler::CatalogSnapshotAssembler(
    CatalogRefreshCoordinator& refresh,
    UsageStore& usage,
    PinStore& pins,
    PanelModel& model,
    const Settings& settings)
    : refresh_(refresh), usage_(usage), pins_(pins), model_(model), settings_(settings) {
}

CatalogSnapshotAssembler::Result CatalogSnapshotAssembler::RefreshPins() {
    Result result;
    result.pin_load_result = pins_.Load();
    result.pin_load_notice = result.pin_load_result == PinLoadResult::Corrupt ||
                             result.pin_load_result == PinLoadResult::NewerSchema;
    // NR-072: a corrupt or newer-schema file is not a trustworthy source for
    // reconciliation or persistence. Missing is writable and intentionally
    // creates the first empty favorites file.
    if (result.pin_load_result == PinLoadResult::Loaded ||
        result.pin_load_result == PinLoadResult::Missing) {
        pins_.Reconcile(refresh_.Snapshot(),
                        static_cast<std::int64_t>(std::time(nullptr)));
        pins_.Save();
    }
    model_.SetPins(pins_.Records());
    return result;
}

void CatalogSnapshotAssembler::StampRankingFields() {
    // design-spec §4.5: pins win equal text matches, then usage score. Both
    // fields are derived from stores and must be stamped after every snapshot
    // rebuild and every store change.
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    std::unordered_map<std::wstring_view, int> scores;
    scores.reserve(usage_.Records().size());
    for (const UsageRecord& record : usage_.Records()) {
        scores.emplace(record.stable_id, UsageScore(record, now));
    }
    const std::vector<std::wstring> pins = pins_.OrderedPins();
    for (AppEntry& entry : refresh_.MutableSnapshot()) {
        const auto score = scores.find(entry.stable_id);
        entry.usage_score = score == scores.end() ? 0 : score->second;
        entry.is_pinned =
            std::find(pins.begin(), pins.end(), entry.stable_id) != pins.end();
    }
}

CatalogSnapshotAssembler::Result CatalogSnapshotAssembler::Refresh() {
    // NR-083: the keys borrow stable_id strings from this snapshot. The map is
    // installed only while SetPins/SetCatalog/SetRecent can trigger RefreshRows.
    std::unordered_map<std::wstring_view, std::size_t> snapshot_index;
    snapshot_index.reserve(refresh_.Snapshot().size());
    for (std::size_t i = 0; i < refresh_.Snapshot().size(); ++i) {
        snapshot_index.emplace(refresh_.Snapshot()[i].stable_id, i);
    }
    model_.SetCatalogIndex(&snapshot_index);

    // Pins are loaded first because they feed the is_pinned stamp.
    const Result result = RefreshPins();
    // NR-061: reconciling against an empty startup snapshot would wipe every
    // usage record, so only a real catalog may remove absent records.
    if (!refresh_.Snapshot().empty() && usage_.Reconcile(refresh_.Snapshot())) {
        usage_.Save();
    }
    StampRankingFields();
    model_.SetCatalog(&refresh_.MutableSnapshot());

    std::vector<UsageRecord> recent_records = usage_.Recent(settings_.recent_count);
    std::vector<AppEntry> recent_entries;
    recent_entries.reserve(recent_records.size());
    for (const UsageRecord& record : recent_records) {
        const auto found = snapshot_index.find(record.stable_id);
        if (found != snapshot_index.end()) {
            recent_entries.push_back(refresh_.Snapshot()[found->second]);
        }
    }
    // SetRecent is last, so its RefreshRows decides the visible rows; RefreshPins
    // already handed the pin list to the model.
    model_.SetRecent(std::move(recent_entries));
    // NR-083: no caller may retain views into this stack-owned index.
    model_.SetCatalogIndex(nullptr);
    return result;
}

CatalogSnapshotAssembler::Result CatalogSnapshotAssembler::OnPinsChanged(bool refresh_rows) {
    StampRankingFields();
    if (refresh_rows) {
        model_.SetPins(pins_.Records());
    }
    return {};
}

} // namespace nimblerun
