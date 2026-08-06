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
    // NR-047: secondary search key -- the resolved target's file stem for a
    // Start Menu shortcut, or the AUMID's package-family part for a packaged
    // app. Empty when the source cannot supply one. Stored raw by the
    // enumerators and normalized once in CatalogRefreshCoordinator::SetSnapshot,
    // exactly like normalized_name. Never part of the stable id (design-spec
    // §10.3) and never part of dedup.
    std::wstring search_alias;
};

} // namespace nimblerun
