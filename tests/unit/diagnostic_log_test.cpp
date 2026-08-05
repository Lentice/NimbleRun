#include "diagnostics/diagnostic_log.h"
#include "storage/atomic_text_file.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::DiagnosticLog;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

std::wstring TempDir() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    (void)length;
    const std::wstring dir =
        std::wstring(buffer) + L"NimbleRun_diaglog_test_" + std::to_wstring(GetCurrentProcessId());
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

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

std::wstring ReadFile(const std::wstring& path) {
    std::ifstream in(fs::path(path), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring text;
    nimblerun::DecodeUtf8(bytes, text);
    return text;
}

void TestAppendsAndSanitizes() {
    const std::wstring dir = TempDir();
    DiagnosticLog log(dir, L"nimblerun.log");

    log.Write(L"hotkey", L"error 5");
    log.Write(L"catalog", L"hash 0000");
    // Newlines and tabs are stripped so each record stays one line.
    log.Write(L"stage", L"detail with\t tab and\n newline");

    const std::wstring content = ReadFile(dir + L"\\nimblerun.log");
    Expect(content.find(L"hotkey\terror 5") != std::wstring::npos, "record appended");
    Expect(content.find(L"catalog\thash 0000") != std::wstring::npos, "second record appended");
    Expect(content.find(L"detail with  tab and  newline") != std::wstring::npos,
           "control characters stripped to spaces");
    // Three records -> exactly three lines; no embedded newline broke a record.
    int newlines = 0;
    for (const wchar_t c : content) {
        if (c == L'\n') {
            ++newlines;
        }
    }
    Expect(newlines == 3, "each record stays on one line");

    RemoveTreeBestEffort(dir);
}

void TestRotationKeepsTwoFiles() {
    const std::wstring dir = TempDir();
    DiagnosticLog log(dir, L"nimblerun.log");

    // Write large records until the active file exceeds the cap; it must rotate
    // to ".1" and a fresh active file starts.
    const std::wstring big_detail(64 * 1024, L'x');
    int writes = 0;
    std::uint64_t active_size = 0;
    do {
        log.Write(L"soak", big_detail);
        ++writes;
        active_size = fs::exists(fs::path(dir) / L"nimblerun.log")
                          ? fs::file_size(fs::path(dir) / L"nimblerun.log")
                          : 0;
    } while (active_size < DiagnosticLog::kMaxFileBytes);
    Expect(writes > 0, "wrote enough to cross the cap");
    // The active file is now over the cap; one more write triggers rotation.
    log.Write(L"soak", big_detail);

    const bool active_exists = fs::exists(fs::path(dir) / L"nimblerun.log");
    const bool rotated_exists = fs::exists(fs::path(dir) / L"nimblerun.log.1");
    Expect(active_exists, "fresh active log exists after rotation");
    Expect(rotated_exists, "previous file kept as .1 after rotation");

    const std::uint64_t new_active_size =
        fs::file_size(fs::path(dir) / L"nimblerun.log");
    Expect(new_active_size <= DiagnosticLog::kMaxFileBytes,
           "active log stays within the size cap");
    // Only two files ever exist: the active one and the single rotated one.
    int dot_files = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(dir))) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.find(L"nimblerun.log") == 0) {
            ++dot_files;
        }
    }
    Expect(dot_files <= 2, "at most two log files are retained");

    RemoveTreeBestEffort(dir);
}

void TestFailedAppendDoesNotThrow() {
    // A read-only directory makes the append fail; the log must not throw.
    const std::wstring dir = TempDir();
    const std::wstring locked = dir + L"\\locked";
    fs::create_directories(fs::path(locked));
    const std::wstring file = locked + L"\\nimblerun.log";
    std::ofstream create(fs::path(file), std::ios::binary);
    create << "x";
    create.close();
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);

    DiagnosticLog log(locked, L"nimblerun.log");
    log.Write(L"stage", L"detail");  // must not crash

    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveTreeBestEffort(dir);
}

} // namespace

int wmain() {
    TestAppendsAndSanitizes();
    TestRotationKeepsTwoFiles();
    TestFailedAppendDoesNotThrow();
    std::printf("NR-017 diagnostic log check PASSED\n");
    return 0;
}
