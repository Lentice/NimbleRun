#include "test_util.h"

#include "catalog/stable_id.h"
#include "catalog/user_folder_catalog.h"
#include "settings/settings_store.h"

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
using nimblerun::CatalogRoot;
using nimblerun::DefaultExtensions;
using nimblerun::EnumerateUserFolderCatalog;
using nimblerun::HashStableId;
using nimblerun::NormalizePathKey;
using nimblerun::Settings;

namespace {

std::wstring MakeTempDir(const char* label) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_user_folder_catalog_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::wstring(label, label + std::char_traits<char>::length(label));
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Best-effort fixture cleanup so a transient OS/AV hold never fails the test.
void RemoveTreeBestEffort(const std::wstring& path) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::error_code ec;
        fs::remove_all(fs::path(path), ec);
        if (!ec || !fs::exists(fs::path(path), ec)) {
            return;
        }
        Sleep(150);
    }
}

// NR-092: the mid-walk failure path is an OS-level FindNextFileW error that a
// unit test cannot inject without a fake filesystem layer, so the runnable
// guard is a source-code sanity check pinning the clean-end/failure branch and
// the worker handoff to the source failure route. Locates the repo root from
// the test executable (built under <repo>/build/tests).
std::wstring FindRepoRoot() {
    wchar_t exe[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = length == 0 ? L"" : std::wstring(exe, length);
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (fs::exists(fs::path(dir) / L"src" / L"catalog" / L"user_folder_catalog.cpp")) {
            return dir;
        }
        dir = fs::path(dir).parent_path().wstring();
    }
    return {};
}

