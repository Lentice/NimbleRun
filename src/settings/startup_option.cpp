#include "settings/startup_option.h"

#include <windows.h>

#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>

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

// Windows paths are compared case-insensitively.
bool PathsMatch(std::wstring_view left, std::wstring_view right) {
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

} // namespace

StartupStatus GetStartupStatus(const StartupOptionRegistry& registry) {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(registry.base, registry.subkey.c_str(), 0,
                                KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        return StartupStatus::Disabled;
    }
    if (status != ERROR_SUCCESS) {
        return StartupStatus::UnknownError;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = RegQueryValueExW(key, kRunValueName, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return StartupStatus::Disabled;
    }
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(key);
        return StartupStatus::UnknownError;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, kRunValueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(value.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return StartupStatus::UnknownError;
    }
    value.resize(value.find(L'\0'));

    const std::wstring module = CurrentModulePath();
    if (module.empty()) {
        return StartupStatus::UnknownError;
    }
    return PathsMatch(value, module) ? StartupStatus::Enabled
                                     : StartupStatus::EnabledMoved;
}

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
    status = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(path.c_str()),
                            static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace nimblerun
