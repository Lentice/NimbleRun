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

} // namespace

bool RebuildPipeline::AcceptRebuildStart(std::int64_t last, std::int64_t now) {
    return last == kNoRebuildStart || now - last >= kRebuildStartMinIntervalMs;
}

RebuildPipeline::RebuildPipeline(CatalogRefreshCoordinator& refresh,
                                 SettingsSnapshot settings,
                                 PostToUi post_to_ui,
                                 EnumerateSource enumerate_source,
                                 Complete on_complete,
                                 Complete on_repaint,
                                 ScheduleDebounce schedule_debounce,
                                 Complete on_exception,
                                 ThreadFactory thread_factory)
    : refresh_(refresh),
      settings_(std::move(settings)),
      post_to_ui_(std::move(post_to_ui)),
      enumerate_source_(std::move(enumerate_source)),
      on_complete_(std::move(on_complete)),
      on_repaint_(std::move(on_repaint)),
      schedule_debounce_(std::move(schedule_debounce)),
      on_exception_(std::move(on_exception)),
      thread_factory_(thread_factory
                          ? std::move(thread_factory)
                          : [](std::function<void()> fn) {
                                return std::thread(std::move(fn));
                            }),
      failure_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      cancel_(std::make_shared<std::atomic<bool>>(false)) {
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
        // NR-147: throttle is enforced in OnDebounceTimer; events are never
        // dropped here.
        refresh_.NotifySourceEvent(source, now);
    }
    if (refresh_.ShouldStartRebuild(now)) {
        Start(refresh_.DueSources(now));
    } else if (schedule_debounce_) {
        schedule_debounce_();
    }
}

void RebuildPipeline::Start(std::vector<CatalogSource> sources) {
    // NR-182: bounded wait for the previous generation, mirroring the
    // WM_DESTROY path (NR-123): a worker stuck in an uninterruptible Shell
    // call is detached after kJoinTimeoutMs instead of hanging the UI thread.
    Shutdown(kJoinTimeoutMs);
    // NR-182: a shutdown timeout leaves the old cancel flag set (the detached
    // workers keep reading it and self-cancel). Swap in a fresh flag for this
    // generation so the new workers never observe it.
    const std::shared_ptr<std::atomic<bool>> cancel_flag =
        std::make_shared<std::atomic<bool>>(false);
    cancel_ = cancel_flag;
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
            workers_.push_back(thread_factory_(
                [this, generation, source, snapshot, cancel_flag]() {
                RebuildResult* result = nullptr;
                try {
                    result = new RebuildResult;
                    result->generation = generation;
                    result->source = source;
                    RebuildEnumeration enumeration =
                        enumerate_source_(source, snapshot, cancel_flag.get());
                    result->failed = !enumeration.source_ok;
                    // NR-173: the enumeration result is moved into
                    // RebuildResult; no copy is retained.
                    result->entries = std::move(enumeration.entries);
                    result->diagnostics = std::move(enumeration.diagnostics);
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
            }));
        } catch (...) {
            if (on_exception_) on_exception_();
            refresh_.ApplySourceFailure(generation, source);
            CompleteIfReady(generation);
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
    // NR-151: deliver through the handoff registry, same shape as
    // OnResultMessage, so a forged message can never name a source by
    // value. The failures_ entry stays: the drain path and the
    // failure_event_ fallback below still rely on it.
    bool posted_token = false;
    try {
        std::unique_ptr<RebuildResult> owned(new RebuildResult);
        owned->generation = generation;
        owned->source = source;
        owned->failed = true;
        const std::uintptr_t token = handoffs_.Register(std::move(owned));
        if (token) {
            if (post_to_ui_(kRebuildDeliveryFailedMessage,
                            static_cast<WPARAM>(generation),
                            static_cast<LPARAM>(token))) {
                posted_token = true;
            } else {
                handoffs_.Erase(token);
            }
        }
    } catch (...) {
        if (on_exception_) on_exception_();
    }
    if (recorded) {
        if (!posted_token && !post_to_ui_(kRebuildDeliveryFailedMessage, 0, 0) &&
            failure_event_) {
            SetEvent(failure_event_);
        }
    } else if (!posted_token && on_exception_) {
        // NR-160: register or post failure here means the OOM condition
        // persists. Give up -- no inline fallback, the receiver only
        // understands tokens -- and accept the stuck generation: the next
        // rebuild trigger cancels it via Shutdown.
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
    if (w_param != 0) {
        // NR-151: the payload is a handoff token, never a source value. A
        // forged message names no registered token (or a non-failure result)
        // and is dropped without any content effect.
        std::unique_ptr<RebuildResult> result =
            handoffs_.Take(static_cast<std::uintptr_t>(l_param));
        if (!result || !result->failed) {
            return 0;
        }
        if (refresh_.ApplySourceFailure(result->generation, result->source)) {
            CompleteIfReady(result->generation);
        }
        DrainFailures();
        if (on_repaint_) on_repaint_();
        return 0;
    }
    DrainFailures();
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
    cancel_->store(true);
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
        cancel_->store(false);
    } else {
        // NR-123: timeout gives up the join; TerminateThread is unsafe for
        // Shell/COM locks. The cancel flag stays set -- the detached workers
        // hold their own copy and self-cancel; the next Start() swaps in a
        // fresh per-generation flag (NR-182), so this generation never
        // poisons the next one.
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
