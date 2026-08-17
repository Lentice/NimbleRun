#include "test_util.h"

#include "settings/settings_store.h"

#include "catalog/catalog_cache.h"
#include "diagnostics/diagnostic_log.h"
#include "icons/icon_store.h"
#include "pins/pin_store.h"
#include "storage/atomic_text_file.h"
#include "usage/usage_store.h"

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
using nimblerun::IsAcceptableDriveType;
using nimblerun::IsLocalAbsolutePath;
using nimblerun::ReadVersionedLines;
using nimblerun::Settings;
using nimblerun::SettingsLoadResult;
using nimblerun::SettingsStore;
using nimblerun::Theme;
using nimblerun::UsageLoadResult;
using nimblerun::UsageStore;
using nimblerun::VersionedReadStatus;
using nimblerun::UserDataDirFromLocalAppData;
using nimblerun::kMaxCatalogRoots;
using nimblerun::kMaxHotkeyLength;

namespace {

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
    Expect(loaded.english_input_on_show == false, "default english_input_on_show");
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
    expected.english_input_on_show = true;
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
    Expect(loaded.english_input_on_show == expected.english_input_on_show,
           "round-trip english_input_on_show");
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

// NR-190: a settings.ini written before english_input_on_show existed has no
// such key; it must load disabled (backward-compatible default false), with
// schema unchanged.
void TestEnglishInputOnShowMissingKey(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini",
        "schema=1\n"
        "hotkey=Alt+Space\n"
        "recent_count=20\n");
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "old-format load");
    Expect(loaded.english_input_on_show == false,
           "missing english_input_on_show loads the default false");
}

// NR-190: an unparseable value must fall back to the default false, never make
// the setting unexpectedly enabled.
void TestEnglishInputOnShowInvalidValue(const std::wstring& dir) {
    WriteBytes(dir + L"\\settings.ini",
        "schema=1\n"
        "english_input_on_show=banana\n");
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "invalid-value load");
    Expect(loaded.english_input_on_show == false,
           "invalid english_input_on_show value loads false");
}

// NR-190: explicit true and false both round-trip through Save/Load.
void TestEnglishInputOnShowRoundTrip(const std::wstring& dir) {
    SettingsStore store(dir);
    Settings expected;
    expected.english_input_on_show = true;
    Expect(store.Save(expected), "save english_input_on_show=true");
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "true round-trip load");
    Expect(loaded.english_input_on_show == true, "english_input_on_show=true round-trips");

    expected.english_input_on_show = false;
    Expect(store.Save(expected), "save english_input_on_show=false");
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "false round-trip load");
    Expect(loaded.english_input_on_show == false, "english_input_on_show=false round-trips");
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

// NR-164: mapped network drives fail FR-005 ("local paths only") at the
// drive-type predicate; every other GetDriveTypeW result stays acceptable so
// a disconnected local volume is skipped by the enumerator (NR-092) instead
// of being rejected here.
void TestIsAcceptableDriveType() {
    Expect(IsAcceptableDriveType(DRIVE_REMOTE) == false,
           "DRIVE_REMOTE rejected: mapped network drives are not local (FR-005)");
    Expect(IsAcceptableDriveType(DRIVE_FIXED) == true, "DRIVE_FIXED accepted");
    Expect(IsAcceptableDriveType(DRIVE_REMOVABLE) == true, "DRIVE_REMOVABLE accepted");
    Expect(IsAcceptableDriveType(DRIVE_NO_ROOT_DIR) == true,
           "DRIVE_NO_ROOT_DIR accepted: missing root is skipped, not rejected (NR-092)");
    Expect(IsAcceptableDriveType(DRIVE_UNKNOWN) == true, "DRIVE_UNKNOWN accepted");
}

