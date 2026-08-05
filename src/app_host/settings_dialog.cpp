#include "app_host/settings_dialog.h"

#include "app_host/hotkey.h"
#include "resources/resource.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "settings/startup_option.h"
#include "usage/usage_store.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

// File-scope dialog state. The app is single-instance and the dialog is modal
// (DialogBox runs a nested message loop on the UI thread), so only one dialog
// exists at a time and the pointers outlive the proc calls.
struct DialogContext {
    SettingsEditor* editor = nullptr;
    SettingsStore* store = nullptr;
    UsageStore* usage = nullptr;
    GlobalHotkey* hotkey = nullptr;
    // NR-014: auto_start as loaded when the dialog opened. Apply reconciles the
    // HKCU Run entry against this, so Reset (working copy flips to defaults)
    // still removes a previously enabled entry.
    bool initial_auto_start = false;
};

DialogContext g_dialog;

std::wstring StringText(SettingsString key) {
    return std::wstring(SettingsStringText(key));
}

void SetStatus(HWND dialog, SettingsString key) {
    SetDlgItemTextW(dialog, IDC_STATUS, StringText(key).c_str());
}

void SetControlText(HWND dialog, int id, SettingsString key) {
    SetDlgItemTextW(dialog, id, StringText(key).c_str());
}

// All user-visible labels come from the centralized string table.
void InitLabels(HWND dialog) {
    SetWindowTextW(dialog, StringText(SettingsString::DialogTitle).c_str());
    SetControlText(dialog, IDC_HOTKEY_GROUP, SettingsString::HotkeyGroup);
    SetControlText(dialog, IDC_HOTKEY_LABEL, SettingsString::HotkeyLabel);
    SetControlText(dialog, IDC_HOTKEY_HINT, SettingsString::HotkeyHint);
    SetControlText(dialog, IDC_LAUNCHER_GROUP, SettingsString::LauncherGroup);
    SetControlText(dialog, IDC_RECENT_LABEL, SettingsString::RecentCountLabel);
    SetControlText(dialog, IDC_HIDE_AFTER_LAUNCH, SettingsString::HideAfterLaunchLabel);
    SetControlText(dialog, IDC_AUTO_START, SettingsString::StartupAutoStartLabel);
    SetControlText(dialog, IDC_THEME_LABEL, SettingsString::ThemeLabel);
    SetControlText(dialog, IDC_FOLDERS_GROUP, SettingsString::UserFoldersGroup);
    SetControlText(dialog, IDC_ADD_FOLDER, SettingsString::AddFolderButton);
    SetControlText(dialog, IDC_REMOVE_FOLDER, SettingsString::RemoveFolderButton);
    SetControlText(dialog, IDC_FOLDER_RECURSIVE, SettingsString::IncludeSubfolders);
    SetControlText(dialog, IDC_EXTENSIONS_GROUP, SettingsString::ExtensionsGroup);
    SetControlText(dialog, IDC_CLEAR_USAGE, SettingsString::ClearUsageButton);
    SetControlText(dialog, IDC_RESET_SETTINGS, SettingsString::ResetSettingsButton);
    SetControlText(dialog, IDOK, SettingsString::OkButton);
    SetControlText(dialog, IDCANCEL, SettingsString::CancelButton);
}

// Re-checks the extension checkboxes from the editor's working list (also used
// to revert a rejected toggle, e.g. the keep-at-least-one guard).
void SyncExtensionChecks(HWND dialog) {
    const std::vector<std::wstring>& exts =
        g_dialog.editor->Working().catalog_extensions;
    const auto has = [&](const wchar_t* ext) {
        return std::find(exts.begin(), exts.end(), ext) != exts.end();
    };
    CheckDlgButton(dialog, IDC_EXT_EXE, has(L".exe") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_EXT_CMD, has(L".cmd") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_EXT_BAT, has(L".bat") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_EXT_LNK, has(L".lnk") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_EXT_APPREF,
                   has(L".appref-ms") ? BST_CHECKED : BST_UNCHECKED);
}

