// Focused check for NR-013 (settings UI integration).
//
// Drives the pure settings-edit model (SettingsEditor) directly: input
// validation, dirty tracking, Apply persist + rollback through a temp-dir
// SettingsStore and an injectable hotkey-swap seam, reset/clear-usage scoping,
// and the centralized English string keys. No dialog clicks are required; the
// model is what the dialog calls into.

#include "test_util.h"

#include "app_host/hotkey.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "usage/usage_store.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::CatalogRoot;
using nimblerun::DefaultSettings;
using nimblerun::FormatHotkey;
using nimblerun::HotkeyBinding;
using nimblerun::HotkeyResult;
using nimblerun::ParseHotkey;
using nimblerun::Settings;
using nimblerun::SettingsApplyResult;
using nimblerun::SettingsEditor;
using nimblerun::SettingsStore;
using nimblerun::SettingsString;
using nimblerun::SettingsStringText;
using nimblerun::Theme;
using nimblerun::UsageStore;

namespace {

std::wstring MakeTempDir(const char* label) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_settings_ui_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::wstring(label, label + std::char_traits<char>::length(label));
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(fs::path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool SameBinding(const HotkeyBinding& a, const HotkeyBinding& b) {
    return a.modifiers == b.modifiers && a.virtual_key == b.virtual_key;
}

bool SameRoot(const CatalogRoot& a, const CatalogRoot& b) {
    return a.path == b.path && a.recursive == b.recursive;
}

bool SameSettings(const Settings& a, const Settings& b) {
    if (a.hotkey != b.hotkey || a.auto_start != b.auto_start || a.theme != b.theme ||
        a.recent_count != b.recent_count || a.hide_after_launch != b.hide_after_launch ||
        a.include_windows_apps != b.include_windows_apps ||
        a.catalog_roots.size() != b.catalog_roots.size() ||
        a.catalog_extensions != b.catalog_extensions) {
        return false;
    }
    for (std::size_t i = 0; i < a.catalog_roots.size(); ++i) {
        if (!SameRoot(a.catalog_roots[i], b.catalog_roots[i])) {
            return false;
        }
    }
    return true;
}

// Injectable hotkey-swap seam. The real host wraps GlobalHotkey::Swap; tests
// record the proposed bindings and can simulate an OS rejection. The call log
// is shared state because Apply() takes the seam by value as a std::function.
struct FakeSwapper {
    std::shared_ptr<std::vector<HotkeyBinding>> calls =
        std::make_shared<std::vector<HotkeyBinding>>();
    bool fail = false;

    HotkeyResult operator()(const HotkeyBinding& binding) {
        calls->push_back(binding);
        HotkeyResult result;
        result.success = !fail;
        result.error = fail ? ERROR_HOTKEY_ALREADY_REGISTERED : ERROR_SUCCESS;
        return result;
    }
};

void TestRecentCountValidation() {
    SettingsEditor editor(DefaultSettings());
    Expect(!editor.Dirty(), "fresh editor is not dirty");
    Expect(editor.Working().recent_count == 20, "default recent_count is 20");
    Expect(editor.SetRecentCount(7) == false, "below 8 rejected");
    Expect(editor.SetRecentCount(41) == false, "above 40 rejected");
    Expect(editor.Working().recent_count == 20, "rejected values leave the default");
    Expect(editor.SetRecentCount(8) == true, "8 accepted (boundary)");
    Expect(editor.SetRecentCount(40) == true, "40 accepted (boundary)");
    Expect(editor.SetRecentCount(30) == true, "30 accepted");
    Expect(editor.Working().recent_count == 30, "accepted value applied");
    Expect(editor.Dirty(), "edit marks the editor dirty");
}

void TestExtensionAllowlist() {
    Settings custom = DefaultSettings();
    custom.catalog_extensions = {L".exe"};
    SettingsEditor editor(custom);
    Expect(editor.SetExtensionEnabled(L".txt", true) == false, "non-allowlist rejected");
    Expect(editor.SetExtensionEnabled(L".exe", false) == false,
           "last enabled extension cannot be disabled");
    Expect(editor.Working().catalog_extensions.size() == 1, "rejected toggles leave the list");
    Expect(editor.SetExtensionEnabled(L".CMD", true) == true, "allowlist add is case-insensitive");
    Expect(editor.SetExtensionEnabled(L"lnk", true) == true, "dot-less extension is normalized");
    Expect(editor.SetExtensionEnabled(L".exe", false) == true, "exe removable once more enabled");
    const std::vector<std::wstring> expected = {L".cmd", L".lnk"};
    Expect(editor.Working().catalog_extensions == expected, "enabled list matches the toggles");
}

void TestDirtyTrackingAndPersist() {
    const std::wstring dir = MakeTempDir("persist");
    SettingsStore store(dir);

    SettingsEditor editor(DefaultSettings());
    Expect(!editor.Dirty(), "not dirty before edits");
    Expect(editor.SetRecentCount(33) == true, "set recent_count");
    Expect(editor.SetHideAfterLaunch(false) == true, "uncheck hide-after-launch");
    Expect(editor.SetIncludeWindowsApps(false) == true, "uncheck include-windows-apps");
    Expect(editor.SetTheme(Theme::Dark) == true, "set theme");
    Expect(editor.SetHotkey(L"Ctrl+Shift+P") == true, "set hotkey");
    Expect(editor.Dirty(), "dirty after edits");

    FakeSwapper swapper;
    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(result.ok, "apply succeeds for valid edits");
    Expect(swapper.calls->size() == 1, "hotkey swapped once");
    HotkeyBinding expected_hotkey{};
    Expect(ParseHotkey(L"Ctrl+Shift+P", expected_hotkey), "reference combo parses");
    Expect(SameBinding(swapper.calls->at(0), expected_hotkey), "swapped to the new combo");
    Expect(!editor.Dirty(), "apply clears the dirty flag");

    Settings loaded;
    Expect(store.Load(loaded) == nimblerun::SettingsLoadResult::Loaded, "round-trip load");
    Expect(loaded.recent_count == 33, "round-trip recent_count");
    Expect(loaded.hide_after_launch == false, "round-trip hide_after_launch");
    Expect(loaded.include_windows_apps == false, "round-trip include_windows_apps");
    Expect(loaded.theme == Theme::Dark, "round-trip theme");
    Expect(loaded.hotkey == L"Ctrl+Shift+P", "round-trip hotkey is canonical");

    // NR-088: a Win-key combo goes through the same edit -> swap -> persist ->
    // reload path and comes back canonically (needed by NR-089's capture
    // dialog, which must save/restore Win combos correctly).
    Expect(editor.SetHotkey(L"Ctrl+Win+E") == true, "set a Win-key hotkey");
    Expect(editor.Apply(store, swapper).ok, "apply the Win-key hotkey");
    Expect(swapper.calls->size() == 2, "the Win-key hotkey was swapped in");
    Settings reloaded;
    Expect(store.Load(reloaded) == nimblerun::SettingsLoadResult::Loaded, "reload after Win-key save");
    Expect(reloaded.hotkey == L"Ctrl+Win+E", "Win-key hotkey round-trips canonically");
    fs::remove_all(dir);
}

void TestApplyRollbackOnSaveFailure() {
    const std::wstring dir = MakeTempDir("savefail");
    SettingsStore store(dir);
    Settings initial = DefaultSettings();
    initial.recent_count = 25;
    Expect(store.Save(initial), "persist the initial settings");

    SettingsEditor editor(initial);
    Expect(editor.SetHotkey(L"Ctrl+Alt+Space") == true, "edit the hotkey");
    Expect(editor.SetRecentCount(30) == true, "edit recent_count");

    FakeSwapper swapper;  // swap succeeds; the save below is forced to fail
    Expect(fs::create_directory(dir + L"\\settings.ini.tmp"), "obstruct the atomic write");

    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(!result.ok, "apply reports failure");
    Expect(result.save_failed, "failure is a save failure");
    Expect(!result.hotkey_rejected, "hotkey swap itself succeeded");
    Expect(swapper.calls->size() == 2, "swap new then swap back the previous binding");
    HotkeyBinding new_binding{};
    HotkeyBinding previous_binding{};
    Expect(ParseHotkey(L"Ctrl+Alt+Space", new_binding), "new combo parses");
    Expect(ParseHotkey(initial.hotkey, previous_binding), "previous combo parses");
    Expect(SameBinding(swapper.calls->at(0), new_binding), "first swap is the new combo");
    Expect(SameBinding(swapper.calls->at(1), previous_binding), "rollback swap restores the old combo");
    Expect(editor.Working().hotkey == initial.hotkey, "working hotkey rolled back");
    Expect(editor.Working().recent_count == 25, "working recent_count rolled back");
    Expect(!editor.Dirty(), "rollback clears the dirty flag");

    Settings loaded;
    Expect(store.Load(loaded) == nimblerun::SettingsLoadResult::Loaded, "store still loads");
    Expect(loaded.hotkey == L"Alt+Space", "stored hotkey untouched by the failed apply");
    Expect(loaded.recent_count == 25, "stored recent_count untouched by the failed apply");
    fs::remove_all(dir);
}

void TestApplySaveFailureWithoutHotkeyChange() {
    const std::wstring dir = MakeTempDir("savefail2");
    SettingsStore store(dir);
    Settings initial = DefaultSettings();
    initial.recent_count = 25;
    Expect(store.Save(initial), "persist the initial settings");

    SettingsEditor editor(initial);
    Expect(editor.SetRecentCount(31) == true, "edit recent_count only");

    FakeSwapper swapper;
    Expect(fs::create_directory(dir + L"\\settings.ini.tmp"), "obstruct the atomic write");

    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(!result.ok && result.save_failed, "save failure reported");
    Expect(swapper.calls->empty(), "no hotkey swap when the hotkey did not change");
    Expect(editor.Working().recent_count == 25, "working copy rolled back");

    Settings loaded;
    store.Load(loaded);
    Expect(loaded.recent_count == 25, "stored settings intact");
    fs::remove_all(dir);
}

void TestApplyHotkeyRejected() {
    const std::wstring dir = MakeTempDir("reject");
    SettingsStore store(dir);
    Settings initial = DefaultSettings();
    initial.recent_count = 21;
    Expect(store.Save(initial), "persist the initial settings");

    SettingsEditor editor(initial);
    Expect(editor.SetHotkey(L"Ctrl+Alt+Space") == true, "edit the hotkey");

    FakeSwapper swapper;
    swapper.fail = true;  // simulate the OS rejecting the new combo
    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(!result.ok, "apply reports failure");
    Expect(result.hotkey_rejected, "failure is a hotkey rejection");
    Expect(!result.save_failed, "nothing reached the store");
    Expect(swapper.calls->size() == 1, "no rollback swap needed on a rejected swap");
    Expect(editor.Working().hotkey == L"Alt+Space", "previous hotkey kept");

    Settings loaded;
    store.Load(loaded);
    Expect(loaded.hotkey == L"Alt+Space", "rejected combo never persisted");
    Expect(loaded.recent_count == 21, "other edits were not persisted either");
    fs::remove_all(dir);
}

void TestInvalidHotkeyRejectedWithoutPersisting() {
    const std::wstring dir = MakeTempDir("badcombo");
    SettingsStore store(dir);
    Settings initial = DefaultSettings();
    Expect(store.Save(initial), "persist the initial settings");

    SettingsEditor editor(initial);
    Expect(editor.SetHotkey(L"") == false, "empty combo rejected");
    Expect(editor.SetHotkey(L"Space") == false, "no-modifier combo rejected");
    Expect(editor.SetHotkey(L"Alt") == false, "modifier-only rejected");
    Expect(!editor.Dirty(), "rejected combos never mark the editor dirty");

    FakeSwapper swapper;
    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(result.ok, "no-op apply succeeds");
    Expect(swapper.calls->empty(), "no swap for an unchanged hotkey");

    Settings loaded;
    store.Load(loaded);
    Expect(loaded.hotkey == L"Alt+Space", "stored hotkey unchanged");
    Expect(SameSettings(loaded, initial), "stored settings unchanged");
    fs::remove_all(dir);
}

void TestHotkeyParseFormat() {
    HotkeyBinding binding{};
    Expect(ParseHotkey(L"Alt+Space", binding), "default combo parses");
    Expect(binding.modifiers == (MOD_ALT | MOD_NOREPEAT), "Alt+Space modifiers");
    Expect(binding.virtual_key == VK_SPACE, "Alt+Space virtual key");
    Expect(FormatHotkey(binding) == L"Alt+Space", "canonical formatting");

    Expect(ParseHotkey(L"Ctrl+Alt+Space", binding), "three-token combo parses");
    Expect(FormatHotkey(binding) == L"Ctrl+Alt+Space", "modifier order Ctrl, Alt");

    Expect(ParseHotkey(L"ctrl+alt+space", binding), "lowercase combo parses");
    Expect(FormatHotkey(binding) == L"Ctrl+Alt+Space", "lowercase normalizes to canonical");

    Expect(ParseHotkey(L"Shift+F13", binding), "function key combo parses");
    Expect(FormatHotkey(binding) == L"Shift+F13", "F-key formatting");

    Expect(ParseHotkey(L"Ctrl+Alt+Space+Extra", binding) == false, "extra token rejected");
    // NR-088: Win-key combos parse now (they warn on conflict, they are not a
    // syntax rejection); covered in detail by TestHotkeyParseAcceptsWinModifier.
    Expect(ParseHotkey(L"Win+R", binding), "Win+R parses after the NR-088 relaxation");
    // NR-088: Win combos parse now; Win+Tab is a warnable conflict, not a
    // syntax rejection (covered by TestHotkeyParseAcceptsWinModifier).
    Expect(ParseHotkey(L"Win+Tab", binding), "Win+Tab parses after the NR-088 relaxation");
}

// NR-086: shell-reserved combinations (task switching / Start menu) must be
// rejected at parse time -- RegisterHotKey accepts them, so the registration
// guard can never catch them (design-spec §4.1).
void TestHotkeyRejectsShellReservedCombos() {
    HotkeyBinding binding{};
    Expect(ParseHotkey(L"Alt+Tab", binding) == false, "Alt+Tab rejected: would hijack window switching");
    Expect(ParseHotkey(L"Alt+Esc", binding) == false, "Alt+Esc rejected: would hijack window cycling");
    Expect(ParseHotkey(L"Ctrl+Esc", binding) == false, "Ctrl+Esc rejected: would hijack the Start menu");
    Expect(ParseHotkey(L"Alt+Space", binding), "Alt+Space still parses");
    Expect(ParseHotkey(L"Ctrl+Alt+Space", binding), "Ctrl+Alt+Space still parses");
    Expect(ParseHotkey(L"Ctrl+Shift+Esc", binding), "Ctrl+Shift+Esc (Task Manager) still parses");
    Expect(ParseHotkey(L"Shift+Alt+Tab", binding), "Shift+Alt+Tab variant still parses");
    Expect(ParseHotkey(L"Alt+F4", binding), "Alt+F4 is an app-level convention, not blocked");
}

// NR-088: Win-key combos are no longer a syntax-level rejection; they parse to
// MOD_WIN and format back out. The NR-086 shell-reserved list stays hard.
void TestHotkeyParseAcceptsWinModifier() {
    HotkeyBinding binding{};
    Expect(ParseHotkey(L"Win+E", binding), "Win+E parses");
    Expect((binding.modifiers & MOD_WIN) != 0, "Win+E carries MOD_WIN");
    Expect(binding.virtual_key == L'E', "Win+E virtual key is E");
    Expect(FormatHotkey(binding) == L"Win+E", "Win+E formats back");

    Expect(ParseHotkey(L"Ctrl+Win+E", binding), "Ctrl+Win+E parses");
    Expect((binding.modifiers & MOD_WIN) != 0 && (binding.modifiers & MOD_CONTROL) != 0,
           "Ctrl+Win+E carries both modifiers");
    Expect(FormatHotkey(binding) == L"Ctrl+Win+E", "Ctrl+Win+E formats back");

    Expect(ParseHotkey(L"Ctrl+Alt+Win+E", binding), "Ctrl+Alt+Win+E parses");
    Expect(FormatHotkey(binding) == L"Ctrl+Alt+Win+E",
           "multi-modifier Win combo formats back in canonical order");

    // NR-086 regression: relaxing Win must not relax the shell-reserved list.
    Expect(ParseHotkey(L"Alt+Tab", binding) == false, "Alt+Tab still hard-rejected");
    Expect(ParseHotkey(L"Alt+Esc", binding) == false, "Alt+Esc still hard-rejected");
    Expect(ParseHotkey(L"Ctrl+Esc", binding) == false, "Ctrl+Esc still hard-rejected");
}

void TestResetRestoresDefaults() {
    const std::wstring dir = MakeTempDir("reset");
    SettingsStore store(dir);
    Settings custom = DefaultSettings();
    custom.hotkey = L"Ctrl+Alt+Space";
    custom.theme = Theme::Light;
    custom.recent_count = 35;
    custom.hide_after_launch = false;
    custom.catalog_roots = {{L"C:\\Tools", true}, {L"D:\\Games", false}};
    custom.catalog_extensions = {L".exe", L".lnk"};
    Expect(store.Save(custom), "persist custom settings");

    SettingsEditor editor(custom);
    Expect(editor.SetRecentCount(9) == true, "edit before reset");
    Expect(editor.AddRoot(L"E:\\Apps", true) == true, "add a root before reset");
    editor.ResetToDefaults();
    Expect(editor.Dirty(), "reset marks dirty");
    Expect(SameSettings(editor.Working(), DefaultSettings()),
           "reset restores every default value including empty catalog_roots");

    FakeSwapper swapper;
    Expect(editor.Apply(store, swapper).ok, "reset applies");

    Settings loaded;
    store.Load(loaded);
    Expect(SameSettings(loaded, DefaultSettings()), "reset persisted defaults");

    // Reset touches only the settings store: no usage/catalog files appear and
    // no other entries are written next to settings.ini.
    int entries = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(dir))) {
        ++entries;
        Expect(entry.path().filename().wstring() == L"settings.ini",
               "only settings.ini exists after reset");
    }
    Expect(entries == 1, "exactly one file after reset");
    fs::remove_all(dir);
}

