#include "test_util.h"

#include "catalog/app_filter.h"
#include "catalog/dedup.h"
#include "catalog/stable_id.h"

#include <algorithm>
#include <chrono>
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
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;

namespace {

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
// resolved target is that path: one entry survives. The target itself beats a
// shortcut to it regardless of source order; source precedence (design-spec
// §FR-007) decides only between two shortcuts or two bodies.
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
        Expect(out.entries[0].source == AppSource::UserFolder, "the EXE body beats the shortcut");
        Expect(out.entries[0].launch_identity == target, "kept entry keeps its launch identity");
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

// The reported case: an AppsFolder item whose Known Folder relative parsing
// name resolved to an EXE, plus the all-users Start Menu shortcut pointing at
// the same EXE. One row survives, and it is the one showing the EXE path.
// A second shortcut to the same EXE but carrying launch arguments is a
// different launch and keeps its own row.
void TestAppsFolderAndShortcutCollapse() {
    const std::wstring target = L"C:\\Program Files\\Notepad++\\notepad++.exe";
    const std::wstring lnk =
        L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Notepad++.lnk";
    const std::wstring id = HashStableId(NormalizePathKey(target));
    std::vector<AppEntry> input = {
        Entry(id, L"Notepad++", AppSource::CommonStartMenu, lnk),
        Entry(id, L"Notepad++", AppSource::AppsFolder, target),
        Entry(HashStableId(NormalizePathKey(target) + L"\n-multiInst"), L"Notepad++ (new)",
              AppSource::CommonStartMenu, lnk),
    };
    const DedupResult out = DeduplicateCatalog(input);
    Expect(out.entries.size() == 2, "AppsFolder + plain shortcut collapse, args entry survives");
    Expect(out.entries[0].source == AppSource::AppsFolder, "the resolved EXE wins the slot");
    Expect(out.entries[0].source_path == target, "kept row shows the EXE path");
    Expect(out.removed_duplicates == 1, "one duplicate removed");
    Expect(out.ambiguous_kept == 0, "a resolved AppsFolder path is judgeable");
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
    Expect(first.entries[0].source == AppSource::UserFolder,
           "ordering: winner picked within first group");
    ExpectEntriesEqual(first, second);
}

// NR-116: a fresh current-source entry (verified) must never lose dedup to a
// retained cache row (unverified) with the same stable_id, even when the cache
// row carries better source precedence or is an EXE body while the fresh row is
// a shortcut. Source priority is unchanged when both entries are verified.
void TestVerifiedBeatsUnverifiedInDedup() {
    const std::wstring target = L"C:\\Program Files\\AppX\\app.exe";
    const std::wstring lnk =
        L"C:\\Users\\me\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\AppX.lnk";
    const std::wstring id = HashStableId(NormalizePathKey(target));

    {
        // Cache row: AppsFolder EXE body, unverified. Fresh row: user start
        // menu shortcut, verified. Without the NR-116 rule the cache row wins
        // on both the shortcut test and source precedence.
        AppEntry cache = Entry(id, L"AppX", AppSource::AppsFolder, target);
        cache.launch_verified = false;
        std::vector<AppEntry> input = {
            cache,
            Entry(id, L"AppX", AppSource::UserStartMenu, lnk),  // verified (default)
        };
        const DedupResult out = DeduplicateCatalog(input);
        Expect(out.entries.size() == 1, "NR-116: verified row keeps the slot");
        Expect(out.entries[0].launch_verified, "NR-116: the verified entry is kept");
        Expect(out.entries[0].source == AppSource::UserStartMenu,
               "NR-116: the fresh shortcut, not the cached body, is kept");
    }
    {
        // Both verified: source precedence decides as before (AppsFolder EXE
        // body beats the user start menu shortcut to it).
        std::vector<AppEntry> input = {
            Entry(id, L"AppX", AppSource::UserStartMenu, lnk),
            Entry(id, L"AppX", AppSource::AppsFolder, target),
        };
        const DedupResult out = DeduplicateCatalog(input);
        Expect(out.entries.size() == 1, "NR-116: verified-only dedup still merges");
        Expect(out.entries[0].source == AppSource::AppsFolder,
               "NR-116: both verified, AppsFolder precedence unchanged");
    }
}

void TestEmptyInput() {
    const DedupResult out = DeduplicateCatalog({});
    Expect(out.entries.empty(), "empty input yields empty result");
    Expect(out.removed_duplicates == 0, "empty input removed count");
    Expect(out.ambiguous_kept == 0, "empty input ambiguous count");
}

// NR-121: the former all-pairs name-collision scan, kept here as the reference
// for the bucketed scan. Replicates the pre-NR-121 DeduplicateCatalog tail:
// every pair, lowercased-name equality + distinct stable id + one Shell side.
std::vector<bool> ReferenceAmbiguousScan(const std::vector<AppEntry>& entries) {
    std::vector<bool> ambiguous(entries.size(), false);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            const AppEntry& a = entries[i];
            const AppEntry& b = entries[j];
            if (a.stable_id != b.stable_id &&
                nimblerun::ToLower(a.display_name) == nimblerun::ToLower(b.display_name) &&
                (a.source == AppSource::AppsFolder) != (b.source == AppSource::AppsFolder)) {
                ambiguous[i] = true;
                ambiguous[j] = true;
            }
        }
    }
    return ambiguous;
}