// Pushes the editor's working copy into every control.
void Populate(HWND dialog, const Settings& settings) {
    SetDlgItemTextW(dialog, IDC_HOTKEY_EDIT, settings.hotkey.c_str());
    SetDlgItemTextW(dialog, IDC_RECENT_COUNT_EDIT,
                    std::to_wstring(settings.recent_count).c_str());
    CheckDlgButton(dialog, IDC_HIDE_AFTER_LAUNCH,
                   settings.hide_after_launch ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_AUTO_START,
                   settings.auto_start ? BST_CHECKED : BST_UNCHECKED);

    HWND theme = GetDlgItem(dialog, IDC_THEME_COMBO);
    SendMessageW(theme, CB_RESETCONTENT, 0, 0);
    const auto add_item = [&](SettingsString key) {
        SendMessageW(theme, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(StringText(key).c_str()));
    };
    add_item(SettingsString::ThemeSystem);
    add_item(SettingsString::ThemeLight);
    add_item(SettingsString::ThemeDark);
    SendMessageW(theme, CB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(settings.theme)), 0);

    HWND list = GetDlgItem(dialog, IDC_FOLDERS_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const CatalogRoot& root : settings.catalog_roots) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(root.path.c_str()));
    }
    if (!settings.catalog_roots.empty()) {
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        CheckDlgButton(dialog, IDC_FOLDER_RECURSIVE,
                       settings.catalog_roots[0].recursive ? BST_CHECKED : BST_UNCHECKED);
    }

    SyncExtensionChecks(dialog);
}

