#include "ui/panel_palette.h"

namespace nimblerun {
namespace palette {

PanelColors ResolveColors(Theme setting, bool system_dark, bool high_contrast,
                          const SystemColors& system) {
    if (high_contrast) {
        // System semantic colors (COLOR_WINDOW / COLOR_HIGHLIGHT / ...): the
        // row cards collapse onto the window background and the selection
        // border uses the highlight text color so it stays visible on the
        // highlight fill.
        PanelColors colors;
        colors.background = system.window;
        colors.card = system.window;
        colors.selected_fill = system.highlight;
        colors.selected_border = system.highlight_text;
        colors.hover_fill = system.highlight;
        colors.text = system.window_text;
        colors.dim = system.gray_text;
        colors.error = system.window_text;
        colors.input_fill = system.window;
        colors.input_border = system.window_text;
        return colors;
    }

    const bool dark =
        setting == Theme::Dark || (setting == Theme::System && system_dark);
    if (dark) {
        return {0x181818, 0x2B2B2B, 0x2B2B2B, 0x3C3C3C, 0x3A5A8C, 0x8FB3D9,
                0x2B2B2B, 0xD0D0D0, 0x707070, 0xE08070};
    }
    return {0xF3F3F3, 0xFFFFFF, 0xFFFFFF, 0xE0E0E0, 0xBFD9F2, 0x2E6DB4,
            0xFFFFFF, 0x1A1A1A, 0x666666, 0xB03020};
}

}  // namespace palette
}  // namespace nimblerun
