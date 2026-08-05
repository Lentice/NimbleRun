#include "catalog/dedup.h"
#include "catalog/stable_id.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::DedupResult;
using nimblerun::DeduplicateCatalog;
using nimblerun::HashStableId;
using nimblerun::NormalizePathKey;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

// Builds a pure-value fixture the way the catalog sources produce one:
// stable_id is the normalized identity hashed by the source of the same kind.
AppEntry Entry(std::wstring stable_id, std::wstring display_name, AppSource source,
               std::wstring launch_identity) {
    AppEntry entry;
    entry.stable_id = std::move(stable_id);
    entry.display_name = std::move(display_name);
    entry.normalized_name = entry.display_name;
    entry.source = source;
    entry.launch_identity = std::move(launch_identity);
    entry.source_path = entry.launch_identity;
    return entry;
}

void ExpectEntriesEqual(const DedupResult& first, const DedupResult& second) {
    Expect(first.entries.size() == second.entries.size(), "repeat: entry count");
    for (std::size_t i = 0; i < first.entries.size(); ++i) {
        Expect(first.entries[i].stable_id == second.entries[i].stable_id, "repeat: stable id");
        Expect(first.entries[i].display_name == second.entries[i].display_name, "repeat: name");
        Expect(first.entries[i].source == second.entries[i].source, "repeat: source");
        Expect(first.entries[i].launch_identity == second.entries[i].launch_identity,
               "repeat: launch identity");
    }
    Expect(first.removed_duplicates == second.removed_duplicates, "repeat: removed count");
    Expect(first.ambiguous_kept == second.ambiguous_kept, "repeat: ambiguous count");
}

// Repeatability of the stable id itself: identical inputs (the same physical
// path written with different case or separators by different sources) hash to
// one identity; Shell canonical identities stay exact.
void TestStableIdNormalization() {
    const std::wstring upper = L"C:\\Program Files\\Editor\\EDITOR.EXE";
    const std::wstring lower = L"c:/program files/editor/editor.exe";
    Expect(HashStableId(NormalizePathKey(upper)) == HashStableId(NormalizePathKey(lower)),
           "path normalization unifies case and separators");
    Expect(HashStableId(NormalizePathKey(upper)) == HashStableId(NormalizePathKey(upper)),
           "same input yields the same stable id");
    const std::wstring parsing = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    Expect(NormalizePathKey(parsing) == parsing, "shell parsing name stays exact");
}

// Same physical app from a UserFolder path and a Start Menu shortcut whose
// resolved target is that path: one entry survives, and precedence picks the
// user Start Menu over the user folder, and a user folder over the Common
// Start Menu (design-spec §FR-007).
void TestCrossSourceMergeAndPrecedence() {
    const std::wstring target = L"C:\\Windows\\System32\\notepad.exe";
    const std::wstring user_lnk =
        L"C:\\Users\\me\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\"
        L"Accessories\\Notepad.lnk";
    const std::wstring common_lnk =
        L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Accessories\\Notepad.lnk";
    const std::wstring id = HashStableId(NormalizePathKey(target));

    {
        std::vector<AppEntry> input = {
            Entry(id, L"Notepad", AppSource::UserFolder, target),
            Entry(id, L"Notepad", AppSource::UserStartMenu, user_lnk),
        };
        const DedupResult out = DeduplicateCatalog(input);
        Expect(out.entries.size() == 1, "cross-source merge: one entry");
        Expect(out.entries[0].source == AppSource::UserStartMenu, "user start menu precedence");
        Expect(out.entries[0].launch_identity == user_lnk, "kept entry keeps its launch identity");
        Expect(out.removed_duplicates == 1, "cross-source merge: removed count");
        Expect(out.ambiguous_kept == 0, "cross-source merge: judgeable, not ambiguous");
    }
    {
        std::vector<AppEntry> input = {
            Entry(id, L"Notepad", AppSource::CommonStartMenu, common_lnk),
            Entry(id, L"Notepad", AppSource::UserFolder, target),
        };
        const DedupResult out = DeduplicateCatalog(input);
        Expect(out.entries.size() == 1, "user folder beats common start menu: one entry");
        Expect(out.entries[0].source == AppSource::UserFolder, "user folder precedence");
    }
    {
        std::vector<AppEntry> input = {
            Entry(id, L"Notepad", AppSource::UserStartMenu, user_lnk),
            Entry(id, L"Notepad", AppSource::CommonStartMenu, common_lnk),
        };
        const DedupResult out = DeduplicateCatalog(input);
        Expect(out.entries.size() == 1, "user start menu beats common: one entry");
        Expect(out.entries[0].source == AppSource::UserStartMenu, "user precedence over common");
    }
}

// Two different apps that happen to share a display name are never merged.
void TestSameNameNeverMerged() {
    const std::wstring target_a = L"C:\\Games\\Launcher\\launcher.exe";
    const std::wstring target_b = L"C:\\Program Files\\Studio\\launcher.exe";
    std::vector<AppEntry> input = {
        Entry(HashStableId(NormalizePathKey(target_a)), L"Launcher",
              AppSource::UserStartMenu, L"C:\\Users\\me\\Start Menu\\Launcher.lnk"),
        Entry(HashStableId(NormalizePathKey(target_b)), L"Launcher",
              AppSource::UserStartMenu, L"C:\\Users\\me\\Start Menu\\Launcher.lnk"),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 2, "same-name different apps both kept");
    Expect(out.removed_duplicates == 0, "same-name pair removes nothing");
    Expect(out.ambiguous_kept == 0, "same-name path pair is judgeable, not ambiguous");
}

