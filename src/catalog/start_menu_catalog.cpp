#include "catalog/app_filter.h"
#include "catalog/stable_id.h"
#include "catalog/start_menu_catalog.h"

#include "win/com.h"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shtypes.h>
#include <knownfolders.h>

#include <string>
#include <string_view>
#include <utility>

namespace nimblerun {
namespace {

bool AcceptExtension(std::wstring_view path) {
    const std::wstring ext = Extension(path);
    return ext == L".lnk" || ext == L".appref-ms" || ext == L".exe";
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
    IShellItem* raw_item = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&raw_item)))) {
        ComPtr<IShellItem> item(raw_item);
        wchar_t* url = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_URL, &url))) {
            web = IsWebUrl(url);
            CoTaskMemFree(url);
        }
    }
    CoTaskMemFree(pidl);
    return web;
}

// "C:\A\B\app.exe" -> "C:\A\B". Empty when there is no separator.
std::wstring ParentDirectory(std::wstring_view path) {
    const std::size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring_view::npos) {
        return {};
    }
    return std::wstring(path.substr(0, cut));
}

struct ResolvedLink {
    bool loadable = false;
    bool web = false;
    std::wstring target;
    std::wstring arguments;
    // Only a working directory that differs from the target's own directory,
    // which is the default the Shell would use anyway. Part of the identity for
    // the same reason arguments are: a shortcut that starts the same EXE
    // elsewhere is a different launch and must not collapse into the bare EXE.
    std::wstring working_directory;
};

// Parses a .lnk with the Shell link API only, never the raw binary format.
// loadable is false only when the file itself is corrupt or unreadable, in
// which case the caller drops the entry. A loadable link whose target cannot
// be resolved is still kept, because the .lnk path is itself Shell-launchable.
ResolvedLink ResolveShortcut(const std::wstring& path) {
    ResolvedLink result;
    IShellLinkW* raw_link = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw_link)))) {
        return result;
    }
    ComPtr<IShellLinkW> shell_link(raw_link);
    IPersistFile* raw_persist = nullptr;
    HRESULT hr = shell_link->QueryInterface(IID_PPV_ARGS(&raw_persist));
    ComPtr<IPersistFile> persist(raw_persist);
    if (SUCCEEDED(hr)) {
        hr = persist->Load(path.c_str(), STGM_READ);
    }
    if (SUCCEEDED(hr)) {
        // NR-051: GetPath / GetArguments / GetWorkingDirectory return S_FALSE
        // for a link that has no such value (a PIDL-only shortcut to a control
        // panel item or a packaged app), and SUCCEEDED(S_FALSE) is true. The
        // buffers used to be uninitialized, so assign() scanned leftover stack
        // bytes for a NUL -- undefined behavior that feeds garbage into
        // search_alias and the §10.3 identity key, making the same shortcut
        // hash differently between scans. Zero-init makes the existing
        // `[0] != L'\0'` test meaningful, and every buffer now has one.
        wchar_t target[1024] = {};
        if (shell_link->GetPath(target, 1024, nullptr, SLGP_UNCPRIORITY) == S_OK &&
            target[0] != L'\0') {
            result.target.assign(target);
        } else {
            result.web = ShortcutIsWeb(*shell_link);
        }
        wchar_t arguments[1024] = {};
        if (shell_link->GetArguments(arguments, 1024) == S_OK && arguments[0] != L'\0') {
            result.arguments.assign(arguments);
        }
        wchar_t working[1024] = {};
        if (shell_link->GetWorkingDirectory(working, 1024) == S_OK && working[0] != L'\0') {
            const std::wstring normalized = NormalizePathKey(working);
            const std::wstring target_directory =
                NormalizePathKey(ParentDirectory(result.target));
            if (normalized != target_directory) {
                result.working_directory = normalized;
            }
        }
        result.loadable = true;
    }
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
        // NR-028: the shared program-like filter (FR-004a) replaces the old
        // local blacklist. An empty target is skipped here on purpose so a
        // loadable .lnk whose target cannot be resolved stays in the catalog.
        if (!link.target.empty() && !IsProgramLikeTarget(link.target)) {
            return;
        }
    } else if (!IsProgramLikeTarget(path)) {
        // NR-102: a direct .exe / .appref-ms passes the same shared program-like
        // filter (FR-004a) as a shortcut target, so a bare uninstaller
        // executable physically inside the Programs folder is excluded too.
        return;
    }

    AppEntry entry;
    entry.display_name = FileStem(path);
    entry.source = source;
    entry.source_path = path;
    // The shortcut/file path is the Shell-launchable identity (NR-008). The
    // resolved target plus arguments feed the stable id (design-spec §10.3).
    entry.launch_identity = path;
    // NR-047: the resolved target's stem is a secondary search key, so a
    // localized shortcut name ("計算機.lnk") is still reachable by what it
    // actually launches ("calc"). Stem only, never the full path (see the item's
    // Non-goals). Empty for an unresolvable target, which stays in the catalog.
    if (!link.target.empty()) {
        entry.search_alias = FileStem(link.target);
    }
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
        // A custom working directory is launch semantics the bare EXE does not
        // carry, so it must keep this shortcut a distinct identity (§FR-007:
        // only entries that launch the same thing may collapse).
        if (!link.working_directory.empty()) {
            identity_key += L"\n";
            identity_key += link.working_directory;
        }
    }
    entry.stable_id = HashStableId(identity_key);
    out.push_back(std::move(entry));
}

