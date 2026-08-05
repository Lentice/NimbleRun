#include "catalog/stable_id.h"
#include "catalog/start_menu_catalog.h"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shtypes.h>
#include <knownfolders.h>

#include <cwctype>
#include <string>
#include <string_view>
#include <utility>

namespace nimblerun {
namespace {

std::wstring ToLower(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        out.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return out;
}

std::wstring_view FileName(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"/\\");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

// File name without the final extension, e.g. "Notepad.lnk" -> "Notepad".
std::wstring FileStem(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    return std::wstring(dot == std::wstring_view::npos ? name : name.substr(0, dot));
}

// Lowercased extension including the dot, or empty when none. A trailing dot in
// a directory name is not treated as an extension.
std::wstring Extension(std::wstring_view path) {
    const std::wstring_view name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring_view::npos) {
        return {};
    }
    return ToLower(name.substr(dot));
}

bool AcceptExtension(std::wstring_view path) {
    const std::wstring ext = Extension(path);
    return ext == L".lnk" || ext == L".appref-ms" || ext == L".exe";
}

// "scheme://..." with a valid RFC scheme prefix.
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

// SIGDN_URL gives a file:/// URI for local files and the real URL for website
// shortcuts. Any non-file scheme is treated as a website shortcut (FR-004).
bool IsWebUrl(std::wstring_view sigdn_url) {
    if (!IsUrlTarget(sigdn_url) || sigdn_url.size() < 5) {
        return false;
    }
    return sigdn_url.compare(0, 5, L"file:") != 0 && sigdn_url.compare(0, 5, L"FILE:") != 0;
}

// True when the loaded link's target is a URL rather than a local file. The
// .lnk stores a URL as a PIDL, so GetPath is empty for those; the Shell's URL
// display name is the reliable signal.
bool ShortcutIsWeb(IShellLinkW& link) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(link.GetIDList(&pidl)) || pidl == nullptr) {
        return false;
    }
    bool web = false;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item)))) {
        wchar_t* url = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_URL, &url))) {
            web = IsWebUrl(url);
            CoTaskMemFree(url);
        }
        item->Release();
    }
    CoTaskMemFree(pidl);
    return web;
}

// Conservative FR-004 filter: drop website, help, and uninstaller targets. The
// target type (URL scheme / document extension) is the primary signal, not a
// name-only blacklist.
bool LooksLikeNonAppTarget(std::wstring_view resolved_target) {
    if (IsUrlTarget(resolved_target)) {
        return true;
    }
    const std::wstring ext = Extension(resolved_target);
    if (ext == L".chm" || ext == L".hlp" || ext == L".html" || ext == L".htm") {
        return true;
    }
    const std::wstring stem = ToLower(FileStem(resolved_target));
    if (stem == L"uninstall" || stem == L"uninstaller" || stem.compare(0, 5, L"unins") == 0) {
        return true;
    }
    return false;
}

class ComGuard {
public:
    ComGuard() {
        const HRESULT hr =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        own_ = hr == S_OK;
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~ComGuard() {
        if (own_) {
            CoUninitialize();
        }
    }
    bool Usable() const { return usable_; }

private:
    bool own_ = false;
    bool usable_ = false;
};

struct ResolvedLink {
    bool loadable = false;
    bool web = false;
    std::wstring target;
    std::wstring arguments;
};

// Parses a .lnk with the Shell link API only, never the raw binary format.
// loadable is false only when the file itself is corrupt or unreadable, in
// which case the caller drops the entry. A loadable link whose target cannot
// be resolved is still kept, because the .lnk path is itself Shell-launchable.
ResolvedLink ResolveShortcut(const std::wstring& path) {
    ResolvedLink result;
    IShellLinkW* shell_link = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell_link)))) {
        return result;
    }
    IPersistFile* persist = nullptr;
    HRESULT hr = shell_link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr)) {
        hr = persist->Load(path.c_str(), STGM_READ);
        persist->Release();
    }
    if (SUCCEEDED(hr)) {
        wchar_t target[1024];
        if (SUCCEEDED(shell_link->GetPath(target, 1024, nullptr, SLGP_UNCPRIORITY)) &&
            target[0] != L'\0') {
            result.target.assign(target);
        } else {
            result.web = ShortcutIsWeb(*shell_link);
        }
        wchar_t arguments[1024];
        if (SUCCEEDED(shell_link->GetArguments(arguments, 1024))) {
            result.arguments.assign(arguments);
        }
        result.loadable = true;
    }
    shell_link->Release();
    return result;
}

