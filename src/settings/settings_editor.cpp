#include "settings/settings_editor.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr int kMinRecentCount = 8;
constexpr int kMaxRecentCount = 40;

bool AreEqualCaseInsensitive(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (towlower(static_cast<wint_t>(left[i])) !=
            towlower(static_cast<wint_t>(right[i]))) {
            return false;
        }
    }
    return true;
}

struct NamedKey {
    const wchar_t* name;
    UINT vk;
};

// Display names are canonical case; lookups compare case-insensitively.
constexpr NamedKey kNamedKeys[] = {
    {L"Space", VK_SPACE},     {L"Tab", VK_TAB},      {L"Enter", VK_RETURN},
    {L"Return", VK_RETURN},   {L"Esc", VK_ESCAPE},   {L"Escape", VK_ESCAPE},
    {L"Backspace", VK_BACK},  {L"Back", VK_BACK},    {L"Delete", VK_DELETE},
    {L"Insert", VK_INSERT},   {L"Home", VK_HOME},    {L"End", VK_END},
    {L"PageUp", VK_PRIOR},    {L"PageDown", VK_NEXT},{L"Up", VK_UP},
    {L"Down", VK_DOWN},       {L"Left", VK_LEFT},    {L"Right", VK_RIGHT},
};

std::wstring KeyName(UINT vk) {
    for (const NamedKey& named : kNamedKeys) {
        if (named.vk == vk) {
            return named.name;
        }
    }
    if (vk >= L'A' && vk <= L'Z') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= L'0' && vk <= L'9') {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(static_cast<int>(vk - VK_F1 + 1));
    }
    return {};
}

UINT KeyFromName(std::wstring_view name) {
    for (const NamedKey& named : kNamedKeys) {
        if (AreEqualCaseInsensitive(name, named.name)) {
            return named.vk;
        }
    }
    return 0;
}

int ParseFnNumber(std::wstring_view text) {
    int value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return -1;
        }
        value = value * 10 + (c - L'0');
        if (value > 24) {
            return -1;
        }
    }
    return value == 0 ? -1 : value;
}

// Lowercased, dot-prefixed, trimmed; used before allowlist membership checks.
std::wstring NormalizeExtension(std::wstring_view value) {
    std::wstring ext;
    ext.reserve(value.size() + 1);
    const std::wstring trimmed = Trim(value);
    if (!trimmed.empty() && trimmed.front() != L'.') {
        ext.push_back(L'.');
    }
    for (const wchar_t c : trimmed) {
        ext.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return ext;
}

} // namespace

std::wstring_view SettingsStringText(SettingsString key) {
    switch (key) {
    case SettingsString::DialogTitle:
        return L"NimbleRun Settings";
    case SettingsString::HotkeyGroup:
        return L"Global hotkey";
    case SettingsString::HotkeyLabel:
        return L"Shortcut:";
    case SettingsString::HotkeyHint:
        return L"Alt+Space, Ctrl+Alt+Space, ... Windows-key combos are rejected.";
    case SettingsString::LauncherGroup:
        return L"Launcher";
    case SettingsString::RecentCountLabel:
        return L"Recent apps to show (8-40):";
    case SettingsString::HideAfterLaunchLabel:
        return L"Hide after launching an app";
    case SettingsString::StartupAutoStartLabel:
        return L"Launch at startup";
    case SettingsString::ThemeLabel:
        return L"Theme:";
    case SettingsString::ThemeSystem:
        return L"Follow system";
    case SettingsString::ThemeLight:
        return L"Light";
    case SettingsString::ThemeDark:
        return L"Dark";
    case SettingsString::CatalogSourcesGroup:
        return L"Catalog sources";
    case SettingsString::IncludeWindowsAppsLabel:
        return L"Include Windows apps (Start Menu and installed apps)";
    case SettingsString::UserFoldersLabel:
        return L"User folders:";
    case SettingsString::AddFolderButton:
        return L"Add...";
    case SettingsString::RemoveFolderButton:
        return L"Remove";
    case SettingsString::BrowseFolderTitle:
        return L"Choose a local folder to scan";
    case SettingsString::IncludeSubfolders:
        return L"Include subfolders";
    case SettingsString::ExtensionsLabel:
        return L"Extensions to scan in user folders:";
    case SettingsString::ClearUsageButton:
        return L"Clear usage history";
    case SettingsString::ResetSettingsButton:
        return L"Reset settings";
    case SettingsString::OpenLogFolderButton:
        return L"Open log folder";
    case SettingsString::OkButton:
        return L"OK";
    case SettingsString::CancelButton:
        return L"Cancel";
    case SettingsString::HotkeyRejectedNotice:
        return L"The hotkey is invalid or already in use. The previous hotkey was kept.";
    case SettingsString::RecentCountNotice:
        return L"Recent apps must be between 8 and 40. The previous value was kept.";
    case SettingsString::SaveFailedNotice:
        return L"Could not save settings. The previous values were kept.";
    case SettingsString::FolderInvalidNotice:
        return L"Only local folder paths are accepted (for example C:\\Tools).";
    case SettingsString::ExtensionsNotice:
        return L"At least one extension must stay enabled.";
    case SettingsString::ClearUsageDoneNotice:
        return L"Usage history cleared.";
    case SettingsString::ResetDoneNotice:
        return L"Settings reset to defaults. Click OK to save.";
    case SettingsString::StartupFailedNotice:
        return L"Could not update the startup entry. The previous values were kept.";
    case SettingsString::OpenLogFolderFailedNotice:
        return L"Could not open the log folder.";
    }
    return L"";
}

