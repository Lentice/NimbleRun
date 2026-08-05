#pragma once

#include <cstdint>
#include <string>

namespace nimblerun {

enum class AppSource : std::uint8_t {
    UserStartMenu,
    CommonStartMenu,
    AppsFolder,
    UserFolder,
};

struct AppEntry {
    std::wstring stable_id;
    std::wstring display_name;
    std::wstring normalized_name;
    std::wstring launch_identity;
    std::wstring source_path;
    AppSource source = AppSource::UserStartMenu;
    bool is_pinned = false;
    int usage_score = 0;
};

} // namespace nimblerun
