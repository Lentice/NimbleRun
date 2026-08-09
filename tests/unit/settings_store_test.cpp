#include "settings/settings_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::DefaultSettings;
using nimblerun::ReadVersionedLines;
using nimblerun::Settings;
using nimblerun::SettingsLoadResult;
using nimblerun::SettingsStore;
using nimblerun::Theme;
using nimblerun::VersionedReadStatus;

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
        std::wstring(buffer) + L"NimbleRun_settings_test_" + std::to_wstring(GetCurrentProcessId()) +
        L"_" + std::wstring(label, label + std::char_traits<char>::length(label));
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

void TestDefaults(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings loaded = DefaultSettings();
    loaded.hotkey = L"changed";
    Expect(store.Load(loaded) == SettingsLoadResult::Missing, "missing file reports Missing");
    Expect(loaded.hotkey == L"Alt+Space", "default hotkey");
    Expect(loaded.auto_start == false, "default auto_start");
    Expect(loaded.theme == Theme::System, "default theme");
    Expect(loaded.recent_count == 20, "default recent_count");
    Expect(loaded.hide_after_launch == true, "default hide_after_launch");
    Expect(loaded.include_windows_apps == true, "default include_windows_apps");
    Expect(loaded.catalog_roots.empty(), "default catalog_roots is empty");
    Expect(loaded.catalog_extensions == nimblerun::DefaultExtensions(),
           "default catalog_extensions is the full allowlist");
}

void TestRoundTrip(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings expected;
    expected.hotkey = L"Ctrl+Shift+P";
    expected.auto_start = true;
    expected.theme = Theme::Dark;
    expected.recent_count = 32;
    expected.hide_after_launch = false;
    expected.include_windows_apps = false;
    Expect(store.Save(expected), "save settings");

    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "round-trip load");
    Expect(loaded.hotkey == expected.hotkey, "round-trip hotkey");
    Expect(loaded.auto_start == expected.auto_start, "round-trip auto_start");
    Expect(loaded.theme == expected.theme, "round-trip theme");
    Expect(loaded.recent_count == expected.recent_count, "round-trip recent_count");
    Expect(loaded.hide_after_launch == expected.hide_after_launch, "round-trip hide_after_launch");
    Expect(loaded.include_windows_apps == expected.include_windows_apps,
           "round-trip include_windows_apps");
}

void TestIncludeWindowsAppsOldFormat(const std::wstring& dir) {
    // A pre-NR-028 settings.ini has no include_windows_apps key; on load the
    // default (true) must win so the AppsFolder source keeps working.
    WriteBytes(dir + L"\\settings.ini",
        "schema=1\n"
        "hotkey=Alt+Space\n"
        "recent_count=20\n"
        "hide_after_launch=true\n");
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "old-format load");
    Expect(loaded.include_windows_apps == true, "missing key loads the default true");
}

void TestCatalogRootsRoundTrip(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings expected;
    expected.catalog_roots.push_back({L"C:\\Tools", true});
    expected.catalog_roots.push_back({L"D:\\Games\\Emu", false});
    expected.catalog_extensions = {L".exe", L".lnk"};
    Expect(store.Save(expected), "save catalog roots");

    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "catalog roots load");
    Expect(loaded.catalog_roots.size() == 2, "catalog roots count");
    Expect(loaded.catalog_roots[0].path == L"C:\\Tools", "catalog root 1 path");
    Expect(loaded.catalog_roots[0].recursive == true, "catalog root 1 recursive");
    Expect(loaded.catalog_roots[1].path == L"D:\\Games\\Emu", "catalog root 2 path");
    Expect(loaded.catalog_roots[1].recursive == false, "catalog root 2 recursive");
    Expect(loaded.catalog_extensions == expected.catalog_extensions, "catalog extensions round-trip");
}