// NR-172: a bare volume root ("X:\" / "X:/") is rejected as a catalog source
// so the recursive scan can never walk an entire drive (design-spec §19.5).
// Subfolders, including one with a trailing separator, stay acceptable.
void TestIsLocalAbsolutePathRejectsVolumeRoot() {
    Expect(IsLocalAbsolutePath(L"C:\\") == false, "C:\\ rejected: bare volume root");
    Expect(IsLocalAbsolutePath(L"C:/") == false, "C:/ rejected: forward-slash volume root");
    Expect(IsLocalAbsolutePath(L"C:\\Tools") == true, "C:\\Tools accepted: subfolder");
    Expect(IsLocalAbsolutePath(L"C:\\Tools\\") == true,
           "C:\\Tools\\ accepted: trailing separator is not a volume root");
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

// NR-122: a settings.ini larger than kMaxReadBytes is rejected by ReadAllBytes
// before any parse. The body is one valid unknown key (ignored), so without the
// size cap the file would load cleanly -- it is the cap, not the content, that
// quarantines it, and the live settings fall back to the safe defaults.
void TestOversizeFileCorrupt(const std::wstring& dir) {
    std::string content = "schema=1\n";
    content.append(nimblerun::kMaxReadBytes, 'x');
    content += "\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings settings;
    Expect(store.Load(settings) == SettingsLoadResult::Corrupt,
           "oversize settings.ini reports Corrupt");
    Expect(settings.hotkey == L"Alt+Space", "oversize load uses default hotkey");
    Expect(settings.theme == Theme::System, "oversize load uses default theme");
    Expect(!fs::exists(dir + L"\\settings.ini"), "oversize file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "oversize file preserved");
}

// NR-140: settings.ini is untrusted input (design-spec §10.4) and every
// catalog_root becomes a watcher thread at startup, so 33 roots are whole-file
// Corrupt -- and the live settings fall back to DefaultSettings, never a
// partial parse (NR-080 contract).
void TestCatalogRootCap(const std::wstring& dir) {
    std::string content = "schema=1\n";
    for (std::size_t i = 0; i < kMaxCatalogRoots + 1; ++i) {
        content += "catalog_root=C:\\root|true\n";
    }
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt, "33 roots reports Corrupt");
    Expect(loaded.hotkey == L"Alt+Space", "over-cap load resets to the default hotkey");
    Expect(loaded.catalog_roots.empty(), "over-cap load keeps no roots");
    Expect(!fs::exists(dir + L"\\settings.ini"), "over-cap file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "over-cap file preserved");
}

// NR-140: 32 roots is the maximum that loads; every root is preserved.
void TestCatalogRootMaxOk(const std::wstring& dir) {
    // Paths avoid \r/\n/\t/\\ sequences: UnescapeText treats them as escapes.
    std::string content = "schema=1\n";
    for (std::size_t i = 0; i < kMaxCatalogRoots; ++i) {
        content += "catalog_root=C:\\Tools" + std::to_string(i) + "|true\n";
    }
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "32 roots loads");
    Expect(loaded.catalog_roots.size() == kMaxCatalogRoots, "all 32 roots preserved");
    Expect(loaded.catalog_roots[0].path == L"C:\\Tools0", "first root preserved");
    Expect(loaded.catalog_roots[kMaxCatalogRoots - 1].path == L"C:\\Tools31",
           "last root preserved");
}

// NR-140: a hotkey value longer than kMaxHotkeyLength never reaches
// ParseHotkey's per-'+' vector; over-limit is the same whole-file Corrupt.
void TestHotkeyLengthCap(const std::wstring& dir) {
    std::string content = "schema=1\nhotkey=";
    content.append(kMaxHotkeyLength + 1, 'A');
    content += "\n";
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt, "257-char hotkey reports Corrupt");
    Expect(loaded.hotkey == L"Alt+Space", "over-long hotkey resets to the default");
    Expect(!fs::exists(dir + L"\\settings.ini"), "over-long hotkey file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "over-long hotkey file preserved");
}

// NR-140 extra evidence: the original DoS shape -- 100k roots -- is rejected
// by the same cap, so StartWatchers never sees a single root.
void TestCatalogRootCap100k(const std::wstring& dir) {
    std::string content = "schema=1\n";
    content.reserve(3 * 100000);
    for (int i = 0; i < 100000; ++i) {
        content += "catalog_root=C:\\root|true\n";
    }
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt, "100k roots reports Corrupt");
    Expect(loaded.catalog_roots.empty(), "100k-root load keeps no roots");
}

