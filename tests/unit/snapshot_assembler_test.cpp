#include "test_util.h"

#include "app_host/panel_model.h"
#include "app_host/snapshot_assembler.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::CatalogRefreshCoordinator;
using nimblerun::CatalogSnapshotAssembler;
using nimblerun::DefaultSettings;
using nimblerun::PanelModel;
using nimblerun::PinLoadResult;
using nimblerun::PinStore;
using nimblerun::Settings;
using nimblerun::UsageStore;

namespace {

std::wstring MakeTempDir(const wchar_t* label) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    Expect(length != 0 && length < MAX_PATH, "get temp path");
    const std::wstring dir = std::wstring(buffer, length) +
                             L"NimbleRun_snapshot_assembler_test_" +
                             std::to_wstring(GetCurrentProcessId()) + L"_" + label;
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp directory");
    return dir;
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
    Expect(out.good(), "open test file for write");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Expect(out.good(), "write test file");
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(fs::path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

AppEntry Entry(std::wstring id, std::wstring name) {
    AppEntry entry;
    entry.stable_id = std::move(id);
    entry.display_name = std::move(name);
    entry.normalized_name = entry.display_name;
    entry.launch_identity = L"C:\\Apps\\" + entry.display_name + L".exe";
    entry.source_path = entry.launch_identity;
    entry.source = AppSource::UserFolder;
    return entry;
}

struct Fixture {
    std::wstring directory;
    CatalogRefreshCoordinator refresh;
    UsageStore usage;
    PinStore pins;
    Settings settings;
    PanelModel model;
    CatalogSnapshotAssembler assembler;

    Fixture(const wchar_t* label, int recent_count = 20)
        : directory(MakeTempDir(label)),
          usage(directory),
          pins(directory),
          settings(DefaultSettings()),
          model(&refresh.Snapshot(), {}),
          assembler(refresh, usage, pins, model, settings) {
        settings.recent_count = recent_count;
    }

    ~Fixture() {
        fs::remove_all(directory);
    }
};

// NR-061: startup may have usage records while the first catalog snapshot is
// empty; assembling that state must not reconcile or rewrite them.
void TestEmptySnapshotDoesNotWipeUsage() {
    Fixture fixture(L"empty");
    Expect(fixture.usage.Load() == nimblerun::UsageLoadResult::Missing,
           "usage starts missing");
    Expect(fixture.usage.RecordLaunch(L"still_recent", 100),
           "record usage before refresh");
    Expect(fixture.usage.Save(), "save usage fixture");
    const std::string before = ReadBytes(fixture.directory + L"\\usage.tsv");

    fixture.assembler.Refresh();

    Expect(fixture.usage.Records().size() == 1,
           "empty snapshot keeps usage record in memory");
    Expect(ReadBytes(fixture.directory + L"\\usage.tsv") == before,
           "empty snapshot does not rewrite usage file");
}

// NR-072: a partial or newer pin load is a safe default, not input that may be
// reconciled and saved back over the user's original file.
void TestCorruptPinsAreNotReconciledOrSaved() {
    Fixture fixture(L"corrupt_pins");
    const std::string corrupt =
        "schema=2\nvalid_pin\t1000\tValid\nnot_a_valid_pin_row\n";
    WriteBytes(fixture.directory + L"\\favorites.txt", corrupt);
    fixture.refresh.SetSnapshot({Entry(L"present", L"Present")});

    const CatalogSnapshotAssembler::Result result = fixture.assembler.Refresh();

    Expect(result.pin_load_notice, "corrupt pin load requests notice");
    Expect(result.pin_load_result == PinLoadResult::Corrupt,
           "corrupt pin load result is returned");
    Expect(!fs::exists(fixture.directory + L"\\favorites.txt"),
           "corrupt favorites is not recreated by refresh");
    Expect(fs::exists(fixture.directory + L"\\favorites.txt.corrupt"),
           "corrupt favorites is preserved");
}

// Pins must be loaded and handed to the model before SetRecent is the final
// RefreshRows trigger; otherwise the pinned-first view loses its stamp.
void TestPinsAreStampedBeforeRecentRows() {
    Fixture fixture(L"pin_order");
    Expect(fixture.pins.Load() == PinLoadResult::Missing,
           "pin fixture starts missing");
    Expect(fixture.pins.Pin(L"pinned", L"Pinned", 100), "create pin fixture");
    Expect(fixture.pins.Save(), "save pin fixture");
    fixture.refresh.SetSnapshot({Entry(L"pinned", L"Pinned"),
                                 Entry(L"other", L"Other")});

    fixture.assembler.Refresh();

    Expect(!fixture.model.Rows().empty(), "pinned row is visible");
    Expect(fixture.model.Rows()[0].stable_id == L"pinned",
           "pinned row is first");
    Expect(fixture.model.Rows()[0].is_pinned,
           "pinned row keeps is_pinned stamp");
}

// NR-083: the index keys borrow the current snapshot and must not remain in
// PanelModel after Refresh returns.
void TestCatalogIndexIsClearedOnReturn() {
    Fixture fixture(L"index_lifetime");
    fixture.refresh.SetSnapshot({Entry(L"app", L"App")});
    std::unordered_map<std::wstring_view, std::size_t> old_index;
    fixture.model.SetCatalogIndex(&old_index);

    fixture.assembler.Refresh();

    Expect(!fixture.model.HasCatalogIndex(),
           "catalog index is cleared before Refresh returns");
}

// Launch updates restamp the snapshot without resetting the visible selection;
// pin edits use the same seam but must republish rows.
void TestPinChangeRefreshMode() {
    Fixture fixture(L"change_modes");
    Expect(fixture.usage.Load() == nimblerun::UsageLoadResult::Missing,
           "usage fixture starts missing");
    Expect(fixture.usage.RecordLaunch(L"app", 100), "record initial usage");
    Expect(fixture.pins.Load() == PinLoadResult::Missing,
           "pin fixture starts missing");
    Expect(fixture.pins.Pin(L"app", L"App", 100), "create pin fixture");
    Expect(fixture.pins.Save(), "save pin fixture");
    fixture.refresh.SetSnapshot({Entry(L"app", L"App")});
    fixture.assembler.Refresh();

    Expect(fixture.model.Rows().size() == 1, "pinned row is visible");
    const int old_row_score = fixture.model.Rows()[0].usage_score;
    Expect(fixture.usage.RecordLaunch(L"app", 200), "record updated usage");

    fixture.assembler.OnPinsChanged(false);
    Expect(fixture.model.Rows()[0].usage_score == old_row_score,
           "stamp-only update does not rebuild visible rows");
    Expect(fixture.refresh.Snapshot()[0].usage_score > old_row_score,
           "stamp-only update reaches the catalog snapshot");

    fixture.assembler.OnPinsChanged(true);
    Expect(fixture.model.Rows()[0].usage_score ==
               fixture.refresh.Snapshot()[0].usage_score,
           "pin update republishes rows with the new score");
}

// Recent rows are resolved against the same snapshot and capped by settings;
// an absent usage record never becomes a visible filler row.
void TestRecentRowsAreSnapshotBoundAndCapped() {
    Fixture fixture(L"recent_cap", 2);
    Expect(fixture.usage.Load() == nimblerun::UsageLoadResult::Missing,
           "usage fixture starts missing");
    Expect(fixture.usage.RecordLaunch(L"old", 100), "record old usage");
    Expect(fixture.usage.RecordLaunch(L"newer", 300), "record newer usage");
    Expect(fixture.usage.RecordLaunch(L"middle", 200), "record middle usage");
    Expect(fixture.usage.RecordLaunch(L"missing", 50), "record absent usage");
    fixture.refresh.SetSnapshot({Entry(L"old", L"Old"),
                                 Entry(L"newer", L"Newer"),
                                 Entry(L"middle", L"Middle")});

    fixture.assembler.Refresh();

    Expect(fixture.model.Rows().size() == 2,
           "recent rows honor recent_count cap");
    Expect(fixture.model.Rows()[0].stable_id == L"newer" &&
               fixture.model.Rows()[1].stable_id == L"middle",
           "recent rows contain only capped snapshot entries in order");
}

} // namespace

int wmain() {
    TestEmptySnapshotDoesNotWipeUsage();
    TestCorruptPinsAreNotReconciledOrSaved();
    TestPinsAreStampedBeforeRecentRows();
    TestCatalogIndexIsClearedOnReturn();
    TestPinChangeRefreshMode();
    TestRecentRowsAreSnapshotBoundAndCapped();
    std::puts("NR-134 snapshot assembler check PASSED");
    return 0;
}
