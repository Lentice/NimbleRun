#include "pins/pin_store.h"

#include "app_host/panel_model.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::kPinRetentionSeconds;
using nimblerun::PanelModel;
using nimblerun::PinLoadResult;
using nimblerun::PinStore;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

std::wstring MakeTempDir(const char* label) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_pinning_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::wstring(label, label + std::char_traits<char>::length(label));
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(fs::path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool SameIds(const std::vector<std::wstring>& actual,
             std::initializer_list<std::wstring> expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    auto it = expected.begin();
    for (const std::wstring& id : actual) {
        if (id != *it) {
            return false;
        }
        ++it;
    }
    return true;
}

AppEntry Entry(std::wstring id, std::wstring name) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = name;
    entry.normalized_name = name;
    entry.launch_identity = L"C:\\Apps\\" + name + L".exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserFolder;
    return entry;
}

// NR-062: SetPins takes pin records, not bare ids; this builds the common case
// (no display name needed by the test) tersely.
std::vector<nimblerun::PinRecord> MakePins(std::initializer_list<std::wstring> ids) {
    std::vector<nimblerun::PinRecord> pins;
    for (const std::wstring& id : ids) {
        nimblerun::PinRecord pin;
        pin.stable_id = id;
        pins.push_back(std::move(pin));
    }
    return pins;
}

// Pin/unpin round-trips across a reload (restart persistence).
void TestPinUnpinRoundTrip() {
    const std::wstring dir = MakeTempDir("roundtrip");
    PinStore store(dir);
    store.Load();
    store.Pin(L"app1", L"", 1000);
    store.Pin(L"app2", L"", 2000);
    Expect(store.IsPinned(L"app1"), "app1 pinned");
    store.Unpin(L"app1");
    Expect(!store.IsPinned(L"app1"), "app1 unpinned in memory");
    Expect(store.Save(), "save pins");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "round-trip load");
    Expect(!reloaded.IsPinned(L"app1"), "unpinned app stays gone after reload");
    Expect(reloaded.IsPinned(L"app2"), "pinned app survives reload");
    Expect(SameIds(reloaded.OrderedPins(), {L"app2"}), "order after unpin + reload");
    fs::remove_all(dir);
}

// Pin order is the creation order and survives a reload.
void TestPinOrderPreserved() {
    const std::wstring dir = MakeTempDir("order");
    PinStore store(dir);
    store.Load();
    store.Pin(L"c", L"", 100);
    store.Pin(L"a", L"", 200);
    store.Pin(L"b", L"", 300);
    Expect(SameIds(store.OrderedPins(), {L"c", L"a", L"b"}), "creation order");
    Expect(store.Save(), "save pinned order");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "reload pinned order");
    Expect(SameIds(reloaded.OrderedPins(), {L"c", L"a", L"b"}),
           "pin order preserved across reload");
    fs::remove_all(dir);
}

void TestUnpinRemovesOnlyThatPin() {
    const std::wstring dir = MakeTempDir("unpin");
    PinStore store(dir);
    store.Load();
    store.Pin(L"a", L"", 100);
    store.Pin(L"b", L"", 200);
    store.Pin(L"c", L"", 300);
    store.Unpin(L"b");
    Expect(!store.IsPinned(L"b"), "b unpinned");
    Expect(store.IsPinned(L"a") && store.IsPinned(L"c"), "other pins untouched");
    Expect(SameIds(store.OrderedPins(), {L"a", L"c"}), "remaining pins keep their order");
    fs::remove_all(dir);
}