void TestCatalogRootsValidation(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini",
        "schema=1\n"
        "catalog_root=C:\\Valid|true\n"
        "catalog_root=unc\\share|true\n"
        "catalog_root=//net\\host|false\n"
        "catalog_root=C:\\NoFlag\n"
        "catalog_extension=.exe\n"
        "catalog_extension=.DLL\n"
        "catalog_extension=.txt\n"
        "catalog_extension=exe\n");
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "catalog validation load");
    Expect(loaded.catalog_roots.size() == 2, "only local absolute roots kept");
    Expect(loaded.catalog_roots[0].path == L"C:\\Valid", "valid root kept");
    Expect(loaded.catalog_roots[0].recursive == true, "valid root recursive flag");
    Expect(loaded.catalog_roots[1].path == L"C:\\NoFlag", "valid root without flag kept");
    Expect(loaded.catalog_roots[1].recursive == true, "missing flag defaults to recursive");
    Expect(loaded.catalog_extensions.size() == 1, "only allowlisted extensions kept");
    Expect(loaded.catalog_extensions[0] == L".exe", "extension normalized and deduped");
}

void TestEscaping(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings expected;
    expected.hotkey = L"Ctrl+Alt+= \\slash\nnewline\ttab";
    expected.theme = Theme::Light;
    Expect(store.Save(expected), "save escaped settings");

    const std::string on_disk = ReadBytes(dir + L"\\settings.ini");
    Expect(on_disk.find("\\=") != std::string::npos, "escaped equals on disk");
    Expect(on_disk.find("\\\\") != std::string::npos, "escaped backslash on disk");
    Expect(on_disk.find("\\n") != std::string::npos, "escaped newline on disk");
    Expect(on_disk.find("\\t") != std::string::npos, "escaped tab on disk");

    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "escaping load");
    Expect(loaded.hotkey == expected.hotkey, "escaping round-trip hotkey");
    Expect(loaded.theme == Theme::Light, "escaping round-trip theme");
}

void TestValidation(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini",
        "schema=1\nrecent_count=1000\ntheme=bogus\nauto_start=banana\n"
        "hide_after_launch=maybe\nhotkey=\n");
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "validation load");
    Expect(loaded.recent_count == 20, "out-of-range recent_count defaults");
    Expect(loaded.theme == Theme::System, "bogus theme defaults");
    Expect(loaded.auto_start == false, "bogus auto_start defaults");
    Expect(loaded.hide_after_launch == true, "bogus hide_after_launch defaults");
    Expect(loaded.hotkey == L"Alt+Space", "empty hotkey defaults");
}

