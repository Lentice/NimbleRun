#include "catalog/app_filter.h"

namespace nimblerun {

bool IsUrlTarget(std::wstring_view target) {
    const std::size_t colon = target.find(L':');
    if (colon == std::wstring_view::npos || colon == 0) {
        return false;
    }
    const wchar_t first = target[0];
    const bool alpha = (first >= L'a' && first <= L'z') || (first >= L'A' && first <= L'Z');
    if (!alpha) {
        return false;
    }
    for (std::size_t i = 1; i < colon; ++i) {
        const wchar_t c = target[i];
        const bool valid = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                           (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.';
        if (!valid) {
            return false;
        }
    }
    return target.compare(colon, 3, L"://") == 0;
}

bool IsDisplayablePath(std::wstring_view target) {
    // "X:\..." or "X:/...". Rejects AUMIDs, "{GUID}\..." and UNC by shape; no
    // filesystem access, so this stays pure and cheap enough to call per frame.
    if (target.size() < 3 || target[1] != L':') {
        return false;
    }
    const wchar_t drive = target[0];
    const bool alpha = (drive >= L'a' && drive <= L'z') || (drive >= L'A' && drive <= L'Z');
    return alpha && (target[2] == L'\\' || target[2] == L'/');
}

namespace {

// A "file:" scheme (case-insensitive) points at a local file, which must fall
// through to the path rules instead of the web rejection (design-spec §FR-004a).
bool IsFileScheme(std::wstring_view target) {
    if (target.size() < 5) {
        return false;
    }
    return ToLower(target.substr(0, 5)) == L"file:";
}

} // namespace

bool IsProgramLikeTarget(std::wstring_view target) {
    if (target.empty()) {
        return false;
    }
    // No path separators: an AUMID. AUMIDs contain dots that are not an
    // extension, so no extension logic may run before this branch (FR-004a;
    // e.g. Microsoft.WindowsCalculator_8wekyb3d8bbwe!App).
    if (target.find(L'\\') == std::wstring_view::npos &&
        target.find(L'/') == std::wstring_view::npos) {
        return true;
    }
    if (IsUrlTarget(target) && !IsFileScheme(target)) {
        return false;
    }
    // Uninstallers are excluded regardless of their (possibly whitelisted)
    // extension: mistargeting a removal prompt is too costly.
    const std::wstring stem = ToLower(FileStem(target));
    if (stem.compare(0, 5, L"unins") == 0) {
        return false;
    }
    // Whitelist (FR-004a) rather than a blacklist: anything not explicitly a
    // launchable program is excluded, so unknown document types never leak in.
    const std::wstring ext = Extension(target);
    return ext == L".exe" || ext == L".com" || ext == L".bat" || ext == L".cmd" ||
           ext == L".lnk" || ext == L".appref-ms" || ext == L".msc";
}

} // namespace nimblerun
