#include "catalog/appsfolder_catalog.h"

#include "catalog/app_filter.h"
#include "catalog/stable_id.h"

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

// CoTaskMem string -> std::wstring, releasing the Shell allocation.
std::wstring TakeCoTaskString(wchar_t* raw) {
    if (raw == nullptr) {
        return {};
    }
    std::wstring result = raw;
    CoTaskMemFree(raw);
    return result;
}

} // namespace

bool BuildAppsFolderEntry(const std::wstring& display_name,
                          const std::wstring& parsing_name,
                          AppEntry& out) {
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
    out.source_path = parsing_name;
    // NR-047: the package-family part of the AUMID is a secondary search key
    // ("Microsoft.WindowsCalculator" reachable by "calc"). Cut at the first '_'
    // to drop the publisher hash, which is identical across every Store app and
    // would otherwise make queries like "8wekyb" return the whole Store.
    out.search_alias = parsing_name.substr(0, parsing_name.find(L'_'));
    out.source = AppSource::AppsFolder;
    // Identity key stays the bare parsing name (design-spec §10.3): the prefix
    // is launch-assembly, not identity, so pins and usage survive unchanged.
    out.stable_id = HashStableId(NormalizePathKey(parsing_name));
    return true;
}

AppsFolderEnumerateResult EnumerateAppsFolderCatalog() {
    AppsFolderEnumerateResult result;
    ComGuard com;
    if (!com.Usable()) {
        return result;
    }

    IShellItem* apps_item = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                    IID_PPV_ARGS(&apps_item)))) {
        return result;  // source-level failure: empty result, other sources untouched
    }

    IEnumShellItems* enumerator = nullptr;
    const HRESULT bind =
        apps_item->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumerator));
    apps_item->Release();
    if (FAILED(bind)) {
        return result;
    }

    for (;;) {
        IShellItem* child = nullptr;
        const HRESULT next = enumerator->Next(1, &child, nullptr);
        if (next != S_OK) {
            break;  // S_FALSE (end of list) or error: stop this walk
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
        child->Release();

        AppEntry entry;
        if (!BuildAppsFolderEntry(display_name, parsing_name, entry)) {
            ++result.failed_items;  // skip one child, keep enumerating
            continue;
        }
        result.entries.push_back(std::move(entry));
    }

    enumerator->Release();
    return result;
}

} // namespace nimblerun
