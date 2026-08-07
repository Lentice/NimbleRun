#include "catalog/appsfolder_catalog.h"
#include "catalog/stable_id.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::AppsFolderEnumerateResult;
using nimblerun::BuildAppsFolderEntry;
using nimblerun::EnumerateAppsFolderCatalog;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

// The GUID expansion itself, against the live Shell. FOLDERID_ProgramFilesX64
// exists on every supported target (Windows 10 22H2 / 11 x64), so the success
// path is checkable without a fixture; the rest are the reject branches.
void TestKnownFolderExpansion() {
    const std::wstring expanded = nimblerun::ExpandKnownFolderPrefix(
        L"{6D809377-6AF0-444B-8957-A3773F02200E}\\Notepad++\\notepad++.exe");
    Expect(expanded.size() > 3 && expanded[1] == L':',
           "ProgramFilesX64 GUID expands to an absolute path");
    Expect(expanded.rfind(L"\\Notepad++\\notepad++.exe") ==
               expanded.size() - wcslen(L"\\Notepad++\\notepad++.exe"),
           "expansion keeps the remainder of the parsing name");
    Expect(nimblerun::ExpandKnownFolderPrefix(L"").empty(), "empty parsing name expands to empty");
    Expect(nimblerun::ExpandKnownFolderPrefix(
               L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App").empty(),
           "AUMID expands to empty");
    Expect(nimblerun::ExpandKnownFolderPrefix(L"D:\\Tools\\Notepad3.exe").empty(),
           "an already-absolute parsing name expands to empty");
    Expect(nimblerun::ExpandKnownFolderPrefix(L"{6D809377-6AF0-444B-8957-A3773F02200E}").empty(),
           "a bare GUID with no remainder expands to empty");
    Expect(nimblerun::ExpandKnownFolderPrefix(L"{not-a-guid}\\App\\app.exe").empty(),
           "a malformed GUID expands to empty");
    Expect(nimblerun::ExpandKnownFolderPrefix(
               L"{00000000-0000-0000-0000-000000000001}\\App\\app.exe").empty(),
           "an unknown folder id expands to empty");
}

// BuildAppsFolderEntry is the per-item boundary the enumerator routes every
// child through: false means "skip this child, count it, keep going". Driving it
// directly verifies the error-isolation branch without a fake IShellFolder.
void TestEntryBuilder() {
    // NR-028: the enumerator hands BuildAppsFolderEntry the child's bare Shell
    // parsing name (AUMID / GUID-relative path / absolute path); the builder
    // assembles the launch identity from it.
    const std::wstring parsing = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    AppEntry entry;
    Expect(BuildAppsFolderEntry(L"Calculator", parsing, entry),
           "builder accepts valid child names");
    Expect(entry.source == AppSource::AppsFolder, "builder source value");
    Expect(entry.display_name == L"Calculator", "builder display name");
    Expect(entry.launch_identity == L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
           "builder launch identity is the prefixed AppsFolder identity");
    Expect(entry.source_path == parsing, "builder source path is the parsing name");
    Expect(entry.stable_id.size() == 16, "builder stable id is a fixed-length hash");

    // A Known Folder relative parsing name whose GUID the enumerator expanded:
    // the launch identity still uses the bare parsing name (§FR-006), while the
    // row shows the real path and the identity is that path, so this entry and a
    // Start Menu shortcut to the same EXE dedup to one row (§FR-007).
    const std::wstring guid_parsing =
        L"{6D809377-6AF0-444B-8957-A3773F02200E}\\Notepad++\\notepad++.exe";
    const std::wstring resolved = L"C:\\Program Files\\Notepad++\\notepad++.exe";
    AppEntry legacy;
    Expect(BuildAppsFolderEntry(L"Notepad++", guid_parsing, legacy, resolved),
           "builder accepts a resolved legacy child");
    Expect(legacy.launch_identity == L"shell:AppsFolder\\" + guid_parsing,
           "resolved child still launches through the AppsFolder namespace");
    Expect(legacy.source_path == resolved, "resolved child shows the real path");
    Expect(legacy.stable_id == nimblerun::HashStableId(nimblerun::NormalizePathKey(resolved)),
           "resolved child hashes from the resolved path");
    // Expansion failed (unknown folder id): unchanged, pre-resolution behavior.
    AppEntry unresolved;
    Expect(BuildAppsFolderEntry(L"Notepad++", guid_parsing, unresolved, L""),
           "builder accepts an unresolved legacy child");
    Expect(unresolved.source_path == guid_parsing, "unresolved child keeps the parsing name");
    Expect(unresolved.stable_id ==
               nimblerun::HashStableId(nimblerun::NormalizePathKey(guid_parsing)),
           "unresolved child hashes from the parsing name");

    // Unusable child data is a per-item failure, never an entry.
    AppEntry failed;
    failed.display_name = L"sentinel";
    Expect(!BuildAppsFolderEntry(L"Calculator", L"", failed),
           "empty parsing name is skipped");
    Expect(!BuildAppsFolderEntry(L"", parsing, failed),
           "empty display name is skipped");
    Expect(failed.display_name == L"sentinel", "failed item left untouched");

    // The stable id is derived from the parsing name only (design-spec §10.3),
    // so a name-only change must not alter it.
    AppEntry renamed;
    Expect(BuildAppsFolderEntry(L"Different Label", parsing, renamed),
           "builder accepts renamed child");
    Expect(renamed.stable_id == entry.stable_id, "stable id independent of display name");
}

// NR-028: the launch identity gains the AppsFolder namespace prefix, while the
// source path and the stable id (zero migration) keep the bare parsing name.
void TestLaunchIdentityAndFilter() {
    const std::wstring calc_parsing = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    AppEntry calc;
    Expect(BuildAppsFolderEntry(L"Calc", calc_parsing, calc),
           "AUMID child is accepted");
    Expect(calc.launch_identity ==
               L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
           "launch identity is the prefixed AppsFolder identity");
    Expect(calc.source_path == calc_parsing, "source path keeps the bare parsing name");
    // Zero migration (design-spec §10.3): the stable id is hashed from the bare
    // parsing name, so pins and usage survive the identity change. Expected
    // value captured from the pre-NR-028 implementation.
    Expect(calc.stable_id == L"445ac7f22c5f914c",
           "stable id unchanged from the pre-NR-028 implementation");

    // A non-program-like parsing name (document / website) is filtered out and
    // the caller's skip-and-count path applies.
    AppEntry help;
    help.display_name = L"sentinel";
    Expect(!BuildAppsFolderEntry(L"Help", L"{7C5A40EF-96BC-48C3-88F4-0B6D1E1C6B4C}\\AutoIt3\\AutoIt.chm",
                                 help),
           "chm child is filtered out");
    Expect(help.display_name == L"sentinel", "filtered item left untouched");
}

void TestAppsFolderInvariants() {
    // FOLDERID_AppsFolder resolves to real inbox packaged apps on a dev machine;
    // a non-empty result is an acceptable smoke check of the wiring (same as the
    // Start Menu known-folder smoke test). The invariants below must hold on any
    // machine, including one with zero packaged apps.
    const AppsFolderEnumerateResult result = EnumerateAppsFolderCatalog();
    // NR-063: the source-ok contract -- a live enumeration is a source-level
    // success even if the machine has zero packaged apps.
    Expect(result.source_ok, "live AppsFolder enumeration reports source_ok");
    for (const AppEntry& entry : result.entries) {
        Expect(!entry.display_name.empty(), "entry display name");
        Expect(!entry.launch_identity.empty(), "entry launch identity");
        Expect(!entry.source_path.empty(), "entry source path");
        Expect(entry.stable_id.size() == 16, "entry stable id length");
        Expect(entry.source == AppSource::AppsFolder, "entry source value");
        // NR-028: the launch identity is the AppsFolder namespace prefix plus
        // the bare parsing name (design-spec §FR-006), so it is always
        // Shell-launchable. Its concrete shape varies per app (bare AUMID,
        // GUID-relative path, absolute path), so only non-empty (checked above)
        // and reproducibility (checked below) are asserted here.
    }

    // Determinism: re-enumeration keeps the same count and the same identities.
    const AppsFolderEnumerateResult again = EnumerateAppsFolderCatalog();
    Expect(again.entries.size() == result.entries.size(),
           "re-enumeration count stable");
    for (std::size_t i = 0; i < result.entries.size(); ++i) {
        Expect(again.entries[i].stable_id == result.entries[i].stable_id,
               "stable id reproducible across runs");
        Expect(again.entries[i].launch_identity == result.entries[i].launch_identity,
               "launch identity reproducible across runs");
    }

    // Observed counts for the NR-006 acceptance record (enumeration / failures).
    std::fprintf(stderr, "NR-006 AppsFolder enumeration: %zu entries, %zu failed items\n",
                 result.entries.size(), result.failed_items);
}

} // namespace

int wmain() {
    const HRESULT hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAILED: COM init\n");
        return 1;
    }

    TestKnownFolderExpansion();
    TestEntryBuilder();
    TestLaunchIdentityAndFilter();
    TestAppsFolderInvariants();

    CoUninitialize();
    std::printf("NR-006 appsfolder catalog check PASSED\n");
    return 0;
}
