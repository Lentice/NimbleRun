// Focused check for NR-014 (startup option).
//
// Drives the HKCU Run-key module (startup_option) against an isolated
// HKCU\Software\NimbleRunTest\<pid> key — never the real Run key — plus the
// SettingsEditor::SetAutoStart round-trip. Covers: enable/disable value
// creation/removal, preservation of unrelated values in the same key,
// per-user scoping, re-creation after a moved EXE, and the settings editor
// round-trip independent of the hotkey swap.

#include "test_util.h"

#include "app_host/hotkey.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "settings/startup_option.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::DefaultSettings;
using nimblerun::HotkeyBinding;
using nimblerun::HotkeyResult;
using nimblerun::Settings;
using nimblerun::SettingsApplyResult;
using nimblerun::SettingsEditor;
using nimblerun::SettingsLoadResult;
using nimblerun::SettingsStore;
using nimblerun::SettingsString;
using nimblerun::SettingsStringText;
using nimblerun::StartupOptionRegistry;

namespace {

std::wstring TestSubkey() {
    return L"Software\\NimbleRunTest\\" + std::to_wstring(GetCurrentProcessId());
}

StartupOptionRegistry TestRegistry() {
    StartupOptionRegistry registry;
    registry.subkey = TestSubkey();
    return registry;
}

// Removes the whole isolated test key; called at the start and end of each
// section so a failed run still leaves no trace under the real Run key. The
// parent key is dropped only when empty (RegDeleteKeyW fails with
// ERROR_ACCESS_DENIED when other test processes still hold PID subkeys).
void RemoveTestKey() {
    RegDeleteTreeW(HKEY_CURRENT_USER, TestSubkey().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\NimbleRunTest");
}

std::wstring ModulePath() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    path.resize(length);
    return path;
}

std::wstring QuotedModulePath() {
    return L"\"" + ModulePath() + L"\"";
}

bool WriteValue(const std::wstring& name, const std::wstring& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, TestSubkey().c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LONG status = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(data.c_str()),
        static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool ReadValue(const std::wstring& name, std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, TestSubkey().c_str(), 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) {
        RegCloseKey(key);
        return false;
    }
    out.resize(size / sizeof(wchar_t));
    status = RegQueryValueExW(key, name.c_str(), nullptr, &type,
                              reinterpret_cast<BYTE*>(out.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    out.resize(out.find(L'\0'));
    return true;
}

bool ValueExists(const std::wstring& name) {
    std::wstring dummy;
    return ReadValue(name, dummy);
}

std::wstring MakeTempDir() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring dir = std::wstring(buffer) + L"NimbleRun_startup_test_" +
        std::to_wstring(GetCurrentProcessId());
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

// Injectable hotkey-swap seam: records proposed bindings and can reject, so
// the editor test can verify SetAutoStart never routes through the swapper.
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

void TestEnableCreatesEntry() {
    RemoveTestKey();
    Expect(SetStartupEnabled(true, TestRegistry()), "enable writes the entry");
    std::wstring value;
    Expect(ReadValue(L"NimbleRun", value), "the Run value exists after enable");
    Expect(value == QuotedModulePath(), "the value points at the quoted module path");
}

void TestDisableRemovesOnlyOwnValue() {
    RemoveTestKey();
    Expect(SetStartupEnabled(true, TestRegistry()), "enable first");
    Expect(WriteValue(L"OtherApp", L"C:\\Other\\app.exe"),
           "pre-existing unrelated value in the same key");
    Expect(SetStartupEnabled(false, TestRegistry()), "disable removes the entry");
    Expect(!ValueExists(L"NimbleRun"), "our value is gone");
    std::wstring other;
    Expect(ReadValue(L"OtherApp", other), "the unrelated value survives");
    Expect(other == L"C:\\Other\\app.exe", "the unrelated value is untouched");
}

void TestDisableIsNoopWhenAbsent() {
    RemoveTestKey();
    Expect(SetStartupEnabled(false, TestRegistry()),
           "disable with nothing present succeeds");
    Expect(!ValueExists(L"NimbleRun"), "no value was created by a no-op disable");
}

void TestPerUserScoping() {
    RemoveTestKey();
    StartupOptionRegistry registry = TestRegistry();
    Expect(registry.base == HKEY_CURRENT_USER,
           "the default registry is HKCU, so startup is always per-user");
    Expect(SetStartupEnabled(true, registry), "enable writes under HKCU");
    Expect(ValueExists(L"NimbleRun"), "the entry lives under the HKCU test key");
    // By construction the module has no HKLM code path: the only registry root
    // it ever touches is `registry.base` (HKEY_CURRENT_USER above), so the
    // change can never affect the machine-wide Run key.
}

// NR-128: GetStartupStatus and its tri-state (Enabled/Disabled/EnabledMoved)
// were the only consumers of the moved-EXE and raw-REG_SZ read paths, which
// are gone with the function. TestMovedExeDetection, TestUnterminatedRegSz-
// DoesNotCrash and TestOddByteRegSzDoesNotCrash exercised only that read; the
// moved-EXE re-creation behavior that production keeps is covered by
// TestRecreateAfterMove below.
void TestRecreateAfterMove() {
    RemoveTestKey();
    Expect(WriteValue(L"NimbleRun", L"C:\\elsewhere\\NimbleRun.exe"), "stale path");
    Expect(SetStartupEnabled(true, TestRegistry()), "enable rewrites the entry");
    std::wstring value;
    Expect(ReadValue(L"NimbleRun", value), "the value exists after re-create");
    Expect(value == QuotedModulePath(), "re-created value points at the quoted module path");
}

void TestAutoStartEditorRoundTrip() {
    const std::wstring dir = MakeTempDir();
    SettingsStore store(dir);
    SettingsEditor editor(DefaultSettings());
    Expect(!editor.Working().auto_start, "auto_start defaults to off");
    Expect(editor.SetAutoStart(true) == true, "enable auto_start");
    Expect(editor.Working().auto_start, "working copy reflects the edit");
    Expect(editor.Dirty(), "the edit marks the editor dirty");

    FakeSwapper swapper;
    Expect(editor.Apply(store, swapper).ok, "apply persists the edit");
    Expect(swapper.calls->empty(),
           "auto_start alone never touches the hotkey swapper");

    Settings loaded;
    Expect(store.Load(loaded) == SettingsLoadResult::Loaded, "reload");
    Expect(loaded.auto_start, "auto_start round-trips through settings.ini");
    fs::remove_all(dir);
}

void TestAutoStartUncoupledFromHotkeyRollback() {
    const std::wstring dir = MakeTempDir();
    SettingsStore store(dir);
    SettingsEditor editor(DefaultSettings());
    Expect(editor.SetAutoStart(true) == true, "set auto_start");
    Expect(editor.SetHotkey(L"Ctrl+Alt+Space") == true, "set hotkey");
    FakeSwapper swapper;
    swapper.fail = true;  // simulate the OS rejecting the new combo
    const SettingsApplyResult result = editor.Apply(store, swapper);
    Expect(!result.ok && result.hotkey_rejected, "rejected hotkey fails the apply");
    Expect(!editor.Working().auto_start, "working copy rolled back with the settings");
    Settings loaded;
    store.Load(loaded);
    Expect(!loaded.auto_start, "a failed apply persists nothing");
    fs::remove_all(dir);
}

void TestStartupStringsCentralized() {
    Expect(SettingsStringText(SettingsString::StartupAutoStartLabel) ==
               L"Launch at startup",
           "auto-start checkbox label key");
    Expect(!SettingsStringText(SettingsString::StartupFailedNotice).empty(),
           "startup failure notice key is non-empty");
    Expect(SettingsStringText(SettingsString::StartupFailedNotice) !=
               SettingsStringText(SettingsString::SaveFailedNotice),
           "startup notice is distinct from the generic save notice");
}

} // namespace

int wmain() {
    TestEnableCreatesEntry();
    TestDisableRemovesOnlyOwnValue();
    TestDisableIsNoopWhenAbsent();
    TestPerUserScoping();
    TestRecreateAfterMove();
    TestAutoStartEditorRoundTrip();
    TestAutoStartUncoupledFromHotkeyRollback();
    TestStartupStringsCentralized();
    RemoveTestKey();
    std::printf("NR-014 startup option check PASSED\n");
    return 0;
}
