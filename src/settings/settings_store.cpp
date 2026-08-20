#include "catalog/app_filter.h"

#include "settings/settings_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"settings.ini";
constexpr int kSchemaVersion = 2;

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

// Drive root of a shape-checked "X:\..." path: the first three characters,
// with the forward-slash form normalized to the backslash GetDriveTypeW
// expects (e.g. "C:/Tools" -> "C:\").
DWORD DriveTypeOfRoot(std::wstring_view path) {
    std::wstring root(path.substr(0, 3));
    if (root.size() == 3 && root[2] == L'/') {
        root[2] = L'\\';
    }
    return GetDriveTypeW(root.c_str());
}

} // namespace

bool IsAcceptableDriveType(DWORD drive_type) {
    return drive_type != DRIVE_REMOTE;
}

// A local absolute path: drive-letter root (e.g. C:\...). UNC, network,
// URI and device paths are rejected (design-spec §FR-005).
// NR-144: converges with IsDisplayablePath (app_filter.cpp) -- same predicate;
// Trim kept because callers (UserDataDirFromLocalAppData, Load) rely on it.
// Mapped network drives are rejected via GetDriveTypeW here (FR-005);
// IsDisplayablePath stays a pure shape check for the per-frame display path.
// A path that is exactly a volume root (e.g. "C:\") is rejected too
// (design-spec §19.5: 不掃描整顆磁碟).
bool IsLocalAbsolutePath(std::wstring_view value) {
    const std::wstring trimmed = Trim(value);
    if (!IsDisplayablePath(trimmed)) {
        return false;
    }
    if (!IsAcceptableDriveType(DriveTypeOfRoot(trimmed))) {
        return false;
    }
    // A bare volume root would make the deep scan walk the whole drive
    // (design-spec §19.5). After the shape check, exactly three characters can
    // only be "X:\" or "X:/"; "C:\Tools\" is longer and stays acceptable.
    if (trimmed.size() == 3) {
        return false;
    }
    return true;
}

Settings DefaultSettings() {
    // All defaults come from the Settings member initializers (settings_store.h);
    // keep the catalog_extensions assignment to state the derived allowlist
    // (DefaultExtensions()) explicitly.
    Settings settings;
    settings.catalog_extensions = DefaultExtensions();
    return settings;
}

std::vector<std::wstring> DefaultExtensions() {
    return {L".exe", L".cmd", L".bat", L".lnk", L".appref-ms"};
}

std::wstring DefaultSettingsDir() {
    PWSTR local_app_data = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                             nullptr, &local_app_data);
    if (FAILED(hr) || local_app_data == nullptr) {
        CoTaskMemFree(local_app_data);
        return {};
    }
    const std::wstring result = UserDataDirFromLocalAppData(local_app_data);
    CoTaskMemFree(local_app_data);
    return result;
}

std::wstring UserDataDirFromLocalAppData(std::wstring_view local_app_data) {
    const std::wstring base = Trim(local_app_data);
    constexpr std::wstring_view suffix = L"\\NimbleRun";
    if (!IsLocalAbsolutePath(base) || base.size() > MAX_PATH - suffix.size() ||
        base.find_first_of(L"\"<>|*?") != std::wstring::npos) {
        return {};
    }
    std::wstring result = base;
    while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
        result.pop_back();
    }
    if (result.empty() || result.size() > MAX_PATH - suffix.size()) {
        return {};
    }
    result += suffix;
    return result;
}

SettingsStore::SettingsStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

