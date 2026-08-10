#include "test_util.h"

#include "app_host/panel_model.h"
#include "search/search_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::PanelAction;
using nimblerun::PanelModel;

namespace {

AppEntry Entry(std::wstring id, std::wstring name) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = name;
    // NR-038: a prefilled normalized_name is used verbatim (no re-normalization
    // in the search loop), so it must already be the normalized form.
    entry.normalized_name = nimblerun::NormalizeName(name);
    entry.launch_identity = L"C:\\Apps\\" + name + L".exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserFolder;
    return entry;
}

std::vector<AppEntry> CatalogOf(int count) {
    std::vector<AppEntry> catalog;
    for (int i = 0; i < count; ++i) {
        catalog.push_back(Entry(L"id" + std::to_wstring(i),
                                L"App" + std::to_wstring(i)));
    }
    return catalog;
}

// NR-062: SetPins takes pin records, not bare ids; this builds the common case
// (no display name needed by the test) tersely.
nimblerun::PinRecord Pin(std::wstring id, std::wstring display_name = L"") {
    nimblerun::PinRecord pin;
    pin.stable_id = std::move(id);
    pin.display_name = std::move(display_name);
    return pin;
}

std::vector<nimblerun::PinRecord> Pins(std::initializer_list<std::wstring> ids) {
    std::vector<nimblerun::PinRecord> pins;
    for (const std::wstring& id : ids) {
        pins.push_back(Pin(id));
    }
    return pins;
}

void TestEmptyQueryShowsRecent() {
    const std::vector<AppEntry> catalog = {Entry(L"a", L"Alpha"), Entry(L"b", L"Beta")};
    std::vector<AppEntry> recent = {Entry(L"b", L"Beta"), Entry(L"a", L"Alpha")};
    PanelModel model(&catalog, std::move(recent));
    Expect(model.Query().empty(), "query starts empty");
    Expect(model.Rows().size() == 2, "recent rows shown on empty query");
    Expect(model.Rows()[0].stable_id == L"b", "recent ordering newest first");
    Expect(model.HasSelection(), "first recent row selected");
}

void TestEmptyStateNoRecords() {
    // NR-053: with a non-empty catalog the empty state now fills to one page
    // (§4.2 rule 3), so the "no records" shape needs an empty catalog to stay
    // empty.
    const std::vector<AppEntry> catalog;
    PanelModel model(&catalog, {});
    Expect(model.Rows().empty(), "no records -> empty state");
    Expect(!model.HasSelection(), "empty state has no selection");
    const PanelAction action = model.Activate();
    Expect(!action.launch, "empty state never launches");
}

void TestQuerySwitchesToFilteredRows() {
    const std::vector<AppEntry> catalog = {
        Entry(L"1", L"Notepad"), Entry(L"2", L"Calculator"), Entry(L"3", L"Paint")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"calc");
    Expect(model.Rows().size() == 1, "query filters to one row");
    Expect(model.Rows()[0].display_name == L"Calculator", "query row is the match");
    Expect(model.HasSelection(), "first result selected");
    // Selecting a row never auto-launches: only an explicit Activate() does.
    model.MoveSelection(0);
    model.MoveSelection(0);
    Expect(model.HasSelection(), "moving selection keeps it selected");
}

void TestMoveSelectionClampsAndWraps() {
    const std::vector<AppEntry> catalog = {
        Entry(L"1", L"One"), Entry(L"2", L"Two"), Entry(L"3", L"Three")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"");
    // Recent list is empty; switch to query to get rows.
    model.SetQuery(L"o");
    const std::size_t count = model.Rows().size();
    Expect(count == 2, "two rows match 'o'");
    model.MoveSelection(1);
    Expect(model.SelectionIndex() == 1, "down moves selection");
    model.MoveSelection(1);
    Expect(model.SelectionIndex() == 0, "down wraps to first");
    model.MoveSelection(-1);
    Expect(model.SelectionIndex() == count - 1, "up wraps to last");
}

void TestEnterLaunchesSelectedOnly() {
    const std::vector<AppEntry> catalog = {
        Entry(L"1", L"One"), Entry(L"2", L"Two")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"two");
    const PanelAction action = model.Activate();
    Expect(action.launch, "Enter on a selection launches");
    Expect(action.identity == catalog[1].launch_identity, "launches the selected entry");
}

void TestEnterEmptyResultNoLaunch() {
    const std::vector<AppEntry> catalog = {Entry(L"1", L"One")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"zzz-no-match");
    Expect(model.Rows().empty(), "no match -> empty rows");
    Expect(!model.HasSelection(), "no selection on empty rows");
    const PanelAction action = model.Activate();
    Expect(!action.launch, "no launch on empty result");
}

void TestEscClearsThenHides() {
    const std::vector<AppEntry> catalog = {Entry(L"1", L"One")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"on");
    Expect(model.Esc() == false, "Esc with query clears, does not hide");
    Expect(model.Query().empty(), "query cleared");
    Expect(model.Esc() == true, "Esc with empty query requests hide");
}

// NR-052: design-spec §4.3 switches layout when the search box "contains a
// non-whitespace character". A whitespace-only query normalizes to empty
// (§4.4), so it must stay in the grid and keep the pinned+recent rows.

void TestWhitespaceQueryStaysInGrid() {
    const std::vector<AppEntry> catalog = CatalogOf(8);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetPins(Pins({L"id0", L"id1"}));
    const std::size_t rows_before = model.Rows().size();
    const int columns_before = model.Columns();
    Expect(columns_before == 6, "empty query uses the grid columns");
    Expect(!model.Rows().empty(), "empty query shows grid rows");
    model.SetQuery(L" ");
    Expect(model.Columns() == 6, "a single space stays in the grid layout");
    Expect(model.Rows().size() == rows_before, "a single space keeps the pinned+recent rows");
    model.SetQuery(L"   ");
    Expect(model.Columns() == 6, "multiple spaces stay in the grid layout");
    Expect(model.Rows().size() == rows_before, "multiple spaces keep the pinned+recent rows");
    model.SetQuery(L"\t");
    Expect(model.Columns() == 6, "a tab stays in the grid layout");
    Expect(model.Rows().size() == rows_before, "a tab keeps the pinned+recent rows");
}

