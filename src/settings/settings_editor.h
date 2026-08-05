#pragma once

#include "app_host/hotkey.h"          // HotkeyBinding / HotkeyResult (plain structs)
#include "settings/settings_store.h"  // Settings, Theme, CatalogRoot, SettingsStore

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nimblerun {

// Centralized English UI strings for the settings dialog (AGENTS.md: centralize
// strings when more than one screen needs them). Keys are stable ids; the
// dialog looks text up by key. The settings model test pins a few keys.
enum class SettingsString {
    DialogTitle,
    HotkeyGroup,
    HotkeyLabel,
    HotkeyHint,
    LauncherGroup,
    RecentCountLabel,
    HideAfterLaunchLabel,
    StartupAutoStartLabel,
    ThemeLabel,
    ThemeSystem,
    ThemeLight,
    ThemeDark,
    CatalogSourcesGroup,
    IncludeWindowsAppsLabel,
    UserFoldersLabel,
    AddFolderButton,
    RemoveFolderButton,
    BrowseFolderTitle,
    IncludeSubfolders,
    ExtensionsLabel,
    ClearUsageButton,
    ResetSettingsButton,
    OkButton,
    CancelButton,
    HotkeyRejectedNotice,
    RecentCountNotice,
    SaveFailedNotice,
    FolderInvalidNotice,
    ExtensionsNotice,
    ClearUsageDoneNotice,
    ResetDoneNotice,
    StartupFailedNotice,
};

std::wstring_view SettingsStringText(SettingsString key);

// Hotkey combo string <-> HotkeyBinding. The string is canonical
// ("Alt+Space", "Ctrl+Alt+Space", ...). Parsing rejects empty combos, combos
// without any Ctrl/Alt/Shift modifier, and combos containing the Windows key
// (design-spec §4.1: Windows-key combos are reserved and never used by
// NimbleRun).
bool ParseHotkey(std::wstring_view text, HotkeyBinding& out);
std::wstring FormatHotkey(const HotkeyBinding& binding);

// Seam for the running global-hotkey swap. The host passes a functor wrapping
// GlobalHotkey::Swap; tests inject a fake so the model stays HWND-free.
using HotkeySwapper = std::function<HotkeyResult(const HotkeyBinding&)>;

// Result of SettingsEditor::Apply.
struct SettingsApplyResult {
    bool ok = true;
    bool hotkey_rejected = false;  // hotkey swap failed; nothing changed
    bool save_failed = false;      // swap ok but save failed; hotkey rolled back
};

// Pure settings-edit model (NR-013). Holds a working copy of Settings plus the
// string-key table, validates every input, tracks dirtiness, and Apply()s
// through a SettingsStore + hotkey seam with rollback so a failure never
// destroys the previous values (design-spec §FR-013, §11). No HWND/Shell
// dependencies; the Win32 dialog calls into this model.
class SettingsEditor {
public:
    explicit SettingsEditor(const Settings& current);

    const Settings& Working() const { return working_; }
    bool Dirty() const { return dirty_; }

    // Typed setters with validation. False means the input was rejected and
    // the working value is left unchanged.
    bool SetRecentCount(int count);             // only 8..40 (default 20)
    bool SetTheme(Theme theme);
    bool SetHideAfterLaunch(bool hide);
    bool SetIncludeWindowsApps(bool enabled);
    bool SetAutoStart(bool enabled);
    bool SetHotkey(std::wstring_view combo);    // empty/invalid/Win-key rejected
    bool SetExtensionEnabled(std::wstring_view extension, bool enabled);
    bool AddRoot(std::wstring_view path, bool recursive);   // local absolute paths only
    bool RemoveRoot(std::size_t index);
    bool SetRootRecursive(std::size_t index, bool recursive);

    // Working copy becomes DefaultSettings() (including clearing catalog_roots)
    // and is marked dirty; Apply() persists it. "Reset settings" restores
    // defaults, it never touches usage or the catalog source.
    void ResetToDefaults();

    // Applies the working copy: the hotkey is swapped first (register-new-first
    // keeps the old binding on failure) and only persisted when the swap
    // succeeds. On any failure the working copy, the running hotkey, and the
    // stored settings are all rolled back to the previous values.
    SettingsApplyResult Apply(SettingsStore& store, HotkeySwapper swapper);

private:
    void Rollback();

    Settings original_;
    Settings working_;
    bool dirty_ = false;
};

} // namespace nimblerun
