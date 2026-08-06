#include "catalog/start_menu_catalog.h"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shtypes.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::EnumerateProgramsDirectory;
using nimblerun::EnumerateStartMenuCatalog;

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
        std::wstring(buffer) + L"NimbleRun_start_menu_catalog_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::wstring(label, label + std::char_traits<char>::length(label));
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Best-effort fixture cleanup: the Search indexer may briefly hold a freshly
// created .lnk, so a transient failure must not fail the test.
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

bool CreateShortcut(const std::wstring& lnk_path, const std::wstring& target,
                    const std::wstring& arguments) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return false;
    }
    bool ok = SUCCEEDED(link->SetPath(target.c_str()));
    if (ok && !arguments.empty()) {
        ok = SUCCEEDED(link->SetArguments(arguments.c_str()));
    }
    IPersistFile* file = nullptr;
    if (ok && SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        ok = SUCCEEDED(file->Save(lnk_path.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

bool LoadShortcutUrl(const std::wstring& lnk_path, std::wstring& url) {
    url.clear();
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return false;
    }
    IPersistFile* file = nullptr;
    HRESULT hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Load(lnk_path.c_str(), STGM_READ);
        file->Release();
    }
    bool got = false;
    if (SUCCEEDED(hr)) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(link->GetIDList(&pidl)) && pidl != nullptr) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item)))) {
                wchar_t* name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_URL, &name))) {
                    url.assign(name);
                    CoTaskMemFree(name);
                    got = true;
                }
                item->Release();
            }
            CoTaskMemFree(pidl);
        }
    }
    link->Release();
    return got;
}

const AppEntry* FindByName(const std::vector<AppEntry>& entries, const std::wstring& name) {
    for (const AppEntry& entry : entries) {
        if (entry.display_name == name) {
            return &entry;
        }
    }
    return nullptr;
}