// Re-pinning the same app is idempotent: one record, original position, and
// the last_seen clock is refreshed (so the re-pin survives a near-30-day gap).
void TestDuplicatePinIdempotent() {
    const std::wstring dir = MakeTempDir("idempotent");
    PinStore store(dir);
    store.Load();
    store.Pin(L"a", L"", 100);
    store.Pin(L"b", L"", 200);
    store.Pin(L"a", L"", 1000);  // re-pin refreshes last_seen, no duplicate
    Expect(SameIds(store.OrderedPins(), {L"a", L"b"}), "re-pin keeps one record and position");
    Expect(store.Save(), "save idempotent pins");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "reload idempotent pins");
    // a was last seen at 1000, so even 30 days + a bit later it is still young.
    reloaded.Reconcile({}, 1000 + kPinRetentionSeconds + 1);
    Expect(reloaded.IsPinned(L"a"), "re-pin refreshed last_seen (not expired)");
    fs::remove_all(dir);
}

// NR-070: favorites.txt is untrusted input -- a hand-edited last_seen of
// INT64_MIN used to make `now - pin.last_seen_utc` a signed overflow (UB) that
// wrapped negative and kept the pin forever. The comparison form
// `last_seen_utc >= now - kPinRetentionSeconds` treats it as expired instead:
// the pin is dropped for an absent app, no UB, no permanent retention.
void TestAbsentPinWithInt64MinLastSeenIsDropped() {
    const std::wstring dir = MakeTempDir("int64min");
    const std::string content =
        "schema=2\nghost_app\t-9223372036854775808\tGhost App\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::Loaded, "INT64_MIN last_seen loads normally");
    Expect(store.IsPinned(L"ghost_app"), "the pin exists after load");

    // The app is absent from a real catalog; an INT64_MIN last_seen is far
    // outside the retention window, so the pin expires instead of overflowing.
    std::vector<AppEntry> catalog = {Entry(L"present_app", L"Present")};
    store.Reconcile(catalog, 1000 + kPinRetentionSeconds);
    Expect(!store.IsPinned(L"ghost_app"), "INT64_MIN pin dropped as expired, no UB");
    Expect(store.IsPinned(L"present_app") == false, "no pin for the catalog-only app");
    fs::remove_all(dir);
}

// A pin for an app absent from a real (non-empty) catalog survives a
// reconcile; it is not deleted on the first failed/missing scan.
void TestAbsentPinSurvivesReconcile() {
    const std::wstring dir = MakeTempDir("absent");
    PinStore store(dir);
    store.Load();
    store.Pin(L"ghost_app", L"", 1000);
    store.Pin(L"present_app", L"", 2000);
    std::vector<AppEntry> catalog = {Entry(L"present_app", L"Present")};
    store.Reconcile(catalog, 1005);
    Expect(store.IsPinned(L"ghost_app"), "absent app's pin kept on first scan");
    Expect(store.IsPinned(L"present_app"), "present app's pin kept");
    fs::remove_all(dir);
}

// An empty catalog (first launch, failed scan, in-flight rebuild) must never
// cause a pin to be deleted, even one that would otherwise be expired.
void TestEmptyCatalogNeverDeletes() {
    const std::wstring dir = MakeTempDir("emptycatalog");
    PinStore store(dir);
    store.Load();
    store.Pin(L"ancient_app", L"", 100);
    store.Reconcile({}, 100 + kPinRetentionSeconds + 999);
    Expect(store.IsPinned(L"ancient_app"),
           "empty catalog never deletes a pin (no failed-scan deletion)");
    fs::remove_all(dir);
}

// 30-day expiry: an absent pin older than the window is dropped on reconcile;
// a recently absent pin is kept; a present pin is kept and last_seen refreshed.
void TestReconcile30DayExpiry() {
    const std::wstring dir = MakeTempDir("expiry");
    PinStore store(dir);
    store.Load();
    store.Pin(L"old", L"", 100);                      // absent long ago -> drop
    store.Pin(L"present", L"", 2000);                 // in catalog -> keep + refresh
    const std::int64_t now = 1000 + kPinRetentionSeconds + 10;
    store.Pin(L"recent_absent", L"", now - 5);        // absent but recent -> keep
    std::vector<AppEntry> catalog = {Entry(L"present", L"Present")};
    store.Reconcile(catalog, now);

    Expect(SameIds(store.OrderedPins(), {L"present", L"recent_absent"}),
           "expired absent pin dropped; present and recent-absent kept");
    Expect(!store.IsPinned(L"old"), "pin absent for > 30 days dropped");
    Expect(store.Save(), "save reconciled pins");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "reload reconciled pins");
    Expect(SameIds(reloaded.OrderedPins(), {L"present", L"recent_absent"}),
           "dropped pin stays gone after reload");
    fs::remove_all(dir);
}

