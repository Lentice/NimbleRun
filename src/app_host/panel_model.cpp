#include "app_host/panel_model.h"

#include "search/search_engine.h"

#include <algorithm>

namespace nimblerun {

PanelModel::PanelModel(const std::vector<AppEntry>* catalog,
                       std::vector<AppEntry> recent)
    : catalog_(catalog),
      recent_(std::move(recent)) {
    Reset();
}

void PanelModel::SetCatalog(const std::vector<AppEntry>* catalog) {
    catalog_ = catalog;
    RefreshRows();
}

void PanelModel::SetCatalogIndex(
    const std::unordered_map<std::wstring_view, std::size_t>* index) {
    catalog_index_ = index;
}

void PanelModel::SetRecent(std::vector<AppEntry> recent) {
    recent_ = std::move(recent);
    RefreshRows();
}

void PanelModel::SetPins(std::vector<PinRecord> pins) {
    pins_ = std::move(pins);
    RefreshRows();
}

void PanelModel::Reset() {
    query_.clear();
    RefreshRows();
}

void PanelModel::SetQuery(const std::wstring& query) {
    query_ = query;
    RefreshRows();
}

void PanelModel::RefreshRows() {
    // NR-052: design-spec §4.3 switches layout when the box "contains a
    // non-whitespace character", not when it is non-empty. A lone space used to
    // drop out of the grid into the single-column list and then show "No
    // matching apps", because SearchApps normalizes the query to empty and
    // returns nothing. Reuse the one §4.4 normalizer rather than writing a
    // second blank test that could drift from it.
    if (NormalizeName(query_).empty()) {
        rows_.clear();
        // Pinned apps first, in pin order, resolved from the catalog snapshot.
        // A pin for an app currently absent from the catalog is still shown --
        // as a placeholder row (IsMissingPin(), NR-062) synthesized from the
        // pin record -- rather than skipped, so the user can see and unpin it
        // instead of it silently vanishing. The record itself stays in the
        // store either way (design-spec §FR-011).
        if (catalog_ != nullptr) {
            for (const PinRecord& pin : pins_) {
                bool found = false;
                if (catalog_index_ != nullptr) {
                    // NR-083: hash lookup instead of a full catalog scan; a
                    // miss takes the same placeholder path as a scan miss.
                    const auto hit = catalog_index_->find(pin.stable_id);
                    if (hit != catalog_index_->end()) {
                        rows_.push_back((*catalog_)[hit->second]);
                        found = true;
                    }
                } else {
                    for (const AppEntry& entry : *catalog_) {
                        if (entry.stable_id == pin.stable_id) {
                            rows_.push_back(entry);
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    AppEntry placeholder;
                    placeholder.stable_id = pin.stable_id;
                    placeholder.display_name =
                        pin.display_name.empty() ? pin.stable_id : pin.display_name;
                    placeholder.normalized_name = placeholder.display_name;
                    placeholder.is_pinned = true;
                    // launch_identity and source_path stay empty: that is what
                    // IsMissingPin() tests, and it keeps the row unlaunchable.
                    rows_.push_back(std::move(placeholder));
                }
            }
        }
        // Recent apps next, skipping any app already pinned so no app appears
        // in both regions (design-spec §4.2, AC-002).
        recent_start_ = static_cast<int>(rows_.size());
        for (const AppEntry& entry : recent_) {
            const bool pinned = std::find_if(pins_.begin(), pins_.end(),
                [&](const PinRecord& pin) { return pin.stable_id == entry.stable_id; }) !=
                pins_.end();
            if (!pinned) {
                rows_.push_back(entry);
            }
        }
        recent_end_ = static_cast<int>(rows_.size());
        // NR-071: the recent region is deliberately NOT sorted here.
        // UsageStore::Recent() already returns records newest-first (last
        // launch descending, stable id ascending on ties) and the loop above
        // preserves that order, so the most recently launched app is the first
        // non-pinned cell. NR-053 used to re-sort this range by usage_score;
        // that made a daily driver outrank an app opened ten minutes ago,
        // which is the opposite of what the recent region means. Apps that
        // need a fixed position are pinned -- design-spec §4.2 rule 2.
    } else if (catalog_ != nullptr) {
        recent_start_ = -1;
        recent_end_ = -1;
        rows_ = SearchApps(*catalog_, query_);
    } else {
        recent_start_ = -1;
        recent_end_ = -1;
        rows_.clear();
    }
    selected_ = rows_.empty() ? -1 : 0;
    first_visible_ = 0;
}

void PanelModel::SetViewportRows(int rows) {
    viewport_rows_ = std::max(1, rows);
    ClampFirstVisible();
}

void PanelModel::SetGridColumns(int columns) {
    grid_columns_ = std::max(1, columns);
    ClampFirstVisible();
}

void PanelModel::ClampFirstVisible() {
    const int count = static_cast<int>(rows_.size());
    const int columns = Columns();
    // NR-084: the upper bound must cover the LAST item, not "RowCount - page
    // floored down". The old formula made the tail of a non-multiple count
    // unreachable: 50 items with a 24-cell page clamped to 24, so items 49/50
    // could never be scrolled into view (and keyboard selection could land on
    // an unpainted cell). ceil(count/columns) is the last row's number; one
    // viewport of rows below it is the largest aligned start that still covers
    // every item, and the final row may be partially filled (design-spec
    // §4.2: the START aligns to whole rows, the content may end short).
    const int row_count = (count + columns - 1) / columns;
    first_visible_ = std::clamp(first_visible_, 0,
        std::max(0, (row_count - viewport_rows_) * columns));
    first_visible_ -= first_visible_ % columns;
}

void PanelModel::EnsureSelectionVisible() {
    if (!HasSelection()) {
        return;
    }
    // Minimal shift, in whole rows: one Columns()-wide row per one-row
    // selection step (design-spec §4.2). With Columns() == 1 this reduces to
    // the original one-row list rule.
    const int columns = Columns();
    const int visible_capacity = viewport_rows_ * columns;
    if (selected_ < first_visible_) {
        first_visible_ = selected_ - (selected_ % columns);
    } else if (selected_ >= first_visible_ + visible_capacity) {
        first_visible_ =
            (selected_ - (selected_ % columns)) + columns - visible_capacity;
    }
    ClampFirstVisible();
}

void PanelModel::MoveSelection(int delta) {
    if (rows_.empty()) {
        selected_ = -1;
        first_visible_ = 0;
        return;
    }
    // ponytail: modular wrap; catalog rows are bounded (<5k, design-spec FR-003).
    const std::size_t count = rows_.size();
    const int next =
        static_cast<int>(selected_) + delta;
    selected_ = static_cast<int>(((next % static_cast<int>(count)) +
                                  static_cast<int>(count)) %
                                 static_cast<int>(count));
    EnsureSelectionVisible();
}

void PanelModel::ScrollBy(int delta_rows) {
    if (rows_.empty()) {
        return;
    }
    // The argument stays in row units; each grid row spans Columns() items
    // (NR-029), so the scroll distance is scaled here.
    first_visible_ += delta_rows * Columns();
    ClampFirstVisible();
    selected_ = first_visible_;
}

int PanelModel::RowForVisibleSlot(int slot) const {
    // slot is the visible-item ordinal (NR-024): one page holds
    // ViewportRows() * Columns() items (NR-029).
    const int capacity = viewport_rows_ * Columns();
    if (slot < 0 || slot >= capacity) {
        return -1;
    }
    const int row = first_visible_ + slot;
    return row < static_cast<int>(rows_.size()) ? row : -1;
}

std::vector<AppEntry> PanelModel::EmptyStatePrewarmEntries(std::size_t max_items) const {
    // Prewarming only applies to the state the next panel show starts in
    // (empty query, NR-037); a non-empty query is a defensive no-op, and
    // max_items == 0 means "prewarm nothing".
    if (!query_.empty() || max_items == 0) {
        return {};
    }
    // Reuse the rows_ RefreshRows already built (pinned then recent,
    // design-spec §4.2) instead of duplicating the merge. The first page
    // holds Columns()*ViewportRows() items, but the prewarm cap is the spec'd
    // page size (design-spec §4.3, 24 cells): §FR-009 forbids predecoding the
    // whole catalog, so the caller passes that one-page bound.
    const std::size_t count = std::min(max_items, rows_.size());
    std::vector<AppEntry> entries;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // NR-062: a missing-pin placeholder has no icon to prewarm (non-goal:
        // no icon caching/prewarm for placeholder tiles).
        if (!IsMissingPin(rows_[i])) {
            entries.push_back(rows_[i]);
        }
    }
    return entries;
}

void PanelModel::SelectRow(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    selected_ = static_cast<int>(index);
    EnsureSelectionVisible();
}

PanelAction PanelModel::Activate() const {
    if (!HasSelection()) {
        return {};
    }
    PanelAction action;
    action.launch = true;
    action.identity = rows_[selected_].launch_identity;
    return action;
}

bool PanelModel::Esc() {
    if (!query_.empty()) {
        SetQuery(L"");
        return false;
    }
    return true;
}

const std::wstring& PanelModel::AccessibleNameFor(std::size_t index) const {
    static const std::wstring kEmpty;
    return index < rows_.size() ? rows_[index].display_name : kEmpty;
}

const std::wstring& PanelModel::SelectedAccessibleName() const {
    static const std::wstring kEmpty;
    return HasSelection() ? rows_[static_cast<std::size_t>(selected_)].display_name
                          : kEmpty;
}

} // namespace nimblerun