// NR-141: a file with more lines than kMaxLines must fail before SplitLines
// allocates one std::wstring per line -- otherwise a 2.2 MB file would blow up
// into millions of string allocations on the UI thread. The lines are unknown
// keys (x=1), so no NR-140 row cap is involved: it is the shared read-layer
// line cap, not content validation, that quarantines this file. The test
// completing quickly is the proof of no allocation explosion.
void TestLineCountCap(const std::wstring& dir) {
    std::string content = "schema=1\n";
    content.reserve(8 * 1100001);
    for (int i = 0; i < 1100001; ++i) {
        content += "x=1\n";
    }
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Corrupt, ">1M-line file reports Corrupt");
    Expect(loaded.hotkey == L"Alt+Space", "over-line-cap load resets to the default hotkey");
    Expect(!fs::exists(dir + L"\\settings.ini"), "over-line-cap file moved aside");
    Expect(fs::exists(dir + L"\\settings.ini.corrupt"), "over-line-cap file preserved");
}

// NR-141 regression: a large-but-legal file (10k rows, well under kMaxLines)
// loads unchanged -- the line-count guard only fires above the cap.
void TestManyLinesOk(const std::wstring& dir) {
    std::string content = "schema=1\n";
    content.reserve(8 * 10000);
    for (int i = 0; i < 10000; ++i) {
        content += "x=1\n";
    }
    WriteBytes(dir + L"\\settings.ini", content);
    SettingsStore store(dir);
    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "10k-line file loads");
    Expect(loaded.hotkey == L"Alt+Space", "10k-line load keeps the default hotkey");
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

// NR-166 regression: the read size cap must never be classified as Missing.
// Before the error_out fix, ReadAllBytes returned false for an over-cap file
// without setting any last error, so a stale ERROR_FILE_NOT_FOUND left on the
// thread made the store treat it as a first run -- skipping PreserveCorrupt
// and letting the next Save() overwrite the file. The thread last-error is
// deliberately poisoned here; the captured ERROR_FILE_TOO_LARGE must win.
void TestReadVersionedLinesOversizeNotMissing(const std::wstring& dir) {
    std::string content = "schema=1\n";
    content.append(nimblerun::kMaxReadBytes + 4096, 'x');
    WriteBytes(dir + L"\\oversize.txt", content);

    SetLastError(ERROR_FILE_NOT_FOUND);
    std::vector<std::wstring> lines;
    Expect(ReadVersionedLines(dir, L"oversize.txt", 1, lines) == VersionedReadStatus::Unreadable,
           "oversize file with stale ERROR_FILE_NOT_FOUND reports Unreadable, not Missing");
    Expect(lines.empty(), "oversize file leaves lines empty");
}