void TestClearUsageOnly() {
    const std::wstring dir = MakeTempDir("clearusage");
    SettingsStore settings_store(dir);
    Settings settings = DefaultSettings();
    settings.recent_count = 22;
    Expect(settings_store.Save(settings), "persist settings");
    const std::string settings_before = ReadBytes(dir + L"\\settings.ini");

    UsageStore usage(dir);
    usage.Load();
    Expect(usage.RecordLaunch(L"app_a", 100), "record launch a");
    Expect(usage.RecordLaunch(L"app_b", 200), "record launch b");
    Expect(usage.Save(), "persist usage");
    Expect(usage.Recent().size() == 2, "two records before clear");

    Expect(usage.Clear(), "clear usage succeeds");
    Expect(usage.Recent().empty(), "in-memory records cleared");

    UsageStore reloaded(dir);
    reloaded.Load();
    Expect(reloaded.Recent().empty(), "usage file cleared on disk");
    Expect(ReadBytes(dir + L"\\settings.ini") == settings_before,
           "clear usage leaves settings byte-identical");
    fs::remove_all(dir);
}

void TestClearUsageFailureRestoresRecords() {
    const std::wstring dir = MakeTempDir("clearfail");
    UsageStore usage(dir);
    usage.Load();
    Expect(usage.RecordLaunch(L"app_a", 100), "record a launch");
    Expect(usage.Save(), "persist usage");
    Expect(fs::create_directory(dir + L"\\usage.tsv.tmp"), "obstruct the atomic write");

    Expect(usage.Clear() == false, "clear reports failure when the write fails");
    Expect(usage.Recent().size() == 1, "failed clear restores in-memory records");

    UsageStore reloaded(dir);
    reloaded.Load();
    Expect(reloaded.Recent().size() == 1, "failed clear left the disk records intact");
    fs::remove_all(dir);
}