std::string ReadSourceFile(const std::wstring& root, const std::wstring& relative) {
    std::ifstream in(fs::path(root) / relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void ExpectContains(const std::string& content, const char* marker, const char* message) {
    Expect(content.find(marker) != std::string::npos, message);
}

const AppEntry* FindByName(const std::vector<AppEntry>& entries, const std::wstring& name) {
    for (const AppEntry& entry : entries) {
        if (entry.display_name == name) {
            return &entry;
        }
    }
    return nullptr;
}

int CountByName(const std::vector<AppEntry>& entries, const std::wstring& name) {
    int count = 0;
    for (const AppEntry& entry : entries) {
        if (entry.display_name == name) {
            ++count;
        }
    }
    return count;
}

void CheckEntryShape(const AppEntry& entry, const std::wstring& full_path,
                     const std::wstring& display_name) {
    Expect(entry.source == AppSource::UserFolder, "user folder source");
    Expect(entry.display_name == display_name, "display name is the file stem");
    Expect(entry.source_path == full_path, "source path is the full path");
    Expect(entry.launch_identity == full_path, "launch identity is the full path");
    Expect(entry.stable_id == HashStableId(NormalizePathKey(full_path)),
           "stable id uses the shared FNV-1a scheme over the normalized path");
    Expect(entry.stable_id.size() == 16, "stable id is a fixed-length hash");
}

void TestMergeAndAllowlist() {
    const std::wstring base = MakeTempDir("merge");
    const std::wstring root_a = base + L"\\A";
    const std::wstring root_b = base + L"\\B";

    WriteBytes(root_a + L"\\AppA.exe", "dummy");
    WriteBytes(root_a + L"\\AppB.cmd", "dummy");
    WriteBytes(root_a + L"\\AppC.bat", "dummy");
    WriteBytes(root_a + L"\\AppD.lnk", "dummy lnk");
    WriteBytes(root_a + L"\\AppE.appref-ms", "dummy appref");
    WriteBytes(root_a + L"\\Tools\\AppF.exe", "dummy");
    WriteBytes(root_a + L"\\Tools\\Sub\\AppG.exe", "dummy");
    WriteBytes(root_a + L"\\Tools\\script.js", "dummy");
    WriteBytes(root_a + L"\\readme.txt", "ignore me");
    WriteBytes(root_b + L"\\AppH.exe", "dummy");
    WriteBytes(root_b + L"\\Sub\\AppI.exe", "dummy");

    Settings settings;
    settings.catalog_roots.push_back({root_a, true});
    settings.catalog_roots.push_back({root_b, false});
    settings.catalog_extensions = DefaultExtensions();

    const nimblerun::UserFolderEnumerateResult result = EnumerateUserFolderCatalog(settings);
    Expect(result.source_ok, "clean multi-root walk reports source_ok");
    const std::vector<AppEntry>& entries = result.entries;

    for (const AppEntry& entry : entries) {
        Expect(entry.source == AppSource::UserFolder, "merged entries source");
    }

    const AppEntry* a = FindByName(entries, L"AppA");
    Expect(a != nullptr, "exe listed");
    CheckEntryShape(*a, root_a + L"\\AppA.exe", L"AppA");
    Expect(FindByName(entries, L"AppB") != nullptr, "cmd listed");
    Expect(FindByName(entries, L"AppC") != nullptr, "bat listed");
    Expect(FindByName(entries, L"AppD") != nullptr, "lnk listed");
    Expect(FindByName(entries, L"AppE") != nullptr, "appref-ms listed");
    const AppEntry* f = FindByName(entries, L"AppF");
    Expect(f != nullptr, "deep item listed under recursive root");
    CheckEntryShape(*f, root_a + L"\\Tools\\AppF.exe", L"AppF");
    const AppEntry* g = FindByName(entries, L"AppG");
    Expect(g != nullptr, "deeper item listed under recursive root");
    CheckEntryShape(*g, root_a + L"\\Tools\\Sub\\AppG.exe", L"AppG");
    Expect(FindByName(entries, L"AppH") != nullptr, "non-recursive first-level item listed");
    Expect(FindByName(entries, L"AppI") == nullptr, "non-recursive subfolder item excluded");
    Expect(FindByName(entries, L"script") == nullptr, "unsupported extension excluded");
    Expect(FindByName(entries, L"readme") == nullptr, "txt excluded");
    Expect(entries.size() == 8, "merged entry count");

    // Determinism: a second run keeps the same count and the same stable ids.
    const nimblerun::UserFolderEnumerateResult again_result = EnumerateUserFolderCatalog(settings);
    Expect(again_result.source_ok, "clean re-walk reports source_ok");
    const std::vector<AppEntry>& again = again_result.entries;
    Expect(again.size() == entries.size(), "re-enumeration count stable");
    const AppEntry* again_a = FindByName(again, L"AppA");
    Expect(again_a != nullptr && again_a->stable_id == a->stable_id,
           "stable id reproducible across runs");

    RemoveTreeBestEffort(base);
}

void TestRecursiveFlag() {
    const std::wstring base = MakeTempDir("recursive");
    const std::wstring root = base + L"\\Root";
    WriteBytes(root + L"\\Top.exe", "dummy");
    WriteBytes(root + L"\\Sub\\Deep.exe", "dummy");
    WriteBytes(root + L"\\Sub\\Inner\\Deeper.exe", "dummy");

    Settings flat;
    flat.catalog_roots.push_back({root, false});
    flat.catalog_extensions = DefaultExtensions();
    const std::vector<AppEntry> flat_entries = EnumerateUserFolderCatalog(flat).entries;
    Expect(FindByName(flat_entries, L"Top") != nullptr, "flat lists first level");
    Expect(FindByName(flat_entries, L"Deep") == nullptr, "flat excludes subfolder");
    Expect(FindByName(flat_entries, L"Deeper") == nullptr, "flat excludes nested subfolder");
    Expect(flat_entries.size() == 1, "flat count");

    Settings deep;
    deep.catalog_roots.push_back({root, true});
    deep.catalog_extensions = DefaultExtensions();
    const std::vector<AppEntry> deep_entries = EnumerateUserFolderCatalog(deep).entries;
    Expect(FindByName(deep_entries, L"Top") != nullptr, "recursive lists first level");
    Expect(FindByName(deep_entries, L"Deep") != nullptr, "recursive lists subfolder");
    Expect(FindByName(deep_entries, L"Deeper") != nullptr, "recursive lists nested subfolder");
    Expect(deep_entries.size() == 3, "recursive count");

    RemoveTreeBestEffort(base);
}

void TestCaseInsensitiveExtensionsAndUnicode() {
    const std::wstring base = MakeTempDir("unicode");
    const std::wstring root = base + L"\\工具";
    WriteBytes(root + L"\\計算機.EXE", "dummy");
    WriteBytes(root + L"\\Build.CMD", "dummy");
    WriteBytes(root + L"\\App.bat", "dummy");
    WriteBytes(root + L"\\深層\\App 工具.exe", "dummy");

    Settings settings;
    settings.catalog_roots.push_back({root, true});
    // Mixed-case allowlist: the enumerator matches case-insensitively.
    settings.catalog_extensions = {L".EXE", L".cmd"};

    const std::vector<AppEntry> entries = EnumerateUserFolderCatalog(settings).entries;

    const AppEntry* calc = FindByName(entries, L"計算機");
    Expect(calc != nullptr, "unicode stem with upper-case extension listed");
    CheckEntryShape(*calc, root + L"\\計算機.EXE", L"計算機");

    const AppEntry* build = FindByName(entries, L"Build");
    Expect(build != nullptr, "upper-case extension matches lowercase allowlist");
    CheckEntryShape(*build, root + L"\\Build.CMD", L"Build");

    const AppEntry* tool = FindByName(entries, L"App 工具");
    Expect(tool != nullptr, "unicode directory and file name preserved");
    CheckEntryShape(*tool, root + L"\\深層\\App 工具.exe", L"App 工具");

    Expect(FindByName(entries, L"App") == nullptr, "unselected extension excluded");
    Expect(entries.size() == 3, "unicode/case count");

    RemoveTreeBestEffort(base);
}

void TestDuplicateRootsAndErrorIsolation() {
    const std::wstring base = MakeTempDir("isolation");
    const std::wstring good = base + L"\\Good";
    WriteBytes(good + L"\\AppA.exe", "dummy");

    // A locked file appears in the walk but fails the readability probe.
    const std::wstring locked_path = good + L"\\Locked.exe";
    const HANDLE locked = CreateFileW(locked_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(locked != INVALID_HANDLE_VALUE, "create locked fixture file");

    // A root path that is a regular file cannot be enumerated as a directory.
    const std::wstring file_as_root = base + L"\\NotADir";
    WriteBytes(file_as_root, "not a directory");

    Settings settings;
    settings.catalog_roots.push_back({good, true});
    settings.catalog_roots.push_back({good, true});                    // duplicate root
    settings.catalog_roots.push_back({base + L"\\missing", true});     // non-existent root
    settings.catalog_roots.push_back({file_as_root, false});           // unreadable root
    settings.catalog_roots.push_back({L"\\\\server\\share", true});    // UNC rejected defensively
    settings.catalog_extensions = DefaultExtensions();

    const nimblerun::UserFolderEnumerateResult result = EnumerateUserFolderCatalog(settings);

    CloseHandle(locked);

    // One bad root never clears the other roots' results.
    Expect(result.source_ok, "missing/unreadable/non-local roots are clean skips (NR-063)");
    const std::vector<AppEntry>& entries = result.entries;
    Expect(!entries.empty(), "other roots survive a failed root");
    Expect(CountByName(entries, L"AppA") == 2, "duplicate root scanned once each");
    Expect(CountByName(entries, L"Locked") == 0, "anomalous unreadable file skipped");
    Expect(entries.size() == 2, "isolation count");
    Expect(entries[0].stable_id == entries[1].stable_id,
           "duplicate entries share the stable id (dedup-ready for NR-007)");

    RemoveTreeBestEffort(base);
}

void TestSourceSanityCheck() {
    const std::wstring root = FindRepoRoot();
    Expect(!root.empty(), "repo root located from the test executable");
    const std::string catalog = ReadSourceFile(root, L"src\\catalog\\user_folder_catalog.cpp");
    const std::string walker = ReadSourceFile(root, L"src\\catalog\\directory_walker.cpp");
    const std::string main_src = ReadSourceFile(root, L"src\\app_host\\main.cpp");
    ExpectContains(walker, "ERROR_NO_MORE_FILES", "clean-end check present in the walk");
    ExpectContains(catalog, "source_ok = false", "mid-walk failure sets source_ok false");
    ExpectContains(main_src, "EnumerateUserFolderCatalog", "worker calls the enumerator");
    ExpectContains(main_src, "result->failed = !res.source_ok",
                   "worker forwards source_ok to failed");
    ExpectContains(main_src, "ApplySourceFailure", "failed results reach the failure route");
}

} // namespace

int wmain() {
    TestMergeAndAllowlist();
    TestRecursiveFlag();
    TestCaseInsensitiveExtensionsAndUnicode();
    TestDuplicateRootsAndErrorIsolation();
    TestSourceSanityCheck();
    std::printf("NR-019 user folder catalog check PASSED\n");
    return 0;
}
