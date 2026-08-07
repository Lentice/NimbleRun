#pragma once

#include <windows.h>

#include <string>

namespace nimblerun {

class DiagnosticLog;
class GlobalHotkey;
class SettingsStore;
class UsageStore;

// Opens the modal settings dialog (native DialogBox) owned by `owner`. Reads
// the current settings from `store` into a SettingsEditor, lets the user edit,
// and Apply()s on OK (hotkey swap through `hotkey`, persist through `store`).
// Clear-usage writes through `usage`; the "Open log folder" button opens
// `log_directory` through the Shell (design-spec §FR-014). A failed reload of
// `store` is logged through `diag` (NR-058: this re-read is user-initiated, so
// it never balloons). Returns true when the settings were applied and persisted.
bool ShowSettingsDialog(HWND owner, SettingsStore& store, UsageStore& usage,
                        GlobalHotkey& hotkey, const std::wstring& log_directory,
                        DiagnosticLog* diag);

} // namespace nimblerun
