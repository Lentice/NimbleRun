#pragma once

#include <windows.h>

#include <string>
#include <string_view>

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

// Preserve the original file for diagnostics by renaming it aside. Never a
// silent overwrite: the user's corrupt data stays recoverable (design-spec §11).
inline void PreserveCorrupt(std::wstring_view directory, std::wstring_view name) {
    const std::wstring path = JoinPath(directory, name);
    if ((GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return;
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
