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
    // NR-113: true when launch_identity was produced by a current source
    // enumeration; false for entries synthesized from catalog.cache, which is
    // a rebuildable accelerator, not a source of truth. Unverified entries stay
    // displayable and searchable but are rejected at the launch boundary until
    // a fresh enumeration produces the identity again. Never serialized into
    // the cache format; a reload always starts unverified.
    bool launch_verified = true;
};

} // namespace nimblerun
