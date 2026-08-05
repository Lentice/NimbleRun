#include "icons/icon_worker.h"

#include <windows.h>
#include <objbase.h>

#include <utility>

namespace nimblerun {

IconWorker::IconWorker(HWND target, UINT result_message, IconProvider& provider)
    : target_(target), result_message_(result_message), provider_(provider) {
}

IconWorker::~IconWorker() {
    Stop();
}

void IconWorker::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
        return;  // already running
    }
    stop_ = false;
    thread_ = std::thread(&IconWorker::Run, this);
}

void IconWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    thread_ = std::thread();
}

void IconWorker::Post(IconRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;  // stopped or never started: drop
        }
        if (request.visible) {
            queue_.push_front(std::move(request));
        } else {
            queue_.push_back(std::move(request));
        }
    }
    cv_.notify_one();
}

void IconWorker::Run() {
    // The worker owns Shell COM on its own thread; it never depends on the UI
    // thread's initialization.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        IconRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
            if (stop_) {
                break;
            }
            request = std::move(queue_.front());
            queue_.pop_front();
        }

        auto* result = new IconResult;
        result->encoded_key = request.key.Encode();
        result->bitmap = provider_.Load(request.entry, request.key);
        if (!PostMessageW(target_, result_message_, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;  // window gone: never leak the handoff
        }
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

} // namespace nimblerun