bool ParseHotkey(std::wstring_view text, HotkeyBinding& out) {
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'+') {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2) {
        return false;  // need at least one modifier and one key
    }

    UINT modifiers = 0;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::wstring token = Trim(parts[i]);
        if (AreEqualCaseInsensitive(token, L"Ctrl") ||
            AreEqualCaseInsensitive(token, L"Control")) {
            modifiers |= MOD_CONTROL;
        } else if (AreEqualCaseInsensitive(token, L"Alt")) {
            modifiers |= MOD_ALT;
        } else if (AreEqualCaseInsensitive(token, L"Shift")) {
            modifiers |= MOD_SHIFT;
        } else if (AreEqualCaseInsensitive(token, L"Win") ||
                   AreEqualCaseInsensitive(token, L"Windows")) {
            return false;  // Windows-key combos are reserved (design-spec §4.1)
        } else {
            return false;
        }
    }
    if (modifiers == 0) {
        return false;
    }

    const std::wstring key = Trim(parts.back());
    UINT virtual_key = 0;
    if (key.size() == 1) {
        const wchar_t c = key[0];
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) {
            virtual_key = static_cast<UINT>(towupper(static_cast<wint_t>(c)));
        } else if (c >= L'0' && c <= L'9') {
            virtual_key = static_cast<UINT>(c);
        }
    } else {
        virtual_key = KeyFromName(key);
        if (virtual_key == 0 && key.size() >= 2 && (key[0] == L'f' || key[0] == L'F')) {
            const int fn = ParseFnNumber(std::wstring_view(key).substr(1));
            if (fn >= 1) {
                virtual_key = static_cast<UINT>(VK_F1 + (fn - 1));
            }
        }
    }
    if (virtual_key == 0) {
        return false;
    }

    // NR-086: shell-reserved combinations (task switching / Start menu).
    // RegisterHotKey never fails on these -- Alt+Tab is implemented by the
    // shell's own keyboard handling, not by the SAS reserved list -- so the
    // "reject when registration fails" guard (design-spec §4.1) cannot catch
    // them; letting Alt+Tab through would steal Windows' window switching
    // from a resident tray process with no clean way back. Only these three
    // shell combos are blocked: Ctrl+Alt+Del is rejected by the OS, Win
    // combos are rejected above, Alt+F4 is an app-level convention rather
    // than a shell reservation.
    if ((modifiers & ~MOD_NOREPEAT) == MOD_ALT &&
        (virtual_key == VK_TAB || virtual_key == VK_ESCAPE)) {
        return false;
    }
    if ((modifiers & ~MOD_NOREPEAT) == MOD_CONTROL && virtual_key == VK_ESCAPE) {
        return false;
    }

    out.modifiers = modifiers | MOD_NOREPEAT;
    out.virtual_key = virtual_key;
    return true;
}

std::wstring FormatHotkey(const HotkeyBinding& binding) {
    std::wstring out;
    const auto add = [&](const wchar_t* token) {
        if (!out.empty()) {
            out += L'+';
        }
        out += token;
    };
    if (binding.modifiers & MOD_CONTROL) {
        add(L"Ctrl");
    }
    if (binding.modifiers & MOD_ALT) {
        add(L"Alt");
    }
    if (binding.modifiers & MOD_SHIFT) {
        add(L"Shift");
    }
    const std::wstring key = KeyName(binding.virtual_key);
    if (!key.empty()) {
        if (!out.empty()) {
            out += L'+';
        }
        out += key;
    }
    return out;
}

SettingsEditor::SettingsEditor(const Settings& current)
    : original_(current), working_(current) {
}

