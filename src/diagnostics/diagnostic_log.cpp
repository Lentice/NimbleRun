#include "diagnostics/diagnostic_log.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <mutex>
#include <string>
#include <utility>

namespace nimblerun {
namespace {

std::wstring Sanitize(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        if (c == L'\t' || c == L'\r' || c == L'\n') {
            out.push_back(L' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

DiagnosticLog::DiagnosticLog(std::wstring directory, std::wstring name)
    : directory_(std::move(directory)), name_(std::move(name)) {
}

void DiagnosticLog::Write(std::wstring_view stage, std::wstring_view detail) noexcept {
    // NR-174: the mutex lock and every allocation below can throw (system_error,
    // bad_alloc). The contract is never-throws best-effort, so the whole body
    // sits inside an empty catch-all: a logging failure must never abort the
    // caller, even when the caller is itself an error path.
    try {
    // NR-054: the whole body is serialized. Write is called from the UI side
    // and from the icon worker (IconStore::WriteLog), and the check-size /
    // rotate / open-append / write sequence must not interleave -- a rotation
    // between another writer's open and its write drops that line into the
    // file that just got moved aside.
    std::lock_guard<std::mutex> lock(write_mutex_);

    // NR-054: EnsureDirectory creates only one level. `logs` sits one level
    // under the per-user root, which nothing is guaranteed to have created
    // before the icon worker's first write on a fresh install (the worker's
    // IconStore::Open runs before any settings save); ensure the parent first.
    std::wstring parent = directory_;
    const std::size_t separator = parent.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        parent.resize(separator);
    }
    if (!EnsureDirectory(parent)) {
        return;
    }
    if (!EnsureDirectory(directory_)) {
        return;
    }

    const std::wstring path = JoinPath(directory_, name_);
    const std::wstring rotated = JoinPath(directory_, name_ + L".1");

    // Rotate when the active file would exceed the cap: keep the last file as
    // ".1", drop the previous ".1".
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        (info.nFileSizeLow | (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32)) >=
            kMaxFileBytes) {
        DeleteFileW(rotated.c_str());
        MoveFileExW(path.c_str(), rotated.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;  // best-effort: a logging failure never aborts the caller
    }

    std::wstring line;
    line.reserve(stage.size() + detail.size() + 16);
    line += Sanitize(stage);
    line += L'\t';
    line += Sanitize(detail);
    line += L'\n';

    const std::string utf8 = EncodeUtf8(line);
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    } catch (...) {
        return;  // NR-174: never throws; a logging failure must not abort the caller
    }
}

} // namespace nimblerun
