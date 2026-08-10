#pragma once

#include "catalog/catalog_refresh.h"
#include "settings/settings_store.h"
#include "win/handoff_registry.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimblerun {

struct RebuildResult {
    std::uint64_t generation = 0;
    CatalogSource source = CatalogSource::StartMenu;
    bool failed = false;
    std::vector<AppEntry> entries;
    GenerationDiagnostics diagnostics;
};

struct RebuildEnumeration {
    std::vector<AppEntry> entries;
    GenerationDiagnostics diagnostics;
    bool source_ok = true;
};

struct RebuildWatchSource {
    std::wstring path;
    bool recursive = true;
    CatalogSource source = CatalogSource::StartMenu;
};

enum class RebuildReason { Explicit, FullRescan, Change };

class RebuildPipeline {
public:
    // NR-145: join timeout for Shutdown(), so a worker stuck in an
    // uninterruptible Shell call never hangs shutdown (design-spec §9.4).
    // Promoted from private so the call site can reference it instead of
    // hardcoding the same value.
    static constexpr DWORD kJoinTimeoutMs = 5000;

    using PostToUi = std::function<bool(UINT, WPARAM, LPARAM)>;
    using EnumerateSource = std::function<RebuildEnumeration(
        CatalogSource, const Settings&, std::atomic<bool>*)>;
    using SettingsSnapshot = std::function<Settings()>;
    using Complete = std::function<void()>;
    using ScheduleDebounce = std::function<void()>;

    RebuildPipeline(CatalogRefreshCoordinator& refresh,
                    SettingsSnapshot settings,
                    PostToUi post_to_ui,
                    EnumerateSource enumerate_source,
                    Complete on_complete,
                    Complete on_repaint,
                    ScheduleDebounce schedule_debounce,
                    Complete on_exception = {});
    ~RebuildPipeline();

    RebuildPipeline(const RebuildPipeline&) = delete;
    RebuildPipeline& operator=(const RebuildPipeline&) = delete;

    void Request(std::vector<CatalogSource> sources, RebuildReason reason);
    LRESULT OnResultMessage(WPARAM w_param, LPARAM l_param);
    LRESULT OnDeliveryFailureMessage(WPARAM w_param, LPARAM l_param);
    void OnDebounceTimer();
    void DrainPending();
    // NR-146: returns true when the workers joined cleanly, false when the
    // bounded wait timed out and the workers were detached. A false return
    // means the caller must keep the object alive (detached workers still
    // touch members); the destructor and Start() call with INFINITE, which
    // never detaches, and ignore the result.
    bool Shutdown(DWORD timeout_ms = INFINITE);

    HANDLE FailureEvent() const { return failure_event_; }
    void SetCacheWritesDisabled(bool disabled) { cache_writes_disabled_ = disabled; }
    bool CacheWritesDisabled() const { return cache_writes_disabled_; }
    void SetWatchSources(std::vector<RebuildWatchSource> sources) {
        watch_sources_ = std::move(sources);
    }
    std::optional<CatalogSource> SourceForIndex(int index) const;

private:
    static constexpr std::int64_t kFullRescanMinIntervalMs = 1000;
    static constexpr std::int64_t kFullRescanNever = -1;
    static constexpr UINT kRebuildDoneMessage = WM_APP + 8;
    static constexpr UINT kRebuildDeliveryFailedMessage = WM_APP + 10;

    void Start(std::vector<CatalogSource> sources);
    void QueueFailure(std::uint64_t generation, CatalogSource source);
    void CompleteIfReady(std::uint64_t generation);
    bool DrainFailures();

    CatalogRefreshCoordinator& refresh_;
    SettingsSnapshot settings_;
    PostToUi post_to_ui_;
    EnumerateSource enumerate_source_;
    Complete on_complete_;
    Complete on_repaint_;
    ScheduleDebounce schedule_debounce_;
    Complete on_exception_;
    HandoffRegistry<RebuildResult> handoffs_;
    std::mutex failure_mutex_;
    std::vector<std::pair<std::uint64_t, CatalogSource>> failures_;
    HANDLE failure_event_ = nullptr;
    std::vector<std::thread> workers_;
    std::atomic<bool> cancel_{false};
    std::unordered_map<CatalogSource, std::int64_t> last_full_rescan_ms_;
    std::vector<RebuildWatchSource> watch_sources_;
    std::uint64_t completed_generation_ = 0;
    bool cache_writes_disabled_ = false;
};

} // namespace nimblerun
