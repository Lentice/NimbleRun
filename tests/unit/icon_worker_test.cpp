// NR-032: IconWorker delivers decoded icons off the UI thread as posted
// messages. Pure Win32 test with a message-only window; no visible UI.
// NR-036: with an IconStore wired in, the disk layer sits between the worker
// and the provider -- a second session on the same pack never touches the
// provider. The store points at %TEMP%\NimbleRunTest\<pid>, never the real
// %LOCALAPPDATA%\NimbleRun.
#include "icons/icon_cache.h"
#include "icons/icon_pack_format.h"
#include "icons/icon_store.h"
#include "icons/icon_worker.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::EncodeHeader;
using nimblerun::IconBitmap;
using nimblerun::IconKey;
using nimblerun::IconProvider;
using nimblerun::IconRequest;
using nimblerun::IconResult;
using nimblerun::IconStore;
using IconStorePaths = nimblerun::IconStore::IconStorePaths;
using nimblerun::IconWorker;
using nimblerun::kIndexCapacity;
using nimblerun::kPayloadStart;
using nimblerun::MakeEmptyPack;
using nimblerun::PackHeader;
using StoreState = nimblerun::IconStore::StoreState;

namespace {

constexpr wchar_t kTestClass[] = L"NimbleRun.IconWorkerTest";
constexpr UINT kReadyMessage = WM_APP + 100;
HINSTANCE g_instance = nullptr;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

AppEntry Entry(const std::wstring& stable_id) {
    AppEntry entry;
    entry.stable_id = stable_id;
    entry.display_name = stable_id;
    entry.launch_identity = L"C:\\Apps\\" + stable_id + L".exe";
    entry.source_path = entry.launch_identity;
    return entry;
}

IconKey Key(const std::wstring& stable_id) {
    return IconKey{stable_id, 48};
}

// Scriptable fake provider: optionally sleeps per load, optionally blocks on a
// gate event, records call order, and fails a configured key list (returns an
// empty bitmap).
class FakeProvider : public IconProvider {
public:
    int delay_ms = 0;
    HANDLE gate = nullptr;             // when set, Load waits on it first
    std::atomic<bool> entered{false};  // true once a gated Load is in flight
    std::vector<std::wstring> failing;
    bool throw_on_load = false;        // NR-076: simulates a throwing Shell/WIC path

    IconBitmap Load(const AppEntry&, const IconKey& key) override {
        if (throw_on_load) {
            throw std::runtime_error("injected icon load failure");
        }
        if (gate != nullptr) {
            entered.store(true);
            WaitForSingleObject(gate, INFINITE);
        }
        if (delay_ms > 0) {
            Sleep(static_cast<DWORD>(delay_ms));
        }
        for (const std::wstring& fail_key : failing) {
            if (fail_key == key.Encode()) {
                return {};
            }
        }
        IconBitmap bitmap;
        bitmap.width = 2;
        bitmap.height = 2;
        bitmap.pixels.assign(4, 0xFF112233u);
        return bitmap;
    }
};

HWND CreateMessageWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = g_instance;
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kTestClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, kTestClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                           nullptr, g_instance, nullptr);
}

// Drains kReadyMessage until `want` results are collected or the timeout hits.
// NR-077: each message's lParam is a token into the shared handoff registry;
// the result is moved out there, exactly as the production receiver does.
// WaitMessage blocks only while the queue is empty, so the test never busy-pins.
bool PumpResults(HWND window, std::vector<std::unique_ptr<IconResult>>& results,
                 int want, DWORD timeout_ms = 5000) {
    const DWORD deadline = GetTickCount() + timeout_ms;
    while (static_cast<int>(results.size()) < want && GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kReadyMessage, kReadyMessage, PM_REMOVE)) {
            std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
            const auto it = nimblerun::g_icon_handoffs.find(static_cast<std::uintptr_t>(msg.lParam));
            if (it != nimblerun::g_icon_handoffs.end()) {
                results.emplace_back(std::move(it->second));
                nimblerun::g_icon_handoffs.erase(it);
            }
        }
        if (static_cast<int>(results.size()) >= want) {
            break;
        }
        WaitMessage();
    }
    return static_cast<int>(results.size()) >= want;
}