void ProcessFile(const std::wstring& path, AppSource source, std::vector<AppEntry>& out) {
    const std::wstring ext = Extension(path);

    ResolvedLink link;
    if (ext == L".lnk") {
        link = ResolveShortcut(path);
        if (!link.loadable) {
            return;  // corrupt shortcut: skip this entry, keep enumerating
        }
        if (link.web) {
            return;  // website shortcut (FR-004)
        }
        if (!link.target.empty() && LooksLikeNonAppTarget(link.target)) {
            return;
        }
    }

    AppEntry entry;
    entry.display_name = FileStem(path);
    entry.source = source;
    entry.source_path = path;
    // The shortcut/file path is the Shell-launchable identity (NR-008). The
    // resolved target plus arguments feed the stable id (design-spec §10.3).
    entry.launch_identity = path;
    std::wstring identity_key = NormalizePathKey(path);
    if (!link.target.empty()) {
        // The resolved target is the identity (design-spec §10.3), normalized
        // so a Start Menu shortcut and a user-folder entry at the same physical
        // path hash identically; arguments stay exact. Non-path targets
        // (shell: parsing names, AUMIDs) pass through NormalizePathKey unchanged.
        identity_key = NormalizePathKey(link.target);
        if (!link.arguments.empty()) {
            identity_key += L"\n";
            identity_key += link.arguments;
        }
    }
    entry.stable_id = HashStableId(identity_key);
    out.push_back(std::move(entry));
}

void EnumerateDirectoryRecursive(const std::wstring& directory, AppSource source,
                                 std::vector<AppEntry>& out) {
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        return;  // missing/unreadable directory: skip this subtree, keep others
    }
    do {
        const std::wstring name = find_data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = directory + L"\\" + name;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever; reparse-point app dirs are not a real source.
                EnumerateDirectoryRecursive(full, source, out);
            }
            continue;
        }
        if (!AcceptExtension(full)) {
            continue;
        }
        ProcessFile(full, source, out);
    } while (FindNextFileW(find, &find_data) != FALSE);
    FindClose(find);
}

std::wstring KnownFolderPath(REFKNOWNFOLDERID folder) {
    wchar_t* buffer = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &buffer))) {
        return {};
    }
    std::wstring result = buffer;
    CoTaskMemFree(buffer);
    return result;
}

} // namespace

std::vector<AppEntry> EnumerateStartMenuCatalog() {
    std::vector<AppEntry> out;
    ComGuard com;
    if (!com.Usable()) {
        return out;
    }

    const std::wstring user_root = KnownFolderPath(FOLDERID_Programs);
    if (!user_root.empty()) {
        EnumerateProgramsDirectory(user_root, AppSource::UserStartMenu, out);
    }
    const std::wstring common_root = KnownFolderPath(FOLDERID_CommonPrograms);
    if (!common_root.empty()) {
        EnumerateProgramsDirectory(common_root, AppSource::CommonStartMenu, out);
    }
    return out;
}

void EnumerateProgramsDirectory(const std::wstring& root, AppSource source,
                                std::vector<AppEntry>& out) {
    ComGuard com;
    if (!com.Usable()) {
        return;
    }
    EnumerateDirectoryRecursive(root, source, out);
}

} // namespace nimblerun
