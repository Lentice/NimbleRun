#pragma once

#include "catalog/app_entry.h"
#include "pins/pin_store.h"
#include "search/search_engine.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nimblerun {

// Result of activating the current selection. identity is the entry to hand to
// the Shell; launch is false for an empty result or no selection.
struct PanelAction {
    bool launch = false;
    std::wstring identity;
};

// Pure interaction model for the list panel (NR-010). No HWND, Shell, or COM
// dependencies: the Win32 window translates keys and clicks into calls on this
// model and renders the rows it exposes. Points at the host's current catalog
// snapshot (lifetime managed by the host); the host may repoint it when a fresh
// snapshot is swapped in (NR-011).
class PanelModel {
public:
    // recent is the pre-computed recent list (newest first, capped by the
    // caller); catalog is the merged, deduplicated app snapshot.
    PanelModel(const std::vector<AppEntry>* catalog,
               std::vector<AppEntry> recent);

    // Points the model at a new snapshot and refreshes the current view.
    void SetCatalog(const std::vector<AppEntry>* catalog);

    // Replaces the recent list (used when usage changes or a snapshot swaps).
    void SetRecent(std::vector<AppEntry> recent);

    // Replaces the pin records in pin order (NR-018). Pins are resolved
    // against the catalog snapshot in the empty-query state; a pin for an app
    // absent from the catalog is shown as a placeholder row (IsMissingPin(),
    // NR-062) rather than dropped -- its record stays in the store either way
    // (design-spec §FR-011).
    void SetPins(std::vector<PinRecord> pins);

    // NR-062: a pinned row whose app is absent from the catalog. Such a row is
    // synthesized from the pin record, carries an empty launch_identity, and
    // must never be launched. The single test for "is this a placeholder"; do
    // not re-derive it from empty fields at each call site.
    static bool IsMissingPin(const AppEntry& row) {
        return row.is_pinned && row.launch_identity.empty();
    }

    // Resets to the empty-query state (pinned apps then recent apps).
    void Reset();

    // Sets the search query; empty clears to the recent list. Resets selection
    // to the first row.
    void SetQuery(const std::wstring& query);

    const std::wstring& Query() const { return query_; }

    // Sets the number of rows visible in one viewport (clamped to >= 1). The
    // host calls this whenever the panel DPI or size changes; the visible
    // window (FirstVisibleRow()..+ViewportRows()) stays inside the list and
    // MoveSelection() keeps the selection within it (design-spec §4.2).
    void SetViewportRows(int rows);
    int ViewportRows() const { return viewport_rows_; }

    // NR-029: sets the empty-query grid column count (clamped to >= 1). The
    // list state is simply "columns == 1": Columns() returns grid_columns_ only
    // while the query is empty, otherwise 1, so both layouts share the same
    // viewport/scroll/selection code (design-spec §4.2/§4.3). One page holds
    // ViewportRows() * Columns() items.
    void SetGridColumns(int columns);
    // NR-052: the grid/list split is the same §4.3/§4.4 decision RefreshRows
    // makes -- a whitespace-only query normalizes to empty and stays in the
    // grid, so this uses the one normalizer, not query_.empty().
    int Columns() const { return NormalizeName(query_).empty() ? grid_columns_ : 1; }

    // First row currently visible in the viewport; clamped to
    // [0, max(0, RowCount() - viewport)] so the window never runs past the
    // list ends.
    int FirstVisibleRow() const { return first_visible_; }

    // True when a non-empty catalog snapshot is available. The renderer uses
    // this to pick the empty-state hint ("building" vs "no matches").
    bool CatalogAvailable() const { return catalog_ != nullptr && !catalog_->empty(); }

    // Visible rows for the current state, in display order.
    const std::vector<AppEntry>& Rows() const { return rows_; }

    // NR-040: index of the first recent-region row in Rows(), or -1 when there
    // is no recent region (a non-empty query produces search results, which
    // belong to neither region). Rows before this index are the pinned region.
    // Equals Rows().size() when the empty-query view happens to be all pins.
    int RecentStartIndex() const { return recent_start_; }

    // One past the last row backed by an actual usage record, or -1 when there
    // is no recent region. NR-053's alphabetical filler also lands after
    // RecentStartIndex(), but has no usage record, so "Remove from recent"
    // must gate on this end instead (it silently did nothing on filler rows).
    int RecentEndIndex() const { return recent_end_; }

    bool HasSelection() const { return !rows_.empty() && selected_ >= 0; }
    std::size_t SelectionIndex() const { return static_cast<std::size_t>(selected_); }
    const AppEntry& SelectedEntry() const { return rows_[selected_]; }

    // Accessible names (design-spec §NFR-006): every row exposes its display
    // name and the current selection is readable without color. The host may
    // wire these into WM_GETOBJECT/IAccessible later; the mapping itself is
    // model state. Empty string for an out-of-range / empty state.
    const std::wstring& AccessibleNameFor(std::size_t index) const;
    const std::wstring& SelectedAccessibleName() const;

    // Moves the selection by delta rows, wrapping around the list.
    void MoveSelection(int delta);

    // Scrolls the visible window by delta_rows (PgUp/PgDn and the mouse wheel
    // share this single entry point). The window is clamped to the list ends
    // and never wraps; the selection follows the new first visible row
    // (design-spec §4.7). No-op on an empty list.
    void ScrollBy(int delta_rows);

    // Absolute row index for the slot-th visible row (0-based slot), or -1 when
    // the slot is outside the current viewport or past the end of the list
    // (design-spec §4.7). Never changes model state.
    int RowForVisibleSlot(int slot) const;

    // Stable IDs of the first page of the empty-query view, capped at
    // max_items. Empty when the model currently has a non-empty query:
    // prewarming is only meaningful for the state the next panel show will
    // actually start in. Never changes model state.
    std::vector<std::wstring> EmptyStatePrewarmIds(std::size_t max_items) const;

    // Selects a specific row (mouse click); no-op when out of range.
    void SelectRow(std::size_t index);

    // Enter on the current selection; no-op when nothing is selected.
    PanelAction Activate() const;

    // Esc: returns true when the panel should hide (query already empty);
    // otherwise clears the query and returns false.
    bool Esc();

private:
    void RefreshRows();
    // Re-clamps first_visible_ to the legal range after a rows/viewport change.
    void ClampFirstVisible();
    // Brings the selection into the visible window with the minimal shift.
    void EnsureSelectionVisible();

    const std::vector<AppEntry>* catalog_ = nullptr;
    std::vector<AppEntry> recent_;
    std::vector<PinRecord> pins_;
    std::vector<AppEntry> rows_;
    std::wstring query_;
    int selected_ = -1;
    int viewport_rows_ = 7;
    int first_visible_ = 0;
    // NR-029: empty-query grid columns; 1 means the model behaves like the
    // original single-column list until the host opts into the grid.
    int grid_columns_ = 1;
    // NR-040: first recent-region row index (see RecentStartIndex()); -1 when
    // there is no recent region.
    int recent_start_ = -1;
    // End of the usage-backed recent rows (see RecentEndIndex()).
    int recent_end_ = -1;
};

} // namespace nimblerun
