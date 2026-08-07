#pragma once

#include <windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {

// Shared persistence helpers for the versioned line-oriented text stores
// (settings.ini, usage.tsv). Every store uses the same conventions: UTF-8 on
// disk, a schema version on the first line, and a tmp + flush + atomic replace
// write so a crash mid-write never corrupts the real file (design-spec §10.2).

inline std::wstring JoinPath(std::wstring_view directory, std::wstring_view name) {
    return std::wstring(directory) + L"\\" + std::wstring(name);
}

inline bool EnsureDirectory(std::wstring_view directory) {
    if (CreateDirectoryW(std::wstring(directory).c_str(), nullptr) != FALSE) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

inline bool DecodeUtf8(std::string_view utf8, std::wstring& out) {
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), out.data(), required);
    return true;
}

inline std::string EncodeUtf8(std::wstring_view text) {
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), required, nullptr, nullptr);
    return out;
}

inline bool ReadAllBytes(const std::wstring& path, std::string& out) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    out.clear();
    char buffer[4096];
    BOOL ok = TRUE;
    while (ok != FALSE) {
        DWORD read = 0;
        ok = ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
        if (ok == FALSE) {
            break;
        }
        if (read == 0) {
            break;
        }
        out.append(buffer, read);
    }
    const bool success = ok != FALSE;
    CloseHandle(file);
    return success;
}

