#include "test_util.h"

#include "app_host/rebuild_pipeline.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace nimblerun;

struct Posted {
    UINT message = 0;
    WPARAM w_param = 0;
    LPARAM l_param = 0;
};

bool WaitForPosts(const std::vector<Posted>& posts, std::mutex& mutex,
                 std::size_t count) {
    for (int i = 0; i < 200; ++i) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (posts.size() >= count) return true;
        }
        Sleep(5);
    }
    return false;
}

RebuildEnumeration Enumerate(CatalogSource source, const Settings&,
                             std::atomic<bool>*) {
    RebuildEnumeration result;
    AppEntry entry;
    entry.display_name = source == CatalogSource::AppsFolder ? L"Apps" : L"Start";
    entry.stable_id = source == CatalogSource::AppsFolder ? L"apps" : L"start";
    entry.source = source == CatalogSource::AppsFolder ? AppSource::AppsFolder
                                                        : AppSource::UserStartMenu;
    entry.launch_verified = true;
    result.entries.push_back(entry);
    result.source_ok = source != CatalogSource::AppsFolder;
    return result;
}

void TestDeliveryFailureCompletesOnce() {
    CatalogRefreshCoordinator refresh;
    std::vector<Posted> posts;
    std::mutex posts_mutex;
    int completed = 0;
    int post_count = 0;
    RebuildPipeline pipeline(
        refresh, [] { return Settings{}; },
        [&](UINT message, WPARAM w_param, LPARAM l_param) {
            std::lock_guard<std::mutex> lock(posts_mutex);
            ++post_count;
            if (message == WM_APP + 8 && post_count == 1) return false;
            posts.push_back({message, w_param, l_param});
            return true;
        },
        [](CatalogSource source, const Settings& settings, std::atomic<bool>* cancel) {
            if (source == CatalogSource::StartMenu) Sleep(20);
            return Enumerate(source, settings, cancel);
        },
        [&] { ++completed; }, [] {}, [] {});

    pipeline.Request({CatalogSource::StartMenu, CatalogSource::AppsFolder},
                     RebuildReason::Explicit);
    for (int i = 0; i < 200; ++i) {
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(posts_mutex);
            for (const Posted& post : posts) found |= post.message == WM_APP + 8;
        }
        if (found) break;
        Sleep(5);
    }
    Posted healthy{};
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        for (const Posted& post : posts) {
            if (post.message == WM_APP + 8) {
                healthy = post;
                break;
            }
        }
    }
    pipeline.OnResultMessage(healthy.w_param, healthy.l_param);
    for (int i = 0; i < 200 && completed == 0; ++i) Sleep(5);
    Expect(completed == 1, "delivery failure completes the generation once");
    Expect(!refresh.IsRebuildInProgress(), "delivery failure clears in-progress state");
}

void TestCacheFailureRetention() {
    CatalogRefreshCoordinator refresh;
    AppEntry cached;
    cached.display_name = L"Cached AppsFolder";
    cached.stable_id = L"cached-apps";
    cached.source = AppSource::AppsFolder;
    cached.launch_verified = false;
    refresh.SetSnapshot({cached});
    refresh.SeedSourceEntriesFromSnapshot();
    std::vector<Posted> posts;
    std::mutex posts_mutex;
    int completed = 0;
    RebuildPipeline pipeline(
        refresh, [] { return Settings{}; },
        [&](UINT message, WPARAM w_param, LPARAM l_param) {
            std::lock_guard<std::mutex> lock(posts_mutex);
            posts.push_back({message, w_param, l_param});
            return true;
        },
        Enumerate, [&] { ++completed; }, [] {}, [] {});
    pipeline.Request({CatalogSource::StartMenu, CatalogSource::AppsFolder},
                     RebuildReason::Explicit);
    Expect(WaitForPosts(posts, posts_mutex, 2), "both source results were posted");
    std::vector<Posted> copy;
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        copy = posts;
    }
    for (const Posted& post : copy) pipeline.OnResultMessage(post.w_param, post.l_param);
    Expect(completed == 1, "cache-failure generation completed");
    Expect(refresh.Snapshot().size() == 2, "failed source cache row is retained");
}

void TestFullRescanThrottleAndWatchMap() {
    CatalogRefreshCoordinator refresh;
    std::atomic<int> enumerations = 0;
    std::vector<Posted> posts;
    std::mutex posts_mutex;
    int scheduled = 0;
    RebuildPipeline pipeline(
        refresh, [] { return Settings{}; }, [&](UINT message, WPARAM w_param, LPARAM l_param) {
            std::lock_guard<std::mutex> lock(posts_mutex);
            posts.push_back({message, w_param, l_param});
            return true;
        },
        [&](CatalogSource source, const Settings& settings, std::atomic<bool>* cancel) {
            ++enumerations;
            return Enumerate(source, settings, cancel);
        }, [] {}, [] {}, [&] { ++scheduled; });
    pipeline.SetWatchSources({{L"missing-a", true, CatalogSource::StartMenu},
                              {L"missing-b", false, CatalogSource::UserFolder}});
    Expect(pipeline.SourceForIndex(1) == CatalogSource::StartMenu,
           "watch index maps to first explicit source");
    Expect(pipeline.SourceForIndex(2) == CatalogSource::UserFolder,
           "watch index maps to second explicit source");
    Expect(!pipeline.SourceForIndex(3), "unknown watch index has no source");
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::FullRescan);
    Expect(WaitForPosts(posts, posts_mutex, 1), "first full-rescan result was posted");
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Change);
    Expect(enumerations.load() == 1 && scheduled == 1,
           "in-flight source change is deferred instead of starting a second generation");
    Posted first{};
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        first = posts.front();
    }
    pipeline.OnResultMessage(first.w_param, first.l_param);
    for (int i = 0; i < 100 && refresh.IsRebuildInProgress(); ++i) Sleep(5);
    Expect(enumerations.load() == 1, "first full-rescan generation completed");
    const int before = enumerations.load();
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::FullRescan);
    Sleep(50);
    Expect(enumerations.load() == before,
           "full-rescan marker is throttled inside the completed-generation window");
    Expect(scheduled == 2, "throttled marker is deferred through debounce");
}

void TestShutdownBounded() {
    CatalogRefreshCoordinator refresh;
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> started = false;
    auto* pipeline = new RebuildPipeline(
        refresh, [] { return Settings{}; }, [finished](UINT, WPARAM, LPARAM) {
            SetEvent(finished);
            return true;
        },
        [&](CatalogSource, const Settings&, std::atomic<bool>*) {
            started.store(true);
            WaitForSingleObject(gate, INFINITE);
            return RebuildEnumeration{};
        }, [] {}, [] {}, [] {});
    pipeline->Request({CatalogSource::StartMenu}, RebuildReason::Explicit);
    for (int i = 0; i < 100 && !started.load(); ++i) Sleep(5);
    const auto begin = std::chrono::steady_clock::now();
    pipeline->Shutdown(10);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();
    Expect(elapsed < 1000, "shutdown returns after its bounded wait");
    SetEvent(gate);
    Expect(WaitForSingleObject(finished, 1000) == WAIT_OBJECT_0,
           "detached worker finishes before its owner is destroyed");
    delete pipeline;
    CloseHandle(gate);
    CloseHandle(finished);
}

} // namespace

int wmain() {
    TestDeliveryFailureCompletesOnce();
    TestCacheFailureRetention();
    TestFullRescanThrottleAndWatchMap();
    TestShutdownBounded();
    return 0;
}