void TestUnavailableUserDataRootIsFailClosed() {
    Expect(UserDataDirFromLocalAppData(L"").empty(), "empty LocalAppData is rejected");
    Expect(UserDataDirFromLocalAppData(L"relative\\path").empty(),
           "relative LocalAppData is rejected");
    Expect(UserDataDirFromLocalAppData(L"\\\\server\\share").empty(),
           "UNC LocalAppData is rejected");
    Expect(UserDataDirFromLocalAppData(L"C:\\bad|root").empty(),
           "malformed LocalAppData is rejected");

    std::wstring overlong = L"C:\\";
    overlong.append(MAX_PATH, L'a');
    Expect(UserDataDirFromLocalAppData(overlong).empty(),
           "overlong LocalAppData is rejected");

    const std::wstring valid = UserDataDirFromLocalAppData(L"C:\\Users\\tester\\AppData\\Local");
    Expect(valid == L"C:\\Users\\tester\\AppData\\Local\\NimbleRun",
           "valid LocalAppData resolves to NimbleRun");

    const std::wstring outside = MakeTempDir("empty_root");
    wchar_t previous[MAX_PATH];
    Expect(GetCurrentDirectoryW(MAX_PATH, previous) != 0, "read current directory");
    Expect(SetCurrentDirectoryW(outside.c_str()) != FALSE, "set controlled current directory");

    SettingsStore settings_store(L"");
    Settings settings;
    Expect(settings_store.Load(settings) == SettingsLoadResult::Missing,
           "empty root disables settings load");
    Expect(!settings_store.Save(settings), "empty root disables settings save");

    UsageStore usage_store(L"");
    Expect(usage_store.Load() == UsageLoadResult::Missing,
           "empty root disables usage load");
    usage_store.RecordLaunch(L"app", 1);
    Expect(!usage_store.Save(), "empty root disables usage save");

    nimblerun::PinStore pin_store(L"");
    Expect(pin_store.Load() == nimblerun::PinLoadResult::Missing,
           "empty root disables pin load");
    pin_store.Pin(L"app", L"App", 1);
    Expect(!pin_store.Save(), "empty root disables pin save");

    nimblerun::SaveCatalogCache(L"", {});
    nimblerun::DiagnosticLog log(L"", L"nimblerun.log");
    log.Write(L"test", L"empty-root");
    nimblerun::IconStore icon_store({});
    Expect(icon_store.Open() == nimblerun::IconStore::StoreState::Disabled,
           "empty root disables icon cache");

    Expect(SetCurrentDirectoryW(previous) != FALSE, "restore current directory");
    Expect(!fs::exists(fs::path(outside) / L"settings.ini"),
           "empty root creates no settings file outside root");
    Expect(!fs::exists(fs::path(outside) / L"usage.tsv"),
           "empty root creates no usage file outside root");
    Expect(!fs::exists(fs::path(outside) / L"favorites.txt"),
           "empty root creates no pin file outside root");
    Expect(!fs::exists(fs::path(outside) / L"catalog.cache"),
           "empty root creates no catalog cache outside root");
    Expect(!fs::exists(fs::path(outside) / L"icons.cache"),
           "empty root creates no icon cache outside root");
    Expect(!fs::exists(fs::path(outside) / L"logs"),
           "empty root creates no log directory outside root");

    const std::wstring marker = L"nr107-empty-root-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    Expect(!nimblerun::AtomicWriteUtf8Text(L"", marker, L"must not be written"),
           "empty root disables atomic writes");
    std::vector<std::wstring> lines;
    Expect(ReadVersionedLines(L"", marker, 1, lines) == VersionedReadStatus::Missing,
           "empty root disables reads");
}

} // namespace

int wmain() {
    TestDefaults(MakeTempDir("defaults"));
    TestRoundTrip(MakeTempDir("roundtrip"));
    TestIncludeWindowsAppsOldFormat(MakeTempDir("oldformat"));
    TestEnglishInputOnShowMissingKey(MakeTempDir("english_missing"));
    TestEnglishInputOnShowInvalidValue(MakeTempDir("english_invalid"));
    TestEnglishInputOnShowRoundTrip(MakeTempDir("english_roundtrip"));
    TestEscaping(MakeTempDir("escaping"));
    TestValidation(MakeTempDir("validation"));
    TestCatalogRootsRoundTrip(MakeTempDir("catalogroots"));
    TestIsAcceptableDriveType();
    TestIsLocalAbsolutePathRejectsVolumeRoot();
    TestCatalogRootsValidation(MakeTempDir("catalogvalidation"));
    TestCorrupt(MakeTempDir("corrupt"));
    TestCorruptMidFileUsesDefaults(MakeTempDir("midcorrupt"));
    TestOversizeFileCorrupt(MakeTempDir("oversize"));
    TestCatalogRootCap(MakeTempDir("rootcap"));
    TestCatalogRootMaxOk(MakeTempDir("rootmax"));
    TestHotkeyLengthCap(MakeTempDir("hotkeycap"));
    TestCatalogRootCap100k(MakeTempDir("rootcap100k"));
    TestLineCountCap(MakeTempDir("linecap"));
    TestManyLinesOk(MakeTempDir("manylines"));
    TestNewerSchema(MakeTempDir("newer"));
    TestNewerSchemaSaveRefused(MakeTempDir("newersave"));
    TestMissingLoadSaveWritesFile(MakeTempDir("writable_missing"));
    TestLoadedLoadSaveWritesFile(MakeTempDir("writable_loaded"));
    TestCorruptLoadSaveWritesFile(MakeTempDir("writable_corrupt"));
    TestAtomicWriteFailure(MakeTempDir("atomic"));
    TestReadVersionedLines(MakeTempDir("versioned"));
    TestReadVersionedLinesOversizeNotMissing(MakeTempDir("oversize_nr166"));
    TestUnavailableUserDataRootIsFailClosed();
    std::printf("NR-004 settings store check PASSED\n");
    return 0;
}
