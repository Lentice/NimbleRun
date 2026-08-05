// NR-032: IconWorker delivers decoded icons off the UI thread as posted
// messages. Pure Win32 test with a message-only window; no visible UI.
#include "icons/icon_cache.h"
#include "icons/icon_worker.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>

using nimblerun::AppEntry;
using nimblerun::IconBitmap;
using nimblerun::IconKey;
using nimblerun::IconProvider;
using nimblerun::IconRequest;
using nimblerun::IconResult;
using nimblerun::IconWorker;

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

    IconBitmap Load(const AppEntry&, const IconKey& key) override {
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
// WaitMessage blocks only while the queue is empty, so the test never busy-pins.
bool PumpResults(HWND window, std::vector<std::unique_ptr<IconResult>>& results,
                 int want, DWORD timeout_ms = 5000) {
    const DWORD deadline = GetTickCount() + timeout_ms;
    while (static_cast<int>(results.size()) < want && GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kReadyMessage, kReadyMessage, PM_REMOVE)) {
            results.emplace_back(reinterpret_cast<IconResult*>(msg.lParam));
        }
        if (static_cast<int>(results.size()) >= want) {
            break;
        }
        WaitMessage();
    }
    return static_cast<int>(results.size()) >= want;
}

// Drains the queue for a fixed short window (used to assert nothing arrives).
bool AnyResultIn(HWND window, std::vector<std::unique_ptr<IconResult>>& results,
                 DWORD wait_ms = 150) {
    const DWORD deadline = GetTickCount() + wait_ms;
    while (GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kReadyMessage, kReadyMessage, PM_REMOVE)) {
            results.emplace_back(reinterpret_cast<IconResult*>(msg.lParam));
            return true;
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
        results.emplace_back(reinterpret_cast<IconResult*>(msg.lParam));
    }
    const std::size_t before = results.size();

    worker.Post({Entry(L"after"), Key(L"after"), true});
    Expect(!AnyResultIn(window, results), "no new results arrive after Stop");
    Expect(results.size() == before, "posting after Stop adds nothing");

    DestroyWindow(window);
}

} // namespace

int wmain() {
    g_instance = GetModuleHandleW(nullptr);

    TestPostReturnsImmediatelyAndResultArrivesAsync();
    TestThreeRequestsDeliverThreeResults();
    TestFailureStillReports();
    TestVisibleJumpsTheQueue();
    TestStopDropsQueueAndSilencesNewPosts();

    std::printf("NR-032 icon worker check PASSED\n");
    return 0;
}
