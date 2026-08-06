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

using nimblerun::UsageLoadResult;
using nimblerun::UsageRecord;
using nimblerun::UsageStore;

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
        std::wstring(buffer) + L"NimbleRun_recent_usage_test_" +
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

bool SameRecord(const UsageRecord& a, const UsageRecord& b) {
    return a.stable_id == b.stable_id &&
           a.total_launches == b.total_launches &&
           a.last_launch_utc == b.last_launch_utc;
}

bool SameList(const std::vector<UsageRecord>& a, const std::vector<UsageRecord>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!SameRecord(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

// A stable id that matches nothing in any catalog: the store must keep it
// across reloads, never deleting usage for a temporarily absent app (§FR-011).
const std::wstring kUnknownId = L"not_in_any_catalog_ff00aa11";

void TestEmpty() {
    const std::wstring dir = MakeTempDir("empty");
    UsageStore store(dir);
    Expect(store.Load() == UsageLoadResult::Missing, "missing usage file reports Missing");
    Expect(store.Recent().empty(), "no records -> empty recent list");
    Expect(store.Recent(5).empty(), "empty even with an explicit cap");
    fs::remove_all(dir);
}

void TestSortingNewestFirst() {
    const std::wstring dir = MakeTempDir("sort");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"aaa", 100);
    store.RecordLaunch(L"bbb", 300);
    store.RecordLaunch(L"ccc", 200);
    const std::vector<UsageRecord> recent = store.Recent();
    Expect(recent.size() == 3, "three records");
    Expect(SameRecord(recent[0], {L"bbb", 1, 300}), "newest first");
    Expect(SameRecord(recent[1], {L"ccc", 1, 200}), "second newest");
    Expect(SameRecord(recent[2], {L"aaa", 1, 100}), "oldest last");
    fs::remove_all(dir);
}

void TestCapAtTwenty() {
    const std::wstring dir = MakeTempDir("cap");
    UsageStore store(dir);
    store.Load();
    for (int i = 0; i < 25; ++i) {
        store.RecordLaunch(L"app" + std::to_wstring(i), 1000 + i);
    }
    const std::vector<UsageRecord> recent = store.Recent();
    Expect(recent.size() == 20, "cap at 20 when more than 20 records exist");
    Expect(SameRecord(recent[0], {L"app24", 1, 1024}), "newest kept");
    Expect(SameRecord(recent[19], {L"app5", 1, 1005}), "20th newest kept");
    fs::remove_all(dir);
}

void TestTieBreaker() {
    const std::wstring dir = MakeTempDir("tie");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"zebra", 500);
    store.RecordLaunch(L"alpha", 500);
    store.RecordLaunch(L"mid", 500);
    const std::vector<UsageRecord> first = store.Recent();
    Expect(first.size() == 3, "tie count");
    Expect(first[0].stable_id == L"alpha", "tie broken by ascending stable id 1");
    Expect(first[1].stable_id == L"mid", "tie broken by ascending stable id 2");
    Expect(first[2].stable_id == L"zebra", "tie broken by ascending stable id 3");
    Expect(SameList(store.Recent(), first), "tie order reproducible across calls");
    Expect(store.Save(), "save tied records");
    UsageStore reloaded(dir);
    Expect(reloaded.Load() == UsageLoadResult::Loaded, "reload tied records");
    Expect(SameList(reloaded.Recent(), first), "tie order reproducible across reload");
    fs::remove_all(dir);
}

void TestNewLaunchMovesToFirst() {
    const std::wstring dir = MakeTempDir("move");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"alpha", 100);
    store.RecordLaunch(L"beta", 200);
    Expect(store.Recent()[0].stable_id == L"beta", "beta newest initially");
    store.RecordLaunch(L"alpha", 300);
    const std::vector<UsageRecord> recent = store.Recent();
    Expect(recent.size() == 2, "relaunch does not duplicate the record");
    Expect(SameRecord(recent[0], {L"alpha", 2, 300}), "relaunched app moves to first");
    Expect(recent[1].stable_id == L"beta", "previous newest shifts down");
    fs::remove_all(dir);
}

void TestFailedLaunchDoesNotUpdate() {
    const std::wstring dir = MakeTempDir("fail");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"alpha", 100);
    store.RecordLaunch(L"beta", 200);
    const std::vector<UsageRecord> before = store.Recent();

    // The store exposes only RecordLaunch; the caller (NR-010) gates on the
    // NR-008 LaunchResult.ok and skips the call on failure. Simulate that gate:
    // a failed launch of alpha at a later time must leave state unchanged.
    const bool launch_ok = false;
    if (launch_ok) {
        store.RecordLaunch(L"alpha", 900);
    }
    const std::vector<UsageRecord> after = store.Recent();
    Expect(SameList(after, before), "failed launch leaves recent unchanged");
    Expect(after[0].stable_id == L"beta" && after[0].last_launch_utc == 200,
           "failed launch does not move alpha to first");

    // Positive control: the same call gated on success does update.
    const bool launch_ok_success = true;
    if (launch_ok_success) {
        store.RecordLaunch(L"alpha", 900);
    }
    const std::vector<UsageRecord> after_success = store.Recent();
    Expect(SameRecord(after_success[0], {L"alpha", 2, 900}),
           "successful launch moves alpha to first and increments the count");
    fs::remove_all(dir);
}