void TestCorrupt() {
    const std::wstring dir = MakeTempDir("corrupt");
    const std::string content = "not a favorites file\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::Corrupt, "corrupt file reports Corrupt");
    Expect(store.OrderedPins().empty(), "corrupt load yields the empty safe default");
    Expect(!fs::exists(dir + L"\\favorites.txt"), "corrupt file moved aside");
    Expect(fs::exists(dir + L"\\favorites.txt.corrupt"), "corrupt file preserved");
    Expect(ReadBytes(dir + L"\\favorites.txt.corrupt") == content,
           "corrupt content preserved verbatim");
    fs::remove_all(dir);
}

void TestMalformedRow() {
    const std::wstring dir = MakeTempDir("badrow");
    const std::string content = "schema=1\nonly_one_field_no_timestamp\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::Corrupt, "malformed row reports Corrupt");
    Expect(store.OrderedPins().empty(), "malformed row yields the empty safe default");
    Expect(!fs::exists(dir + L"\\favorites.txt"), "malformed file moved aside");
    Expect(fs::exists(dir + L"\\favorites.txt.corrupt"), "malformed file preserved");
    fs::remove_all(dir);
}

// NR-072: a corrupt row in the middle of an otherwise valid file must not let
// the valid prefix leak into the live store -- the non-Loaded contract is
// "store is empty" (pin_store.h). The prefix pins were already parsed before
// the bad row, so Load must clear them before reporting Corrupt, or the next
// RefreshPins Save() would write that partial parse as the new favorites.txt.
void TestCorruptMidFileClearsPins() {
    const std::wstring dir = MakeTempDir("midcorrupt");
    const std::string content =
        "schema=2\nvalid_one\t1000\tOne\nvalid_two\t2000\tTwo\nnot_a_valid_row\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::Corrupt, "mid-file corrupt row reports Corrupt");
    Expect(store.Records().empty(), "corrupt load leaves no partial pins in the store");
    Expect(!fs::exists(dir + L"\\favorites.txt"), "corrupt file moved aside");
    Expect(fs::exists(dir + L"\\favorites.txt.corrupt"), "corrupt file preserved");
    Expect(ReadBytes(dir + L"\\favorites.txt.corrupt") == content,
           "corrupt content preserved verbatim");
    fs::remove_all(dir);
}

void TestNewerSchema() {
    const std::wstring dir = MakeTempDir("newer");
    const std::string content = "schema=99\npinned_app\t1000\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::NewerSchema, "newer schema reports NewerSchema");
    Expect(store.OrderedPins().empty(), "newer schema yields the empty safe default");
    Expect(fs::exists(dir + L"\\favorites.txt"), "newer schema file untouched");
    Expect(ReadBytes(dir + L"\\favorites.txt") == content, "newer schema content unchanged");
    Expect(!fs::exists(dir + L"\\favorites.txt.corrupt"), "newer schema not treated as corrupt");
    fs::remove_all(dir);
}

// NR-062 critical path: a schema=1 file (2 fields per line, no display_name
// column) must load cleanly via the OlderSchema path, not be treated as
// corrupt -- otherwise every existing user's favorites.txt would be wiped to
// a .corrupt file on this upgrade.
void TestLoadSchema1File() {
    const std::wstring dir = MakeTempDir("schema1");
    const std::string content = "schema=1\npinned_app\t1000\nother_app\t2000\n";
    WriteBytes(dir + L"\\favorites.txt", content);
    PinStore store(dir);
    Expect(store.Load() == PinLoadResult::Loaded, "schema=1 file loads via OlderSchema, not Corrupt");
    Expect(SameIds(store.OrderedPins(), {L"pinned_app", L"other_app"}),
           "both schema=1 pins loaded");
    Expect(store.Records()[0].display_name.empty(),
           "a schema=1 record has no display name");
    Expect(!fs::exists(dir + L"\\favorites.txt.corrupt"),
           "a schema=1 file is never quarantined as corrupt");
    fs::remove_all(dir);
}

