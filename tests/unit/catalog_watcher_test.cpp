// NR-101: CatalogWatcher delivers directory-change notifications as posted
// messages and stops delivering quietly once the notify window is gone. Pure
// Win32 test with a message-only window and a temp directory; no visible UI.
// Post-failure retention is an OS-only path (a PostMessageW to a live window
// cannot be forced to fail deterministically) and is covered by code inspection
// of PostNotification, mirroring how NR-097/NR-098 handled OS-only paths.
#include "app_host/catalog_watcher.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTestClass[] = L"NimbleRun.CatalogWatcherTest";
constexpr UINT kWatchChangedMessage = WM_APP + 100;
HINSTANCE g_instance = nullptr;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

HWND CreateMessageWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = g_instance;
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kTestClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, kTestClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                           nullptr, g_instance, nullptr);
}

fs::path TestDir() {
    const fs::path dir =
        fs::temp_directory_path() / L"NimbleRunTest" / std::to_wstring(GetCurrentProcessId());
    fs::create_directories(dir);
    return dir;
}

void WriteWatchFile(const fs::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "create watched file");
    DWORD written = 0;
    const char bytes[] = "nimblerun-watcher";
    const BOOL ok = ::WriteFile(file, bytes, sizeof(bytes) - 1, &written, nullptr);
    CloseHandle(file);
    Expect(ok != FALSE && written == sizeof(bytes) - 1, "write watched file");
}

// Pumps the message queue until a normal-change notification (watch index 1,
// lParam 0) arrives or the timeout expires. Sleep-based polling so a deadline is
// always honored, mirroring the AnyResultIn pattern in icon_worker_test.
bool ReceiveChange(HWND window, DWORD timeout_ms) {
    const DWORD deadline = GetTickCount() + timeout_ms;
    while (GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kWatchChangedMessage, kWatchChangedMessage,
                            PM_REMOVE)) {
            if (msg.wParam == 1 && msg.lParam == 0) {
                return true;
            }
        }
        Sleep(2);
    }
    return false;
}

// True when any watch notification is still in the queue within wait_ms.
bool AnyNotificationIn(HWND window, DWORD wait_ms) {
    const DWORD deadline = GetTickCount() + wait_ms;
    while (GetTickCount() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, window, kWatchChangedMessage, kWatchChangedMessage,
                            PM_REMOVE)) {
            return true;
        }
        Sleep(2);
    }
    return false;
}

// Arms the watch: SetRoots starts the watcher thread asynchronously, so a single
// file could be created before ReadDirectoryChangesW is armed and its event
// missed. Keep creating files until a change notification arrives (the watch is
// then provably live and re-armed for the real assertion) or the deadline hits.
bool ArmWatch(HWND window, const fs::path& dir, DWORD timeout_ms = 5000) {
    const DWORD deadline = GetTickCount() + timeout_ms;
    for (int i = 0; GetTickCount() < deadline; ++i) {
        WriteWatchFile(dir / (L"arm" + std::to_wstring(i) + L".txt"));
        if (ReceiveChange(window, 500)) {
            return true;
        }
    }
    return false;
}

void TestWatchDeliversChange() {
    const fs::path dir = TestDir();
    const HWND window = CreateMessageWindow();
    {
        nimblerun::CatalogWatcher watcher(window, kWatchChangedMessage);
        watcher.SetRoots({dir}, {true});
        Expect(ArmWatch(window, dir), "the watch is live");
        WriteWatchFile(dir / L"trigger.txt");
        Expect(ReceiveChange(window, 5000),
               "a normal change arrives with the watch index and lParam 0");
        watcher.Stop();
    }
    DestroyWindow(window);
    fs::remove_all(dir);
}

void TestWatchStopsQuietly() {
    // Part 1: after Stop() the watcher thread has joined, so no further
    // notification arrives for the still-valid window.
    {
        const fs::path dir = TestDir();
        const HWND window = CreateMessageWindow();
        {
            nimblerun::CatalogWatcher watcher(window, kWatchChangedMessage);
            watcher.SetRoots({dir}, {true});
            Expect(ArmWatch(window, dir), "watch is live before Stop");
            watcher.Stop();
            // Drain anything already queued, then a new file must not notify.
            MSG msg{};
            while (PeekMessageW(&msg, window, kWatchChangedMessage, kWatchChangedMessage,
                                PM_REMOVE)) {
            }
            WriteWatchFile(dir / L"after-stop.txt");
            Expect(!AnyNotificationIn(window, 400),
                   "no notification arrives after Stop");
        }
        DestroyWindow(window);
        fs::remove_all(dir);
    }
    // Part 2: destroying the notify window makes the watcher stop delivering
    // (the IsWindow guard) without crashing; Stop() still joins cleanly.
    {
        const fs::path dir = TestDir();
        const HWND window = CreateMessageWindow();
        nimblerun::CatalogWatcher watcher(window, kWatchChangedMessage);
        watcher.SetRoots({dir}, {true});
        Expect(ArmWatch(window, dir), "watch is live before teardown");
        DestroyWindow(window);  // HWND invalid while the watcher thread runs
        WriteWatchFile(dir / L"after-teardown.txt");
        Sleep(300);  // let the watcher hit the invalid-window path
        const DWORD start = GetTickCount();
        watcher.Stop();  // must join, never crash or hang
        Expect(GetTickCount() - start < 2000, "Stop after teardown does not hang");
        fs::remove_all(dir);
    }
}

} // namespace

int wmain() {
    g_instance = GetModuleHandleW(nullptr);

    TestWatchDeliversChange();
    TestWatchStopsQuietly();

    std::printf("NR-101 catalog watcher check PASSED\n");
    return 0;
}
