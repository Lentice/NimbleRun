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
    if (query_.empty()) {
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
        for (const AppEntry& entry : recent_) {
            if (std::find(pins_.begin(), pins_.end(), entry.stable_id) == pins_.end()) {
                rows_.push_back(entry);
            }
        }
    } else if (catalog_ != nullptr) {
        rows_ = SearchApps(*catalog_, query_);
    } else {
        rows_.clear();
    }
    selected_ = rows_.empty() ? -1 : 0;
    first_visible_ = 0;
}

void PanelModel::SetViewportRows(int rows) {
    viewport_rows_ = std::max(1, rows);
    ClampFirstVisible();
}

void PanelModel::ClampFirstVisible() {
    const int count = static_cast<int>(rows_.size());
    first_visible_ = std::clamp(first_visible_, 0, std::max(0, count - viewport_rows_));
}

void PanelModel::EnsureSelectionVisible() {
    if (!HasSelection()) {
        return;
    }
    // Minimal shift: one row per one-row selection step (design-spec §4.2).
    if (selected_ < first_visible_) {
        first_visible_ = selected_;
    } else if (selected_ >= first_visible_ + viewport_rows_) {
        first_visible_ = selected_ - viewport_rows_ + 1;
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
    first_visible_ += delta_rows;
    ClampFirstVisible();
    selected_ = first_visible_;
}

int PanelModel::RowForVisibleSlot(int slot) const {
    if (slot < 0 || slot >= viewport_rows_) {
        return -1;
    }
    const int row = first_visible_ + slot;
    return row < static_cast<int>(rows_.size()) ? row : -1;
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
