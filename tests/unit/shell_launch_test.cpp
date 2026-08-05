#include "catalog/stable_id.h"
#include "launch/shell_launch.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using nimblerun::AppEntry;
using nimblerun::AppSource;
using nimblerun::HashStableId;
using nimblerun::LaunchEntry;
using nimblerun::LaunchResult;
using nimblerun::NormalizePathKey;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

std::wstring MakeTempDir(const char* label) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_shell_launch_test_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::wstring(label, label + std::char_traits<char>::length(label));
    fs::remove_all(dir);
    Expect(fs::create_directories(dir), "create temp dir");
    return dir;
}

// Best-effort fixture cleanup so a transient OS/AV hold never fails the test.
void RemoveTreeBestEffort(const std::wstring& path) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::error_code ec;
        fs::remove_all(fs::path(path), ec);
        if (!ec || !fs::exists(fs::path(path), ec)) {
            return;
        }
        Sleep(150);
    }
}

// A catalog-shaped entry, as the user-folder source builds it: the full file
// path is the launch identity.
AppEntry MakeUserFolderEntry(const std::wstring& path) {
    AppEntry entry;
    entry.display_name = L"probe";
    entry.source = AppSource::UserFolder;
    entry.source_path = path;
    entry.launch_identity = path;
    entry.stable_id = HashStableId(NormalizePathKey(path));
    return entry;
}

// Self-terminating helper: a .cmd that writes a marker into its own directory
// (%~dp0) and exits. Shell-launchable exactly like a user-folder .cmd catalog
// entry; no process handle is needed because it exits on its own.
void WriteProbeScript(const std::wstring& script) {
    std::ofstream out(fs::path(script), std::ios::binary | std::ios::trunc);
    out << "@echo off\r\n";
    out << "echo launched > \"%~dp0launched.txt\"\r\n";
    out << "exit /b 0\r\n";
}

void TestRejectsEmptyIdentity() {
    const LaunchResult result = LaunchEntry(MakeUserFolderEntry(L""));
    Expect(!result.ok, "empty launch identity is rejected");
    Expect(result.error_code == ERROR_INVALID_PARAMETER,
           "rejection reports ERROR_INVALID_PARAMETER");
}

void TestLaunchesControlledHelper() {
    const std::wstring base = MakeTempDir("launch");
    const std::wstring script = base + L"\\launch_probe.cmd";
    const std::wstring marker = base + L"\\launched.txt";
    WriteProbeScript(script);

    const LaunchResult result = LaunchEntry(MakeUserFolderEntry(script));
    Expect(result.ok, "valid identity launches through the Shell");
    Expect(result.error_code == ERROR_SUCCESS, "success reports ERROR_SUCCESS");

    // Poll: ShellExecuteExW returns before cmd.exe writes the marker.
    bool marker_seen = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (fs::exists(fs::path(marker))) {
            marker_seen = true;
            break;
        }
        Sleep(100);
    }
    Expect(marker_seen, "the launched helper wrote its marker file");

    RemoveTreeBestEffort(base);
}

} // namespace

int wmain() {
    TestRejectsEmptyIdentity();
    TestLaunchesControlledHelper();
    std::printf("NR-008 shell launch check PASSED\n");
    return 0;
}