// Drains the queue for a fixed short window (used to assert nothing arrives).
// An unknown token is consumed from the queue but ignored (never a result).
bool AnyResultIn(HWND window, std::vector<std::unique_ptr<IconResult>>& results,
                 DWORD wait_ms = 150) {
    const DWORD deadline = GetTickCount() + wait_ms;
    while (GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kReadyMessage, kReadyMessage, PM_REMOVE)) {
            std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
            const auto it = nimblerun::g_icon_handoffs.find(static_cast<std::uintptr_t>(msg.lParam));
            if (it != nimblerun::g_icon_handoffs.end()) {
                results.emplace_back(std::move(it->second));
                nimblerun::g_icon_handoffs.erase(it);
                return true;
            }
        }
        Sleep(2);
    }
    return false;
}

void TestPostReturnsImmediatelyAndResultArrivesAsync() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.delay_ms = 30;  // the provider is deliberately slow
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    const DWORD start = GetTickCount();
    worker.Post({Entry(L"app1"), Key(L"app1"), /*visible=*/true});
    const DWORD elapsed = GetTickCount() - start;
    Expect(elapsed < 5, "Post returns without waiting for the provider");

    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 1), "one result arrives asynchronously");
    Expect(results[0]->encoded_key == L"app1|48", "result carries the encoded key");
    Expect(!results[0]->bitmap.Empty(), "delayed provider returns a bitmap");

    worker.Stop();
    DestroyWindow(window);
}

void TestThreeRequestsDeliverThreeResults() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    worker.Post({Entry(L"a"), Key(L"a"), false});
    worker.Post({Entry(L"b"), Key(L"b"), false});
    worker.Post({Entry(L"c"), Key(L"c"), false});

    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 3), "three posts deliver three results");
    std::set<std::wstring> seen;
    for (const auto& result : results) {
        Expect(!result->bitmap.Empty(), "every result carries a bitmap");
        seen.insert(result->encoded_key);
    }
    Expect(seen == std::set<std::wstring>({L"a|48", L"b|48", L"c|48"}),
           "encoded keys match the posted keys one-to-one");

    worker.Stop();
    DestroyWindow(window);
}

void TestFailureStillReports() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.failing.push_back(L"bad|48");
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    worker.Post({Entry(L"bad"), Key(L"bad"), true});

    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 1), "a failed load still reports a result");
    Expect(results[0]->encoded_key == L"bad|48", "failure carries the encoded key");
    Expect(results[0]->bitmap.Empty(), "failure carries an empty bitmap");

    worker.Stop();
    DestroyWindow(window);
}

// NR-076: a provider that throws must not terminate the worker thread (design-
// spec §11: catch, log, discard). The worker reports an empty bitmap so the UI
// clears the pending key and keeps the fallback, and later requests are still
// processed normally.
void TestThrowingProviderIsContained() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.throw_on_load = true;
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    worker.Post({Entry(L"boom"), Key(L"boom"), true});
    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 1), "a throwing load still reports a result");
    Expect(results[0]->encoded_key == L"boom|48", "thrown load carries the encoded key");
    Expect(results[0]->bitmap.Empty(), "thrown load reports an empty bitmap (fallback)");

    // The worker survived; a second request is processed normally.
    provider.throw_on_load = false;
    worker.Post({Entry(L"fine"), Key(L"fine"), true});
    std::vector<std::unique_ptr<IconResult>> later;
    Expect(PumpResults(window, later, 1), "a later request is processed");
    Expect(!later[0]->bitmap.Empty(), "a later request returns a real bitmap");

    worker.Stop();
    DestroyWindow(window);
}

// NR-077: a message whose lParam is not a registered handoff token must be
// ignored, never dereferenced -- a same-integrity process can post WM_APP+9
// to our HWND. The consumption helpers here mirror the production receiver:
// the unknown token is removed from the queue but produces no result, and the
// process keeps working.
void TestUnknownTokenIgnored() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    worker.Post({Entry(L"ok"), Key(L"ok"), true});
    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 1), "a real request still arrives");
    Expect(results[0]->encoded_key == L"ok|48", "real result key matches");

    // Post a token that was never registered (lParam = 1). It must not be
    // consumed as a result.
    PostMessageW(window, kReadyMessage, 0, 1);
    std::vector<std::unique_ptr<IconResult>> unknown;
    Expect(!AnyResultIn(window, unknown), "an unknown token is ignored");

    // A later real request still flows through the registry normally.
    worker.Post({Entry(L"two"), Key(L"two"), true});
    std::vector<std::unique_ptr<IconResult>> later;
    Expect(PumpResults(window, later, 1), "a later real request arrives");
    Expect(later[0]->encoded_key == L"two|48", "later real result key matches");

    worker.Stop();
    DestroyWindow(window);
}