void TestTrimmedQuerySameAsUntrimmed() {
    const std::vector<AppEntry> catalog = {
        Entry(L"1", L"Notepad"), Entry(L"2", L"Calculator"), Entry(L"3", L"Paint")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"a");
    const std::vector<AppEntry> untrimmed = model.Rows();
    Expect(model.Columns() == 1, "a real query switches to the single-column list");
    model.SetQuery(L" a ");
    Expect(model.Columns() == 1, "a padded query is still a search");
    Expect(model.Rows().size() == untrimmed.size(), "padded query matches the same row count");
    for (std::size_t i = 0; i < untrimmed.size(); ++i) {
        Expect(model.Rows()[i].stable_id == untrimmed[i].stable_id,
               "padded query returns the same entries as the raw query");
    }
}

void TestSetQueryEmptyMatchesReset() {
    const std::vector<AppEntry> catalog = CatalogOf(6);
    PanelModel model(&catalog, catalog);
    model.SetPins(Pins({L"id0", L"id2"}));
    model.SetQuery(L"App");
    Expect(model.Columns() == 1, "query switches to the list layout");
    model.SetQuery(L"");
    const std::vector<AppEntry> after_set_query = model.Rows();
    model.SetQuery(L"App");
    model.Reset();
    const std::vector<AppEntry> after_reset = model.Rows();
    Expect(after_set_query.size() == after_reset.size(),
           "SetQuery(empty) and Reset produce the same row count");
    for (std::size_t i = 0; i < after_set_query.size(); ++i) {
        Expect(after_set_query[i].stable_id == after_reset[i].stable_id,
               "SetQuery(empty) and Reset produce the same rows");
    }
}

// NR-052: design-spec §4.7 clears the search box first whenever it "has
// content" -- judged by the user-visible raw text, so a whitespace-only query
// still counts and Esc must clear (return false) rather than hide the panel.

void TestEscOnWhitespaceQueryClearsFirst() {
    const std::vector<AppEntry> catalog = CatalogOf(4);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetQuery(L" ");
    Expect(model.Columns() == 6, "whitespace query is still the grid view");
    Expect(model.Esc() == false, "Esc on whitespace clears, does not request hide");
    Expect(model.Query().empty(), "whitespace query cleared by Esc");
    Expect(model.Esc() == true, "Esc after clearing requests hide");
}

void TestQueryChangeResetsSelection() {
    const std::vector<AppEntry> catalog = {
        Entry(L"1", L"One"), Entry(L"2", L"Two"), Entry(L"3", L"Three")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"o");
    model.MoveSelection(1);
    Expect(model.SelectionIndex() == 1, "moved off first row");
    model.SetQuery(L"e");
    Expect(model.SelectionIndex() == 0, "query change resets to first row");
}

void TestFailureKeepsModelIntact() {
    const std::vector<AppEntry> catalog = {Entry(L"1", L"One")};
    PanelModel model(&catalog, {});
    model.SetQuery(L"on");
    // Simulate a launch failure: the model must stay queryable and the panel
    // (caller) keeps showing rows.
    Expect(model.Rows().size() == 1, "rows visible before failure");
    Expect(model.HasSelection(), "selection present before failure");
    Expect(model.Activate().launch, "activation succeeds at model level");
    Expect(model.Rows().size() == 1, "rows still visible after a failed launch");
    Expect(model.SelectionIndex() == 0, "selection intact after a failed launch");
}

// NR-020: visible-range (viewport) state is pure PanelModel state. The host
// pushes the row count it derives from DPI/window size and only renders
// [FirstVisibleRow(), FirstVisibleRow() + ViewportRows()).

void TestSetViewportRowsClampsToOne() {
    const std::vector<AppEntry> catalog = CatalogOf(5);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(0);
    Expect(model.ViewportRows() == 1, "viewport rows clamp to at least 1");
    model.SetViewportRows(-3);
    Expect(model.ViewportRows() == 1, "negative viewport rows clamp to 1");
    Expect(model.FirstVisibleRow() == 0, "first visible stays 0");
}

void TestFewRowsKeepFirstVisibleZero() {
    const std::vector<AppEntry> catalog = CatalogOf(2);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(7);
    // Selection moves inside the whole list; the window never needs to scroll.
    model.MoveSelection(1);
    Expect(model.FirstVisibleRow() == 0, "rows fewer than viewport keep first visible 0");
    model.MoveSelection(-1);  // wraps to the last row
    Expect(model.FirstVisibleRow() == 0, "wrap with rows fewer than viewport stays 0");
}

void TestMoveSelectionScrollsViewportByOne() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(3);
    model.MoveSelection(1);  // 1
    model.MoveSelection(1);  // 2
    Expect(model.FirstVisibleRow() == 0, "selection inside viewport does not scroll");
    model.MoveSelection(1);  // 3 -> below the window
    Expect(model.FirstVisibleRow() == 1, "moving down out of view scrolls by exactly one");
    model.MoveSelection(-1);  // 2, still visible in [1,4)
    model.MoveSelection(-1);  // 1
    Expect(model.FirstVisibleRow() == 1, "moving up inside viewport does not scroll");
    model.MoveSelection(-1);  // 0 -> above the window
    Expect(model.FirstVisibleRow() == 0, "moving up out of view scrolls by exactly one");
}

void TestWrapToLastRowScrollsToTail() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(3);
    Expect(model.SelectionIndex() == 0 && model.FirstVisibleRow() == 0,
           "starts at the first row");
    model.MoveSelection(-1);  // up on the first row wraps to the last
    Expect(model.SelectionIndex() == 9, "up from the first row wraps to the last");
    Expect(model.FirstVisibleRow() == 7, "visible window jumps to the tail");
    Expect(model.FirstVisibleRow() == 10 - 3, "tail window does not run past the end");
}