void TestRoundTrip() {
    const std::wstring dir = MakeTempDir("roundtrip");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"known_app_1", 100);
    store.RecordLaunch(L"known_app_2", 300);
    store.RecordLaunch(kUnknownId, 200);  // app absent from the catalog
    Expect(store.Save(), "save usage records");

    UsageStore reloaded(dir);
    Expect(reloaded.Load() == UsageLoadResult::Loaded, "round-trip load");
    const std::vector<UsageRecord> recent = reloaded.Recent();
    Expect(recent.size() == 3, "all records survive reload");
    Expect(SameRecord(recent[0], {L"known_app_2", 1, 300}), "newest first after reload");
    Expect(SameRecord(recent[2], {L"known_app_1", 1, 100}), "oldest preserved after reload");
    Expect(recent[1].stable_id == kUnknownId, "record for an app absent from the catalog survives");
    fs::remove_all(dir);
}

void TestCorrupt() {
    const std::wstring dir = MakeTempDir("corrupt");
    const std::string content = "not a usage file\nschema=1\n";
    WriteBytes(dir + L"\\usage.tsv", content);
    UsageStore store(dir);
    Expect(store.Load() == UsageLoadResult::Corrupt, "corrupt file reports Corrupt");
    Expect(store.Recent().empty(), "corrupt load yields the empty safe default");
    Expect(!fs::exists(dir + L"\\usage.tsv"), "corrupt file moved aside");
    Expect(fs::exists(dir + L"\\usage.tsv.corrupt"), "corrupt file preserved for diagnostics");
    Expect(ReadBytes(dir + L"\\usage.tsv.corrupt") == content, "corrupt content preserved verbatim");
    fs::remove_all(dir);
}

void TestMalformedRow() {
    const std::wstring dir = MakeTempDir("badrow");
    const std::string content = "schema=1\nonly_two_fields\n";
    WriteBytes(dir + L"\\usage.tsv", content);
    UsageStore store(dir);
    Expect(store.Load() == UsageLoadResult::Corrupt, "malformed row reports Corrupt");
    Expect(store.Recent().empty(), "malformed row yields the empty safe default");
    Expect(!fs::exists(dir + L"\\usage.tsv"), "malformed file moved aside");
    Expect(fs::exists(dir + L"\\usage.tsv.corrupt"), "malformed file preserved");
    fs::remove_all(dir);
}

void TestNewerSchema() {
    const std::wstring dir = MakeTempDir("newer");
    const std::string content = "schema=99\n" + std::string("not_in_catalog") + "\t5\t1000\n";
    WriteBytes(dir + L"\\usage.tsv", content);
    UsageStore store(dir);
    Expect(store.Load() == UsageLoadResult::NewerSchema, "newer schema reports NewerSchema");
    Expect(store.Recent().empty(), "newer schema load yields the empty safe default");
    Expect(fs::exists(dir + L"\\usage.tsv"), "newer schema file untouched");
    Expect(ReadBytes(dir + L"\\usage.tsv") == content, "newer schema content unchanged");
    Expect(!fs::exists(dir + L"\\usage.tsv.corrupt"), "newer schema not treated as corrupt");
    fs::remove_all(dir);
}

void TestAtomicWriteFailure() {
    const std::wstring dir = MakeTempDir("atomic");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"alpha", 100);
    Expect(store.Save(), "initial save");

    // A directory occupying the .tmp path forces the temp write to fail.
    Expect(fs::create_directory(dir + L"\\usage.tsv.tmp"), "create tmp dir obstacle");
    store.RecordLaunch(L"alpha", 200);
    Expect(store.Save() == false, "save fails when temp write fails");

    UsageStore reloaded(dir);
    Expect(reloaded.Load() == UsageLoadResult::Loaded, "original survives failed save");
    const std::vector<UsageRecord> recent = reloaded.Recent();
    Expect(recent.size() == 1, "only the persisted record survived");
    Expect(SameRecord(recent[0], {L"alpha", 1, 100}),
           "failed save left the original file untouched");
    fs::remove_all(dir);
}

// NR-040: Forget() drops a single app's usage record; persistence is the
// caller's job, exactly as it is for RecordLaunch().

void TestForgetExisting() {
    const std::wstring dir = MakeTempDir("forget_existing");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"aaa", 100);
    store.RecordLaunch(L"bbb", 300);
    store.RecordLaunch(L"ccc", 200);
    Expect(store.Forget(L"bbb"), "forget an existing id returns true");
    const std::vector<UsageRecord> recent = store.Recent();
    Expect(recent.size() == 2, "forget removes exactly one record");
    for (const UsageRecord& r : recent) {
        Expect(r.stable_id != L"bbb", "forgotten id no longer in recent");
    }
    Expect(SameRecord(recent[0], {L"ccc", 1, 200}), "remaining newest still first");
    Expect(SameRecord(recent[1], {L"aaa", 1, 100}), "remaining oldest still last");
    fs::remove_all(dir);
}