void TestVisibleJumpsTheQueue() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    // a is grabbed by the worker and blocks on the gate. b is then queued;
    // c arrives visible=true and must jump ahead of b. Results arrive in
    // processing order, so the message order proves the jump.
    worker.Post({Entry(L"a"), Key(L"a"), false});
    const DWORD deadline = GetTickCount() + 1000;
    while (!provider.entered.load() && GetTickCount() < deadline) {
        Sleep(1);
    }
    Expect(provider.entered.load(), "worker is blocked on the gated request");
    worker.Post({Entry(L"b"), Key(L"b"), false});
    worker.Post({Entry(L"c"), Key(L"c"), true});
    SetEvent(provider.gate);  // release a; c and b are already queued

    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 3), "all three results arrive");
    Expect(results[0]->encoded_key == L"a|48", "gated request finishes first");
    Expect(results[1]->encoded_key == L"c|48", "visible=true jumps the queued b");
    Expect(results[2]->encoded_key == L"b|48", "non-visible request is processed last");

    CloseHandle(provider.gate);
    worker.Stop();
    DestroyWindow(window);
}

void TestVisibleJumpsAheadOfQueuedPrewarm() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    // NR-037: three prewarm requests (visible=false), then one visible=true
    // request. The first prewarm is grabbed by the worker and blocks on the
    // gate; the other two sit queued behind it. The visible request must jump
    // ahead of the queued prewarm requests, so its result arrives before the
    // first unprocessed prewarm one.
    worker.Post({Entry(L"a"), Key(L"a"), false});
    const DWORD deadline = GetTickCount() + 1000;
    while (!provider.entered.load() && GetTickCount() < deadline) {
        Sleep(1);
    }
    Expect(provider.entered.load(), "worker is blocked on the gated prewarm request");
    worker.Post({Entry(L"b"), Key(L"b"), false});
    worker.Post({Entry(L"c"), Key(L"c"), false});
    worker.Post({Entry(L"d"), Key(L"d"), true});
    SetEvent(provider.gate);  // release a; d jumped ahead of the queued b, c

    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, 4), "all four results arrive");
    Expect(results[0]->encoded_key == L"a|48", "gated prewarm request finishes first");
    Expect(results[1]->encoded_key == L"d|48",
           "visible=true jumps ahead of the queued prewarm requests");
    Expect(results[2]->encoded_key == L"b|48",
           "first queued prewarm request is processed after the visible one");
    Expect(results[3]->encoded_key == L"c|48",
           "second queued prewarm request is processed last");

    CloseHandle(provider.gate);
    worker.Stop();
    DestroyWindow(window);
}

void TestStopDropsQueueAndSilencesNewPosts() {
    const HWND window = CreateMessageWindow();
    FakeProvider provider;
    provider.delay_ms = 30;
    IconWorker worker(window, kReadyMessage, provider);
    worker.Start();

    for (int i = 0; i < 5; ++i) {
        worker.Post({Entry(std::to_wstring(i)), Key(std::to_wstring(i)), false});
    }

    const DWORD start = GetTickCount();
    worker.Stop();  // queue is non-empty (worker is busy on the first item)
    const DWORD elapsed = GetTickCount() - start;
    Expect(elapsed < 2000, "Stop with a non-empty queue does not hang");

    // Only the request that was already in flight may have reported; the four
    // queued ones are dropped. Drain whatever landed, then post again.
    std::vector<std::unique_ptr<IconResult>> results;
    MSG msg{};
    while (PeekMessageW(&msg, window, kReadyMessage, kReadyMessage, PM_REMOVE)) {
        std::lock_guard<std::mutex> lock(nimblerun::g_handoff_mutex);
        const auto it = nimblerun::g_icon_handoffs.find(static_cast<std::uintptr_t>(msg.lParam));
        if (it != nimblerun::g_icon_handoffs.end()) {
            results.emplace_back(std::move(it->second));
            nimblerun::g_icon_handoffs.erase(it);
        }
    }
    const std::size_t before = results.size();

    worker.Post({Entry(L"after"), Key(L"after"), true});
    Expect(!AnyResultIn(window, results), "no new results arrive after Stop");
    Expect(results.size() == before, "posting after Stop adds nothing");

    DestroyWindow(window);
}

