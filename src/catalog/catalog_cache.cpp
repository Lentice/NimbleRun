#include "catalog/catalog_cache.h"

#include "catalog/dedup.h"
#include "catalog/stable_id.h"
#include "storage/atomic_text_file.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"catalog.cache";
constexpr std::wstring_view kSchemaPrefix = L"schema=";
constexpr int kSchemaVersion = 1;

constexpr int kFieldCount = 6;  // stable_id, display_name, normalized_name,
                                // launch_identity, source_path, source

std::vector<std::wstring_view> SplitFields(std::wstring_view line) {
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

bool ParseSource(std::wstring_view text, AppSource& out) {
    if (text == L"0") {
        out = AppSource::UserStartMenu;
    } else if (text == L"1") {
        out = AppSource::CommonStartMenu;
    } else if (text == L"2") {
        out = AppSource::AppsFolder;
    } else if (text == L"3") {
        out = AppSource::UserFolder;
    } else {
        return false;
    }
    return true;
}

std::wstring SourceNumber(AppSource source) {
    switch (source) {
    case AppSource::UserStartMenu:
        return L"0";
    case AppSource::CommonStartMenu:
        return L"1";
    case AppSource::AppsFolder:
        return L"2";
    case AppSource::UserFolder:
        return L"3";
    }
    return L"0";
}

std::wstring SerializeEntry(const AppEntry& entry) {
    std::wstring line = EscapeText(entry.stable_id);
    line += L'\t';
    line += EscapeText(entry.display_name);
    line += L'\t';
    line += EscapeText(entry.normalized_name);
    line += L'\t';
    line += EscapeText(entry.launch_identity);
    line += L'\t';
    line += EscapeText(entry.source_path);
    line += L'\t';
    line += SourceNumber(entry.source);
    return line;
}

// cache version 1 is just merged entries; older/none handled by rebuild.
void WriteCache(const std::wstring& directory, const std::vector<AppEntry>& entries) {
    std::wstring text;
    text += kSchemaPrefix;
    text += std::to_wstring(kSchemaVersion);
    text += L'\n';
    for (const AppEntry& entry : entries) {
        text += SerializeEntry(entry);
        text += L'\n';
    }
    AtomicWriteUtf8Text(directory, kFileName, text);
}

} // namespace

void SaveCatalogCache(const std::wstring& directory, const std::vector<AppEntry>& entries) {
    WriteCache(directory, entries);
}

bool LoadCatalogCache(const std::wstring& directory, std::vector<AppEntry>& out) {
    const std::wstring path = JoinPath(directory, kFileName);
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        return false;
    }
    std::wstring text;
    if (!DecodeUtf8(bytes, text) || text.empty()) {
        PreserveCorrupt(directory, kFileName);
        return false;
    }
    if (text.front() == L'\uFEFF') {
        text.erase(text.begin());
    }

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
    if (lines.empty()) {
        PreserveCorrupt(directory, kFileName);
        return false;
    }
    if (lines[0].compare(0, kSchemaPrefix.size(), kSchemaPrefix) != 0) {
        PreserveCorrupt(directory, kFileName);
        return false;
    }
    const std::wstring schema_text = lines[0].substr(kSchemaPrefix.size());
    wchar_t* end = nullptr;
    errno = 0;
    const long long schema = wcstoll(schema_text.c_str(), &end, 10);
    if (errno == ERANGE || end == schema_text.c_str() || *end != L'\0') {
        PreserveCorrupt(directory, kFileName);
        return false;
    }
    if (schema > kSchemaVersion) {
        return false;  // newer schema: leave the file, rebuild
    }
    if (schema != kSchemaVersion) {
        PreserveCorrupt(directory, kFileName);
        return false;
    }

    out.clear();
    out.reserve(lines.size());
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            continue;
        }
        const std::vector<std::wstring_view> fields = SplitFields(lines[i]);
        if (fields.size() != kFieldCount) {
            PreserveCorrupt(directory, kFileName);
            out.clear();
            return false;
        }
        AppEntry entry;
        entry.stable_id = UnescapeText(fields[0]);
        entry.display_name = UnescapeText(fields[1]);
        entry.normalized_name = UnescapeText(fields[2]);
        entry.launch_identity = UnescapeText(fields[3]);
        entry.source_path = UnescapeText(fields[4]);
        if (entry.stable_id.empty() || !ParseSource(fields[5], entry.source)) {
            PreserveCorrupt(directory, kFileName);
            out.clear();
            return false;
        }
        out.push_back(std::move(entry));
    }
    // The cache is a snapshot; run it through dedup so load never reintroduces
    // duplicates from a hand-edited or stale file.
    out = DeduplicateCatalog(out).entries;
    return true;
}

} // namespace nimblerun
