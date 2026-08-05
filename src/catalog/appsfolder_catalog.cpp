#include "catalog/appsfolder_catalog.h"

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
    out = AppEntry{};
    out.display_name = display_name;
    // The Shell parsing name is the canonical launch identity NR-008 can hand to
    // the Shell, and the identity a later icon query can resolve. Packaged apps
    // have no filesystem path, so it doubles as source_path.
    out.launch_identity = parsing_name;
    out.source_path = parsing_name;
    out.source = AppSource::AppsFolder;
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