// --- NR-036: disk-layer round trips through a temp pack. ---

fs::path TestDir() {
    const fs::path dir =
        fs::temp_directory_path() / L"NimbleRunTest" / std::to_wstring(GetCurrentProcessId());
    fs::create_directories(dir);
    return dir;
}

fs::path PackPath() {
    return TestDir() / L"icons.cache";
}

// A stable ID the store's ParseStableIdHash accepts: 16 lowercase hex digits.
std::wstring Id(int n) {
    wchar_t buf[24];
    std::swprintf(buf, 24, L"%016x", n);
    return buf;
}

void ResetCache() {
    DeleteFileW(PackPath().c_str());
    DeleteFileW((PackPath().wstring() + L".tmp").c_str());
}

// An entry whose source is a real temp file, so the worker's GetFileAttributesExW
// produces a stable source stamp across two sessions.
AppEntry FileEntry(const std::wstring& stable_id, const fs::path& source) {
    AppEntry entry;
    entry.stable_id = stable_id;
    entry.display_name = stable_id;
    entry.launch_identity = source.wstring();
    entry.source_path = source.wstring();
    return entry;
}

void WriteSourceFile(const fs::path& path, int content) {
    const std::string bytes = "nimblerun-icon-source-" + std::to_string(content);
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "create source file");
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                              &written, nullptr);
    CloseHandle(file);
    Expect(ok != FALSE && written == bytes.size(), "write source file");
}

void BumpSourceMtime(const fs::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "open source for mtime bump");
    FILETIME ft{};
    Expect(GetFileTime(file, nullptr, nullptr, &ft) != FALSE, "read source mtime");
    ULARGE_INTEGER value{};
    value.HighPart = ft.dwHighDateTime;
    value.LowPart = ft.dwLowDateTime;
    value.QuadPart += 3600ull * 10'000'000ull;  // +1 hour
    ft.dwHighDateTime = value.HighPart;
    ft.dwLowDateTime = value.LowPart;
    Expect(SetFileTime(file, nullptr, nullptr, &ft) != FALSE, "bump source mtime");
    CloseHandle(file);
}

std::vector<std::uint8_t> ReadFileBytes(const fs::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != FALSE && read > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    CloseHandle(file);
    return bytes;
}

void WriteFileBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "write file handle");
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                              &written, nullptr);
    CloseHandle(file);
    Expect(ok != FALSE && written == bytes.size(), "write file bytes");
}

// NR-036: the bitmap must be valid PNG-encodable input and decode back to the
// exact same pixels (the worker hands it to EncodeIconPng on the first miss),
// so the bitmap is key.variant-sized, opaque, and colored per stable_id.
class CountingProvider : public IconProvider {
public:
    std::atomic<int> calls{0};
    std::vector<std::wstring> failing;
    int delay_ms = 0;

    IconBitmap Load(const AppEntry& entry, const IconKey& key) override {
        calls.fetch_add(1);
        if (delay_ms > 0) {
            Sleep(static_cast<DWORD>(delay_ms));
        }
        for (const std::wstring& fail_key : failing) {
            if (fail_key == key.Encode()) {
                return {};
            }
        }
        const std::size_t size = key.variant > 0 ? key.variant : 2;
        const std::uint32_t color =
            0xFF000000u | ((0x1u + static_cast<std::uint32_t>(std::stoul(entry.stable_id)) * 0x23u) & 0xFFFFFFu);
        IconBitmap bitmap;
        bitmap.width = static_cast<std::uint32_t>(size);
        bitmap.height = static_cast<std::uint32_t>(size);
        bitmap.pixels.assign(size * size, color);
        return bitmap;
    }
};

// Runs one session over `store` with the given entries/keys: posts every key,
// waits for every result, then (optionally) posts a flush signal and stops the
// worker. Returns each result's pixels in arrival order.
std::vector<std::vector<std::uint32_t>> RunRound(
    HWND window, IconStore& store, CountingProvider& provider,
    const std::vector<AppEntry>& entries, const std::vector<IconKey>& keys,
    bool flush_then_stop) {
    IconWorker worker(window, kReadyMessage, provider, &store);
    worker.Start();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        worker.Post({entries[i], keys[i], /*visible=*/true});
    }
    std::vector<std::unique_ptr<IconResult>> results;
    Expect(PumpResults(window, results, static_cast<int>(entries.size())),
           "every posted request reports a result");
    std::vector<std::vector<std::uint32_t>> pixels;
    for (const auto& result : results) {
        pixels.push_back(result->bitmap.pixels);
    }
    if (flush_then_stop) {
        worker.PostFlush({}, 1);  // fixed wall clock; the stamp path ignores it
    }
    worker.Stop();
    return pixels;
}

