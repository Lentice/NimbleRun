#include "app_host/icon_request_session.h"

namespace nimblerun {

bool IconRequestSession::ShouldRequest(const std::wstring& encoded_key,
                                       bool cached) const {
    return !cached && pending_.count(encoded_key) == 0 &&
           requested_.count(encoded_key) == 0;
}

void IconRequestSession::BeginRequest(const std::wstring& encoded_key) {
    pending_.insert(encoded_key);
}

void IconRequestSession::OnResult(const std::wstring& encoded_key, bool ok) {
    pending_.erase(encoded_key);
    if (!ok) {
        requested_.insert(encoded_key);
    }
}

void IconRequestSession::OnShow() {
    requested_.clear();
}

void IconRequestSession::DrainDropped(
    const std::vector<std::wstring>& dropped_keys) {
    for (const std::wstring& key : dropped_keys) {
        pending_.erase(key);
    }
}

} // namespace nimblerun