SettingsLoadResult SettingsStore::Load(Settings& out) const {
    out = DefaultSettings();
    // NR-096: any non-NewerSchema outcome stays writable; only the NewerSchema
    // branch below sets write_protected_.
    write_protected_ = false;

    std::vector<std::wstring> lines;
    switch (ReadVersionedLines(directory_, kFileName, kSchemaVersion, lines)) {
    case VersionedReadStatus::Loaded:
        break;
    case VersionedReadStatus::Missing:
        return SettingsLoadResult::Missing;
    case VersionedReadStatus::NewerSchema:
        write_protected_ = true;
        return SettingsLoadResult::NewerSchema;  // original untouched (design-spec §10.4)
    case VersionedReadStatus::OlderSchema:
        break;  // NR-193: schema=1 catalog_root booleans migrate below; Save upgrades to 2.
    default:  // Unreadable / Malformed
        PreserveCorrupt(directory_, kFileName);
        out = DefaultSettings();  // NR-080: honor "non-Loaded out holds DefaultSettings"
        return SettingsLoadResult::Corrupt;
    }

    bool extensions_replaced = false;
    std::size_t catalog_root_count = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
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
            // NR-080: a mid-file corrupt row must not leak the valid prefix
            // that was already parsed into `out` -- the non-Loaded contract is
            // DefaultSettings(), never a partial parse (settings_store.h).
            out = DefaultSettings();
            return SettingsLoadResult::Corrupt;
        }
        const std::wstring key = Trim(line.substr(0, equals));
        const std::wstring value = UnescapeText(Trim(line.substr(equals + 1)));

        if (key == L"hotkey") {
            // NR-140: over-long hotkey values are whole-file Corrupt, never a
            // trimmed guess (NR-080: non-Loaded out holds DefaultSettings).
            if (value.size() > kMaxHotkeyLength) {
                PreserveCorrupt(directory_, kFileName);
                out = DefaultSettings();
                return SettingsLoadResult::Corrupt;
            }
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
        } else if (key == L"english_input_on_show") {
            if (const auto parsed = ParseBool(value)) {
                out.english_input_on_show = *parsed;
            }
        } else if (key == L"catalog_root") {
            // NR-140: each root becomes a watcher thread at startup
            // (main.cpp StartWatchers), so the row cap is a startup-DoS guard.
            // Over-limit is whole-file Corrupt (NR-080: no partial state).
            if (++catalog_root_count > kMaxCatalogRoots) {
                PreserveCorrupt(directory_, kFileName);
                out = DefaultSettings();
                return SettingsLoadResult::Corrupt;
            }
            // Format: <escaped path>|<max_depth>. Schema=1 booleans are
            // accepted below for migration; '|' cannot appear in a Windows
            // path, so it remains a safe separator.
            const std::size_t bar = value.find(L'|');
            const std::wstring raw_path = bar == std::wstring::npos ? value : value.substr(0, bar);
            const std::wstring raw_depth =
                bar == std::wstring::npos ? std::wstring{} : value.substr(bar + 1);
            const std::wstring path = Trim(raw_path);
            if (IsLocalAbsolutePath(path)) {
                CatalogRoot root;
                root.path = path;
                if (const auto parsed = ParseBool(raw_depth)) {
                    root.max_depth = *parsed ? root.max_depth : kMinCatalogDepth;
                } else {
                    int depth = 0;
                    if (!ParseInt(raw_depth, depth) ||
                        depth < kMinCatalogDepth || depth > kMaxCatalogDepth) {
                        continue;
                    }
                    root.max_depth = depth;
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
    // NR-096: a newer-schema file is another build's data (design-spec §10.4).
    // Refuse without touching the original or the tmp file; the caller's
    // save-failed handling runs.
    if (write_protected_) {
        return false;
    }
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
    text += L"english_input_on_show=" + std::wstring(settings.english_input_on_show ? L"true" : L"false") + L"\n";
    for (const CatalogRoot& root : settings.catalog_roots) {
        text += L"catalog_root=" + EscapeText(root.path) + L"|" +
                std::to_wstring(root.max_depth) + L"\n";
    }
    for (const std::wstring& ext : settings.catalog_extensions) {
        text += L"catalog_extension=" + EscapeText(ext) + L"\n";
    }
    return AtomicWriteUtf8Text(directory_, kFileName, text);
}

} // namespace nimblerun