// Pin()'s display_name round-trips through Save()/Load().
void TestSaveRoundTripsDisplayName() {
    const std::wstring dir = MakeTempDir("displayname");
    PinStore store(dir);
    store.Load();
    store.Pin(L"app1", L"My App", 1000);
    Expect(store.Save(), "save with display name");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "reload after save");
    Expect(reloaded.Records().size() == 1, "one pin reloaded");
    Expect(reloaded.Records()[0].display_name == L"My App",
           "display name survives the round trip");
    fs::remove_all(dir);
}

// Save() always writes the current schema version's header.
void TestSaveWritesSchema2() {
    const std::wstring dir = MakeTempDir("schema2write");
    PinStore store(dir);
    store.Load();
    store.Pin(L"app1", L"App One", 1000);
    Expect(store.Save(), "save pins");
    const std::string bytes = ReadBytes(dir + L"\\favorites.txt");
    Expect(bytes.rfind("schema=2", 0) == 0, "Save() writes a schema=2 header");
    fs::remove_all(dir);
}

void TestAtomicWriteFailure() {
    const std::wstring dir = MakeTempDir("atomic");
    PinStore store(dir);
    store.Load();
    store.Pin(L"alpha", L"", 100);
    Expect(store.Save(), "initial save");

    // A directory occupying the .tmp path forces the temp write to fail.
    Expect(fs::create_directory(dir + L"\\favorites.txt.tmp"), "create tmp dir obstacle");
    store.Pin(L"beta", L"", 200);
    Expect(store.Save() == false, "save fails when temp write fails");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "original survives failed save");
    Expect(SameIds(reloaded.OrderedPins(), {L"alpha"}),
           "failed save left the original file untouched");
    fs::remove_all(dir);
}

// Empty-query ordering: pinned entries come first (in pin order) and a pinned
// recent app appears only once (in the pinned region, not recent).
void TestPanelModelPinnedFirst() {
    const std::vector<AppEntry> catalog = {
        Entry(L"r1", L"RecentOne"), Entry(L"p", L"PinnedApp"), Entry(L"r2", L"RecentTwo")};
    std::vector<AppEntry> recent = {Entry(L"r1", L"RecentOne"), Entry(L"p", L"PinnedApp")};
    PanelModel model(&catalog, std::move(recent));
    model.SetPins(MakePins({L"p"}));
    const auto& rows = model.Rows();
    Expect(rows[0].stable_id == L"p", "pinned entry comes before recent");
    Expect(rows[1].stable_id == L"r1", "recent follows; pinned app not repeated");
    // NR-061 overrode NR-053's alphabetical filler: r2 has no usage record and
    // is not pinned, so it never appears in the empty-query view.
    Expect(rows.size() == 2, "no filler: only the pin and the one recent app show");
    std::size_t pinned_count = 0;
    for (const AppEntry& row : rows) {
        if (row.stable_id == L"p") {
            ++pinned_count;
        }
    }
    Expect(pinned_count == 1, "pinned app appears exactly once");
}

// NR-062 overrode this NR-018 behavior (docs/work-items/NR-062-missing-pin-
// placeholder.md): a pin whose app is absent from the catalog is no longer
// hidden -- it is shown as an unlaunchable placeholder row so the user can see
// and unpin it, instead of it silently vanishing. The record itself still
// stays in the store either way.
void TestPanelModelShowsAbsentPinAsPlaceholder() {
    const std::vector<AppEntry> catalog = {Entry(L"a", L"Alpha")};
    PanelModel model(&catalog, {});
    nimblerun::PinRecord ghost;
    ghost.stable_id = L"ghost_app";
    ghost.display_name = L"Ghost App";
    model.SetPins({ghost});
    Expect(model.Rows().size() == 1, "absent pin shows as a placeholder row");
    Expect(model.Rows()[0].stable_id == L"ghost_app", "placeholder keeps the pin's stable id");
    Expect(model.Rows()[0].display_name == L"Ghost App", "placeholder shows the pin's display name");
    Expect(PanelModel::IsMissingPin(model.Rows()[0]), "the row is reported as a missing pin");
}

