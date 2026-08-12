#pragma once

#include "settings/settings_store.h"

#include <cstdint>

namespace nimblerun {
namespace palette {

// Opaque 0xRRGGBB color; D2D1::ColorF accepts this form directly. The model is
// pure (no HWND / GDI / Shell), so the host reads the OS and injects the system
// colors here instead.
using Rgb = std::uint32_t;

// System semantic colors (GetSysColor values converted to 0xRRGGBB). Defaults
// match the classic light high-contrast mapping so callers that skip injection
// still get a sane palette; they are only used when high_contrast is on.
struct SystemColors {
    Rgb window = 0xFFFFFF;
    Rgb window_text = 0x000000;
    Rgb highlight = 0x3399FF;
    Rgb highlight_text = 0xFFFFFF;
    Rgb gray_text = 0x808080;
};

struct PanelColors {
    Rgb background = 0;
    Rgb card = 0;
    Rgb input_fill = 0;    // NR-023: search box fill (and EDIT background)
    Rgb input_border = 0;  // NR-023: 1 DIP search box border
    Rgb selected_fill = 0;
    Rgb selected_border = 0;  // non-color selection signal (design-spec §NFR-006)
    Rgb pin_marker = 0;       // pinned marker color; shape, not color, carries the state (design-spec §NFR-006)
    Rgb hover_fill = 0;       // NR-029: grid hover cell fill (card-level)
    Rgb text = 0;
    Rgb dim = 0;
    Rgb error = 0;

    bool operator==(const PanelColors&) const = default;
};

// Maps the theme setting plus OS state to concrete panel colors. Never touches
// App Catalog data: the signature has no AppEntry, so theme/high-contrast state
// cannot change catalog or launch identity by construction. When high_contrast
// is on the injected system colors win over the custom light/dark palettes.
PanelColors ResolveColors(Theme setting, bool system_dark, bool high_contrast,
                          const SystemColors& system);

}  // namespace palette
}  // namespace nimblerun
