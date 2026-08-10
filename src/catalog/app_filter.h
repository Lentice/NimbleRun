#pragma once

#include <cwctype>
#include <string>
#include <string_view>

namespace nimblerun {

// Case-folds a string via towlower, used for extension and stem comparisons.
inline std::wstring ToLower(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        out.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return out;
}

namespace {

// Final path segment, e.g. "C:\A\B.exe" -> "B.exe".
inline std::wstring_view FileName(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"/\\");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

} // namespace

// File name without the final extension, e.g. "Notepad.lnk" -> "Notepad".
inline std::wstring FileStem(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    return std::wstring(dot == std::wstring_view::npos ? name : name.substr(0, dot));
}

// Lowercased extension including the dot, or empty when none. A trailing dot in
// a directory name is not treated as an extension.
inline std::wstring Extension(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos) {
        return {};
    }
    return ToLower(name.substr(dot));
}

// "scheme://..." with a valid RFC scheme prefix.
bool IsUrlTarget(std::wstring_view target);

// True when the target is a real local absolute filesystem path, i.e. showable
// to the user as a path (design-spec §4.2/§4.9: an item without one shows a
// source label instead of its Shell parsing name). Of the three AppsFolder
// parsing-name shapes in §2.6 only the third qualifies: an AUMID has no
// separator and a Known Folder GUID-relative path starts with '{'.
bool IsDisplayablePath(std::wstring_view target);

// True when the target looks like a launchable program rather than a document,
// website, or uninstaller (design-spec §FR-004a). `target` is a Start Menu
// shortcut's resolved target or an AppsFolder item's Shell parsing name.
//
// Decision order (first match wins):
//   1. empty -> false;
//   2. no '\' and no '/' -> an AUMID, true (never run extension logic on it);
//   3. URL scheme (scheme://, except file:/FILE:) -> false;
//   4. file stem starting with "unins" (case-insensitive) -> false;
//   5. final extension in the whitelist .exe .com .bat .cmd .lnk .appref-ms
//      .msc (case-insensitive) -> true; no-extension paths -> false.
// Pure value logic: no HWND, no Shell COM, no <windows.h>.
bool IsProgramLikeTarget(std::wstring_view target);

} // namespace nimblerun
