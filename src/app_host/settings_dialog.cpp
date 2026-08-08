#include "app_host/settings_dialog.h"

#include "app_host/hotkey.h"
#include "diagnostics/diagnostic_log.h"
#include "resources/resource.h"
#include "settings/hotkey_capture.h"
#include "settings/settings_editor.h"
#include "settings/settings_store.h"
#include "settings/startup_option.h"
#include "storage/atomic_text_file.h"
#include "usage/usage_store.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
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
    // NR-054: the per-user log directory (design-spec §10.1), opened by the
    // "Open log folder" button.
    std::wstring log_directory;
    // NR-089: the read-only hotkey field's bold display font; created in
    // WM_INITDIALOG, deleted in WM_DESTROY (never leaks across dialog opens).
    HFONT hotkey_font = nullptr;
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

// NR-089: state for the modal hotkey capture dialog. Heap-allocated for the
// dialog's lifetime and freed in its WM_DESTROY. The low-level keyboard hook
// reaches it through g_capture_dialog: the dialog is modal, so the hook
// callback and the dialog proc run on the same thread and never concurrently.
struct CaptureContext {
    SettingsEditor* editor = nullptr;
    HotkeyCaptureState state;
    bool confirmable = false;                // a parseable combo has been captured
    std::optional<HotkeyBinding> candidate;  // the captured combo, when any
};

HWND g_capture_dialog = nullptr;
HHOOK g_capture_hook = nullptr;

// NR-089: WH_KEYBOARD_LL callback for the capture dialog. Runs on the dialog's
// thread (the modal loop pumps it). Only installed while the capture dialog is
// open, so background operation keeps using RegisterHotKey alone (design-spec
// §FR-002 clarification). Swallows the Windows key so the Start menu can never
// pop during capture, and a plain Esc cancels the dialog.
LRESULT CALLBACK CaptureKeyboardProc(int n_code, WPARAM w_param, LPARAM l_param) {
    const bool is_down = w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN;
    const bool is_up = w_param == WM_KEYUP || w_param == WM_SYSKEYUP;
    if (n_code < 0 || !g_capture_dialog || (!is_down && !is_up)) {
        return CallNextHookEx(nullptr, n_code, w_param, l_param);
    }
    auto* context = reinterpret_cast<CaptureContext*>(
        GetWindowLongPtrW(g_capture_dialog, GWLP_USERDATA));
    if (context == nullptr) {
        return CallNextHookEx(nullptr, n_code, w_param, l_param);
    }

    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
    const UINT vk = kb->vkCode;
    const bool is_win = vk == VK_LWIN || vk == VK_RWIN;

    // NR-089 decision 7: a plain Esc (no capture in progress) cancels the
    // dialog like Cancel; modifier+Esc falls through and becomes a combo that
    // ParseHotkey's shell-reserved list rejects as invalid input.
    if (is_down && vk == VK_ESCAPE && !context->state.Capturing()) {
        PostMessageW(g_capture_dialog, WM_COMMAND, IDCANCEL, 0);
        return 1;
    }

    // While nothing is being captured, let dialog navigation through so the
    // Confirm/Cancel buttons stay keyboard-reachable; every other key is
    // capture input (a bare main key is the invalid-input case).
    if (!context->state.Capturing() && !is_win) {
        if (vk == VK_TAB || vk == VK_RETURN || vk == VK_SPACE || vk == VK_UP ||
            vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) {
            return CallNextHookEx(nullptr, n_code, w_param, l_param);
        }
    }

    const HotkeyCaptureState::Event event = context->state.OnKey(vk, is_down);
    if (event.invalid_press) {
        context->state.Reset();
        context->confirmable = false;
        context->candidate.reset();
        SetDlgItemTextW(g_capture_dialog, IDC_CAPTURE_DISPLAY, L"");
        SetDlgItemTextW(g_capture_dialog, IDC_CAPTURE_STATUS,
                        StringText(SettingsString::CaptureInvalidNotice).c_str());
        EnableWindow(GetDlgItem(g_capture_dialog, IDOK), FALSE);
    } else if (event.captured) {
        // NR-089 decisions 4/5: the combo must round-trip through ParseHotkey
        // (the NR-086 shell-reserved list and keys FormatHotkey cannot name
        // make it invalid input that cannot be confirmed); a registration
        // conflict is only a warning and stays confirmable.
        const std::wstring text = FormatHotkey(event.binding);
        HotkeyBinding parsed{};
        const bool parseable = ParseHotkey(text, parsed);
        context->confirmable = parseable;
        context->candidate = event.binding;
        const std::wstring notice =
            !parseable
                ? StringText(SettingsString::CaptureInvalidNotice)
                : (TryRegisterHotkey(g_capture_dialog, event.binding).success
                       ? std::wstring{}
                       : StringText(SettingsString::CaptureConflictNotice));
        SetDlgItemTextW(g_capture_dialog, IDC_CAPTURE_DISPLAY, text.c_str());
        SetDlgItemTextW(g_capture_dialog, IDC_CAPTURE_STATUS, notice.c_str());
        EnableWindow(GetDlgItem(g_capture_dialog, IDOK), parseable ? TRUE : FALSE);
    } else {
        const std::wstring preview = context->state.Preview();
        if (!preview.empty()) {
            SetDlgItemTextW(g_capture_dialog, IDC_CAPTURE_DISPLAY, preview.c_str());
        }
    }

    if (is_win) {
        return 1;  // swallow so the Start menu never opens mid-capture
    }
    return CallNextHookEx(nullptr, n_code, w_param, l_param);
}