void TestResetOperationsClearScroll() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(3);
    model.MoveSelection(1);
    model.MoveSelection(1);
    model.MoveSelection(1);
    Expect(model.FirstVisibleRow() == 1, "viewport scrolled down");
    model.SetQuery(L"App");
    Expect(model.FirstVisibleRow() == 0, "SetQuery resets first visible");
    model.MoveSelection(1);
    model.MoveSelection(1);
    model.MoveSelection(1);
    model.Reset();
    Expect(model.FirstVisibleRow() == 0, "Reset resets first visible");
    model.SetQuery(L"App");
    model.MoveSelection(1);
    model.MoveSelection(1);
    model.MoveSelection(1);
    Expect(model.FirstVisibleRow() == 1, "viewport scrolled down again");
    model.SetPins({Pin(L"id0")});
    Expect(model.FirstVisibleRow() == 0, "SetPins resets first visible");
}

void TestViewportLargerThanRowsNeverNegative() {
    const std::vector<AppEntry> catalog = CatalogOf(3);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(10);
    model.MoveSelection(-1);  // wraps 0 -> 2
    Expect(model.FirstVisibleRow() == 0, "viewport larger than rows keeps first visible 0");
    model.MoveSelection(-1);  // 2 -> 1
    Expect(model.FirstVisibleRow() == 0, "first visible never goes negative");
}

void TestSelectRowBringsSelectionIntoView() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(3);
    model.SelectRow(5);
    Expect(model.SelectionIndex() == 5, "click selects the row");
    Expect(model.FirstVisibleRow() == 3, "clicked row scrolled into view");
    model.SelectRow(10);
    Expect(model.SelectionIndex() == 5, "out-of-range click selection is a no-op");
}

// NR-021: paging scroll (PgUp/PgDn and the mouse wheel share ScrollBy; the
// visible window is clamped to the list ends and never wraps).

void TestScrollByPagesForward() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    model.ScrollBy(5);
    Expect(model.FirstVisibleRow() == 5, "PgDn advances the first visible row by the viewport");
    Expect(model.SelectionIndex() == 5, "selection follows the new first visible row");
    model.ScrollBy(5);
    Expect(model.FirstVisibleRow() == 10, "a second PgDn advances another page");
    Expect(model.SelectionIndex() == 10, "selection follows the second page");
}

void TestScrollByClampsAtTail() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    model.ScrollBy(100);
    Expect(model.FirstVisibleRow() == 15, "ScrollBy past the tail clamps to RowCount - viewport");
    Expect(model.FirstVisibleRow() == 20 - 5, "clamped value equals RowCount - viewport");
    model.ScrollBy(5);
    Expect(model.FirstVisibleRow() == 15, "PgDn at the tail does not advance further");
}

void TestScrollByClampsAtStart() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    model.ScrollBy(5);
    Expect(model.FirstVisibleRow() == 5, "scrolled away from the start");
    model.ScrollBy(-100);
    Expect(model.FirstVisibleRow() == 0, "ScrollBy before the start clamps at 0");
    model.ScrollBy(-5);
    Expect(model.FirstVisibleRow() == 0, "PgUp at the start does not move");
}

void TestScrollByFewerRowsThanViewport() {
    const std::vector<AppEntry> catalog = CatalogOf(3);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(7);
    model.ScrollBy(7);
    Expect(model.FirstVisibleRow() == 0, "rows fewer than viewport never scroll down");
    model.ScrollBy(-7);
    Expect(model.FirstVisibleRow() == 0, "negative scroll keeps first visible 0");
}

void TestScrollByEmptyList() {
    const std::vector<AppEntry> catalog = CatalogOf(1);
    PanelModel model(&catalog, {});
    model.SetQuery(L"zzz-no-match");
    Expect(model.Rows().empty(), "empty result list");
    model.ScrollBy(3);
    Expect(model.FirstVisibleRow() == 0, "empty list keeps first visible 0");
    Expect(!model.HasSelection(), "empty list has no selection");
    model.ScrollBy(-3);
    Expect(model.FirstVisibleRow() == 0, "empty list negative scroll is a no-op");
}

void TestScrollByRoundTripNoWrap() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    model.ScrollBy(5);  // 5
    model.ScrollBy(5);  // 10
    model.ScrollBy(5);  // 15 (tail)
    model.ScrollBy(5);  // stays 15 (clamped, no wrap)
    model.ScrollBy(-5);  // 10
    model.ScrollBy(-5);  // 5
    model.ScrollBy(-5);  // 0
    Expect(model.FirstVisibleRow() == 0, "scroll to the tail then back returns to the start");
    Expect(model.SelectionIndex() == 0, "selection follows the returned first visible row");
}

// NR-024: Alt+digit maps a visible slot (0-based position in the viewport) to
// an absolute row index; the mapping is pure (design-spec §4.7) and never
// mutates model state.

void TestRowForVisibleSlotBasics() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    Expect(model.RowForVisibleSlot(0) == model.FirstVisibleRow(),
           "slot 0 maps to the first visible row");
    Expect(model.RowForVisibleSlot(4) == model.FirstVisibleRow() + 4,
           "last viewport slot maps to the last visible row");
    Expect(model.RowForVisibleSlot(0) == 0, "visible window starts at row 0");
    Expect(model.RowForVisibleSlot(4) == 4, "last slot at the start is row 4");
}

void TestRowForVisibleSlotTracksScroll() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    model.ScrollBy(5);
    Expect(model.RowForVisibleSlot(0) == model.FirstVisibleRow(),
           "after a page, slot 0 maps to the new first visible row");
    Expect(model.RowForVisibleSlot(0) == 5, "absolute row advances with the page");
    Expect(model.RowForVisibleSlot(4) == 9, "new last visible slot maps to row 9");
}

void TestRowForVisibleSlotOutOfRange() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    Expect(model.RowForVisibleSlot(-1) == -1, "negative slot is invalid");
    Expect(model.RowForVisibleSlot(5) == -1, "slot at the viewport edge is invalid");
    Expect(model.RowForVisibleSlot(7) == -1, "slot past the viewport is invalid");
}