void TestFixtureEnumeration() {
    const std::wstring root = MakeTempDir("fixture");

    Expect(fs::create_directories(root + L"\\Tools\\Git"), "create deep fixture dir");
    Expect(fs::create_directories(root + L"\\Unicode 資料夾"), "create unicode fixture dir");

    // Normal shortcut pointing at a real system app.
    Expect(CreateShortcut(root + L"\\Notepad.lnk", L"C:\\Windows\\System32\\notepad.exe", L""),
           "create notepad shortcut");
    // Two links to the same target share a stable id (design-spec §10.3).
    Expect(CreateShortcut(root + L"\\Notepad Copy.lnk", L"C:\\Windows\\System32\\notepad.exe", L""),
           "create notepad copy shortcut");
    // Unicode display name and target path (target file need not exist).
    const std::wstring unicode_target = root + L"\\Unicode 資料夾\\計算機 app.exe";
    Expect(CreateShortcut(root + L"\\計算機.lnk", unicode_target, L""), "create unicode shortcut");
    // NR-047: a localized shortcut name over an English target; "calc" must be
    // reachable through the target stem.
    Expect(CreateShortcut(root + L"\\小算盤.lnk", L"C:\\Windows\\System32\\calc.exe", L""),
           "create localized calculator shortcut");
    // Deeply nested shortcut with spaces and arguments in the target.
    Expect(CreateShortcut(root + L"\\Tools\\Git\\Git Bash.lnk",
                          L"C:\\Program Files\\Git\\bin\\bash.exe", L"-i"),
           "create deep shortcut");
    // Website shortcut must be excluded.
    Expect(CreateShortcut(root + L"\\Homepage.lnk", L"https://example.com", L""),
           "create homepage shortcut");
    // Uninstaller shortcut must be excluded.
    Expect(CreateShortcut(root + L"\\Uninstall Helper.lnk",
                          L"C:\\Program Files\\Some App\\uninstall.exe", L""),
           "create uninstaller shortcut");
    // Corrupt .lnk (garbage bytes) must be skipped, not abort the walk.
    WriteBytes(root + L"\\Broken.lnk", "this is not a shell link file");
    // ClickOnce app reference is kept as-is; the path is the launch identity.
    WriteBytes(root + L"\\ClickOnce.appref-ms", "application reference");
    // A bare .exe physically inside the Programs directory is accepted.
    WriteBytes(root + L"\\Portable.exe", "dummy");
    // Non-app extensions are ignored.
    WriteBytes(root + L"\\readme.txt", "ignore me");

    // The website exclusion is only meaningful if the link really stores a URL.
    {
        std::wstring url;
        const bool stored = LoadShortcutUrl(root + L"\\Homepage.lnk", url);
        Expect(stored && url.compare(0, 8, L"https://") == 0, "homepage link stores a URL");
    }

    std::vector<AppEntry> entries;
    EnumerateProgramsDirectory(root, AppSource::UserStartMenu, entries);

    const AppEntry* notepad = FindByName(entries, L"Notepad");
    Expect(notepad != nullptr, "notepad entry present");
    Expect(notepad->source_path == root + L"\\Notepad.lnk", "notepad source path");
    Expect(notepad->launch_identity == root + L"\\Notepad.lnk", "notepad launch identity");
    Expect(notepad->stable_id.size() == 16, "notepad stable id");
    Expect(notepad->source == AppSource::UserStartMenu, "fixture source");
    Expect(notepad->search_alias == L"notepad", "notepad target stem is the alias");

    const AppEntry* copy = FindByName(entries, L"Notepad Copy");
    Expect(copy != nullptr, "notepad copy present");
    Expect(copy->stable_id == notepad->stable_id, "same target shares stable id");

    const AppEntry* calc = FindByName(entries, L"計算機");
    Expect(calc != nullptr, "unicode entry present");
    Expect(calc->display_name == L"計算機", "unicode display name kept");
    Expect(calc->source_path == root + L"\\計算機.lnk", "unicode source path");
    Expect(calc->launch_identity == root + L"\\計算機.lnk", "unicode launch identity");

    const AppEntry* calc_localized = FindByName(entries, L"小算盤");
    Expect(calc_localized != nullptr, "localized calc shortcut present");
    Expect(calc_localized->display_name == L"小算盤", "localized calc display name kept");
    Expect(calc_localized->search_alias == L"calc", "target stem is the search alias");

    const AppEntry* bash = FindByName(entries, L"Git Bash");
    Expect(bash != nullptr, "deep nested entry present");
    Expect(bash->source_path == root + L"\\Tools\\Git\\Git Bash.lnk", "deep source path");
    Expect(bash->stable_id.size() == 16, "deep stable id");

    const AppEntry* appref = FindByName(entries, L"ClickOnce");
    Expect(appref != nullptr, "appref-ms entry present");
    Expect(appref->launch_identity == root + L"\\ClickOnce.appref-ms", "appref-ms identity");

    const AppEntry* portable = FindByName(entries, L"Portable");
    Expect(portable != nullptr, "bare exe entry present");
    Expect(portable->search_alias.empty(), "bare exe has no search alias");

    Expect(FindByName(entries, L"Broken") == nullptr, "corrupt shortcut skipped");
    Expect(FindByName(entries, L"Homepage") == nullptr, "website shortcut excluded");
    Expect(FindByName(entries, L"Uninstall Helper") == nullptr, "uninstaller shortcut excluded");
    Expect(FindByName(entries, L"readme") == nullptr, "non-app extension ignored");

    Expect(entries.size() == 7, "expected entry count");

    // Determinism: a second run produces the same stable ids.
    {
        std::vector<AppEntry> second;
        EnumerateProgramsDirectory(root, AppSource::UserStartMenu, second);
        Expect(second.size() == entries.size(), "re-enumeration count stable");
        const AppEntry* again = FindByName(second, L"Notepad");
        Expect(again != nullptr && again->stable_id == notepad->stable_id,
               "stable id reproducible across runs");
    }

    RemoveTreeBestEffort(root);
}

void TestMissingDirectory() {
    const std::wstring root = MakeTempDir("missing");
    std::vector<AppEntry> entries;
    EnumerateProgramsDirectory(root + L"\\does-not-exist", AppSource::UserStartMenu, entries);
    Expect(entries.empty(), "missing root yields no entries");
    RemoveTreeBestEffort(root);
}
void TestKnownFoldersSmoke() {
    // SHGetKnownFolderPath(FOLDERID_Programs) on a dev machine resolves to the
    // real user Start Menu; acceptable as a smoke test of the wiring only.
    const std::vector<AppEntry> entries = EnumerateStartMenuCatalog();
    Expect(!entries.empty(), "real Start Menu produced entries");
    for (const AppEntry& entry : entries) {
        Expect(!entry.display_name.empty(), "smoke display name");
        Expect(!entry.source_path.empty(), "smoke source path");
        Expect(!entry.launch_identity.empty(), "smoke launch identity");
        Expect(!entry.stable_id.empty(), "smoke stable id");
        Expect(entry.source == AppSource::UserStartMenu ||
                   entry.source == AppSource::CommonStartMenu,
               "smoke source value");
    }
}

} // namespace

int wmain() {
    const HRESULT hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAILED: COM init\n");
        return 1;
    }

    TestFixtureEnumeration();
    TestMissingDirectory();
    TestKnownFoldersSmoke();

    CoUninitialize();
    std::printf("NR-005 start menu catalog check PASSED\n");
    return 0;
}
