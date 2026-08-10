#include "catalog/directory_walker.h"

#include "test_util.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nimblerun::WalkDirectory;
using nimblerun::WalkOptions;

namespace {

fs::path MakeTempDir() {
    wchar_t buffer[MAX_PATH]{};
    GetTempPathW(MAX_PATH, buffer);
    const fs::path path = fs::path(buffer) /
        (L"NimbleRun_directory_walker_" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(path);
    fs::create_directories(path);
    return path;
}

void WriteFile(const fs::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << 'x';
}

std::size_t CountFiles(const fs::path& root, bool recursive,
                       std::atomic<bool>* cancel = nullptr) {
    std::size_t count = 0;
    Expect(WalkDirectory(root.wstring(), {recursive, cancel},
                         [&](const std::wstring&, DWORD) { ++count; }),
           "walk completes");
    return count;
}

void TestWalkModes(const fs::path& root) {
    WriteFile(root / L"root.txt");
    fs::create_directories(root / L"child");
    WriteFile(root / L"child" / L"nested.txt");
    Expect(CountFiles(root, true) == 2, "recursive walk visits all files");
    Expect(CountFiles(root, false) == 1, "non-recursive walk skips child files");
}

void TestCancellation(const fs::path& root) {
    WriteFile(root / L"first.txt");
    WriteFile(root / L"second.txt");
    std::atomic<bool> cancel = false;
    std::size_t visited = 0;
    Expect(!WalkDirectory(root.wstring(), {true, &cancel},
                          [&](const std::wstring&, DWORD) {
                              ++visited;
                              cancel = true;
                          }),
           "mid-walk cancellation fails");
    Expect(visited == 1, "cancellation stops at the next boundary");
}

void TestMissingDirectory(const fs::path& root) {
    std::size_t missing = 0;
    Expect(WalkDirectory((root / L"missing").wstring(), {true, nullptr},
                         [](const std::wstring&, DWORD) {}, [&] { ++missing; }),
           "missing directory is a clean skip");
    Expect(missing == 1, "missing-directory hook is called once");
}

void TestReparsePoint(const fs::path& root) {
    const fs::path target = root / L"target";
    const fs::path link = root / L"link";
    fs::create_directories(target);
    WriteFile(target / L"hidden.txt");
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY) != 0) {
        std::vector<std::wstring> visited;
        Expect(WalkDirectory(root.wstring(), {true, nullptr},
                             [&](const std::wstring& path, DWORD) { visited.push_back(path); }),
               "reparse-point walk completes");
        Expect(visited.size() == 1 && visited[0].find(L"\\target\\") != std::wstring::npos,
               "reparse-point directory is not followed");
    }
}

} // namespace

int wmain() {
    const fs::path root = MakeTempDir();
    TestWalkModes(root);
    fs::remove_all(root);

    const fs::path cancel_root = MakeTempDir();
    TestCancellation(cancel_root);
    fs::remove_all(cancel_root);

    const fs::path missing_root = MakeTempDir();
    TestMissingDirectory(missing_root);
    fs::remove_all(missing_root);

    const fs::path reparse_root = MakeTempDir();
    TestReparsePoint(reparse_root);
    fs::remove_all(reparse_root);

    std::printf("NR-137 directory walker check PASSED\n");
    return 0;
}