// Returns false only when the walk started but did not finish cleanly
// (NR-091): a mid-walk FindNextFileW failure, including a recursive child's,
// poisons the whole source so the caller keeps the old entries. A missing or
// unreadable directory is still a clean skip (NR-063 empty-walk success). A
// set `cancel` token also yields false at the next safe iteration boundary
// (NR-098), so the collected prefix is never committed as a complete source.
bool EnumerateDirectoryRecursive(const std::wstring& directory, AppSource source,
                                 std::vector<AppEntry>& out,
                                 std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) {
        return false;  // NR-098: cancelled before this subtree: report failure
    }
    const std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW find_data{};
    const HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        return true;  // missing/unreadable directory: skip this subtree, keep others
    }
    bool failed = false;
    do {
        if (cancel && cancel->load()) {
            failed = true;  // NR-098: cancelled mid-walk: no partial commit
            break;
        }
        const std::wstring name = find_data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring full = directory + L"\\" + name;
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                // ponytail: junctions/symlinks are not followed so a loop cannot
                // recurse forever; reparse-point app dirs are not a real source.
                if (!EnumerateDirectoryRecursive(full, source, out, cancel)) {
                    failed = true;  // NR-091: a child's failure must reach the caller
                }
            }
            continue;
        }
        if (!AcceptExtension(full)) {
            continue;
        }
        ProcessFile(full, source, out);
    } while (FindNextFileW(find, &find_data) != FALSE);
    // NR-091: FALSE is a clean end only when it means the list ran out; any
    // other error (I/O, access) means this directory was not fully read, so the
    // collected prefix must not be committed as a complete source.
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        failed = true;
    }
    FindClose(find);
    return !failed;
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

StartMenuEnumerateResult EnumerateStartMenuCatalog(std::atomic<bool>* cancel) {
    StartMenuEnumerateResult result;
    if (cancel && cancel->load()) {
        result.source_ok = false;  // NR-098: cancelled: keep old entries
        return result;
    }
    ComGuard com;
    if (!com.Usable()) {
        result.source_ok = false;  // source-level failure (design-spec §FR-008)
        return result;
    }

    const std::wstring user_root = KnownFolderPath(FOLDERID_Programs);
    if (!user_root.empty() &&
        !EnumerateProgramsDirectory(user_root, AppSource::UserStartMenu, result.entries, cancel)) {
        result.source_ok = false;  // NR-091: mid-walk failure: keep old entries
        if (cancel && cancel->load()) {
            return result;  // NR-098: cancelled: stop before the next root
        }
    }
    const std::wstring common_root = KnownFolderPath(FOLDERID_CommonPrograms);
    if (!common_root.empty() &&
        !EnumerateProgramsDirectory(common_root, AppSource::CommonStartMenu, result.entries, cancel)) {
        result.source_ok = false;  // NR-091: mid-walk failure: keep old entries
    }
    // NR-063: at least one known folder resolved (even to an empty walk) is a
    // success; only when both fail is this a source-level failure. A user who
    // removed the Start Menu folder would otherwise have their old entries wiped
    // on every rebuild.
    if (user_root.empty() && common_root.empty()) {
        result.source_ok = false;
    }
    return result;
}

bool EnumerateProgramsDirectory(const std::wstring& root, AppSource source,
                                std::vector<AppEntry>& out,
                                std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) {
        return false;  // NR-098: cancelled before the walk: report failure
    }
    ComGuard com;
    if (!com.Usable()) {
        return false;  // source-level failure (design-spec §FR-008)
    }
    return EnumerateDirectoryRecursive(root, source, out, cancel);
}

} // namespace nimblerun
