#include "settings/settings_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <cerrno>
#include <cstdlib>
#include <cwctype>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"settings.ini";
constexpr std::wstring_view kSchemaPrefix = L"schema=";
constexpr int kSchemaVersion = 1;
constexpr int kMinRecentCount = 8;
constexpr int kMaxRecentCount = 40;

std::wstring Trim(std::wstring_view value) {
    const auto is_space = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && is_space(value[begin])) {
        ++begin;
    }
    while (end > begin && is_space(value[end - 1])) {
        --end;
    }
    return std::wstring(value.substr(begin, end - begin));
}

std::wstring ToLower(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        out.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return out;
}

bool ParseInt(std::wstring_view text, int& out) {
    const std::wstring value = Trim(text);
    if (value.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = wcstol(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != L'\0') {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

std::optional<bool> ParseBool(std::wstring_view text) {
    const std::wstring value = ToLower(Trim(text));
    if (value == L"true") {
        return true;
    }
    if (value == L"false") {
        return false;
    }
    return std::nullopt;
}

// Lowercased extension including the dot, or empty when there is none.
std::wstring Extension(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"/\\");
    const std::wstring_view name =
        slash == std::wstring_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    return dot == std::wstring_view::npos ? std::wstring{} : ToLower(name.substr(dot));
}

bool IsSupportedExtension(std::wstring_view extension) {
    const std::wstring ext = Extension(extension);
    for (const std::wstring& supported : DefaultExtensions()) {
        if (ext == supported) {
            return true;
        }
    }
    return false;
}

std::wstring ThemeToString(Theme theme) {
    switch (theme) {
        case Theme::Light:
            return L"light";
        case Theme::Dark:
            return L"dark";
        case Theme::System:
            return L"system";
    }
    return L"system";
}

Theme ParseTheme(std::wstring_view text) {
    const std::wstring value = ToLower(Trim(text));
    if (value == L"light") {
        return Theme::Light;
    }
    if (value == L"dark") {
        return Theme::Dark;
    }
    return Theme::System;
}

std::vector<std::wstring> SplitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            std::wstring line(text.substr(start, i - start));
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    return lines;
}

} // namespace

// A local absolute path: drive-letter root (e.g. C:\...). UNC, network,
// URI and device paths are rejected (design-spec §FR-005).
bool IsLocalAbsolutePath(std::wstring_view value) {
    const std::wstring path = Trim(value);
    if (path.size() < 3) {
        return false;
    }
    const wchar_t drive = path[0];
    if (!((drive >= L'a' && drive <= L'z') || (drive >= L'A' && drive <= L'Z'))) {
        return false;
    }
    if (path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) {
        return false;
    }
    return true;
}

Settings DefaultSettings() {
    Settings settings;
    settings.hotkey = L"Alt+Space";
    settings.auto_start = false;
    settings.theme = Theme::System;
    settings.recent_count = 20;
    settings.hide_after_launch = true;
    settings.include_windows_apps = true;
    settings.catalog_roots.clear();
    settings.catalog_extensions = DefaultExtensions();
    return settings;
}

std::vector<std::wstring> DefaultExtensions() {
    return {L".exe", L".cmd", L".bat", L".lnk", L".appref-ms"};
}

std::wstring DefaultSettingsDir() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(buffer) + L"\\NimbleRun";
}

SettingsStore::SettingsStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

