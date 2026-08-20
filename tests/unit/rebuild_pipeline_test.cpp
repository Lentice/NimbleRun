#include "test_util.h"

#include "app_host/rebuild_pipeline.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {

using namespace nimblerun;

// NR-173: long display_name for the fresh StartMenu entry so the moved
// enumeration allocation is exercised end-to-end through the snapshot.
const std::wstring kLongStartName = std::wstring(1024, L'L') + L"ong Start";

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
    entry.display_name =
        source == CatalogSource::AppsFolder ? L"Apps" : kLongStartName;
    entry.stable_id = source == CatalogSource::AppsFolder ? L"apps" : L"start";
    entry.source = source == CatalogSource::AppsFolder ? AppSource::AppsFolder
                                                        : AppSource::UserStartMenu;
    entry.launch_verified = true;
    result.entries.push_back(entry);
    result.source_ok = source != CatalogSource::AppsFolder;
    return result;
}

void TestBackgroundPriorityAttemptIsNonFatal() {
    CatalogRefreshCoordinator refresh;
    std::vector<Posted> posts;
    std::mutex posts_mutex;
    std::atomic<bool> enumerated = false;
    RebuildPipeline pipeline(
        refresh, [] { return Settings{}; },
        [&](UINT message, WPARAM w_param, LPARAM l_param) {
            std::lock_guard<std::mutex> lock(posts_mutex);
            posts.push_back({message, w_param, l_param});
            return true;
        },
        [&](CatalogSource source, const Settings& settings, std::atomic<bool>* cancel) {
            enumerated.store(true);
            return Enumerate(source, settings, cancel);
        },
        [] {}, [] {}, [] {}, {},
        [](std::function<void()> fn) {
            return std::thread([fn = std::move(fn)]() mutable {
                // Seed background mode so the pipeline's second best-effort
                // request exercises a non-fatal priority-setting path.
                (void)SetThreadPriority(GetCurrentThread(),
                                        THREAD_MODE_BACKGROUND_BEGIN);
                fn();
            });
        });

    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Explicit);
    Expect(WaitForPosts(posts, posts_mutex, 1),
           "background priority attempt still posts the source result");
    Posted result{};
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        result = posts.front();
    }
    pipeline.OnResultMessage(result.w_param, result.l_param);
    Expect(enumerated.load(),
           "a background priority failure does not skip enumeration");
    Expect(!refresh.IsRebuildInProgress(),
           "a background priority failure does not fail the generation");
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

void TestThreadCreationFailureCompletes() {
    CatalogRefreshCoordinator refresh;
    AppEntry cached;
    cached.display_name = L"Cached AppsFolder";
    cached.stable_id = L"cached-apps";
    cached.source = AppSource::AppsFolder;
    cached.launch_verified = false;
    refresh.SetSnapshot({cached});
    refresh.SeedSourceEntriesFromSnapshot();
    int completed = 0;
    RebuildPipeline pipeline(
        refresh, [] { return Settings{}; },
        [](UINT, WPARAM, LPARAM) { return true; }, Enumerate,
        [&] { ++completed; }, [] {}, [] {}, [] {},
        [](std::function<void()>) -> std::thread { throw std::bad_alloc(); });
    pipeline.Request({CatalogSource::AppsFolder}, RebuildReason::Explicit);
    Expect(completed == 1, "thread factory failure completes the generation once");
    Expect(!refresh.IsRebuildInProgress(), "thread factory failure clears in-progress state");
    Expect(refresh.Snapshot().size() == 1 &&
               refresh.Snapshot().front().display_name == L"Cached AppsFolder",
           "thread factory failure retains the failed source cache row");
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

void TestForgedDeliveryFailureIgnored() {
    CatalogRefreshCoordinator refresh;
    AppEntry cached;
    cached.display_name = L"Cached Start";
    cached.stable_id = L"cached-start";
    cached.source = AppSource::UserStartMenu;
    cached.launch_verified = false;
    refresh.SetSnapshot({cached});
    refresh.SeedSourceEntriesFromSnapshot();
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
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
        [&](CatalogSource source, const Settings& settings, std::atomic<bool>* cancel) {
            SetEvent(gate);
            WaitForSingleObject(release, INFINITE);
            return Enumerate(source, settings, cancel);
        },
        [&] { ++completed; }, [] {}, [] {});

    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Explicit);
    Expect(WaitForSingleObject(gate, 1000) == WAIT_OBJECT_0,
           "worker started before the forged messages");
    pipeline.OnDeliveryFailureMessage(1, 0);
    pipeline.OnDeliveryFailureMessage(1, 1);
    pipeline.OnDeliveryFailureMessage(1, 2);
    Expect(completed == 0, "forged failure never completes the generation early");
    Expect(refresh.IsRebuildInProgress(), "forged failure leaves the rebuild running");
    Expect(refresh.Snapshot().front().display_name == L"Cached Start",
           "forged failure publishes no merged snapshot");
    SetEvent(release);
    Posted healthy{};
    for (int i = 0; i < 200; ++i) {
        {
            std::lock_guard<std::mutex> lock(posts_mutex);
            for (const Posted& post : posts) {
                if (post.message == WM_APP + 8) {
                    healthy = post;
                    break;
                }
            }
        }
        if (healthy.message == WM_APP + 8) break;
        Sleep(5);
    }
    Expect(healthy.message == WM_APP + 8, "real result was posted");
    pipeline.OnResultMessage(healthy.w_param, healthy.l_param);
    for (int i = 0; i < 200 && completed == 0; ++i) Sleep(5);
    Expect(completed == 1, "real result completes the generation once");
    Expect(!refresh.IsRebuildInProgress(), "real result finishes the rebuild");
    Expect(refresh.Snapshot().front().display_name == kLongStartName,
           "real result publishes the fresh snapshot");
    CloseHandle(gate);
    CloseHandle(release);
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