void TestRowForVisibleSlotPastListEnd() {
    const std::vector<AppEntry> catalog = CatalogOf(3);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(7);
    const std::size_t selection_before = model.SelectionIndex();
    Expect(model.RowForVisibleSlot(3) == -1, "slot beyond RowCount is invalid");
    Expect(model.RowForVisibleSlot(0) == 0, "first slot still maps to row 0");
    Expect(model.SelectionIndex() == selection_before,
           "RowForVisibleSlot never changes the selection");
}

void TestRowForVisibleSlotEmptyList() {
    const std::vector<AppEntry> catalog = CatalogOf(1);
    PanelModel model(&catalog, {});
    model.SetQuery(L"zzz-no-match");
    Expect(model.Rows().empty(), "empty result list");
    for (int slot = -1; slot <= 10; ++slot) {
        Expect(model.RowForVisibleSlot(slot) == -1,
               "empty list has no visible rows for any slot");
    }
}

void TestRowForVisibleSlotIsConst() {
    const std::vector<AppEntry> catalog = CatalogOf(20);
    PanelModel model(&catalog, {});
    model.SetQuery(L"App");
    model.SetViewportRows(5);
    const PanelModel& const_model = model;
    const int first_before = const_model.FirstVisibleRow();
    const std::size_t selection_before = const_model.SelectionIndex();
    for (int slot = -2; slot <= 8; ++slot) {
        (void)const_model.RowForVisibleSlot(slot);
    }
    Expect(const_model.FirstVisibleRow() == first_before,
           "repeated RowForVisibleSlot calls leave the viewport untouched");
    Expect(const_model.SelectionIndex() == selection_before,
           "repeated RowForVisibleSlot calls leave the selection untouched");
}

// NR-029: the empty-query icon grid reuses the same viewport/scroll/selection
// state as the list; the only difference is Columns(). Columns() is 1 whenever
// a query is active, so the list behavior above is untouched. The empty query
// shows pins + recent, so each grid test feeds the catalog as the recent list
// to get rows.

void TestGridColumnsClampAndQuerySwitch() {
    const std::vector<AppEntry> catalog = CatalogOf(40);
    PanelModel model(&catalog, catalog);
    Expect(model.Columns() == 1, "default grid columns is the single-column list");
    model.SetGridColumns(6);
    Expect(model.Columns() == 6, "empty query uses the grid columns");
    model.SetQuery(L"App");
    Expect(model.Columns() == 1, "non-empty query forces single column");
    model.SetGridColumns(0);
    Expect(model.Columns() == 1, "grid columns clamp to >= 1");
    model.SetGridColumns(-3);
    Expect(model.Columns() == 1, "negative grid columns clamp to 1");
    model.SetQuery(L"");
    Expect(model.Columns() == 1, "clamped grid columns = 1 on empty query");
    model.SetGridColumns(6);
    Expect(model.Columns() == 6, "grid columns restored");
}

void TestGridFirstVisibleAlignedToColumns() {
    const std::vector<AppEntry> catalog = CatalogOf(50);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);  // 4 grid rows -> 24-cell page
    model.ScrollBy(1);  // one row -> 6 items
    Expect(model.FirstVisibleRow() == 6 && model.FirstVisibleRow() % 6 == 0,
           "one-row scroll stays aligned to whole grid rows");
    model.ScrollBy(100);  // past the tail; 50 is not a multiple of 6
    // NR-084: the tail page start is the largest aligned position that still
    // covers the last item (ceil(50/6)=9 rows, minus the 4-row viewport -> 30),
    // not RowCount - page floored down (24, which hid items 49 and 50).
    Expect(model.FirstVisibleRow() == 30,
           "tail page start covers the last partial row");
    Expect(model.FirstVisibleRow() % 6 == 0,
           "tail window is floored to a whole grid row");
}

// NR-084: the tail of a non-page-multiple count must be reachable by paging
// and by keyboard selection.
void TestGridTailItemsReachable() {
    const std::vector<AppEntry> catalog = CatalogOf(50);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    model.ScrollBy(4);  // first page
    Expect(model.FirstVisibleRow() == 24, "first PgDn lands on page two");
    model.ScrollBy(4);  // second page -> tail
    Expect(model.FirstVisibleRow() == 30, "second PgDn reaches the tail page");
    Expect(model.RowForVisibleSlot(18) == 48,
           "item 49 is inside the tail viewport");
    Expect(model.RowForVisibleSlot(19) == 49,
           "the last item (50) is inside the tail viewport");
    PanelModel keyboard(&catalog, catalog);
    keyboard.SetGridColumns(6);
    keyboard.SetViewportRows(4);
    keyboard.SelectRow(49);
    Expect(keyboard.SelectionIndex() == 49, "selection is item 50");
    Expect(keyboard.FirstVisibleRow() == 30,
           "keyboard selection of the last item scrolls to the tail page");
    Expect(keyboard.RowForVisibleSlot(19) == 49,
           "the selected last item is actually painted");
}

// NR-084: 25 items (25 % 24 == 1) reach item 25 on the second page.
void TestGridPageNotMultipleStillReachesAll() {
    const std::vector<AppEntry> catalog = CatalogOf(25);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    model.ScrollBy(4);
    Expect(model.FirstVisibleRow() == 6, "one partial row starts page two");
    Expect(model.RowForVisibleSlot(0) == 6, "page two starts at item 7");
    Expect(model.RowForVisibleSlot(18) == 24,
           "the last item (25) is inside the tail viewport");
}

void TestGridMoveSelectionRows() {
    const std::vector<AppEntry> catalog = CatalogOf(30);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    Expect(model.SelectionIndex() == 0, "grid starts at item 0");
    model.MoveSelection(6);
    Expect(model.SelectionIndex() == 6, "down by Columns() moves one grid row");
    model.MoveSelection(6);
    Expect(model.SelectionIndex() == 12, "down again moves to the third row");
    model.SelectRow(29);  // last item of the last row
    model.MoveSelection(6);
    Expect(model.SelectionIndex() == 5, "down past the last row wraps to the top");
}