// NR-121: the bucketed scan must mark exactly the entries the all-pairs scan
// marked. Recompute the reference over the produced entries and compare counts
// on a mixed fixture that exercises every branch of the predicate: an
// unjudgeable pair (same name, one Shell side), same-name judgeable pairs
// (merged-by-id and both-path), unrelated names, and a verified/unverified
// provenance mix (NR-116).
void TestBucketedAmbiguityMatchesAllPairs() {
    const std::wstring parsing = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    const std::wstring lnk = L"C:\\Users\\me\\Start Menu\\Calculator.lnk";
    const std::wstring target = L"C:\\Program Files\\Note\\notepad.exe";
    const std::wstring id = HashStableId(NormalizePathKey(target));

    AppEntry unverified = Entry(HashStableId(NormalizePathKey(lnk)), L"Calculator",
                                AppSource::UserStartMenu, lnk);
    unverified.launch_verified = false;  // a cache row (NR-113)

    std::vector<AppEntry> input = {
        // Same stable id -> merged, not ambiguous.
        Entry(id, L"Notepad", AppSource::UserStartMenu, L"C:\\Users\\me\\Start Menu\\Notepad.lnk"),
        Entry(id, L"Notepad", AppSource::UserFolder, target),
        // Same name, one Shell side -> the ambiguous pair.
        Entry(HashStableId(parsing), L"Calculator", AppSource::AppsFolder, parsing),
        unverified,
        // Same name, both path -> judgeable distinct, not ambiguous.
        Entry(HashStableId(NormalizePathKey(L"C:\\A\\a.exe")), L"Editor",
              AppSource::UserFolder, L"C:\\A\\a.exe"),
        Entry(HashStableId(NormalizePathKey(L"C:\\A\\b.exe")), L"Editor",
              AppSource::UserFolder, L"C:\\A\\b.exe"),
    };

    const DedupResult out = DeduplicateCatalog(input);
    const std::vector<bool> reference = ReferenceAmbiguousScan(out.entries);
    const std::size_t reference_ambiguous =
        static_cast<std::size_t>(std::count(reference.begin(), reference.end(), true));
    Expect(reference_ambiguous == out.ambiguous_kept,
           "bucketed scan marks exactly the all-pairs scan's entries");
    Expect(out.ambiguous_kept == 2, "only the unjudgeable Calculator pair is ambiguous");
}

