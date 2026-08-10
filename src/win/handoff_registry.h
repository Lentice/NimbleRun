#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace nimblerun {

// WM_APP payloads carry only an address token. The registry validates the
// token before transferring ownership, so an unknown token is harmless.
template <typename T>
class HandoffRegistry {
public:
    std::uintptr_t Register(std::unique_ptr<T> value) {
        if (!value) {
            return 0;
        }
        const auto token = reinterpret_cast<std::uintptr_t>(value.get());
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            const auto [it, inserted] = map_.emplace(token, std::move(value));
            return inserted ? token : 0;
        } catch (...) {
            return 0;
        }
    }

    std::unique_ptr<T> Take(std::uintptr_t token) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = map_.find(token);
        if (it == map_.end()) {
            return nullptr;
        }
        auto value = std::move(it->second);
        map_.erase(it);
        return value;
    }

    void Erase(std::uintptr_t token) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.erase(token);
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.empty();
    }

    template <typename Pred>
    void EraseIf(Pred pred) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = map_.begin(); it != map_.end();) {
            if (pred(*it->second)) {
                it = map_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uintptr_t, std::unique_ptr<T>> map_;
};

} // namespace nimblerun