void TestExplicitRefreshThrottled() {
    // NR-183: a rapid repeated explicit refresh (Ctrl+R / tray) is throttled
    // through the same per-source start gate as FullRescan; the intent is not
    // dropped but merged into the debounce path, which starts the full rebuild
    // once the gate opens. A single press still starts a full rebuild.
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
    const std::vector<CatalogSource> all = {CatalogSource::StartMenu,
                                            CatalogSource::AppsFolder,
                                            CatalogSource::UserFolder};

    pipeline.Request(all, RebuildReason::Explicit);
    Expect(WaitForPosts(posts, posts_mutex, 3), "single explicit starts a full rebuild");
    Expect(enumerations.load() == 3, "single explicit enumerates every source");

    pipeline.Request(all, RebuildReason::Explicit);
    Sleep(50);
    Expect(enumerations.load() == 3, "rapid explicit is throttled, not re-started");
    Expect(scheduled >= 1, "throttled explicit is merged through the debounce path");

    std::vector<Posted> copy;
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        copy = posts;
    }
    for (const Posted& post : copy) pipeline.OnResultMessage(post.w_param, post.l_param);
    for (int i = 0; i < 100 && refresh.IsRebuildInProgress(); ++i) Sleep(5);

    Sleep(600);
    pipeline.OnDebounceTimer();
    Sleep(600);
    pipeline.OnDebounceTimer();
    Expect(WaitForPosts(posts, posts_mutex, 6), "merged explicit rebuild starts once the gate opens");
    Expect(enumerations.load() == 6, "throttled explicit intent is not lost");
}

void TestChangeThrottle() {
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

    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Change);
    Sleep(550);
    pipeline.OnDebounceTimer();
    Expect(WaitForPosts(posts, posts_mutex, 1), "event burst starts one rebuild");
    Posted first{};
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        first = posts.front();
    }
    pipeline.OnResultMessage(first.w_param, first.l_param);
    for (int i = 0; i < 100 && refresh.IsRebuildInProgress(); ++i) Sleep(5);
    Expect(enumerations.load() == 1, "one event burst produces one rebuild");

    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Change);
    Sleep(550);
    pipeline.OnDebounceTimer();
    Expect(enumerations.load() == 1,
           "pulsed change inside the 1 s window starts no rebuild");
    Expect(scheduled == 3, "throttled event arms and re-arms the debounce timer");

    Sleep(1100);
    pipeline.OnDebounceTimer();
    Expect(WaitForPosts(posts, posts_mutex, 2), "throttled event starts once the gate opens");
    Expect(enumerations.load() == 2, "throttled change is not lost");

    Posted second{};
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        second = posts[1];
    }
    pipeline.OnResultMessage(second.w_param, second.l_param);
    for (int i = 0; i < 100 && refresh.IsRebuildInProgress(); ++i) Sleep(5);
    Sleep(1100);
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Change);
    Sleep(550);
    pipeline.OnDebounceTimer();
    Expect(WaitForPosts(posts, posts_mutex, 3),
           "change at least one second after the previous start starts a rebuild");
    Expect(enumerations.load() == 3, "each separated change starts its own rebuild");
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