void TestStringKeysCentralized() {
    Expect(SettingsStringText(SettingsString::DialogTitle) == L"NimbleRun Settings",
           "dialog title key");
    Expect(SettingsStringText(SettingsString::OkButton) == L"OK", "OK button key");
    Expect(SettingsStringText(SettingsString::CancelButton) == L"Cancel", "Cancel button key");
    Expect(SettingsStringText(SettingsString::ThemeDark) == L"Dark", "theme key");
    Expect(SettingsStringText(SettingsString::ResetSettingsButton) == L"Reset settings",
           "reset button key");
    // NR-054: "Open log folder" is a dialog-layer action (design-spec §FR-014);
    // it lives in the centralized string table like its sibling buttons.
    Expect(SettingsStringText(SettingsString::OpenLogFolderButton) == L"Open log folder",
           "open log folder button key");
    Expect(!SettingsStringText(SettingsString::OpenLogFolderButton).empty(),
           "open log folder key is non-empty English text");
    Expect(!SettingsStringText(SettingsString::HotkeyRejectedNotice).empty(),
           "hotkey notice key is non-empty");
    Expect(SettingsStringText(SettingsString::HotkeyRejectedNotice) !=
               SettingsStringText(SettingsString::SaveFailedNotice),
           "distinct notices map to distinct text");
}

} // namespace

int wmain() {
    TestRecentCountValidation();
    TestExtensionAllowlist();
    TestDirtyTrackingAndPersist();
    TestApplyRollbackOnSaveFailure();
    TestApplySaveFailureWithoutHotkeyChange();
    TestApplyHotkeyRejected();
    TestInvalidHotkeyRejectedWithoutPersisting();
    TestHotkeyParseFormat();
    TestHotkeyRejectsShellReservedCombos();
    TestHotkeyParseAcceptsWinModifier();
    TestResetRestoresDefaults();
    TestClearUsageOnly();
    TestClearUsageFailureRestoresRecords();
    TestStringKeysCentralized();
    std::printf("NR-013 settings editor check PASSED\n");
    return 0;
}
