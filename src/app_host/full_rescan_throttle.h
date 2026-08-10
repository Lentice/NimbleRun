#pragma once

#include <cstdint>

namespace nimblerun {

// NR-130: rebuild-storm throttle. Any same-session process can PostMessage a
// full-rescan marker (kWatchChangedMessage with lParam != 0) at will; without a
// gate every marker forces an immediate full rebuild (design-spec §11). The
// host stamps the last accepted marker per source and routes a marker arriving
// within kFullRescanMinIntervalMs of it into the existing debounce path. A
// source with no accepted marker yet (kFullRescanNever) is always accepted.
// Pure decision: no HWND, no timer -- the host keeps the per-source state.
inline constexpr std::int64_t kFullRescanMinIntervalMs = 1000;
inline constexpr std::int64_t kFullRescanNever = -1;

inline bool ShouldAcceptFullRescan(std::int64_t last_accepted_ms, std::int64_t now_ms) {
    return last_accepted_ms == kFullRescanNever ||
           now_ms - last_accepted_ms >= kFullRescanMinIntervalMs;
}

} // namespace nimblerun
