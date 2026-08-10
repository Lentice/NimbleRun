// Focused check for NR-003 (normal global hotkey and conflict handling).
//
// Verifies via Win32 registration results and error codes only: successful
// registration, rejection of an already-registered combo
// (ERROR_HOTKEY_ALREADY_REGISTERED), unregister, and swap rollback (the old
// binding survives a rejected swap). No UI, no windows, no simulated input.
//
// Combos are kept off the default Alt+Space (except the tolerant default
// probe at the end) so the test is deterministic on machines where another
// launcher already owns Alt+Space.

#include "test_util.h"

#include "app_host/hotkey.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

using nimblerun::GlobalHotkey;
using nimblerun::HotkeyBinding;

void Section(const char* name) {
    std::printf("== %s ==\n", name);
}

HotkeyBinding RareA() {
    return {MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F13};
}

HotkeyBinding RareB() {
    return {MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F14};
}

HotkeyBinding AltSpace() {
    return {nimblerun::kDefaultHotkeyModifiers, nimblerun::kDefaultHotkeyVk};
}

// True if `binding` is free to register under `id` (i.e. no thread holds it).
// Releases its own probe registration before returning.
bool IsComboFree(const HotkeyBinding& binding, int id) {
    const bool free = RegisterHotKey(nullptr, id, binding.modifiers, binding.virtual_key) != 0;
    if (free) {
        UnregisterHotKey(nullptr, id);
    }
    return free;
}

} // namespace

int wmain() {
    const HWND null_window = nullptr;

    Section("success");
    {
        GlobalHotkey hotkey;
        const auto result = hotkey.Initialize(null_window, RareA());
        Expect(result.success, "a free combo must register successfully");
        Expect(result.error == ERROR_SUCCESS, "success carries ERROR_SUCCESS");
        Expect(hotkey.IsActive(), "hotkey is active after success");
        Expect(hotkey.Current().virtual_key == VK_F13, "current binding is the registered one");
        hotkey.Shutdown();
        Expect(!hotkey.IsActive(), "shutdown deactivates the hotkey");
    }

    Section("conflict rejection");
    {
        // Simulate another program owning RareB.
        Expect(IsComboFree(RareB(), 100), "test setup: RareB is free");
        Expect(RegisterHotKey(nullptr, 101, RareB().modifiers, RareB().virtual_key) != 0,
               "test setup: RareB now held by another registration");

        GlobalHotkey hotkey;
        const auto result = hotkey.Initialize(null_window, RareB());
        Expect(!result.success, "an already-registered combo is rejected");
        Expect(result.error == ERROR_HOTKEY_ALREADY_REGISTERED,
               "rejection records ERROR_HOTKEY_ALREADY_REGISTERED");
        Expect(!hotkey.IsActive(), "rejected hotkey is not active");

        const auto swap_result = hotkey.Swap(RareB());
        Expect(!swap_result.success, "swap to a conflicting combo is rejected");
        Expect(swap_result.error == ERROR_HOTKEY_ALREADY_REGISTERED,
               "swap rejection records ERROR_HOTKEY_ALREADY_REGISTERED");
        hotkey.Shutdown();
        UnregisterHotKey(nullptr, 101);
    }

    Section("unregister");
    {
        GlobalHotkey hotkey;
        Expect(hotkey.Initialize(null_window, RareA()).success, "register RareA");
        hotkey.Shutdown();
        Expect(IsComboFree(RareA(), 102), "RareA is free after unregister");
    }

    Section("swap rollback keeps old binding");
    {
        GlobalHotkey hotkey;
        Expect(hotkey.Initialize(null_window, RareA()).success, "register RareA");
        // Another registration owns RareB, so the swap must fail.
        Expect(RegisterHotKey(nullptr, 103, RareB().modifiers, RareB().virtual_key) != 0,
               "test setup: RareB held");

        const auto result = hotkey.Swap(RareB());
        Expect(!result.success, "swap to conflicting RareB fails");
        Expect(result.error == ERROR_HOTKEY_ALREADY_REGISTERED,
               "rollback failure records ERROR_HOTKEY_ALREADY_REGISTERED");
        Expect(hotkey.IsActive(), "hotkey stays active after failed swap");
        Expect(hotkey.Current().virtual_key == VK_F13, "old binding is still the current one");
        Expect(!IsComboFree(RareA(), 104), "old RareA binding is still registered");

        hotkey.Shutdown();
        UnregisterHotKey(nullptr, 103);
        Expect(IsComboFree(RareA(), 105), "RareA released after shutdown");
    }

    Section("swap success registers new before releasing old");
    {
        GlobalHotkey hotkey;
        Expect(hotkey.Initialize(null_window, RareA()).success, "register RareA");
        const auto result = hotkey.Swap(RareB());
        Expect(result.success, "swap to a free combo succeeds");
        Expect(hotkey.Current().virtual_key == VK_F14, "current binding is the new one");
        Expect(IsComboFree(RareA(), 106), "old RareA is released after successful swap");
        Expect(!IsComboFree(RareB(), 107), "new RareB is now registered");

        hotkey.Shutdown();
        Expect(IsComboFree(RareB(), 108), "RareB released after shutdown");
    }

    Section("TryRegisterHotkey probe is side-effect free (NR-088)");
    {
        // A free combo probes as registerable and is left free afterwards: a
        // second direct registration must also succeed, proving the probe
        // unregistered its scratch id.
        const auto probe = nimblerun::TryRegisterHotkey(nullptr, RareA());
        Expect(probe.success, "a free combo probes as registerable");
        Expect(probe.error == ERROR_SUCCESS, "probe success carries ERROR_SUCCESS");
        Expect(IsComboFree(RareA(), 111), "the probe released its scratch registration");

        // A held combo probes as a conflict without disturbing the holder.
        Expect(RegisterHotKey(nullptr, 112, RareB().modifiers, RareB().virtual_key) != 0,
               "test setup: RareB held");
        const auto blocked = nimblerun::TryRegisterHotkey(nullptr, RareB());
        Expect(!blocked.success, "a held combo probes as a conflict");
        Expect(blocked.error == ERROR_HOTKEY_ALREADY_REGISTERED,
               "conflict probe records ERROR_HOTKEY_ALREADY_REGISTERED");
        Expect(!IsComboFree(RareB(), 113), "the holder was not disturbed by the probe");
        UnregisterHotKey(nullptr, 112);
    }

    Section("default Alt+Space");
    {
        // Alt+Space may be owned by another launcher on this machine. Both
        // outcomes are correct: it registers, or it is rejected with the
        // expected error while the app keeps running without a hotkey.
        GlobalHotkey hotkey;
        const auto result = hotkey.Initialize(null_window, AltSpace());
        if (result.success) {
            Expect(!IsComboFree(AltSpace(), 109), "default Alt+Space is held while active");
            hotkey.Shutdown();
            Expect(IsComboFree(AltSpace(), 110), "default Alt+Space freed after shutdown");
        } else {
            Expect(result.error == ERROR_HOTKEY_ALREADY_REGISTERED,
                   "Alt+Space conflict is rejected with ERROR_HOTKEY_ALREADY_REGISTERED");
            Expect(!hotkey.IsActive(), "conflicting default hotkey is not active");
        }
    }

    std::printf("NR-003 hotkey registration check PASSED\n");
    return 0;
}
