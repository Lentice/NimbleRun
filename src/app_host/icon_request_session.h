#pragma once

#include <set>
#include <string>
#include <vector>

namespace nimblerun {

// UI-thread-owned icon request state for one panel session. It deliberately
// knows only encoded keys; cache and worker ownership stay at the host edges.
class IconRequestSession {
public:
    bool ShouldRequest(const std::wstring& encoded_key, bool cached) const;
    void BeginRequest(const std::wstring& encoded_key);
    void OnResult(const std::wstring& encoded_key, bool ok);
    void OnShow();
    void DrainDropped(const std::vector<std::wstring>& dropped_keys);

private:
    std::set<std::wstring> pending_;
    std::set<std::wstring> requested_;
};

} // namespace nimblerun