bool SettingsEditor::SetRecentCount(int count) {
    if (count < kMinRecentCount || count > kMaxRecentCount) {
        return false;
    }
    if (count != working_.recent_count) {
        working_.recent_count = count;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetTheme(Theme theme) {
    if (theme != working_.theme) {
        working_.theme = theme;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetHideAfterLaunch(bool hide) {
    if (hide != working_.hide_after_launch) {
        working_.hide_after_launch = hide;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetIncludeWindowsApps(bool enabled) {
    if (enabled != working_.include_windows_apps) {
        working_.include_windows_apps = enabled;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetAutoStart(bool enabled) {
    if (enabled != working_.auto_start) {
        working_.auto_start = enabled;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetHotkey(std::wstring_view combo) {
    HotkeyBinding binding;
    if (!ParseHotkey(combo, binding)) {
        return false;  // empty/invalid/Win-key combos keep the previous value.
    }
    const std::wstring canonical = FormatHotkey(binding);
    if (canonical != working_.hotkey) {
        working_.hotkey = canonical;
        dirty_ = true;
    }
    return true;
}

bool SettingsEditor::SetExtensionEnabled(std::wstring_view extension, bool enabled) {
    const std::wstring ext = NormalizeExtension(extension);
    const std::vector<std::wstring>& allowlist = DefaultExtensions();
    if (std::find(allowlist.begin(), allowlist.end(), ext) == allowlist.end()) {
        return false;  // only the supported launchable extensions are accepted.
    }
    std::vector<std::wstring>& current = working_.catalog_extensions;
    const bool present = std::find(current.begin(), current.end(), ext) != current.end();
    if (present == enabled) {
        return true;
    }
    // ponytail: refuse to disable the last extension. settings.ini cannot
    // persist an empty allowlist (Load treats "no catalog_extension line" as
    // "defaults"), so "none selected" would silently snap back to all-defaults.
    if (!enabled && current.size() == 1) {
        return false;
    }
    if (enabled) {
        current.push_back(ext);
    } else {
        current.erase(std::remove(current.begin(), current.end(), ext), current.end());
    }
    dirty_ = true;
    return true;
}

bool SettingsEditor::AddRoot(std::wstring_view path, bool recursive) {
    if (!IsLocalAbsolutePath(path)) {
        return false;
    }
    const std::wstring normalized = Trim(path);
    for (const CatalogRoot& existing : working_.catalog_roots) {
        if (existing.path == normalized) {
            return false;  // no duplicate roots.
        }
    }
    CatalogRoot root;
    root.path = normalized;
    root.recursive = recursive;
    working_.catalog_roots.push_back(std::move(root));
    dirty_ = true;
    return true;
}

bool SettingsEditor::RemoveRoot(std::size_t index) {
    if (index >= working_.catalog_roots.size()) {
        return false;
    }
    working_.catalog_roots.erase(working_.catalog_roots.begin() + index);
    dirty_ = true;
    return true;
}

bool SettingsEditor::SetRootRecursive(std::size_t index, bool recursive) {
    if (index >= working_.catalog_roots.size()) {
        return false;
    }
    if (working_.catalog_roots[index].recursive != recursive) {
        working_.catalog_roots[index].recursive = recursive;
        dirty_ = true;
    }
    return true;
}

void SettingsEditor::ResetToDefaults() {
    working_ = DefaultSettings();
    dirty_ = true;
}

SettingsApplyResult SettingsEditor::Apply(SettingsStore& store, HotkeySwapper swapper) {
    SettingsApplyResult result;
    if (!dirty_) {
        return result;
    }

    const bool hotkey_changed = working_.hotkey != original_.hotkey;
    if (hotkey_changed) {
        HotkeyBinding binding;
        if (!ParseHotkey(working_.hotkey, binding)) {
            result.ok = false;
            result.hotkey_rejected = true;
            Rollback();
            return result;
        }
        // GlobalHotkey::Swap registers the proposed combo first and keeps the
        // old binding on failure, so a rejected combo never kills the working
        // hotkey (design-spec §FR-002). Persist only after the swap succeeds.
        const HotkeyResult swap = swapper(binding);
        if (!swap.success) {
            result.ok = false;
            result.hotkey_rejected = true;
            Rollback();
            return result;
        }
    }

    if (!store.Save(working_)) {
        result.ok = false;
        result.save_failed = true;
        // The running hotkey moved to the new combo; put the previous one back.
        if (hotkey_changed) {
            HotkeyBinding previous;
            if (ParseHotkey(original_.hotkey, previous)) {
                swapper(previous);
            }
        }
        Rollback();
        return result;
    }

    original_ = working_;
    dirty_ = false;
    return result;
}

void SettingsEditor::Rollback() {
    working_ = original_;
    dirty_ = false;
}

} // namespace nimblerun