INT_PTR CALLBACK HotkeyCaptureDialogProc(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowTextW(dialog, StringText(SettingsString::CaptureDialogTitle).c_str());
        SetDlgItemTextW(dialog, IDC_CAPTURE_PROMPT,
                        StringText(SettingsString::CapturePrompt).c_str());
        SetDlgItemTextW(dialog, IDOK, StringText(SettingsString::OkButton).c_str());
        SetDlgItemTextW(dialog, IDCANCEL, StringText(SettingsString::CancelButton).c_str());
        auto* context = new CaptureContext;
        context->editor = reinterpret_cast<SettingsEditor*>(l_param);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        g_capture_dialog = dialog;
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        g_capture_hook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureKeyboardProc,
                                           GetModuleHandleW(nullptr), 0);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(w_param)) {
        case IDOK: {
            auto* context = reinterpret_cast<CaptureContext*>(
                GetWindowLongPtrW(dialog, GWLP_USERDATA));
            if (context && context->confirmable && context->candidate) {
                // NR-089 decision 9: writing the working copy here; the main
                // dialog's own OK performs the real Apply/Swap/persist.
                if (!context->editor->SetHotkey(FormatHotkey(*context->candidate))) {
                    // Unreachable: capture verified ParseHotkey. Stay open
                    // rather than silently dropping the user's choice.
                    SetDlgItemTextW(dialog, IDC_CAPTURE_STATUS,
                        StringText(SettingsString::CaptureInvalidNotice).c_str());
                    return TRUE;
                }
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_DESTROY:
        // NR-089: every exit path funnels into EndDialog, so WM_DESTROY is the
        // single place the hook and context are torn down -- no early return
        // can leak the hook.
        if (g_capture_hook) {
            UnhookWindowsHookEx(g_capture_hook);
            g_capture_hook = nullptr;
        }
        g_capture_dialog = nullptr;
        if (auto* context = reinterpret_cast<CaptureContext*>(
                GetWindowLongPtrW(dialog, GWLP_USERDATA))) {
            delete context;
            SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
        }
        return TRUE;
    }
    return FALSE;
}

// Opens the capture dialog; returns true when the user confirmed a new combo
// (already written into the editor's working copy).
bool ShowHotkeyCaptureDialog(HWND owner, SettingsEditor& editor) {
    return DialogBoxParamW(GetModuleHandleW(nullptr),
                           MAKEINTRESOURCEW(IDD_HOTKEY_CAPTURE), owner,
                           HotkeyCaptureDialogProc,
                           reinterpret_cast<LPARAM>(&editor)) == IDOK;
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
    SetControlText(dialog, IDC_CATALOG_SOURCES_GROUP, SettingsString::CatalogSourcesGroup);
    SetControlText(dialog, IDC_INCLUDE_WINDOWS_APPS, SettingsString::IncludeWindowsAppsLabel);
    SetControlText(dialog, IDC_FOLDERS_LABEL, SettingsString::UserFoldersLabel);
    SetControlText(dialog, IDC_ADD_FOLDER, SettingsString::AddFolderButton);
    SetControlText(dialog, IDC_REMOVE_FOLDER, SettingsString::RemoveFolderButton);
    SetControlText(dialog, IDC_FOLDER_RECURSIVE, SettingsString::IncludeSubfolders);
    SetControlText(dialog, IDC_EXTENSIONS_LABEL, SettingsString::ExtensionsLabel);
    SetControlText(dialog, IDC_CLEAR_USAGE, SettingsString::ClearUsageButton);
    SetControlText(dialog, IDC_RESET_SETTINGS, SettingsString::ResetSettingsButton);
    SetControlText(dialog, IDC_OPEN_LOG_FOLDER, SettingsString::OpenLogFolderButton);
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
    CheckDlgButton(dialog, IDC_INCLUDE_WINDOWS_APPS,
                   settings.include_windows_apps ? BST_CHECKED : BST_UNCHECKED);
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
        // NR-089: the hotkey field is a read-only display now; give it a
        // slightly larger, bold font so it reads as a value rather than an
        // input box. The font lives exactly as long as this dialog (deleted
        // in WM_DESTROY).
        {
            const HFONT dialog_font = reinterpret_cast<HFONT>(
                SendMessageW(dialog, WM_GETFONT, 0, 0));
            LOGFONTW lf{};
            if (dialog_font) {
                GetObjectW(dialog_font, sizeof(lf), &lf);
            }
            if (lf.lfHeight < 0) {
                lf.lfHeight -= 2;
            } else {
                lf.lfHeight += 2;
            }
            lf.lfWeight = FW_BOLD;
            g_dialog.hotkey_font = CreateFontIndirectW(&lf);
            SendMessageW(GetDlgItem(dialog, IDC_HOTKEY_EDIT), WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_dialog.hotkey_font), TRUE);
        }
        return TRUE;

    case WM_DESTROY:
        if (g_dialog.hotkey_font) {
            DeleteObject(g_dialog.hotkey_font);
            g_dialog.hotkey_font = nullptr;
        }
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

        case IDC_HOTKEY_CHANGE:
            // NR-089: the capture dialog writes the new combo into the working
            // copy; the field is refreshed whether the user confirmed or
            // cancelled. The real Apply/Swap/persist still happens on the main
            // dialog's OK.
            if (ShowHotkeyCaptureDialog(dialog, *g_dialog.editor)) {
                Populate(dialog, g_dialog.editor->Working());
            }
            return TRUE;

        case IDC_HIDE_AFTER_LAUNCH:
            g_dialog.editor->SetHideAfterLaunch(
                IsDlgButtonChecked(dialog, IDC_HIDE_AFTER_LAUNCH) == BST_CHECKED);
            return TRUE;

        case IDC_INCLUDE_WINDOWS_APPS:
            g_dialog.editor->SetIncludeWindowsApps(
                IsDlgButtonChecked(dialog, IDC_INCLUDE_WINDOWS_APPS) == BST_CHECKED);
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

        case IDC_OPEN_LOG_FOLDER: {
            // NR-054: design-spec §FR-014. Hand the directory to the Shell; never
            // build a command line (AGENTS.md). Create the directory first so the
            // user does not get an error dialog on a clean install that has not
            // logged anything yet. The button changes no settings: it never marks
            // the editor dirty and takes no part in Apply/rollback.
            if (!nimblerun::EnsureDirectory(g_dialog.log_directory)) {
                SetStatus(dialog, SettingsString::OpenLogFolderFailedNotice);
                return TRUE;
            }
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"open";
            sei.lpFile = g_dialog.log_directory.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&sei)) {
                SetStatus(dialog, SettingsString::OpenLogFolderFailedNotice);
            }
            return TRUE;
        }

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
                        GlobalHotkey& hotkey, const std::wstring& log_directory,
                        DiagnosticLog* diag) {
    Settings current = DefaultSettings();
    // NR-058: the dialog's own re-read surfaces what it shows; only a log line,
    // never a balloon. The switch keeps every load result accounted for.
    const SettingsLoadResult load_result = store.Load(current);
    switch (load_result) {
    case SettingsLoadResult::Loaded:
        break;
    case SettingsLoadResult::Missing:
        if (diag) {
            diag->Write(L"settings_load", L"result=Missing");
        }
        break;
    case SettingsLoadResult::Corrupt:
        if (diag) {
            diag->Write(L"settings_load", L"result=Corrupt");
        }
        break;
    case SettingsLoadResult::NewerSchema:
        if (diag) {
            diag->Write(L"settings_load", L"result=NewerSchema");
        }
        break;
    }
    SettingsEditor editor(current);

    DialogContext context;
    context.editor = &editor;
    context.store = &store;
    context.usage = &usage;
    context.hotkey = &hotkey;
    context.initial_auto_start = current.auto_start;
    context.log_directory = log_directory;
    g_dialog = context;

    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner,
        SettingsDialogProc, 0);

    g_dialog = DialogContext{};
    return result == IDOK;
}

} // namespace nimblerun