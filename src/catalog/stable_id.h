#pragma once

#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>

namespace nimblerun {

// True for drive-letter, UNC and extended-length path strings. Everything else
// (shell: parsing names, AUMIDs, URLs) is a canonical Shell identity and is
// compared exactly, never path-normalized.
inline bool IsPathIdentity(std::wstring_view value) {
    if (value.size() >= 4 &&
        (value.compare(0, 4, L"\\\\?\\") == 0 || value.compare(0, 4, L"\\\\.\\") == 0)) {
        return true;
    }
    if (value.size() >= 3 && value[1] == L':' && (value[2] == L'\\' || value[2] == L'/')) {
        return true;  // drive-letter absolute path
    }
    return value.size() >= 2 && value[0] == L'\\' && value[1] == L'\\';  // UNC
}

// Case-folds and canonicalizes an absolute Windows path so the same physical
// file found by different sources (Start Menu resolved target vs a user-folder
// enumeration) hashes to one stable id (design-spec §10.3): '/' -> '\',
// consecutive separators collapse, a trailing separator is dropped, and the
// extended-length "\\?\" prefix is stripped. Non-path identities pass through
// unchanged.
inline std::wstring NormalizePathKey(std::wstring_view value) {
    if (!IsPathIdentity(value)) {
        return std::wstring(value);
    }
    std::size_t start = 0;
    if (value.size() >= 4 &&
        (value.compare(0, 4, L"\\\\?\\") == 0 || value.compare(0, 4, L"\\\\.\\") == 0)) {
        start = 4;
    }
    std::wstring out;
    out.reserve(value.size() - start);
    while (start < value.size() && (value[start] == L'\\' || value[start] == L'/')) {
        out.push_back(L'\\');  // a UNC root keeps its double backslash
        ++start;
    }
    bool pending_separator = false;
    for (; start < value.size(); ++start) {
        wchar_t c = value[start];
        if (c == L'/') {
            c = L'\\';
        }
        if (c == L'\\') {
            pending_separator = true;
            continue;
        }
        if (pending_separator) {
            out.push_back(L'\\');
            pending_separator = false;
        }
        out.push_back(static_cast<wchar_t>(towlower(static_cast<wint_t>(c))));
    }
    return out;
}

// FNV-1a 64 over the UTF-16 code units, the single stable-id scheme shared by
// every catalog source (design-spec §10.3). Reproducible across runs and
// independent of display name; used for identification only, never as a trust
// decision.
inline std::wstring HashStableId(std::wstring_view value) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const wchar_t c : value) {
        const std::uint64_t code = static_cast<std::uint16_t>(c);
        hash ^= code & 0xFF;
        hash *= 1099511628211ull;
        hash ^= (code >> 8) & 0xFF;
        hash *= 1099511628211ull;
    }
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(16);
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(kHex[(hash >> shift) & 0xF]);
    }
    return out;
}

} // namespace nimblerun