void TestGridScrollByPages() {
    const std::vector<AppEntry> catalog = CatalogOf(60);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    model.ScrollBy(4);  // one page of 4 rows
    Expect(model.FirstVisibleRow() == 24, "PgDn advances a full grid page (24 items)");
    Expect(model.SelectionIndex() == 24, "selection follows the new first visible item");
    model.ScrollBy(100);
    Expect(model.FirstVisibleRow() == 36, "scroll past the tail clamps to RowCount - page");
    Expect(model.FirstVisibleRow() % 6 == 0, "clamped tail stays row-aligned");
}

void TestGridFewerThanPageNoScroll() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);  // 24-cell page, more than the 10 items
    model.ScrollBy(4);
    Expect(model.FirstVisibleRow() == 0, "fewer items than a page never scrolls down");
    model.ScrollBy(-4);
    Expect(model.FirstVisibleRow() == 0, "negative page scroll keeps first visible 0");
    model.ScrollBy(1);
    Expect(model.FirstVisibleRow() == 0, "single-row scroll keeps first visible 0");
}

void TestGridQueryTransitionResetsViewport() {
    const std::vector<AppEntry> catalog = CatalogOf(60);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    model.ScrollBy(4);
    Expect(model.FirstVisibleRow() == 24, "grid scrolled down one page");
    Expect(model.Columns() == 6, "grid columns active before search");
    model.SetQuery(L"App");
    Expect(model.Columns() == 1, "search switches to the single-column list");
    Expect(model.FirstVisibleRow() == 0, "search resets the viewport to the top");
    model.SetQuery(L"");
    Expect(model.Columns() == 6, "clearing the query restores the grid");
    Expect(model.FirstVisibleRow() == 0, "back to the grid resets the viewport to the top");
}

void TestGridRowForVisibleSlot() {
    const std::vector<AppEntry> catalog = CatalogOf(30);
    PanelModel model(&catalog, catalog);
    model.SetGridColumns(6);
    model.SetViewportRows(4);
    for (int slot = 0; slot < 10; ++slot) {
        Expect(model.RowForVisibleSlot(slot) == slot,
               "grid slot maps to the matching visible cell");
    }
    Expect(model.RowForVisibleSlot(24) == -1, "slot at the page capacity is invalid");
    model.ScrollBy(4);
    const int first = model.FirstVisibleRow();
    for (int slot = 0; slot < 10; ++slot) {
        Expect(model.RowForVisibleSlot(slot) == first + slot,
               "after a page, slots map to the new first visible cells");
    }
}

// NR-037: the empty-query page prewarm is a pure const query over rows_.
// It returns the stable IDs of the first page (pinned first, then recent)
// capped at the caller's page-size bound, and never mutates model state.

void TestEmptyStatePrewarmIdsPinsThenRecent() {
    const std::vector<AppEntry> catalog = CatalogOf(8);
    std::vector<AppEntry> recent;
    for (int i = 3; i < 8; ++i) {
        recent.push_back(catalog[static_cast<std::size_t>(i)]);
    }
    PanelModel model(&catalog, std::move(recent));
    model.SetPins(Pins({L"id0", L"id1", L"id2"}));
    const std::vector<std::wstring> ids = model.EmptyStatePrewarmIds(24);
    Expect(ids.size() == 8, "3 pins + 5 recent prewarm 8 ids");
    Expect(ids.size() == model.Rows().size(), "prewarm count matches the row count");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        Expect(ids[i] == model.Rows()[i].stable_id,
               "prewarm ids match rows_ order (pins first)");
    }
    Expect(ids[0] == L"id0" && ids[2] == L"id2", "pins lead in pin order");
}

void TestEmptyStatePrewarmIdsCapsAtOnePage() {
    const std::vector<AppEntry> catalog = CatalogOf(40);
    std::vector<nimblerun::PinRecord> pins;
    for (int i = 0; i < 40; ++i) {
        pins.push_back(Pin(L"id" + std::to_wstring(i)));
    }
    PanelModel model(&catalog, {});
    model.SetPins(pins);
    const std::vector<std::wstring> ids = model.EmptyStatePrewarmIds(24);
    Expect(ids.size() == 24, "40 pinned items cap at exactly one page of 24");
    Expect(ids.front() == L"id0" && ids.back() == L"id23",
           "the first page is the first 24 pins in pin order");
}

void TestEmptyStatePrewarmIdsZeroMax() {
    const std::vector<AppEntry> catalog = CatalogOf(5);
    PanelModel model(&catalog, catalog);
    Expect(model.EmptyStatePrewarmIds(0).empty(), "max_items 0 returns empty");
}

void TestEmptyStatePrewarmIdsNonEmptyQuery() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, catalog);
    model.SetQuery(L"App");
    Expect(!model.Rows().empty(), "query has rows");
    Expect(model.EmptyStatePrewarmIds(24).empty(), "non-empty query returns empty");
}

void TestEmptyStatePrewarmIdsEmptyCatalog() {
    const std::vector<AppEntry> catalog;
    PanelModel model(&catalog, {});
    Expect(model.EmptyStatePrewarmIds(24).empty(), "empty catalog returns empty");
}

void TestEmptyStatePrewarmIdsIsConst() {
    const std::vector<AppEntry> catalog = CatalogOf(10);
    PanelModel model(&catalog, catalog);
    model.SetPins(Pins({L"id0", L"id1"}));
    model.SetGridColumns(6);
    const std::size_t selection_before = model.SelectionIndex();
    const int first_before = model.FirstVisibleRow();
    const std::size_t rows_before = model.Rows().size();
    const PanelModel& const_model = model;
    (void)const_model.EmptyStatePrewarmIds(24);
    (void)const_model.EmptyStatePrewarmIds(5);
    Expect(model.SelectionIndex() == selection_before,
           "prewarm query leaves the selection untouched");
    Expect(model.FirstVisibleRow() == first_before,
           "prewarm query leaves the viewport untouched");
    Expect(model.Rows().size() == rows_before, "prewarm query leaves rows_ untouched");
}