void TestDiskRoundTripServesSecondSessionFromDisk() {
    const HWND window = CreateMessageWindow();
    const fs::path dir = TestDir();
    ResetCache();

    constexpr int kCount = 4;
    std::vector<AppEntry> entries;
    std::vector<IconKey> keys;
    for (int i = 0; i < kCount; ++i) {
        const fs::path source = dir / (L"app" + std::to_wstring(i) + L".exe");
        WriteSourceFile(source, i);
        entries.push_back(FileEntry(Id(i), source));
        keys.push_back(Key(Id(i)));
    }
    const IconStorePaths paths{PackPath()};

    std::vector<std::vector<std::uint32_t>> first;
    {
        IconStore store(paths);
        CountingProvider provider;
        first = RunRound(window, store, provider, entries, keys, /*flush_then_stop=*/true);
        Expect(provider.calls.load() == kCount, "round 1 calls the provider once per key");
    }

    {
        IconStore store(paths);
        CountingProvider provider;
        const std::vector<std::vector<std::uint32_t>> second =
            RunRound(window, store, provider, entries, keys, /*flush_then_stop=*/false);
        Expect(provider.calls.load() == 0, "round 2 is served entirely from the disk pack");
        Expect(second.size() == first.size(), "round 2 delivers every result");
        for (std::size_t i = 0; i < first.size(); ++i) {
            Expect(!first[i].empty() && first[i] == second[i],
                   "round 2 pixels are byte-identical to round 1");
        }
    }

    DestroyWindow(window);
}

void TestSourceStampChangeRefetchesFromProvider() {
    const HWND window = CreateMessageWindow();
    const fs::path dir = TestDir();
    ResetCache();
    const fs::path source = dir / L"app.exe";
    WriteSourceFile(source, 1);
    const AppEntry entry = FileEntry(Id(0), source);
    const IconKey key = Key(Id(0));
    const IconStorePaths paths{PackPath()};

    {
        IconStore store(paths);
        CountingProvider provider;
        RunRound(window, store, provider, {entry}, {key}, /*flush_then_stop=*/true);
        Expect(provider.calls.load() == 1, "round 1 calls the provider");
    }

    BumpSourceMtime(source);  // same bytes, newer last-write-time -> new stamp

    {
        IconStore store(paths);
        CountingProvider provider;
        const auto pixels = RunRound(window, store, provider, {entry}, {key},
                                     /*flush_then_stop=*/false);
        Expect(provider.calls.load() == 1, "a stale source stamp refetches from the provider");
        Expect(pixels.size() == 1 && !pixels[0].empty(), "a fresh result arrives");
    }

    DestroyWindow(window);
}

void TestDisabledStoreAlwaysCallsProviderAndNeverWrites() {
    const HWND window = CreateMessageWindow();
    const fs::path dir = TestDir();
    const fs::path pack = PackPath();
    ResetCache();

    // A valid pack whose schema_version is newer than the codec supports: the
    // store must open as Disabled, leave the file untouched, and the worker
    // must keep serving every icon through the provider.
    std::vector<std::uint8_t> bytes = MakeEmptyPack();
    PackHeader newer;
    newer.schema_version = 2;
    newer.generation = 1;
    newer.index_capacity = static_cast<std::uint32_t>(kIndexCapacity);
    newer.payload_end = kPayloadStart;
    EncodeHeader(newer, bytes.data());
    WriteFileBytes(pack, bytes);
    const std::vector<std::uint8_t> before = ReadFileBytes(pack);

    const fs::path source = dir / L"app.exe";
    WriteSourceFile(source, 1);
    const AppEntry entry = FileEntry(Id(0), source);
    const IconKey key = Key(Id(0));
    const IconStorePaths paths{pack};

    {
        IconStore store(paths);
        CountingProvider provider;
        RunRound(window, store, provider, {entry}, {key}, /*flush_then_stop=*/true);
        Expect(provider.calls.load() == 1, "disabled store round 1 calls the provider");
    }
    {
        IconStore store(paths);
        CountingProvider provider;
        RunRound(window, store, provider, {entry}, {key}, /*flush_then_stop=*/false);
        Expect(provider.calls.load() == 1, "disabled store round 2 calls the provider again");
    }

    Expect(ReadFileBytes(pack) == before, "a disabled store never modifies the pack");
    DestroyWindow(window);
}

