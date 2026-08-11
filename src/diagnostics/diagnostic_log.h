#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace nimblerun {

// Bounded, rotating local diagnostic log (design-spec §FR-014). One line per
// record: <stage>\t<hex error code>\t<detail>. It never records search text,
// usernames, full personal paths, or command lines — callers pass only
// sanitized stage names, error codes, hashed stable IDs and short details.
// A single active file is capped at kMaxFileBytes; when it would exceed the
// cap it is renamed aside as "<name>.1" (dropping an older ".1") and a fresh
// file is started, so at most 2 files exist. Pure Win32 file I/O; no HWND.
class DiagnosticLog {
public:
    static constexpr std::uint64_t kMaxFileBytes = 512u * 1024u;  // §FR-014

    // `directory` is the per-user data dir; `name` is the log base name.
    DiagnosticLog(std::wstring directory, std::wstring name);

    DiagnosticLog(const DiagnosticLog&) = delete;
    DiagnosticLog& operator=(const DiagnosticLog&) = delete;

    // Appends one record. Newlines and tabs in `stage`/`detail` are stripped so
    // each record stays on one line. Appends are best-effort: a failure never
    // throws or aborts the caller.
    void Write(std::wstring_view stage, std::wstring_view detail) noexcept;

private:
    // NR-054: Write is called from the UI thread and the icon worker
    // (IconStore::WriteLog). Its check-size / rotate / open-append / write
    // sequence is not atomic: a rotation between another thread's open and its
    // write drops that line into the file that just got moved aside, silently
    // losing the diagnostic record for whatever failure was being reported --
    // which is the one moment §11 needs the log to work. Two writers doing
    // short appends: a plain mutex is the whole fix.
    mutable std::mutex write_mutex_;
    std::wstring directory_;
    std::wstring name_;
};

} // namespace nimblerun