// NR-062 overrode the old "absent pin is hidden" behavior: an absent pin now
// shows as a placeholder row in Rows() (see TestMissingPinBecomesPlaceholder
// below), but it still has no icon to prewarm (non-goal: no icon caching or
// prewarm for placeholder tiles), so it must stay out of the prewarm id list.
void TestEmptyStatePrewarmIdsAbsentPinSkipped() {
    const std::vector<AppEntry> catalog = CatalogOf(12);
    PanelModel model(&catalog, catalog);
    model.SetPins(Pins({L"id0", L"ghost", L"id1"}));
    const std::vector<std::wstring> ids = model.EmptyStatePrewarmIds(24);
    Expect(ids.size() == 12, "the placeholder row is excluded from the prewarm ids");
    for (const std::wstring& id : ids) {
        Expect(id != L"ghost", "an absent pin's id is never prewarmed");
        bool found = false;
        for (const AppEntry& entry : catalog) {
            if (entry.stable_id == id) {
                found = true;
                break;
            }
        }
        Expect(found, "every prewarm id exists in the catalog snapshot");
    }
}

// NR-040: RecentStartIndex() marks where the recent region begins in the
// empty-query rows (pinned first, then recent); -1 when there is no region.

void TestRecentStartIndexPinsThenRecent() {
    const std::vector<AppEntry> catalog = CatalogOf(8);
    std::vector<AppEntry> recent;
    for (int i = 3; i < 8; ++i) {
        recent.push_back(catalog[static_cast<std::size_t>(i)]);
    }
    PanelModel model(&catalog, std::move(recent));
    model.SetPins(Pins({L"id0", L"id1", L"id2"}));
    Expect(model.RecentStartIndex() == 3, "recent region starts after 3 pins");
    Expect(model.Rows().size() == 8, "3 pins + 5 recent rows");
    for (std::size_t i = 3; i < model.Rows().size(); ++i) {
        Expect(model.Rows()[i].stable_id == L"id" + std::to_wstring(i),
               "rows from the recent region are the recent items");
    }
}

void TestRecentStartIndexAllPinned() {
    const std::vector<AppEntry> catalog = CatalogOf(5);
    PanelModel model(&catalog, catalog);
    model.SetPins(Pins({L"id0", L"id1", L"id2", L"id3", L"id4"}));
    Expect(model.Rows().size() == 5, "all 5 pinned rows shown");
    Expect(model.RecentStartIndex() == static_cast<int>(model.Rows().size()),
           "all-pinned empty view has no recent rows");
}

void TestRecentStartIndexFiltered() {
    const std::vector<AppEntry> catalog = CatalogOf(8);
    PanelModel model(&catalog, catalog);
    model.SetQuery(L"App");
    Expect(model.Rows().size() == catalog.size(), "broad query matches the whole catalog");
    Expect(model.RecentStartIndex() == -1, "search results have no recent region");
}

void TestRecentStartIndexNoPins() {
    const std::vector<AppEntry> catalog = CatalogOf(6);
    PanelModel model(&catalog, catalog);
    Expect(model.Rows().size() == 6, "all recent rows shown");
    Expect(model.RecentStartIndex() == 0, "no pins -> recent region starts at row 0");
}

// NR-071: design-spec §4.2 rule 2 -- the non-pinned (recent) region keeps the
// order SetRecent() was called with, which is UsageStore::Recent()'s newest-first
// order. The fixture keeps NR-053's deliberately "wrong" scores (5/100/20) and
// passes the entries newest-first; the model must preserve the input order and
// ignore the scores entirely, so the app launched most recently leads.

void TestRecentOrderedByRecency() {
    const std::vector<AppEntry> catalog = {
        Entry(L"low", L"Low"), Entry(L"high", L"High"), Entry(L"mid", L"Mid")};
    std::vector<AppEntry> recent = {
        Entry(L"low", L"Low"), Entry(L"high", L"High"), Entry(L"mid", L"Mid")};
    recent[0].usage_score = 5;   // newest launch, but the worst score
    recent[1].usage_score = 100;  // best score, but not the newest
    recent[2].usage_score = 20;
    PanelModel model(&catalog, std::move(recent));
    Expect(model.Rows().size() == 3, "three recent entries fill no catalog rows");
    Expect(model.Rows()[0].stable_id == L"low", "the most recently launched leads the recent region");
    Expect(model.Rows()[1].stable_id == L"high", "input order kept despite the best score");
    Expect(model.Rows()[2].stable_id == L"mid", "the oldest launch trails");
    Expect(model.RecentStartIndex() == 0, "no pins keeps the recent boundary at 0");
}

// NR-071: the recent region preserves input order even when every entry has the
// same usage score -- the old NR-053 tie-breaks (name length, then
// case-insensitive name) no longer apply, because the region is never sorted.

void TestRecentIgnoresNameAndScoreTieBreaks() {
    const std::vector<AppEntry> catalog = {
        Entry(L"a", L"Zebra"), Entry(L"b", L"alpha"), Entry(L"c", L"Beta")};
    std::vector<AppEntry> recent = {catalog[0], catalog[1], catalog[2]};
    for (AppEntry& entry : recent) {
        entry.usage_score = 7;
    }
    PanelModel model(&catalog, std::move(recent));
    Expect(model.Rows()[0].stable_id == L"a", "input order kept: Zebra first");
    Expect(model.Rows()[1].stable_id == L"b", "input order kept: alpha second");
    Expect(model.Rows()[2].stable_id == L"c", "input order kept: Beta last");
}

// NR-071: five entries with usage_score deliberately ascending (opposite to the
// newest-first input order) must come out in exactly the input order, starting
// at RecentStartIndex().

void TestRecentPreservesInputOrder() {
    const std::vector<AppEntry> catalog = {
        Entry(L"id0", L"App0"), Entry(L"id1", L"App1"), Entry(L"id2", L"App2"),
        Entry(L"id3", L"App3"), Entry(L"id4", L"App4")};
    std::vector<AppEntry> recent = {
        catalog[4], catalog[3], catalog[2], catalog[1], catalog[0]};
    for (std::size_t i = 0; i < recent.size(); ++i) {
        recent[i].usage_score = static_cast<int>(i);  // ascending, opposite to input order
    }
    PanelModel model(&catalog, std::move(recent));
    Expect(model.RecentStartIndex() == 0, "recent region starts at the first row");
    Expect(model.Rows().size() == 5, "all five entries show");
    for (std::size_t i = 0; i < 5; ++i) {
        Expect(model.Rows()[i].stable_id == L"id" + std::to_wstring(4 - i),
               "recent region matches the newest-first input order exactly");
    }
}