SettingsLoadResult SettingsStore::Load(Settings& out) const {
    out = DefaultSettings();

    const std::wstring path = JoinPath(directory_, kFileName);
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return SettingsLoadResult::Missing;
        }
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }

    std::wstring text;
    if (!DecodeUtf8(bytes, text) || text.empty()) {
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }
    if (text.front() == L'\uFEFF') {
        text.erase(text.begin());
    }

    const std::vector<std::wstring> lines = SplitLines(text);
    if (lines.empty()) {
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }

    const std::wstring schema_line = Trim(lines[0]);
    if (schema_line.size() <= kSchemaPrefix.size() ||
        schema_line.compare(0, kSchemaPrefix.size(), kSchemaPrefix) != 0) {
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }
    int schema = 0;
    if (!ParseInt(schema_line.substr(kSchemaPrefix.size()), schema)) {
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }
    if (schema > kSchemaVersion) {
        return SettingsLoadResult::NewerSchema;
    }
    if (schema != kSchemaVersion) {
        PreserveCorrupt(directory_, kFileName);
        return SettingsLoadResult::Corrupt;
    }

    bool extensions_replaced = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::wstring line = Trim(lines[i]);
        if (line.empty()) {
            continue;
        }
        std::size_t equals = std::wstring::npos;
        for (std::size_t j = 0; j < line.size(); ++j) {
            if (line[j] == L'=' && (j == 0 || line[j - 1] != L'\\')) {
                equals = j;
                break;
            }
        }
        if (equals == std::wstring::npos) {
            PreserveCorrupt(directory_, kFileName);
            return SettingsLoadResult::Corrupt;
        }
        const std::wstring key = Trim(line.substr(0, equals));
        const std::wstring value = UnescapeText(Trim(line.substr(equals + 1)));

        if (key == L"hotkey") {
            if (!value.empty()) {
                out.hotkey = value;
            }
        } else if (key == L"auto_start") {
            if (const auto parsed = ParseBool(value)) {
                out.auto_start = *parsed;
            }
        } else if (key == L"theme") {
            out.theme = ParseTheme(value);
        } else if (key == L"recent_count") {
            int count = 0;
            if (ParseInt(value, count) && count >= kMinRecentCount && count <= kMaxRecentCount) {
                out.recent_count = count;
            }
        } else if (key == L"hide_after_launch") {
            if (const auto parsed = ParseBool(value)) {
                out.hide_after_launch = *parsed;
            }
        } else if (key == L"include_windows_apps") {
            if (const auto parsed = ParseBool(value)) {
                out.include_windows_apps = *parsed;
            }
        } else if (key == L"catalog_root") {
            // Format: <escaped path>|<recursive 0/1>. '|' cannot appear in a
            // Windows path, so it is a safe separator.
            const std::size_t bar = value.find(L'|');
            const std::wstring raw_path = bar == std::wstring::npos ? value : value.substr(0, bar);
            const std::wstring raw_recursive =
                bar == std::wstring::npos ? std::wstring{} : value.substr(bar + 1);
            const std::wstring path = Trim(raw_path);
            if (IsLocalAbsolutePath(path)) {
                CatalogRoot root;
                root.path = path;
                if (const auto parsed = ParseBool(raw_recursive)) {
                    root.recursive = *parsed;
                }
                out.catalog_roots.push_back(std::move(root));
            }
        } else if (key == L"catalog_extension") {
            // The extension allowlist in the file is authoritative: the first
            // entry replaces the default list instead of appending to it.
            if (!extensions_replaced) {
                out.catalog_extensions.clear();
                extensions_replaced = true;
            }
            const std::wstring ext = Extension(value);
            if (IsSupportedExtension(ext) &&
                std::find(out.catalog_extensions.begin(), out.catalog_extensions.end(), ext) ==
                    out.catalog_extensions.end()) {
                out.catalog_extensions.push_back(ext);
            }
        }
        // Unknown keys are ignored so a same-schema file stays readable.
    }
    return SettingsLoadResult::Loaded;
}

bool SettingsStore::Save(const Settings& settings) const {
    std::wstring text;
    text += kSchemaPrefix;
    text += std::to_wstring(kSchemaVersion);
    text += L"\n";
    text += L"hotkey=" + EscapeText(settings.hotkey) + L"\n";
    text += L"auto_start=" + std::wstring(settings.auto_start ? L"true" : L"false") + L"\n";
    text += L"theme=" + ThemeToString(settings.theme) + L"\n";
    text += L"recent_count=" + std::to_wstring(settings.recent_count) + L"\n";
    text += L"hide_after_launch=" + std::wstring(settings.hide_after_launch ? L"true" : L"false") + L"\n";
    text += L"include_windows_apps=" + std::wstring(settings.include_windows_apps ? L"true" : L"false") + L"\n";
    for (const CatalogRoot& root : settings.catalog_roots) {
        text += L"catalog_root=" + EscapeText(root.path) + L"|" +
                std::wstring(root.recursive ? L"true" : L"false") + L"\n";
    }
    for (const std::wstring& ext : settings.catalog_extensions) {
        text += L"catalog_extension=" + EscapeText(ext) + L"\n";
    }
    return AtomicWriteUtf8Text(directory_, kFileName, text);
}

} // namespace nimblerun