void TestEmptyResultIsNotPersisted() {
    const HWND window = CreateMessageWindow();
    const fs::path dir = TestDir();
    ResetCache();
    const fs::path source = dir / L"app.exe";
    WriteSourceFile(source, 1);
    const AppEntry entry = FileEntry(Id(0), source);
    const IconKey key = Key(Id(0));
    const IconStorePaths paths{PackPath()};

    {
        IconStore store(paths);
        CountingProvider provider;
        provider.failing.push_back(key.Encode());
        const auto pixels = RunRound(window, store, provider, {entry}, {key},
                                     /*flush_then_stop=*/true);
        Expect(pixels.size() == 1 && pixels[0].empty(),
               "the failing key reports an empty bitmap");
        Expect(provider.calls.load() == 1, "round 1 calls the provider");
    }
    {
        IconStore store(paths);
        CountingProvider provider;
        RunRound(window, store, provider, {entry}, {key}, /*flush_then_stop=*/false);
        Expect(provider.calls.load() == 1,
               "round 2 calls the provider again: an empty result is never written");
    }

    DestroyWindow(window);
}

void TestStopWithPendingDataFlushesAndDoesNotHang() {
    const HWND window = CreateMessageWindow();
    const fs::path dir = TestDir();
    ResetCache();
    const fs::path source = dir / L"app.exe";
    WriteSourceFile(source, 1);
    const AppEntry entry = FileEntry(Id(0), source);

    {
        IconStore store(IconStorePaths{PackPath()});
        CountingProvider provider;
        provider.delay_ms = 100;  // long enough to observe the in-flight request
        IconWorker worker(window, kReadyMessage, provider, &store);
        worker.Start();

        // Six requests: the worker is busy on the first (delayed), the rest stay
        // queued. Stop() must not hang: the in-flight request finishes
        // (buffering a Put), then the final flush persists it before joining.
        for (int i = 0; i < 6; ++i) {
            worker.Post({entry, Key(Id(i)), false});
        }
        const DWORD entered_deadline = GetTickCount() + 2000;
        while (provider.calls.load() == 0 && GetTickCount() < entered_deadline) {
            Sleep(1);
        }
        Expect(provider.calls.load() >= 1, "the worker is processing the first request");
        const DWORD start = GetTickCount();
        worker.Stop();
        const DWORD elapsed = GetTickCount() - start;
        Expect(elapsed < 2000, "Stop with a non-empty queue and pending data does not hang");
        Expect(provider.calls.load() >= 1 && provider.calls.load() <= 6,
               "only queued-then-processed requests reach the provider");
        // worker and store are destroyed here (Stop then Unmap) so the pack is
        // no longer mapped when the verification store below opens it.
    }

    // The final flush persisted whatever had been buffered before the stop.
    {
        IconStore verify(IconStorePaths{PackPath()});
        Expect(verify.Open() == StoreState::Ready, "the pack opens after the final flush");
        Expect(verify.Stats().entries >= 1, "the final flush wrote the buffered icons");
    }

    DestroyWindow(window);
}

} // namespace

int wmain() {
    g_instance = GetModuleHandleW(nullptr);

    TestPostReturnsImmediatelyAndResultArrivesAsync();
    TestThreeRequestsDeliverThreeResults();
    TestFailureStillReports();
    TestThrowingProviderIsContained();
    TestUnknownTokenIgnored();
    TestVisibleJumpsTheQueue();
    TestVisibleJumpsAheadOfQueuedPrewarm();
    TestStopDropsQueueAndSilencesNewPosts();

    TestDiskRoundTripServesSecondSessionFromDisk();
    TestSourceStampChangeRefetchesFromProvider();
    TestDisabledStoreAlwaysCallsProviderAndNeverWrites();
    TestEmptyResultIsNotPersisted();
    TestStopWithPendingDataFlushesAndDoesNotHang();

    std::printf("NR-032/NR-036/NR-037 icon worker check PASSED\n");
    return 0;
}
