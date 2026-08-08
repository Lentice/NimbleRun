#include "catalog/appsfolder_catalog.h"

#include "catalog/app_filter.h"
#include "catalog/stable_id.h"

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

// CoTaskMem string -> std::wstring, releasing the Shell allocation.
std::wstring TakeCoTaskString(wchar_t* raw) {
    if (raw == nullptr) {
        return {};
    }
    std::wstring result = raw;
    CoTaskMemFree(raw);
    return result;
}

// The Shell keeps the real program behind an AUMID child in this property.
// Not declared by the toolchain's headers, so it is spelled out here:
// PKEY_Link_TargetParsingPath.
constexpr PROPERTYKEY kLinkTargetParsingPath = {
    {0xb9b4b3fc, 0x2b51, 0x4a42, {0xb5, 0xd8, 0x32, 0x41, 0x46, 0xaf, 0xcf, 0x25}}, 2};

// The absolute path of the program an AppsFolder child launches, or empty for a
// genuinely packaged app (which has no such path) or a Shell that will not
// answer. Display only -- see the call site.
std::wstring LinkTargetPath(IShellItem* child) {
    IShellItem2* item2 = nullptr;
    if (FAILED(child->QueryInterface(IID_PPV_ARGS(&item2)))) {
        return {};
    }
    wchar_t* raw = nullptr;
    const HRESULT hr = item2->GetString(kLinkTargetParsingPath, &raw);
    item2->Release();
    if (FAILED(hr)) {
        return {};
    }
    return TakeCoTaskString(raw);
}

} // namespace

std::wstring ExpandKnownFolderPrefix(const std::wstring& parsing_name) {
    if (parsing_name.empty() || parsing_name.front() != L'{') {
        return {};
    }
    const std::size_t close = parsing_name.find(L'}');
    if (close == std::wstring::npos || close + 1 >= parsing_name.size() ||
        parsing_name[close + 1] != L'\\') {
        return {};
    }
    GUID folder{};
    if (FAILED(IIDFromString(parsing_name.substr(0, close + 1).c_str(), &folder))) {
        return {};
    }
    wchar_t* root = nullptr;
    // SHGetKnownFolderPath's contract: the caller frees the allocation whether
    // the call succeeded or not, so take it unconditionally.
    const HRESULT hr = SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &root);
    std::wstring result = TakeCoTaskString(root);
    if (FAILED(hr) || result.empty()) {
        return {};
    }
    result += parsing_name.substr(close + 1);  // keeps the leading backslash
    return result;
}

bool BuildAppsFolderEntry(const std::wstring& display_name,
                          const std::wstring& parsing_name,
                          AppEntry& out,
                          const std::wstring& resolved_path) {
    if (display_name.empty() || parsing_name.empty()) {
        return false;  // unusable child: caller skips and counts the failure
    }
    // NR-028: only program-like children enter the catalog (FR-004a, shared
    // with the Start Menu source). Documents, websites and uninstallers that
    // leak through AppsFolder are excluded; the caller skips and counts them.
    if (!IsProgramLikeTarget(parsing_name)) {
        return false;
    }
    out = AppEntry{};
    out.display_name = display_name;
    // NR-028: the launch identity is the AppsFolder namespace prefix plus the
    // child's parsing name (design-spec §FR-006). The bare parsing name is not
    // Shell-launchable on its own: AUMIDs and Known Folder GUID-relative paths
    // resolve only inside the AppsFolder namespace.
    out.launch_identity = L"shell:AppsFolder\\" + parsing_name;
    // A resolved absolute path is both nicer to show and the only key that can
    // be compared against the other sources' resolved targets; without one the
    // bare parsing name stands in, exactly as before.
    const bool resolved = IsDisplayablePath(resolved_path);
    out.source_path = resolved ? resolved_path : parsing_name;
    // NR-047: the package-family part of the AUMID is a secondary search key
    // ("Microsoft.WindowsCalculator" reachable by "calc"). Cut at the first '_'
    // to drop the publisher hash, which is identical across every Store app and
    // would otherwise make queries like "8wekyb" return the whole Store.
    out.search_alias = parsing_name.substr(0, parsing_name.find(L'_'));
    out.source = AppSource::AppsFolder;
    // Never the "shell:AppsFolder\" prefix (design-spec §10.3): the prefix is
    // launch-assembly, not identity, so pins and usage survive a change to the
    // assembly rule. When the parsing name resolves to a real path, that path is
    // the identity, which is how this entry and the Start Menu shortcut for the
    // same EXE end up as one row. Deviates from §10.3's "parsing name" wording;
    // GUID-relative AppsFolder items lose their pin/usage once, by design.
    out.stable_id =
        HashStableId(NormalizePathKey(resolved ? resolved_path : parsing_name));
    return true;
}

AppsFolderEnumerateResult EnumerateAppsFolderCatalog() {
    AppsFolderEnumerateResult result;
    ComGuard com;
    if (!com.Usable()) {
        result.source_ok = false;  // source-level failure (design-spec §FR-008)
        return result;
    }

    IShellItem* apps_item = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                    IID_PPV_ARGS(&apps_item)))) {
        result.source_ok = false;  // source-level failure: keep old entries
        return result;
    }

    IEnumShellItems* enumerator = nullptr;
    const HRESULT bind =
        apps_item->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumerator));
    apps_item->Release();
    if (FAILED(bind)) {
        result.source_ok = false;  // source-level failure: keep old entries
        return result;
    }

    for (;;) {
        IShellItem* child = nullptr;
        const HRESULT next = enumerator->Next(1, &child, nullptr);
        if (next == S_FALSE) {
            if (child != nullptr) {
                child->Release();
            }
            break;  // clean end of list
        }
        if (next != S_OK || child == nullptr) {
            if (child != nullptr) {
                child->Release();
            }
            result.source_ok = false;  // enumerator failure: keep old entries
            break;
        }

        wchar_t* raw_name = nullptr;
        std::wstring display_name;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &raw_name))) {
            display_name = TakeCoTaskString(raw_name);
        }
        wchar_t* raw_parsing = nullptr;
        std::wstring parsing_name;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &raw_parsing))) {
            parsing_name = TakeCoTaskString(raw_parsing);
        }
        // Most AUMID children are ordinary desktop apps whose Start Menu
        // shortcut merely declares an AppUserModelID; the Shell still knows
        // their EXE. Fetched only for display: it never feeds the identity key,
        // because unrelated shortcuts share a target (both AutoHotkey entries
        // point at AutoHotkeyUX.exe, Anaconda Prompt at cmd.exe) and would
        // otherwise collapse into one row (§FR-007).
        const std::wstring target_path = LinkTargetPath(child);
        child->Release();

        AppEntry entry;
        if (!BuildAppsFolderEntry(display_name, parsing_name, entry,
                                  ExpandKnownFolderPrefix(parsing_name))) {
            ++result.failed_items;  // skip one child, keep enumerating
            continue;
        }
        if (!IsDisplayablePath(entry.source_path) && IsDisplayablePath(target_path)) {
            entry.source_path = target_path;  // §4.2/§4.9: show the program path
        }
        result.entries.push_back(std::move(entry));
    }

    enumerator->Release();
    return result;
}

} // namespace nimblerun
