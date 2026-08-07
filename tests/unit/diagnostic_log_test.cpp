#include "diagnostics/diagnostic_log.h"
#include "diagnostics/load_notice.h"
#include "storage/atomic_text_file.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
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

// NR-054: design-spec §10.1 puts the log in logs\, one level under the
// per-user root. The subdirectory does not exist yet; Write must create it.
void TestWritesIntoLogsSubdirectory() {
    const std::wstring root = TempDir();
    const std::wstring logs = root + L"\\logs";

    DiagnosticLog log(logs, L"nimblerun.log");
    log.Write(L"hotkey", L"error 5");

    const std::wstring content = ReadFile(logs + L"\\nimblerun.log");
    Expect(content.find(L"hotkey\terror 5") != std::wstring::npos,
           "record appended in the logs subdirectory");
    Expect(!fs::exists(fs::path(root) / L"nimblerun.log"),
           "no log file appears in the root directory");

    RemoveTreeBestEffort(root);
}

// NR-054: rotation must stay inside logs\; the root directory holds no .log.
void TestRotationStaysInsideLogsSubdirectory() {
    const std::wstring root = TempDir();
    const std::wstring logs = root + L"\\logs";
    DiagnosticLog log(logs, L"nimblerun.log");

    const std::wstring big_detail(64 * 1024, L'x');
    std::uint64_t active_size = 0;
    do {
        log.Write(L"soak", big_detail);
        active_size = fs::exists(fs::path(logs) / L"nimblerun.log")
                          ? fs::file_size(fs::path(logs) / L"nimblerun.log")
                          : 0;
    } while (active_size < DiagnosticLog::kMaxFileBytes);
    log.Write(L"soak", big_detail);  // one more write triggers rotation

    Expect(fs::exists(fs::path(logs) / L"nimblerun.log"),
           "fresh active log exists in logs");
    Expect(fs::exists(fs::path(logs) / L"nimblerun.log.1"),
           "rotated file stays in logs");

    int root_log_files = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(root))) {
        if (entry.path().filename().wstring().find(L".log") != std::wstring::npos) {
            ++root_log_files;
        }
    }
    Expect(root_log_files == 0, "no .log files in the root directory");

    RemoveTreeBestEffort(root);
}

// NR-054: Write is called from the UI thread and the icon worker at the same
// time. The whole check-size / rotate / open-append / write sequence is under a
// mutex, so a rotation can never land between another thread's open and its
// write. Two threads each write lines sized so the combined volume crosses one
// rotation boundary but not a second (which would drop the older .1), then the
// test verifies every line survived intact and exactly 2N lines exist.
void TestConcurrentWritesNeverInterleave() {
    const std::wstring root = TempDir();
    const std::wstring logs = root + L"\\logs";
    DiagnosticLog log(logs, L"nimblerun.log");

    constexpr int kWritesPerThread = 2000;
    // Line = "thread\t<writer>-<index><padding>\n". ~136 bytes each; 4000 lines
    // land between 512 KiB (first rotation) and 1 MiB (second rotation would
    // overwrite .1), so rotation is exercised without dropping any line.
    constexpr int kLineWidth = 128;
    std::vector<std::thread> writers;
    for (int writer = 0; writer < 2; ++writer) {
        writers.emplace_back([&log, writer] {
            for (int i = 0; i < kWritesPerThread; ++i) {
                const std::wstring base =
                    std::to_wstring(writer) + L"-" + std::to_wstring(i);
                const std::wstring detail =
                    base + std::wstring(kLineWidth - base.size(), L'x');
                log.Write(L"thread", detail);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    Expect(fs::exists(fs::path(logs) / L"nimblerun.log"), "active log exists");
    Expect(fs::exists(fs::path(logs) / L"nimblerun.log.1"),
           "rotation was exercised across the threads");

    // Every line in both files must be complete: a single tab splits stage from
    // detail, the line ends with '\n', and the detail matches the writer's own
    // "<writer>-<index>x..." shape -- no interleaved or truncated record.
    int total_lines = 0;
    for (const wchar_t* file : {L"nimblerun.log", L"nimblerun.log.1"}) {
        const std::wstring content = ReadFile(logs + L"\\" + file);
        std::size_t pos = 0;
        while (pos < content.size()) {
            const std::size_t line_end = content.find(L'\n', pos);
            Expect(line_end != std::wstring::npos,
                   "every record ends with a newline");
            const std::wstring line = content.substr(pos, line_end - pos);
            const std::size_t tab = line.find(L'\t');
            Expect(tab != std::wstring::npos,
                   "every line is tab-separated (stage\\tdetail)");
            Expect(line.substr(0, tab) == L"thread", "stage field intact");
            const std::wstring detail = line.substr(tab + 1);
            Expect(detail.size() >= 4 && (detail[0] == L'0' || detail[0] == L'1') &&
                       detail[1] == L'-',
                   "detail starts with the writer id");
            Expect(detail.find_first_not_of(L"0123456789-x") == std::wstring::npos,
                   "detail holds only writer/counter/padding");
            ++total_lines;
            pos = line_end + 1;
        }
    }
    Expect(total_lines == 2 * kWritesPerThread,
           "every written line survived rotation exactly once");

    RemoveTreeBestEffort(root);
}

// NR-058: the notice text is the pure decision behind the balloon. None -> no
// text; corrupt/too-new each have their sentence; both bits -> both sentences.
void TestStoreLoadNoticeText() {
    using nimblerun::StoreLoadIssue;
    const unsigned corrupt = static_cast<unsigned>(StoreLoadIssue::Corrupt);
    const unsigned too_new = static_cast<unsigned>(StoreLoadIssue::TooNew);

    const std::wstring none = nimblerun::StoreLoadNoticeText(0);
    Expect(none.empty(), "no issues -> empty text (nothing to notify)");

    const std::wstring only_corrupt = nimblerun::StoreLoadNoticeText(corrupt);
    Expect(only_corrupt.find(L"Some settings could not be read and were reset") !=
               std::wstring::npos,
           "corrupt-only notice mentions the reset settings");
    Expect(only_corrupt.find(L".corrupt suffix") != std::wstring::npos,
           "corrupt-only notice points at the .corrupt files");
    Expect(only_corrupt.find(L"written by a newer version") == std::wstring::npos,
           "corrupt-only notice does not mention a newer version");

    const std::wstring only_too_new = nimblerun::StoreLoadNoticeText(too_new);
    Expect(only_too_new.find(L"written by a newer version of NimbleRun") !=
               std::wstring::npos,
           "too-new-only notice mentions the newer version");
    Expect(only_too_new.find(L"left unchanged") != std::wstring::npos,
           "too-new-only notice says the originals were left unchanged");
    Expect(only_too_new.find(L"Some settings could not be read") == std::wstring::npos,
           "too-new-only notice does not mention reset settings");

    const std::wstring both = nimblerun::StoreLoadNoticeText(corrupt | too_new);
    Expect(both.find(L"Some settings could not be read and were reset") !=
               std::wstring::npos,
           "combined notice includes the corrupt sentence");
    Expect(both.find(L"written by a newer version of NimbleRun") !=
               std::wstring::npos,
           "combined notice includes the too-new sentence");
}

} // namespace

int wmain() {
    TestAppendsAndSanitizes();
    TestRotationKeepsTwoFiles();
    TestFailedAppendDoesNotThrow();
    TestWritesIntoLogsSubdirectory();
    TestRotationStaysInsideLogsSubdirectory();
    TestConcurrentWritesNeverInterleave();
    TestStoreLoadNoticeText();
    std::printf("NR-017 diagnostic log check PASSED\n");
    return 0;
}