// Duplicate user-folder roots scan the same file twice; the two identical
// entries collapse to one (dedup within the source too).
void TestUserFolderDuplicatesCollapse() {
    const std::wstring path = L"C:\\Tools\\AppA.exe";
    const std::wstring id = HashStableId(NormalizePathKey(path));
    std::vector<AppEntry> input = {
        Entry(id, L"AppA", AppSource::UserFolder, path),
        Entry(id, L"AppA", AppSource::UserFolder, path),
        Entry(id, L"AppA", AppSource::UserFolder, path),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 1, "user folder duplicates collapse");
    Expect(out.entries[0].source == AppSource::UserFolder, "duplicate keeps the user folder item");
    Expect(out.removed_duplicates == 2, "user folder duplicates removed count");
    Expect(out.ambiguous_kept == 0, "identical duplicates are not ambiguous");
}

// Two AppsFolder items with the same parsing name are the same app and merge.
void TestAppsFolderDuplicateMerges() {
    const std::wstring parsing = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    const std::wstring id = HashStableId(parsing);
    std::vector<AppEntry> input = {
        Entry(id, L"Calculator", AppSource::AppsFolder, parsing),
        Entry(id, L"Calculator", AppSource::AppsFolder, parsing),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 1, "identical parsing name merges");
    Expect(out.removed_duplicates == 1, "apps folder duplicate removed");
    Expect(out.ambiguous_kept == 0, "merged, not ambiguous");
}

// Two packaged apps sharing a display name but with distinct parsing names are
// judgeable as different apps: kept, and not counted ambiguous.
void TestAppsFolderSameNameDistinct() {
    const std::wstring p1 = L"shell:AppsFolder\\Microsoft.XYZ_8wekyb3d8bbwe!App";
    const std::wstring p2 = L"shell:AppsFolder\\OtherCorp.XYZ_8wekyb3d8bbwe!App";
    std::vector<AppEntry> input = {
        Entry(HashStableId(p1), L"XYZ", AppSource::AppsFolder, p1),
        Entry(HashStableId(p2), L"XYZ", AppSource::AppsFolder, p2),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 2, "distinct packaged apps both kept");
    Expect(out.ambiguous_kept == 0, "two shell identities are judgeable distinct");
}

// A packaged app appearing as a Start Menu shortcut (path identity) and as an
// AppsFolder item (parsing name) cannot be reliably judged the same: both are
// kept, not merged by display name, and counted as a diagnostic (design-spec
// §FR-007 item 3).
void TestPackagedAppAmbiguityKept() {
    const std::wstring lnk =
        L"C:\\Users\\me\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\"
        L"Calculator.lnk";
    const std::wstring parsing = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    std::vector<AppEntry> input = {
        Entry(HashStableId(NormalizePathKey(lnk)), L"Calculator", AppSource::UserStartMenu, lnk),
        Entry(HashStableId(parsing), L"Calculator", AppSource::AppsFolder, parsing),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 2, "unjudgeable packaged app keeps both entries");
    Expect(out.removed_duplicates == 0, "unjudgeable pair removes nothing");
    Expect(out.ambiguous_kept == 2, "unjudgeable pair counted as diagnostic");
}

// Output ordering is stable and reproducible across runs, and kept entries
// keep their input order.
void TestOrderingReproducible() {
    const std::wstring t1 = L"C:\\A\\one.exe";
    const std::wstring t2 = L"C:\\B\\two.exe";
    std::vector<AppEntry> input = {
        Entry(HashStableId(NormalizePathKey(t1)), L"One", AppSource::UserFolder, t1),
        Entry(HashStableId(NormalizePathKey(t2)), L"Two", AppSource::UserFolder, t2),
        Entry(HashStableId(NormalizePathKey(t1)), L"One", AppSource::UserStartMenu,
              L"C:\\A\\One.lnk"),
    };
    const DedupResult first = DeduplicateCatalog(input);
    const DedupResult second = DeduplicateCatalog(input);
    Expect(first.entries.size() == 2, "ordering: dedup count");
    Expect(first.entries[0].display_name == L"One", "ordering: first kept entry input order");
    Expect(first.entries[1].display_name == L"Two", "ordering: second kept entry input order");
    Expect(first.entries[0].source == AppSource::UserStartMenu,
           "ordering: precedence applied within first group");
    ExpectEntriesEqual(first, second);
}

void TestEmptyInput() {
    const DedupResult out = DeduplicateCatalog({});
    Expect(out.entries.empty(), "empty input yields empty result");
    Expect(out.removed_duplicates == 0, "empty input removed count");
    Expect(out.ambiguous_kept == 0, "empty input ambiguous count");
}

} // namespace

int wmain() {
    TestStableIdNormalization();
    TestCrossSourceMergeAndPrecedence();
    TestSameNameNeverMerged();
    TestUserFolderDuplicatesCollapse();
    TestAppsFolderDuplicateMerges();
    TestAppsFolderSameNameDistinct();
    TestPackagedAppAmbiguityKept();
    TestOrderingReproducible();
    TestEmptyInput();
    std::printf("NR-007 identity and dedup check PASSED\n");
    return 0;
}
