#pragma once

#include "catalog/catalog_refresh.h"
#include "pins/pin_store.h"
#include "settings/settings_store.h"
#include "usage/usage_store.h"

namespace nimblerun {

class PanelModel;

// Owns the cross-store ordering that turns one catalog snapshot into the
// panel's pinned, ranked, and recent views. The host supplies the collaborators
// but this module owns the borrowed catalog-index lifetime (NR-083).
class CatalogSnapshotAssembler {
public:
    struct Result {
        bool pin_load_notice = false;
        PinLoadResult pin_load_result = PinLoadResult::Loaded;
    };

    CatalogSnapshotAssembler(CatalogRefreshCoordinator& refresh,
                             UsageStore& usage,
                             PinStore& pins,
                             PanelModel& model,
                             const Settings& settings);

    // Loads and reconciles stores, stamps ranking fields, then publishes the
    // catalog and recent rows in the order required by the panel model.
    Result Refresh();

    // Re-publishes derived fields after a pin reorder or another in-memory
    // store change without reloading favorites.txt. Usage changes pass false
    // to preserve the old stamp-only launch path; pin changes keep the default
    // row refresh.
    Result OnPinsChanged(bool refresh_rows = true);

private:
    Result RefreshPins();
    void StampRankingFields();

    CatalogRefreshCoordinator& refresh_;
    UsageStore& usage_;
    PinStore& pins_;
    PanelModel& model_;
    const Settings& settings_;
};

} // namespace nimblerun