void TestCorrupt(const std::wstring& dir) {
    const std::string content = "not a settings file\nschema=1\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt, "corrupt file reports Corrupt");
    Expect(loaded.hotkey == L"Alt+Space", "corrupt load uses default hotkey");
    Expect(loaded.recent_count == 20, "corrupt load uses default recent_count");
    Expect(!fs::exists(dir + L"\\settings.ini"), "corrupt file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "corrupt file preserved");
    Expect(ReadBytes(dir + L"\\settings.ini.corrupt") == content, "corrupt content preserved verbatim");
}

// NR-080: a corrupt row in the middle of an otherwise valid settings.ini must
// not leak the valid prefix into the live settings -- the non-Loaded contract
// is DefaultSettings(), never a partial parse (settings_store.h). Without the
// reset, the prefix's hotkey/theme would be adopted while the balloon claims
// "defaults in use".
void TestCorruptMidFileUsesDefaults(const std::wstring& dir) {
    const std::string content =
        "schema=1\nhotkey=Ctrl+1\ntheme=Dark\nthis_line_has_no_equals\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt,
           "mid-file corrupt row reports Corrupt");
    Expect(loaded.hotkey == L"Alt+Space", "corrupt load resets to the default hotkey");
    Expect(loaded.theme == Theme::System, "corrupt load resets to the default theme");
    Expect(loaded.recent_count == 20, "corrupt load resets to the default recent_count");
    Expect(!fs::exists(dir + L"\\settings.ini"), "corrupt file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "corrupt file preserved");
    Expect(ReadBytes(dir + L"\\settings.ini.corrupt") == content,
           "corrupt content preserved verbatim");
}

void TestNewerSchema(const std::wstring& dir) {
    const std::string content = "schema=99\nrecent_count=30\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::NewerSchema, "newer schema reports NewerSchema");
    Expect(loaded.recent_count == 20, "newer schema load uses defaults");
    Expect(loaded.hotkey == L"Alt+Space", "newer schema default hotkey");
    Expect(fs::exists(dir + L"\\settings.ini"), "newer schema file untouched");
    Expect(ReadBytes(dir + L"\\settings.ini") == content, "newer schema content unchanged");
    Expect(!fs::exists(dir + L"\\settings.ini.corrupt"), "newer schema not treated as corrupt");
}

// NR-096: a newer-schema file is another build's data -- Save() must refuse
// and leave the original file byte-for-byte unchanged, even after a runtime
// mutation (the settings dialog Apply path), and stay refused across re-loads.
void TestNewerSchemaSaveRefused(const std::wstring& dir) {
    const std::string content = "schema=99\nrecent_count=30\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::NewerSchema, "newer schema reports NewerSchema");
    loaded.recent_count = 35;
    Expect(store.Save(loaded) == false, "Save() refuses a newer-schema store");
    Expect(ReadBytes(dir + L"\\settings.ini") == content, "original file unchanged");
    Expect(!fs::exists(dir + L"\\settings.ini.tmp"), "no tmp file left behind");
    Expect(store.Load(loaded) == SettingsLoadResult::NewerSchema, "re-load still reports NewerSchema");
    Expect(store.Save(loaded) == false, "Save() still refused after re-load");
}

// NR-096 regression: every non-NewerSchema load outcome stays writable.
void TestMissingLoadSaveWritesFile(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings settings;
    Expect(store.Load(settings) == SettingsLoadResult::Missing, "missing reports Missing");
    settings.recent_count = 22;
    Expect(store.Save(settings), "Save() after a Missing load succeeds");
    Settings reloaded;
    Expect(store.Load(reloaded) == SettingsLoadResult::Loaded, "saved file reloads");
    Expect(reloaded.recent_count == 22, "saved value reloads");
}

void TestLoadedLoadSaveWritesFile(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini", "schema=1\nrecent_count=20\n");
    SettingsStore store(dir);
    Settings settings;
    Expect(store.Load(settings) == SettingsLoadResult::Loaded, "valid file loads");
    settings.recent_count = 30;
    Expect(store.Save(settings), "Save() after a Loaded load succeeds");
    Settings reloaded;
    Expect(store.Load(reloaded) == SettingsLoadResult::Loaded, "saved file reloads");
    Expect(reloaded.recent_count == 30, "saved value reloads");
}

void TestCorruptLoadSaveWritesFile(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini", "garbage not a settings file\n");
    SettingsStore store(dir);
    Settings settings;
    Expect(store.Load(settings) == SettingsLoadResult::Corrupt, "corrupt reports Corrupt");
    Expect(store.Save(settings), "Save() after a Corrupt load succeeds (fresh file)");
    Expect(fs::exists(dir + L"\\settings.ini"), "a fresh settings.ini was created");
    Settings reloaded;
    Expect(store.Load(reloaded) == SettingsLoadResult::Loaded, "fresh file reloads");
}

void TestAtomicWriteFailure(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings expected;
    expected.recent_count = 25;
    Expect(store.Save(expected), "initial save");

    // A directory occupying the .tmp path forces the temp write to fail.
    Expect(fs::create_directory(dir + L"\\settings.ini.tmp"), "create tmp dir obstacle");

    Settings modified = expected;
    modified.recent_count = 30;
    Expect(store.Save(modified) == false, "save fails when temp write fails");

    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "original survives failed save");
    Expect(loaded.recent_count == 25, "original value preserved after failed save");
}