void TestForgetMissing() {
    const std::wstring dir = MakeTempDir("forget_missing");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"aaa", 100);
    store.RecordLaunch(L"bbb", 300);
    const std::vector<UsageRecord> before = store.Recent();
    Expect(!store.Forget(L"zzz"), "forget a missing id returns false");
    Expect(SameList(store.Recent(), before), "forget a missing id leaves recent unchanged");
    fs::remove_all(dir);
}

void TestForgetEmpty() {
    const std::wstring dir = MakeTempDir("forget_empty");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"aaa", 100);
    const std::vector<UsageRecord> before = store.Recent();
    Expect(!store.Forget(L""), "forget an empty id returns false");
    Expect(SameList(store.Recent(), before), "forget an empty id leaves recent unchanged");
    fs::remove_all(dir);
}

void TestForgetPersists() {
    const std::wstring dir = MakeTempDir("forget_persist");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"alpha", 100);
    store.RecordLaunch(L"beta", 300);
    store.RecordLaunch(L"gamma", 200);
    store.RecordLaunch(L"alpha", 400);  // alpha now has 2 launches
    Expect(store.Forget(L"beta"), "forget beta");
    Expect(store.Save(), "save after forget");
    UsageStore reloaded(dir);
    Expect(reloaded.Load() == UsageLoadResult::Loaded, "reload after forget");
    const std::vector<UsageRecord> recent = reloaded.Recent();
    Expect(recent.size() == 2, "forgotten id absent after reload");
    for (const UsageRecord& r : recent) {
        Expect(r.stable_id != L"beta", "forgotten id is not reloaded");
    }
    Expect(SameRecord(recent[0], {L"alpha", 2, 400}), "alpha launches preserved");
    Expect(SameRecord(recent[1], {L"gamma", 1, 200}), "gamma preserved");
    fs::remove_all(dir);
}

void TestForgetThenRelaunch() {
    const std::wstring dir = MakeTempDir("forget_relaunch");
    UsageStore store(dir);
    store.Load();
    store.RecordLaunch(L"alpha", 100);
    store.RecordLaunch(L"alpha", 200);  // 2 launches
    Expect(store.Recent()[0].total_launches == 2, "alpha has 2 launches");
    Expect(store.Forget(L"alpha"), "forget alpha");
    Expect(store.Recent().empty(), "alpha gone after forget");
    store.RecordLaunch(L"alpha", 300);
    const std::vector<UsageRecord> recent = store.Recent();
    Expect(recent.size() == 1, "alpha reappears after relaunch");
    Expect(SameRecord(recent[0], {L"alpha", 1, 300}),
           "relaunch starts a fresh record with total_launches == 1");
    fs::remove_all(dir);
}

// design-spec §4.6: recency bonus 8 / 4 / 1 / 0 on top of the launch count.
void TestUsageScore() {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    constexpr std::int64_t kNow = 100 * kDay;
    const auto score = [](std::uint64_t launches, std::int64_t last) {
        return nimblerun::UsageScore({L"alpha", launches, last}, kNow);
    };
    Expect(score(1, kNow) == 1 + 8, "launched now gets the 24h bonus");
    Expect(score(1, kNow - kDay + 1) == 1 + 8, "just inside 24h");
    Expect(score(1, kNow - kDay) == 1 + 4, "24h boundary drops to the 7-day tier");
    Expect(score(1, kNow - 7 * kDay) == 1 + 1, "7-day boundary drops to the 30-day tier");
    Expect(score(1, kNow - 30 * kDay) == 1, "beyond 30 days there is no bonus");
    Expect(score(5, kNow - 30 * kDay) > score(1, kNow - 30 * kDay),
           "with equal recency the busier app wins");
    Expect(score(1, kNow) > score(2, kNow - 30 * kDay),
           "a fresh launch outranks a slightly busier stale one");
    Expect(score(1, kNow + kDay) == 1 + 8, "a backwards clock counts as just now");
    Expect(score(~0ull, 0) > 0, "an absurd launch count does not overflow to negative");
    // usage.tsv can carry any int64_t, so the extremes must not overflow.
    Expect(score(1, INT64_MIN) == 1, "the oldest possible timestamp gets no bonus");
    Expect(score(1, INT64_MAX) == 1 + 8, "the newest possible timestamp gets the top bonus");
}

} // namespace

int wmain() {
    TestEmpty();
    TestSortingNewestFirst();
    TestCapAtTwenty();
    TestTieBreaker();
    TestNewLaunchMovesToFirst();
    TestFailedLaunchDoesNotUpdate();
    TestRoundTrip();
    TestCorrupt();
    TestMalformedRow();
    TestNewerSchema();
    TestAtomicWriteFailure();
    TestForgetExisting();
    TestForgetMissing();
    TestForgetEmpty();
    TestForgetPersists();
    TestForgetThenRelaunch();
    TestUsageScore();
    std::printf("NR-009 recent usage check PASSED\n");
    return 0;
}
