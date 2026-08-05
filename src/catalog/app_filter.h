#pragma once

#include <string>
#include <string_view>

namespace nimblerun {

// Case-folds a string via towlower, used for extension and stem comparisons.
std::wstring ToLower(std::wstring_view value);

// Final path segment, e.g. "C:\A\B.exe" -> "B.exe".
std::wstring_view FileName(std::wstring_view path);

// File name without the final extension, e.g. "Notepad.lnk" -> "Notepad".
std::wstring FileStem(std::wstring_view path);

// Lowercased extension including the dot, or empty when none. A trailing dot in
// a directory name is not treated as an extension.
std::wstring Extension(std::wstring_view path);

// "scheme://..." with a valid RFC scheme prefix.
bool IsUrlTarget(std::wstring_view target);

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