// NR-057: one direct case per VersionedReadStatus, plus the Loaded case
// asserting the schema header is excluded from the returned data lines.
void TestReadVersionedLines(const std::wstring& dir) {
    std::vector<std::wstring> lines;

    Expect(ReadVersionedLines(dir, L"missing.txt", 1, lines) == VersionedReadStatus::Missing,
           "missing file reports Missing");
    Expect(lines.empty(), "missing file leaves lines empty");

    WriteBytes(dir + L"\\bad_utf8.txt", std::string("\xC3\x28", 2));
    Expect(ReadVersionedLines(dir, L"bad_utf8.txt", 1, lines) == VersionedReadStatus::Malformed,
           "undecodable UTF-8 reports Malformed");
    Expect(lines.empty(), "undecodable UTF-8 leaves lines empty");

    WriteBytes(dir + L"\\empty.txt", "");
    Expect(ReadVersionedLines(dir, L"empty.txt", 1, lines) == VersionedReadStatus::Malformed,
           "empty file reports Malformed");

    WriteBytes(dir + L"\\no_schema.txt", "first\nsecond\n");
    Expect(ReadVersionedLines(dir, L"no_schema.txt", 1, lines) == VersionedReadStatus::Malformed,
           "file without a schema= header reports Malformed");

    WriteBytes(dir + L"\\bad_version.txt", "schema=abc\n");
    Expect(ReadVersionedLines(dir, L"bad_version.txt", 1, lines) == VersionedReadStatus::Malformed,
           "non-integer schema version reports Malformed");

    WriteBytes(dir + L"\\older.txt", "schema=0\nline1\n");
    Expect(ReadVersionedLines(dir, L"older.txt", 1, lines) == VersionedReadStatus::OlderSchema,
           "older schema reports OlderSchema");

    WriteBytes(dir + L"\\newer.txt", "schema=2\n");
    Expect(ReadVersionedLines(dir, L"newer.txt", 1, lines) == VersionedReadStatus::NewerSchema,
           "newer schema reports NewerSchema");

    WriteBytes(dir + L"\\ok.txt", "schema=1\nline one\nline two");
    Expect(ReadVersionedLines(dir, L"ok.txt", 1, lines) == VersionedReadStatus::Loaded,
           "valid file reports Loaded");
    Expect(lines.size() == 2 && lines[0] == L"line one" && lines[1] == L"line two",
           "loaded lines exclude the schema header line");
}

} // namespace

int wmain() {
    TestDefaults(MakeTempDir("defaults"));
    TestRoundTrip(MakeTempDir("roundtrip"));
    TestIncludeWindowsAppsOldFormat(MakeTempDir("oldformat"));
    TestEscaping(MakeTempDir("escaping"));
    TestValidation(MakeTempDir("validation"));
    TestCatalogRootsRoundTrip(MakeTempDir("catalogroots"));
    TestCatalogRootsValidation(MakeTempDir("catalogvalidation"));
    TestCorrupt(MakeTempDir("corrupt"));
    TestCorruptMidFileUsesDefaults(MakeTempDir("midcorrupt"));
    TestNewerSchema(MakeTempDir("newer"));
    TestNewerSchemaSaveRefused(MakeTempDir("newersave"));
    TestMissingLoadSaveWritesFile(MakeTempDir("writable_missing"));
    TestLoadedLoadSaveWritesFile(MakeTempDir("writable_loaded"));
    TestCorruptLoadSaveWritesFile(MakeTempDir("writable_corrupt"));
    TestAtomicWriteFailure(MakeTempDir("atomic"));
    TestReadVersionedLines(MakeTempDir("versioned"));
    std::printf("NR-004 settings store check PASSED\n");
    return 0;
}
