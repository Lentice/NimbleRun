#pragma once

#include "icons/icon_cache.h"

namespace nimblerun {

// Real icon provider backed by the Windows Shell (design-spec §FR-009):
// SHCreateItemFromParsingName + IShellItemImageFactory::GetImage, which covers
// both filesystem identities (Start Menu / user-folder paths) and AppsFolder
// parsing names. Requires the calling thread to have initialized STA COM, as
// the UI thread does. Any failure returns an empty IconBitmap so the caller
// keeps its fallback; a failure never throws or takes down the list.
class ShellIconProvider final : public IconProvider {
public:
    IconBitmap Load(const AppEntry& entry, const IconKey& key) override;
};

} // namespace nimblerun
