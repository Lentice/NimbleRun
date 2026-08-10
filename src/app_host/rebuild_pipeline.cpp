#include "app_host/rebuild_pipeline.h"

#include "catalog/appsfolder_catalog.h"
#include "catalog/start_menu_catalog.h"
#include "catalog/user_folder_catalog.h"

#include <algorithm>
#include <limits>

namespace nimblerun {
namespace {

std::int64_t NowMs() {
    return static_cast<std::int64_t>(GetTickCount64());
}

bool AcceptRebuildStart(std::int64_t last, std::int64_t now) {
    return last == -1 || now - last >= 1000;
}

} // namespace

RebuildPipeline::RebuildPipeline(CatalogRefreshCoordinator& refresh,
                                 SettingsSnapshot settings,
                                 PostToUi post_to_ui,
                                 EnumerateSource enumerate_source,
                                 Complete on_complete,
                                 Complete on_repaint,
                                 ScheduleDebounce schedule_debounce,
                                 Complete on_exception)
    : refresh_(refresh),
      settings_(std::move(settings)),
      post_to_ui_(std::move(post_to_ui)),
      enumerate_source_(std::move(enumerate_source)),
      on_complete_(std::move(on_complete)),
      on_repaint_(std::move(on_repaint)),
      schedule_debounce_(std::move(schedule_debounce)),
      on_exception_(std::move(on_exception)),
      failure_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
}

RebuildPipeline::~RebuildPipeline() {
    Shutdown();
    if (failure_event_) {
        CloseHandle(failure_event_);
    }
}

std::optional<CatalogSource> RebuildPipeline::SourceForIndex(int index) const {
    if (index < 1 || index > static_cast<int>(watch_sources_.size())) {
        return std::nullopt;
    }
    return watch_sources_[static_cast<std::size_t>(index - 1)].source;
}

void RebuildPipeline::Request(std::vector<CatalogSource> sources, RebuildReason reason) {
    if (sources.empty()) {
        return;
    }
    const std::int64_t now = NowMs();
    if (reason == RebuildReason::Explicit) {
        Start(std::move(sources));
        return;
    }
    if (reason == RebuildReason::FullRescan) {
        const CatalogSource source = sources.front();
        const auto it = last_rebuild_start_ms_.find(source);
        const std::int64_t last = it == last_rebuild_start_ms_.end()
                                      ? kNoRebuildStart
                                      : it->second;
        if (AcceptRebuildStart(last, now)) {
            last_rebuild_start_ms_[source] = now;
            refresh_.MarkSourceFullRescan(source);
        } else {
            refresh_.NotifySourceEvent(source, now);
        }
    } else {
        const CatalogSource source = sources.front();
        const auto it = last_rebuild_start_ms_.find(source);
        const std::int64_t last = it == last_rebuild_start_ms_.end()
                                      ? kNoRebuildStart
                                      : it->second;
        if (AcceptRebuildStart(last, now)) {
            refresh_.NotifySourceEvent(source, now);
        } else {
            // NR-147: throttled Change event -- never dropped. The source stays
            // pending and the debounce timer is armed below; OnDebounceTimer
            // starts the rebuild once the per-source 1 s gate opens.
            refresh_.NotifySourceEvent(source, now);
        }
    }
    if (refresh_.ShouldStartRebuild(now)) {
        Start(refresh_.DueSources(now));
    } else if (schedule_debounce_) {
        schedule_debounce_();
    }
}

void RebuildPipeline::Start(std::vector<CatalogSource> sources) {
    Shutdown();
    const std::uint64_t generation = refresh_.BeginGeneration(sources);
    Settings snapshot;
    try {
        snapshot = settings_ ? settings_() : Settings{};
        workers_.reserve(workers_.size() + sources.size());
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failures_.reserve(failures_.size() + sources.size());
    } catch (...) {
        if (on_exception_) on_exception_();
        for (const CatalogSource source : sources) {
            refresh_.ApplySourceFailure(generation, source);
        }
        CompleteIfReady(generation);
        return;
    }
    for (const CatalogSource source : sources) {
        try {
            workers_.emplace_back([this, generation, source, snapshot]() {
                RebuildResult* result = nullptr;
                try {
                    result = new RebuildResult;
                    result->generation = generation;
                    result->source = source;
                    const RebuildEnumeration enumeration =
                        enumerate_source_(source, snapshot, &cancel_);
                    result->failed = !enumeration.source_ok;
                    result->entries = enumeration.entries;
                    result->diagnostics = enumeration.diagnostics;
                } catch (...) {
                    if (!result) {
                        if (on_exception_) on_exception_();
                        QueueFailure(generation, source);
                        return;
                    }
                    result->failed = true;
                    if (on_exception_) on_exception_();
                }
                std::unique_ptr<RebuildResult> owned(result);
                const std::uintptr_t token = handoffs_.Register(std::move(owned));
                if (!token) {
                    if (on_exception_) on_exception_();
                    QueueFailure(generation, source);
                    return;
                }
                if (!post_to_ui_(kRebuildDoneMessage,
                                 static_cast<WPARAM>(generation),
                                 static_cast<LPARAM>(token))) {
                    handoffs_.Erase(token);
                    QueueFailure(generation, source);
                }
            });
        } catch (...) {
            if (on_exception_) on_exception_();
            refresh_.ApplySourceFailure(generation, source);
        }
    }
}

