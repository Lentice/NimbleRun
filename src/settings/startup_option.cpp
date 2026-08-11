#include "settings/startup_option.h"

#include <windows.h>

#include <string>

namespace nimblerun {
namespace {

constexpr wchar_t kRunValueName[] = L"NimbleRun";

// The current executable's full path, empty on failure.
std::wstring CurrentModulePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

} // namespace

bool SetStartupEnabled(bool enabled, const StartupOptionRegistry& registry) {
    HKEY key = nullptr;
    LONG status = RegCreateKeyExW(registry.base, registry.subkey.c_str(), 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                  &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    if (!enabled) {
        // Remove only NimbleRun's own value; never the whole Run key, so other
        // apps' entries are untouched. Deleting a missing value is a no-op.
        status = RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    const std::wstring path = CurrentModulePath();
    if (path.empty()) {
        RegCloseKey(key);
        return false;
    }
    // Run values are command strings: Explorer runs them through
    // CreateProcessW(lpApplicationName = nullptr), so a path containing
    // spaces must be quoted to survive the command-line tokenizer.
    const std::wstring quoted = L"\"" + path + L"\"";
    status = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(quoted.c_str()),
                            static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace nimblerun
