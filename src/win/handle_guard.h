#pragma once

#include <windows.h>

namespace nimblerun {

class HandleGuard {
public:
    explicit HandleGuard(HANDLE handle = nullptr) : handle_(handle) {}

    ~HandleGuard() noexcept {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    HANDLE Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HANDLE handle_ = nullptr;
};

} // namespace nimblerun