void TestStartBoundedSupersedeAndFreshCancelFlag() {
    // NR-182: a second Explicit request while the first generation's worker is
    // stuck (uninterruptible Shell call) must wait at most kJoinTimeoutMs, then
    // detach the old worker and start the new generation with a fresh cancel
    // flag. The old worker keeps reading its own (set) flag and its late result
    // is dropped by the generation check; the new worker reads false.
    CatalogRefreshCoordinator refresh;
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<int> enumerations = 0;
    std::atomic<bool> new_worker_saw_false = false;
    std::atomic<bool> old_worker_saw_own_flag = false;
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
        [&](CatalogSource, const Settings&, std::atomic<bool>* cancel) {
            const int n = enumerations.fetch_add(1) + 1;
            if (n == 1) {
                // Simulates a worker stuck inside a single uninterruptible
                // Shell call: it never re-checks the cancel flag while blocked.
                WaitForSingleObject(gate, INFINITE);
                old_worker_saw_own_flag.store(cancel->load());
            } else {
                new_worker_saw_false.store(!cancel->load());
            }
            RebuildEnumeration result;
            AppEntry entry;
            entry.display_name = n == 1 ? L"stale-generation" : L"fresh-generation";
            entry.stable_id = L"start";
            entry.source = AppSource::UserStartMenu;
            entry.launch_verified = true;
            result.entries.push_back(entry);
            result.source_ok = true;
            return result;
        },
        [&] { ++completed; }, [] {}, [] {});
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Explicit);
    for (int i = 0; i < 200 && enumerations.load() != 1; ++i) Sleep(5);
    Expect(enumerations.load() == 1, "first generation worker started");
    // NR-183: a back-to-back explicit request now merges through the 1 s
    // per-source gate, so the supersede scenario has to wait the gate out
    // first; the first worker stays stuck on `gate` throughout.
    Sleep(1100);
    const auto begin = std::chrono::steady_clock::now();
    pipeline.Request({CatalogSource::StartMenu}, RebuildReason::Explicit);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin).count();
    Expect(elapsed >= 4000, "Start() waits at most kJoinTimeoutMs for the stuck worker");
    Expect(WaitForPosts(posts, posts_mutex, 1), "superseded generation posted a result");
    Expect(enumerations.load() == 2, "superseded generation started new workers");
    Expect(new_worker_saw_false.load(), "new generation worker reads a fresh cancel flag");
    SetEvent(gate);
    Expect(WaitForPosts(posts, posts_mutex, 2), "detached old worker posted its stale result");
    std::vector<Posted> copy;
    {
        std::lock_guard<std::mutex> lock(posts_mutex);
        copy = posts;
    }
    for (const Posted& post : copy) pipeline.OnResultMessage(post.w_param, post.l_param);
    Expect(completed == 1, "stale generation never completes the pipeline twice");
    Expect(refresh.Snapshot().front().display_name == L"fresh-generation",
           "stale detached worker's result is dropped by the generation check");
    Expect(old_worker_saw_own_flag.load(),
           "detached old worker kept reading its own set flag");
    CloseHandle(gate);
}

} // namespace

int wmain() {
    TestBackgroundPriorityAttemptIsNonFatal();
    TestDeliveryFailureCompletesOnce();
    TestThreadCreationFailureCompletes();
    TestCacheFailureRetention();
    TestForgedDeliveryFailureIgnored();
    TestFullRescanThrottleAndWatchMap();
    TestExplicitRefreshThrottled();
    TestChangeThrottle();
    TestShutdownBounded();
    TestStartBoundedSupersedeAndFreshCancelFlag();
    return 0;
}