// NR-053: §FR-011 -- the pinned region is never sorted by score, and
// RecentStartIndex() keeps pointing at the pinned/recent boundary across the
// score sort.

// NR-061 overrode NR-053's alphabetical filler (docs/work-items/NR-061-empty-
// state-no-filler.md): RecentEndIndex() no longer needs to exclude filler
// rows because there is no filler, but it must still mark the end of the
// usage-backed region and go to -1 outside the empty-query state.
void TestRecentEndIndexExcludesFiller() {
    std::vector<AppEntry> catalog = {
        Entry(L"p1", L"PinOne"), Entry(L"r1", L"RecentOne"),
        Entry(L"f1", L"FillerOne"), Entry(L"f2", L"FillerTwo")};
    PanelModel model(&catalog, {catalog[1]});
    model.SetPins(Pins({L"p1"}));
    Expect(model.RecentStartIndex() == 1, "recent region starts after the pin");
    Expect(model.RecentEndIndex() == 2, "recent region holds only the one recent row");
    Expect(model.Rows().size() == 2, "no filler: only the pin and the recent row show");
    model.SetQuery(L"pin");
    Expect(model.RecentEndIndex() == -1, "search results have no recent region");
}

// NR-071: the pinned region keeps pin order even after the recent-region
// score sort was removed entirely -- pins are never sorted by anything, and
// RecentStartIndex() still points at the pinned/recent boundary.
void TestPinnedRegionStillNotSorted() {
    std::vector<AppEntry> catalog = {
        Entry(L"p1", L"PinOne"), Entry(L"p2", L"PinTwo"),
        Entry(L"r1", L"RecentOne"), Entry(L"r2", L"RecentTwo")};
    catalog[0].usage_score = 1;
    catalog[1].usage_score = 999;
    catalog[2].usage_score = 50;
    catalog[3].usage_score = 10;
    PanelModel model(&catalog, {});
    model.SetPins(Pins({L"p1", L"p2"}));
    Expect(model.Rows()[0].stable_id == L"p1", "pin order kept despite score 1");
    Expect(model.Rows()[1].stable_id == L"p2", "pin order kept despite score 999");
    Expect(model.RecentStartIndex() == 2, "recent boundary sits right after the two pins");
    Expect(model.Rows().size() == 2, "no filler: only the two pins show, no recent apps given");
}

// NR-053: both empty-catalog shapes (nullptr and an empty vector) show no
// rows and never crash.

void TestEmptyStateEmptyCatalogNoCrash() {
    PanelModel model_null(nullptr, {});
    Expect(model_null.Rows().empty(), "nullptr catalog shows no rows");
    Expect(!model_null.HasSelection(), "no selection with a nullptr catalog");
    const std::vector<AppEntry> empty;
    PanelModel model_empty(&empty, {});
    Expect(model_empty.Rows().empty(), "empty catalog vector shows no rows");
    Expect(!model_empty.HasSelection(), "no selection with an empty catalog");
}

// NR-061: docs/work-items/NR-061-empty-state-no-filler.md Agent check -- the
// empty-query view is exactly the pin and the recent app, nothing else from
// the catalog leaks in.
void TestEmptyStateHasNoFiller() {
    const std::vector<AppEntry> catalog = CatalogOf(4);
    std::vector<AppEntry> recent = {catalog[1]};
    PanelModel model(&catalog, std::move(recent));
    model.SetPins(Pins({L"id0"}));
    Expect(model.Rows().size() == 2, "only the pin and the one recent app show");
    Expect(model.Rows()[0].stable_id == L"id0", "the pin leads");
    Expect(model.Rows()[1].stable_id == L"id1", "the recent app follows");
}

// NR-061: no pins and no recent apps means an empty Rows(), not a catalog
// dump -- the host shows "No pinned or recent apps yet" for this state.
void TestEmptyStateAllEmpty() {
    const std::vector<AppEntry> catalog = CatalogOf(4);
    PanelModel model(&catalog, {});
    Expect(model.Rows().empty(), "no pins, no recent -> no rows at all");
}

// NR-062: docs/work-items/NR-062-missing-pin-placeholder.md Agent checks --
// a pin whose app is absent from the catalog is synthesized into a
// placeholder row instead of being dropped.

void TestMissingPinBecomesPlaceholder() {
    const std::vector<AppEntry> catalog = {Entry(L"other", L"Other")};
    PanelModel model(&catalog, {});
    model.SetPins({Pin(L"p1", L"Gone App")});
    Expect(model.Rows().size() == 1, "the missing pin still produces one row");
    Expect(model.Rows()[0].stable_id == L"p1", "placeholder keeps the pin's stable id");
    Expect(model.Rows()[0].display_name == L"Gone App",
           "placeholder shows the pin's recorded display name");
    Expect(PanelModel::IsMissingPin(model.Rows()[0]),
           "the synthesized row is reported as a missing pin");
}

void TestMissingPinKeepsOrder() {
    const std::vector<AppEntry> catalog = {Entry(L"p2", L"Present")};
    PanelModel model(&catalog, {});
    model.SetPins({Pin(L"p1", L"Gone"), Pin(L"p2")});
    Expect(model.Rows().size() == 2, "one placeholder plus one present pin");
    Expect(PanelModel::IsMissingPin(model.Rows()[0]), "the missing pin sits first, in pin order");
    Expect(model.Rows()[1].stable_id == L"p2", "the present pin follows in pin order");
    Expect(!PanelModel::IsMissingPin(model.Rows()[1]), "the present pin is not a placeholder");
    Expect(model.RecentStartIndex() == 2, "both pinned rows count toward the pinned region");
}