// Backslash-escapes '\\', '=', and control characters so a value never corrupts
// the line- or tab-oriented structure. Decode with UnescapeText.
inline std::wstring EscapeText(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const wchar_t c : value) {
        switch (c) {
            case L'\\':
                out += L"\\\\";
                break;
            case L'=':
                out += L"\\=";
                break;
            case L'\n':
                out += L"\\n";
                break;
            case L'\r':
                out += L"\\r";
                break;
            case L'\t':
                out += L"\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

inline std::wstring UnescapeText(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size()) {
            const wchar_t escaped = value[i + 1];
            switch (escaped) {
                case L'n':
                    out.push_back(L'\n');
                    break;
                case L'r':
                    out.push_back(L'\r');
                    break;
                case L't':
                    out.push_back(L'\t');
                    break;
                case L'\\':
                    out.push_back(L'\\');
                    break;
                case L'=':
                    out.push_back(L'=');
                    break;
                default:
                    // Unknown escape keeps the backslash so raw values
                    // survive a hand-edited file verbatim.
                    out.push_back(L'\\');
                    out.push_back(escaped);
                    break;
            }
            ++i;
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

inline std::wstring Trim(std::wstring_view value) {
    const auto is_space = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && is_space(value[begin])) {
        ++begin;
    }
    while (end > begin && is_space(value[end - 1])) {
        --end;
    }
    return std::wstring(value.substr(begin, end - begin));
}

inline std::vector<std::wstring> SplitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            std::wstring line(text.substr(start, i - start));
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    return lines;
}

// Splits a TSV row on tabs. Views point into `line`, which outlives the use.
inline std::vector<std::wstring_view> SplitFields(std::wstring_view line) {
    std::vector<std::wstring_view> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == L'\t') {
            fields.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

inline bool ParseInt64(std::wstring_view text, std::int64_t& out) {
    const std::wstring value = Trim(text);
    if (value.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const long long parsed = wcstoll(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != L'\0') {
        return false;
    }
    out = parsed;
    return true;
}

inline bool ParseUint64(std::wstring_view text, std::uint64_t& out) {
    const std::wstring value = Trim(text);
    if (value.empty()) {
        return false;
    }
    // NR-070: C's wcstoull accepts a leading '-' and wraps it modulo 2^64
    // ("-1" -> ULLONG_MAX) without setting ERANGE, so a hand-edited data file
    // with a negative unsigned field would load as a legal value and pollute
    // the store. Reject the sign here; the caller's corrupt path isolates the
    // file.
    if (value.front() == L'-') {
        return false;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = wcstoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != L'\0') {
        return false;
    }
    out = static_cast<std::uint64_t>(parsed);
    return true;
}

// The versioned-header check every store shares, reported as a status instead
// of a yes/no. Each caller keeps its own disposition: the user-data stores
// quarantine corrupt files via PreserveCorrupt, the rebuildable cache does not.
enum class VersionedReadStatus {
    Loaded,       // file read, UTF-8 decodable, header version == expected_schema
    Missing,      // file does not exist (ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND)
    Unreadable,   // exists but could not be read (permissions, lock, ...)
    Malformed,    // not decodable UTF-8, empty, no schema= header, or non-integer version
    OlderSchema,  // header version < expected_schema
    NewerSchema,  // header version > expected_schema
};

// Reads <directory>\<name>, strips a BOM, splits into lines and validates the
// first line's schema= header. On Loaded or OlderSchema, `lines` receives the
// data lines without the header (verbatim, not Trimmed) -- OlderSchema still
// gets them so a caller that knows how to migrate an older format forward can
// (NR-062); a caller that does not want to migrate simply does not read
// `lines` in that branch. On any other status `lines` is left empty. This
// function never renames, writes or deletes any file: disposition is the
// caller's decision.
inline VersionedReadStatus ReadVersionedLines(std::wstring_view directory,
                                              std::wstring_view name,
                                              int expected_schema,
                                              std::vector<std::wstring>& lines) {
    lines.clear();

    const std::wstring path = JoinPath(directory, name);
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return VersionedReadStatus::Missing;
        }
        return VersionedReadStatus::Unreadable;
    }

    std::wstring text;
    if (!DecodeUtf8(bytes, text) || text.empty()) {
        return VersionedReadStatus::Malformed;
    }
    if (text.front() == L'\uFEFF') {
        text.erase(text.begin());
    }

    const std::vector<std::wstring> all_lines = SplitLines(text);
    if (all_lines.empty()) {
        return VersionedReadStatus::Malformed;
    }

    constexpr std::wstring_view kSchemaPrefix = L"schema=";
    const std::wstring schema_line = Trim(all_lines[0]);
    if (schema_line.size() <= kSchemaPrefix.size() ||
        schema_line.compare(0, kSchemaPrefix.size(), kSchemaPrefix) != 0) {
        return VersionedReadStatus::Malformed;
    }
    std::int64_t schema = 0;
    if (!ParseInt64(schema_line.substr(kSchemaPrefix.size()), schema)) {
        return VersionedReadStatus::Malformed;
    }
    // NR-062: populate `lines` before the version comparison, not just on the
    // Loaded path. A caller that must migrate an older format forward (e.g.
    // PinStore upgrading a schema=1 favorites.txt) needs the actual data lines
    // on OlderSchema, not an empty vector -- otherwise "handle OlderSchema as
    // valid" silently degrades into "treat it as empty", which is the data
    // loss this comment is here to prevent. Callers that leave the file
    // untouched on a schema mismatch (the common case) simply do not read
    // `lines` in that branch, so this is free for them.
    lines.reserve(all_lines.size() - 1);
    for (std::size_t i = 1; i < all_lines.size(); ++i) {
        lines.push_back(all_lines[i]);
    }

    if (schema > expected_schema) {
        return VersionedReadStatus::NewerSchema;
    }
    if (schema != expected_schema) {
        return VersionedReadStatus::OlderSchema;
    }
    return VersionedReadStatus::Loaded;
}

// Preserve the original file for diagnostics by renaming it aside. Never a
// silent overwrite: the user's corrupt data stays recoverable (design-spec §11).
inline void PreserveCorrupt(std::wstring_view directory, std::wstring_view name) {
    const std::wstring path = JoinPath(directory, name);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return;   // no file, or that name is a directory: nothing to preserve
    }
    const std::wstring corrupt = JoinPath(directory, std::wstring(name) + L".corrupt");
    MoveFileExW(path.c_str(), corrupt.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Writes text as UTF-8 to <dir>\<name>.tmp, flushes, then atomically replaces
// <dir>\<name>. On any failure the original file is untouched and the temp file
// is deleted (best effort). Returns false on any I/O error.
inline bool AtomicWriteUtf8Text(std::wstring_view directory, std::wstring_view name,
                                std::wstring_view text) {
    if (!EnsureDirectory(directory)) {
        return false;
    }

    const std::wstring temp_path = JoinPath(directory, std::wstring(name) + L".tmp");
    const std::wstring final_path = JoinPath(directory, name);
    const std::string utf8 = EncodeUtf8(text);

    const HANDLE file = CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = false;
    if (utf8.size() <= static_cast<std::size_t>(MAXDWORD)) {
        DWORD written = 0;
        const BOOL wrote = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        ok = wrote != FALSE && written == static_cast<DWORD>(utf8.size()) &&
             FlushFileBuffers(file) != FALSE;
    }
    if (CloseHandle(file) == FALSE) {
        ok = false;
    }
    if (!ok) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    if (MoveFileExW(temp_path.c_str(), final_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        DeleteFileW(temp_path.c_str());
        return false;
    }
    return true;
}

} // namespace nimblerun
