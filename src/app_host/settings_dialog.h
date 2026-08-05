#pragma once

#include <windows.h>

namespace nimblerun {

class GlobalHotkey;
class SettingsStore;
class UsageStore;

// Opens the modal settings dialog (native DialogBox) owned by `owner`. Reads
// the current settings from `store` into a SettingsEditor, lets the user edit,
// and Apply()s on OK (hotkey swap through `hotkey`, persist through `store`).
// Clear-usage writes through `usage`. Returns true when the settings were
// applied and persisted.
bool ShowSettingsDialog(HWND owner, SettingsStore& store, UsageStore& usage,
                        GlobalHotkey& hotkey);

} // namespace nimblerun