// NR-046: ReorderPresent reorders only the pins named in the new visual order;
// a pin not named there (an app absent from the current catalog) keeps the
// absolute slot it already occupies, so it never moves in favorites.txt
// (acceptance step 9). The new order survives a Save/Load round-trip.
//
// On a starting store [a, b, c, d], ReorderPresent({d, a, c}) places d onto a's
// old slot 0, a onto c's old slot 2 and c onto d's old slot 3; b was not named
// and keeps its original index 1 -> [d, b, a, c]. The grid, which skips the
// absent b, then shows [d, a, c] -- the dragged visual order.
void TestReorderKeepsAbsentPinsInPlace() {
    const std::wstring dir = MakeTempDir("reorder");
    PinStore store(dir);
    store.Load();
    store.Pin(L"a", L"", 100);
    store.Pin(L"b", L"", 200);
    store.Pin(L"c", L"", 300);
    store.Pin(L"d", L"", 400);
    Expect(store.ReorderPresent({L"d", L"a", L"c"}), "reorder reports a change");
    Expect(SameIds(store.OrderedPins(), {L"d", L"b", L"a", L"c"}),
           "unlisted pin keeps its absolute slot");
    Expect(store.Save(), "save reordered pins");

    PinStore reloaded(dir);
    Expect(reloaded.Load() == PinLoadResult::Loaded, "reload reordered pins");
    Expect(SameIds(reloaded.OrderedPins(), {L"d", L"b", L"a", L"c"}),
           "reordered order survives the round-trip");
    fs::remove_all(dir);

    // A fresh store already in order a, b, c, d: the identity reorder is a
    // no-op and an unknown id changes nothing.
    const std::wstring dir2 = MakeTempDir("reorder_idle");
    PinStore fresh(dir2);
    fresh.Load();
    fresh.Pin(L"a", L"", 100);
    fresh.Pin(L"b", L"", 200);
    fresh.Pin(L"c", L"", 300);
    fresh.Pin(L"d", L"", 400);
    Expect(!fresh.ReorderPresent({L"a", L"b", L"c", L"d"}),
           "identity reorder on an already-ordered store returns false");
    Expect(SameIds(fresh.OrderedPins(), {L"a", L"b", L"c", L"d"}),
           "identity reorder is a no-op");
    Expect(!fresh.ReorderPresent({L"z"}), "unknown id changes nothing");
    Expect(SameIds(fresh.OrderedPins(), {L"a", L"b", L"c", L"d"}),
           "unknown id leaves the order intact");
    fs::remove_all(dir2);
}

} // namespace

int wmain() {
    TestPinUnpinRoundTrip();
    TestPinOrderPreserved();
    TestUnpinRemovesOnlyThatPin();
    TestDuplicatePinIdempotent();
    TestAbsentPinSurvivesReconcile();
    TestAbsentPinWithInt64MinLastSeenIsDropped();
    TestEmptyCatalogNeverDeletes();
    TestReconcile30DayExpiry();
    TestCorrupt();
    TestMalformedRow();
    TestCorruptMidFileClearsPins();
    TestNewerSchema();
    TestLoadSchema1File();
    TestSaveRoundTripsDisplayName();
    TestSaveWritesSchema2();
    TestAtomicWriteFailure();
    TestPanelModelPinnedFirst();
    TestPanelModelShowsAbsentPinAsPlaceholder();
    TestReorderKeepsAbsentPinsInPlace();
    std::printf("NR-018 pin store check PASSED\n");
    return 0;
}