void RebuildPipeline::QueueFailure(std::uint64_t generation, CatalogSource source) {
    bool recorded = false;
    try {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failures_.emplace_back(generation, source);
        recorded = true;
    } catch (...) {
        if (on_exception_) on_exception_();
    }
    if (recorded) {
        if (!post_to_ui_(kRebuildDeliveryFailedMessage, 0, 0) && failure_event_) {
            SetEvent(failure_event_);
        }
    } else if (!post_to_ui_(kRebuildDeliveryFailedMessage,
                            static_cast<WPARAM>(generation),
                            static_cast<LPARAM>(source)) && on_exception_) {
        on_exception_();
    }
}

void RebuildPipeline::CompleteIfReady(std::uint64_t generation) {
    if (refresh_.GenerationComplete(generation) &&
        completed_generation_ != generation) {
        completed_generation_ = generation;
        if (on_complete_) on_complete_();
    }
}

bool RebuildPipeline::DrainFailures() {
    std::vector<std::pair<std::uint64_t, CatalogSource>> failures;
    {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failures.swap(failures_);
    }
    bool applied = false;
    for (const auto& [generation, source] : failures) {
        applied |= refresh_.ApplySourceFailure(generation, source);
        CompleteIfReady(generation);
    }
    if (failure_event_) {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        if (failures_.empty()) ResetEvent(failure_event_);
    }
    return applied;
}

LRESULT RebuildPipeline::OnResultMessage(WPARAM, LPARAM l_param) {
    std::unique_ptr<RebuildResult> result =
        handoffs_.Take(static_cast<std::uintptr_t>(l_param));
    if (!result) return 0;
    bool applied = false;
    if (result->failed) {
        applied = refresh_.ApplySourceFailure(result->generation, result->source);
    } else {
        applied = refresh_.ApplySourceResult(result->generation, result->source,
                                             std::move(result->entries),
                                             result->diagnostics);
        if (applied && result->source == CatalogSource::AppsFolder) {
            refresh_.RecordAppsFolderSuccess(NowMs());
        }
    }
    if (applied) CompleteIfReady(result->generation);
    DrainFailures();
    if (on_repaint_) on_repaint_();
    return 0;
}

LRESULT RebuildPipeline::OnDeliveryFailureMessage(WPARAM w_param, LPARAM l_param) {
    if (w_param != 0 && static_cast<std::uintptr_t>(l_param) <=
                            static_cast<std::uintptr_t>(CatalogSource::UserFolder)) {
        const auto generation = static_cast<std::uint64_t>(w_param);
        const auto source = static_cast<CatalogSource>(l_param);
        refresh_.ApplySourceFailure(generation, source);
        CompleteIfReady(generation);
    } else {
        DrainFailures();
    }
    if (on_repaint_) on_repaint_();
    return 0;
}

void RebuildPipeline::OnDebounceTimer() {
    const std::int64_t now = NowMs();
    const std::vector<CatalogSource> due = refresh_.DueSources(now);
    std::vector<CatalogSource> to_start;
    for (const CatalogSource source : due) {
        const auto it = last_rebuild_start_ms_.find(source);
        const std::int64_t last = it == last_rebuild_start_ms_.end()
                                      ? kNoRebuildStart
                                      : it->second;
        if (AcceptRebuildStart(last, now)) {
            // NR-147: the gate is a rebuild-start gate, so record when the
            // rebuild actually starts, not when the event was accepted.
            last_rebuild_start_ms_[source] = now;
            to_start.push_back(source);
        }
    }
    if (!to_start.empty() && !refresh_.IsRebuildInProgress() &&
        to_start.size() == due.size()) {
        Start(std::move(to_start));
        return;
    }
    // Re-arm while any due source was not started: a rebuild is already
    // running, or a per-source gate (NR-147) is still closed -- the next tick
    // picks the pending source up once the gate opens.
    if (schedule_debounce_ && !due.empty()) {
        schedule_debounce_();
    }
}

void RebuildPipeline::DrainPending() {
    DrainFailures();
    if (on_repaint_) on_repaint_();
}

bool RebuildPipeline::Shutdown(DWORD timeout_ms) {
    cancel_.store(true);
    bool finished = true;
    if (timeout_ms != INFINITE) {
        std::vector<HANDLE> handles;
        for (std::thread& worker : workers_) {
            if (worker.joinable()) handles.push_back(worker.native_handle());
        }
        if (!handles.empty()) {
            finished = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                              handles.data(), TRUE, timeout_ms) == WAIT_OBJECT_0;
        }
    }
    if (finished) {
        for (std::thread& worker : workers_) if (worker.joinable()) worker.join();
        workers_.clear();
        cancel_.store(false);
    } else {
        // NR-123: timeout gives up the join; TerminateThread is unsafe for Shell/COM locks.
        for (std::thread& worker : workers_) if (worker.joinable()) worker.detach();
        workers_.clear();
    }
    handoffs_.Clear();
    {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failures_.clear();
        if (failure_event_) ResetEvent(failure_event_);
    }
    return finished;
}

} // namespace nimblerun
