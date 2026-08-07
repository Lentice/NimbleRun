#include "app_host/panel_model.h"

#include "search/search_engine.h"

#include <algorithm>
#include <string_view>

namespace nimblerun {
namespace {

// NR-053: mirrors search_engine.cpp's NormalizedName() so the empty state and
// the search results order names identically (design-spec §4.4). The catalog
// snapshot is pre-normalized; the display-name fallback covers an entry whose
// normalized_name is empty.
std::wstring_view DisplayNameKey(const AppEntry& entry) {
    return entry.normalized_name.empty()
        ? std::wstring_view(entry.display_name)
        : std::wstring_view(entry.normalized_name);
}

// NR-053: design-spec §4.2 rule 2. The non-pinned region is ordered by the
// usage score already stamped on each entry, with the §4.5 tie-breaks below
// it (higher score, then shorter name, then case-insensitive name, then
// stable id). This is the SearchApps comparator's tail (search_engine.cpp:
// 170-181) with the "pinned first" layer dropped -- the sorted region is
// entirely non-pinned. Keep the two in sync: the recency store only sorts by
// last launch, so without this sort a rarely used app opened an hour ago would
// outrank a daily driver.
bool OrderByScoreThenName(const AppEntry& left, const AppEntry& right) {
    if (left.usage_score != right.usage_score) {
        return left.usage_score > right.usage_score;
    }
    if (left.display_name.size() != right.display_name.size()) {
        return left.display_name.size() < right.display_name.size();
    }
    const std::wstring_view left_name = DisplayNameKey(left);
    const std::wstring_view right_name = DisplayNameKey(right);
    if (left_name != right_name) {
        return left_name < right_name;
    }
    return left.stable_id < right.stable_id;
}

} // namespace

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

void PanelModel::SetRecent(std::vector<AppEntry> recent) {
    recent_ = std::move(recent);
    RefreshRows();
}

void PanelModel::SetPins(std::vector<std::wstring> pins) {
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
        // Pinned apps first, in pin order, resolved from the catalog snapshot
        // so a pin for an app currently absent from the catalog is not shown
        // (the record itself stays in the store, design-spec §FR-011).
        if (catalog_ != nullptr) {
            for (const std::wstring& pin_id : pins_) {
                for (const AppEntry& entry : *catalog_) {
                    if (entry.stable_id == pin_id) {
                        rows_.push_back(entry);
                        break;
                    }
                }
            }
        }
        // Recent apps next, skipping any app already pinned so no app appears
        // in both regions (design-spec §4.2, AC-002).
        recent_start_ = static_cast<int>(rows_.size());
        for (const AppEntry& entry : recent_) {
            if (std::find(pins_.begin(), pins_.end(), entry.stable_id) == pins_.end()) {
                rows_.push_back(entry);
            }
        }
        recent_end_ = static_cast<int>(rows_.size());
        // NR-053: design-spec §4.2 rule 2 orders the non-pinned region by
        // usage score, not by last-launch recency. The score is the value
        // already stamped on every snapshot entry by StampRankingFields
        // (§4.6), so no extra lookup is needed here. The range starts at
        // recent_start_ so the pinned region is never sorted (§FR-011), and
        // stable_sort keeps the recency order among entries the comparator
        // considers equal.
        std::stable_sort(rows_.begin() + recent_start_, rows_.end(),
                         OrderByScoreThenName);
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
    // One page holds ViewportRows() * Columns() items (NR-029); the window is
    // clamped to the list ends and then floored down to a whole-row boundary so
    // the grid never shows a partial row (design-spec §4.2/§4.9).
    first_visible_ = std::clamp(first_visible_, 0,
                                std::max(0, count - viewport_rows_ * columns));
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

std::vector<std::wstring> PanelModel::EmptyStatePrewarmIds(std::size_t max_items) const {
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
    std::vector<std::wstring> ids;
    ids.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ids.push_back(rows_[i].stable_id);
    }
    return ids;
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