// NR-121: worst-path latency of dedup over 5000 distinct-name kept entries.
// Distinct names collapse every bucket to size 1, so this measures the scan's
// cost when the O(n^2) all-pairs version would pay all 12.5M pairs. Threshold
// (50 ms) is ~20x the measured ~2 ms (see the item's handoff) so it only flags
// a regression that re-introduces the all-pairs scan.
void TestDedup5000Timing() {
    std::vector<AppEntry> big;
    big.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        big.push_back(Entry(L"id" + std::to_wstring(i), L"App " + std::to_wstring(i) + L" edition",
                            AppSource::UserFolder, L"C:\\A\\app" + std::to_wstring(i) + L".exe"));
    }

    const auto start = steady_clock::now();
    const DedupResult out = DeduplicateCatalog(big);
    const auto elapsed_us =
        duration_cast<microseconds>(steady_clock::now() - start).count();

    std::wprintf(L"NR-121: DeduplicateCatalog over 5000 distinct-name entries took %lld us (%lld ms), kept %zu\n",
                 elapsed_us, elapsed_us / 1000, out.entries.size());
    Expect(out.entries.size() == 5000, "5000 distinct entries all kept");
    Expect(out.ambiguous_kept == 0, "distinct names mark nothing ambiguous");
    Expect(elapsed_us / 1000 < 50, "5000-entry dedup stays under 50 ms");
}

// NR-158: a crafted catalog holding 6000 same-name apps (distinct stable ids)
// must not stall the UI thread with the unbound in-bucket O(n^2) scan (18M
// pairs at 6000 rows, ~200M at the kMaxCacheRows cap). The bucket cap marks
// rows past kMaxSameNameRows ambiguous directly, bounding comparisons at
// 5000^2/2. Legal data (<5000 same-name rows) is untouched: the first 5000
// rows here carry exactly the marks the pre-fix scan gave them (all-path
// rows: none), and the 1000 overflow rows are the guard's direct marks.
void TestSameNameBucketBound() {
    std::vector<AppEntry> big;
    big.reserve(6000);
    for (int i = 0; i < 6000; ++i) {
        big.push_back(Entry(L"id" + std::to_wstring(i), L"App", AppSource::UserFolder,
                            L"C:\\A\\app" + std::to_wstring(i) + L".exe"));
    }

    const auto start = steady_clock::now();
    const DedupResult out = DeduplicateCatalog(big);
    const auto elapsed_us =
        duration_cast<microseconds>(steady_clock::now() - start).count();

    std::wprintf(L"NR-158: DeduplicateCatalog over 6000 same-name entries took %lld us (%lld ms), kept %zu\n",
                 elapsed_us, elapsed_us / 1000, out.entries.size());
    Expect(out.entries.size() == 6000, "same-name bound: all distinct apps kept");
    Expect(out.removed_duplicates == 0, "same-name bound: nothing removed");
    Expect(out.ambiguous_kept == 1000, "same-name bound: overflow rows marked ambiguous");
    Expect(elapsed_us / 1000 < 1000, "6000 same-name dedup stays under 1 s");

    {
        // One Shell-side row among the 6000: pre-fix that row's pairs mark
        // every same-name entry ambiguous, and the bounded scan must land on
        // the same count (its pairs mark all of the first 5000; the overflow
        // rows are pre-marked).
        std::vector<AppEntry> mixed = big;
        mixed[0] = Entry(L"shell", L"App", AppSource::AppsFolder,
                         L"shell:AppsFolder\\Microsoft.App_8wekyb3d8bbwe!App");
        const DedupResult out = DeduplicateCatalog(mixed);
        Expect(out.entries.size() == 6000, "same-name bound: mixed input keeps all");
        Expect(out.ambiguous_kept == 6000, "same-name bound: Shell row marks everything");
    }
}

} // namespace

int wmain() {
    TestStableIdNormalization();
    TestCrossSourceMergeAndPrecedence();
    TestSameNameNeverMerged();
    TestUserFolderDuplicatesCollapse();
    TestAppsFolderAndShortcutCollapse();
    TestAppsFolderDuplicateMerges();
    TestAppsFolderSameNameDistinct();
    TestPackagedAppAmbiguityKept();
    TestOrderingReproducible();
    TestVerifiedBeatsUnverifiedInDedup();
    TestEmptyInput();
    TestBucketedAmbiguityMatchesAllPairs();
    TestDedup5000Timing();
    TestSameNameBucketBound();
    std::printf("NR-007 identity and dedup check PASSED\n");
    return 0;
}