void TestMissingPinNotInSearch() {
    const std::vector<AppEntry> catalog = {Entry(L"other", L"Other")};
    PanelModel model(&catalog, {});
    model.SetPins({Pin(L"p1", L"Gone App")});
    model.SetQuery(L"gone");
    for (const AppEntry& row : model.Rows()) {
        Expect(row.stable_id != L"p1", "a missing pin's placeholder never appears in search results");
    }
}

void TestPresentPinIsNotMissing() {
    const std::vector<AppEntry> catalog = {Entry(L"p1", L"Present")};
    PanelModel model(&catalog, {});
    model.SetPins({Pin(L"p1")});
    Expect(model.Rows().size() == 1, "the present pin produces its one row");
    Expect(!PanelModel::IsMissingPin(model.Rows()[0]), "a resolved pin is never reported as missing");
}

// NR-083: the host hands the model a stable_id -> catalog index so pin
// resolution is a hash lookup instead of a full catalog scan. The indexed path
// must produce exactly the same rows as the linear scan -- present pins
// resolved from the catalog, absent pins as placeholders -- and clearing the
// hint with nullptr falls back to the scan.
void TestCatalogIndexResolvesPinsLikeScan() {
    const std::vector<AppEntry> catalog = {
        Entry(L"p2", L"Present"), Entry(L"other", L"Other")};
    std::unordered_map<std::wstring_view, std::size_t> index;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        index.emplace(catalog[i].stable_id, i);
    }
    PanelModel model(&catalog, {});
    model.SetPins({Pin(L"p1", L"Gone"), Pin(L"p2")});
    const auto scan_rows = model.Rows();

    model.SetCatalogIndex(&index);
    model.SetPins({Pin(L"p1", L"Gone"), Pin(L"p2")});
    Expect(model.Rows().size() == scan_rows.size(),
           "indexed path yields the same row count as the scan");
    Expect(model.Rows()[0].stable_id == scan_rows[0].stable_id,
           "indexed path keeps pin order for a missing pin");
    Expect(PanelModel::IsMissingPin(model.Rows()[0]),
           "indexed path still synthesizes the missing pin placeholder");
    Expect(model.Rows()[1].stable_id == scan_rows[1].stable_id,
           "indexed path resolves a present pin to the same entry");
    Expect(model.Rows()[1].display_name == scan_rows[1].display_name,
           "indexed path copies the catalog entry like the scan");

    model.SetCatalogIndex(nullptr);
    model.SetPins({Pin(L"p1", L"Gone"), Pin(L"p2")});
    Expect(model.Rows().size() == scan_rows.size(),
           "clearing the index falls back to the linear scan");
    Expect(model.Rows()[0].stable_id == scan_rows[0].stable_id,
           "fallback path keeps the missing pin placeholder");
}

} // namespace

int wmain() {
    TestEmptyQueryShowsRecent();
    TestEmptyStateNoRecords();
    TestQuerySwitchesToFilteredRows();
    TestMoveSelectionClampsAndWraps();
    TestEnterLaunchesSelectedOnly();
    TestEnterEmptyResultNoLaunch();
    TestEscClearsThenHides();
    TestWhitespaceQueryStaysInGrid();
    TestTrimmedQuerySameAsUntrimmed();
    TestSetQueryEmptyMatchesReset();
    TestEscOnWhitespaceQueryClearsFirst();
    TestQueryChangeResetsSelection();
    TestFailureKeepsModelIntact();
    TestSetViewportRowsClampsToOne();
    TestFewRowsKeepFirstVisibleZero();
    TestMoveSelectionScrollsViewportByOne();
    TestWrapToLastRowScrollsToTail();
    TestResetOperationsClearScroll();
    TestViewportLargerThanRowsNeverNegative();
    TestSelectRowBringsSelectionIntoView();
    TestScrollByPagesForward();
    TestScrollByClampsAtTail();
    TestScrollByClampsAtStart();
    TestScrollByFewerRowsThanViewport();
    TestScrollByEmptyList();
    TestScrollByRoundTripNoWrap();
    TestRowForVisibleSlotBasics();
    TestRowForVisibleSlotTracksScroll();
    TestRowForVisibleSlotOutOfRange();
    TestRowForVisibleSlotPastListEnd();
    TestRowForVisibleSlotEmptyList();
    TestRowForVisibleSlotIsConst();
    TestGridColumnsClampAndQuerySwitch();
    TestGridFirstVisibleAlignedToColumns();
    TestGridTailItemsReachable();
    TestGridPageNotMultipleStillReachesAll();
    TestGridMoveSelectionRows();
    TestGridScrollByPages();
    TestGridFewerThanPageNoScroll();
    TestGridQueryTransitionResetsViewport();
    TestGridRowForVisibleSlot();
    TestEmptyStatePrewarmIdsPinsThenRecent();
    TestEmptyStatePrewarmIdsCapsAtOnePage();
    TestEmptyStatePrewarmIdsZeroMax();
    TestEmptyStatePrewarmIdsNonEmptyQuery();
    TestEmptyStatePrewarmIdsEmptyCatalog();
    TestEmptyStatePrewarmIdsIsConst();
    TestEmptyStatePrewarmIdsAbsentPinSkipped();
    TestRecentStartIndexPinsThenRecent();
    TestRecentStartIndexAllPinned();
    TestRecentStartIndexFiltered();
    TestRecentStartIndexNoPins();
    TestRecentEndIndexExcludesFiller();
    TestRecentOrderedByRecency();
    TestRecentIgnoresNameAndScoreTieBreaks();
    TestRecentPreservesInputOrder();
    TestPinnedRegionStillNotSorted();
    TestEmptyStateEmptyCatalogNoCrash();
    TestEmptyStateHasNoFiller();
    TestEmptyStateAllEmpty();
    TestMissingPinBecomesPlaceholder();
    TestMissingPinKeepsOrder();
    TestMissingPinNotInSearch();
    TestPresentPinIsNotMissing();
    TestCatalogIndexResolvesPinsLikeScan();
    std::printf("NR-010/NR-020/NR-021/NR-024/NR-029/NR-037/NR-040/NR-053/NR-061/NR-062/NR-083 panel model check PASSED\n");
    return 0;
}