int ParseCountText(const wchar_t* text) {
    wchar_t* end = nullptr;
    const long value = wcstol(text, &end, 10);
    if (end == text || *end != L'\0') {
        return -1;
    }
    return static_cast<int>(value);
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
    (void)l_param;
    switch (message) {
    case WM_INITDIALOG:
        InitLabels(dialog);
        Populate(dialog, g_dialog.editor->Working());
        SetDlgItemTextW(dialog, IDC_STATUS, L"");
        return TRUE;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(w_param)) {
        case IDOK: {
            // Free-text fields are validated through the editor on OK.
            wchar_t buffer[256];
            GetDlgItemTextW(dialog, IDC_HOTKEY_EDIT, buffer, 256);
            if (!g_dialog.editor->SetHotkey(buffer)) {
                SetStatus(dialog, SettingsString::HotkeyRejectedNotice);
                Populate(dialog, g_dialog.editor->Working());
                return TRUE;
            }
            GetDlgItemTextW(dialog, IDC_RECENT_COUNT_EDIT, buffer, 256);
            if (!g_dialog.editor->SetRecentCount(ParseCountText(buffer))) {
                SetStatus(dialog, SettingsString::RecentCountNotice);
                Populate(dialog, g_dialog.editor->Working());
                return TRUE;
            }

            // NR-014: reconcile the HKCU Run entry with the requested
            // auto_start, independently of the hotkey swap below. The Run entry
            // is written first and only kept when the persist succeeds; on an
            // Apply failure it is rolled back, so settings.ini and the Run key
            // never diverge. auto_start=true always re-creates/repairs the
            // entry (moved EXE), auto_start=false removes only our value.
            const bool previous_auto_start = g_dialog.initial_auto_start;
            const bool auto_start = g_dialog.editor->Working().auto_start;
            const bool startup_touched = auto_start || auto_start != previous_auto_start;
            bool startup_written = false;
            if (startup_touched) {
                startup_written = nimblerun::SetStartupEnabled(auto_start);
                if (!startup_written) {
                    SetStatus(dialog, SettingsString::StartupFailedNotice);
                    return TRUE;  // keep the dialog open; nothing was persisted
                }
            }

            const auto swapper = [](const HotkeyBinding& proposed) {
                return g_dialog.hotkey->Swap(proposed);
            };
            const SettingsApplyResult result = g_dialog.editor->Apply(*g_dialog.store, swapper);
            if (!result.ok) {
                if (startup_written) {
                    // Undo the registry change so it never diverges from the
                    // (rolled-back) persisted settings. This is a plain registry
                    // call, not routed through the hotkey swapper.
                    nimblerun::SetStartupEnabled(previous_auto_start);
                }
                SetStatus(dialog, result.hotkey_rejected
                    ? SettingsString::HotkeyRejectedNotice
                    : SettingsString::SaveFailedNotice);
                Populate(dialog, g_dialog.editor->Working());  // rolled-back values
                return TRUE;  // keep the dialog open; nothing was lost
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        case IDC_HIDE_AFTER_LAUNCH:
            g_dialog.editor->SetHideAfterLaunch(
                IsDlgButtonChecked(dialog, IDC_HIDE_AFTER_LAUNCH) == BST_CHECKED);
            return TRUE;

        case IDC_AUTO_START:
            g_dialog.editor->SetAutoStart(
                IsDlgButtonChecked(dialog, IDC_AUTO_START) == BST_CHECKED);
            return TRUE;

        case IDC_THEME_COMBO:
            if (HIWORD(w_param) == CBN_SELCHANGE) {
                const LRESULT sel = SendMessageW(
                    GetDlgItem(dialog, IDC_THEME_COMBO), CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_dialog.editor->SetTheme(static_cast<Theme>(sel));
                }
            }
            return TRUE;

        case IDC_FOLDER_RECURSIVE: {
            const LRESULT sel = SendMessageW(
                GetDlgItem(dialog, IDC_FOLDERS_LIST), LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                g_dialog.editor->SetRootRecursive(
                    static_cast<std::size_t>(sel),
                    IsDlgButtonChecked(dialog, IDC_FOLDER_RECURSIVE) == BST_CHECKED);
            }
            return TRUE;
        }

        case IDC_FOLDERS_LIST:
            if (HIWORD(w_param) == LBN_SELCHANGE) {
                const LRESULT sel = SendMessageW(
                    GetDlgItem(dialog, IDC_FOLDERS_LIST), LB_GETCURSEL, 0, 0);
                const std::vector<CatalogRoot>& roots =
                    g_dialog.editor->Working().catalog_roots;
                if (sel != LB_ERR && static_cast<std::size_t>(sel) < roots.size()) {
                    CheckDlgButton(dialog, IDC_FOLDER_RECURSIVE,
                        roots[static_cast<std::size_t>(sel)].recursive
                            ? BST_CHECKED
                            : BST_UNCHECKED);
                }
            }
            return TRUE;

        case IDC_ADD_FOLDER: {
            const std::wstring title = StringText(SettingsString::BrowseFolderTitle);
            BROWSEINFOW browse{};
            browse.hwndOwner = dialog;
            browse.lpszTitle = title.c_str();
            browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    if (!g_dialog.editor->AddRoot(path, true)) {
                        SetStatus(dialog, SettingsString::FolderInvalidNotice);
                    }
                    Populate(dialog, g_dialog.editor->Working());
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }

        case IDC_REMOVE_FOLDER: {
            const LRESULT sel = SendMessageW(
                GetDlgItem(dialog, IDC_FOLDERS_LIST), LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                g_dialog.editor->RemoveRoot(static_cast<std::size_t>(sel));
                Populate(dialog, g_dialog.editor->Working());
            }
            return TRUE;
        }

        case IDC_EXT_EXE:
        case IDC_EXT_CMD:
        case IDC_EXT_BAT:
        case IDC_EXT_LNK:
        case IDC_EXT_APPREF: {
            const wchar_t* extension = nullptr;
            if (LOWORD(w_param) == IDC_EXT_EXE) extension = L".exe";
            else if (LOWORD(w_param) == IDC_EXT_CMD) extension = L".cmd";
            else if (LOWORD(w_param) == IDC_EXT_BAT) extension = L".bat";
            else if (LOWORD(w_param) == IDC_EXT_LNK) extension = L".lnk";
            else if (LOWORD(w_param) == IDC_EXT_APPREF) extension = L".appref-ms";
            const bool enabled = IsDlgButtonChecked(dialog, LOWORD(w_param)) == BST_CHECKED;
            if (!g_dialog.editor->SetExtensionEnabled(extension, enabled)) {
                SyncExtensionChecks(dialog);  // revert a rejected toggle
                SetStatus(dialog, SettingsString::ExtensionsNotice);
            }
            return TRUE;
        }

        case IDC_CLEAR_USAGE:
            if (g_dialog.usage && g_dialog.usage->Clear()) {
                SetStatus(dialog, SettingsString::ClearUsageDoneNotice);
            } else {
                SetStatus(dialog, SettingsString::SaveFailedNotice);
            }
            return TRUE;

        case IDC_RESET_SETTINGS:
            g_dialog.editor->ResetToDefaults();
            Populate(dialog, g_dialog.editor->Working());
            SetStatus(dialog, SettingsString::ResetDoneNotice);
            return TRUE;

        default:
            break;
        }
        break;

    default:
        break;
    }
    return FALSE;
}

} // namespace

bool ShowSettingsDialog(HWND owner, SettingsStore& store, UsageStore& usage,
                        GlobalHotkey& hotkey) {
    Settings current = DefaultSettings();
    store.Load(current);
    SettingsEditor editor(current);

    DialogContext context;
    context.editor = &editor;
    context.store = &store;
    context.usage = &usage;
    context.hotkey = &hotkey;
    context.initial_auto_start = current.auto_start;
    g_dialog = context;

    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner,
        SettingsDialogProc, 0);

    g_dialog = DialogContext{};
    return result == IDOK;
}

} // namespace nimblerun
