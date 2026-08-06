#pragma once

#include <windows.h>
#include <objbase.h>

#include <memory>

namespace nimblerun {

// Per-thread COM lifetime. Previously duplicated verbatim in
// start_menu_catalog.cpp and appsfolder_catalog.cpp, both of which balanced
// only S_OK -- so a nested guard that got S_FALSE never called CoUninitialize
// and the apartment was never torn down. MSDN requires one CoUninitialize per
// successful CoInitializeEx *including* S_FALSE.
class ComGuard {
public:
    explicit ComGuard(DWORD flags = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) {
        const HRESULT hr = CoInitializeEx(nullptr, flags);
        // NR-051: SUCCEEDED covers both S_OK (this call initialized COM) and
        // S_FALSE (already initialized on this thread; the reference count went
        // up and must come back down). RPC_E_CHANGED_MODE means the thread is
        // already in a different apartment model -- COM is usable but this
        // guard did not add a reference, so it must not remove one.
        own_ = SUCCEEDED(hr);
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~ComGuard() {
        if (own_) {
            CoUninitialize();
        }
    }

    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;

    bool Usable() const { return usable_; }

private:
    bool own_ = false;
    bool usable_ = false;
};

// Release deleter for std::unique_ptr over a COM interface. Moved here from
// png_codec.cpp, which is where the shape was already correct. icon_worker.cpp
// could use this too for its own CoInitializeEx/CoUninitialize pair.
struct ComRelease {
    void operator()(IUnknown* ptr) const noexcept {
        if (ptr != nullptr) {
            ptr->Release();
        }
    }
};

template <typename T>
using ComPtr = std::unique_ptr<T, ComRelease>;

} // namespace nimblerun
