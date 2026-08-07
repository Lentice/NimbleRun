#pragma once

#include <string>

namespace nimblerun {

// NR-058: which per-user data files failed to load at startup. Bit flags,
// because several files can fail together while the user must see exactly one
// notification (design-spec §11: no chained prompts).
enum class StoreLoadIssue : unsigned {
    None    = 0,
    Corrupt = 1u << 0,  // file renamed aside with a .corrupt suffix; defaults used
    TooNew  = 1u << 1,  // written by a newer NimbleRun; original left untouched
};

// Aggregates the startup store-load results into one English balloon notice.
// Returns an empty string when nothing needs telling the user (all Loaded, or
// only Missing). Pure data transformation with no Win32 dependency, so the
// decision is unit-testable without a window (AGENTS.md: core logic stays
// independent of the UI layer).
inline std::wstring StoreLoadNoticeText(unsigned issues) {
    if (issues == 0) {
        return {};
    }
    std::wstring text;
    if ((issues & static_cast<unsigned>(StoreLoadIssue::Corrupt)) != 0) {
        text += L"Some settings could not be read and were reset. The original "
                L"files were kept next to them with a .corrupt suffix.";
    }
    if ((issues & static_cast<unsigned>(StoreLoadIssue::TooNew)) != 0) {
        if (!text.empty()) {
            text += L" ";
        }
        text += L"Some data files were written by a newer version of NimbleRun "
                L"and were not used. They were left unchanged.";
    }
    return text;
}

} // namespace nimblerun
