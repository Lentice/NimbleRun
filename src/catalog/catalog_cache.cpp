#include "catalog/catalog_cache.h"

#include "catalog/dedup.h"
#include "catalog/stable_id.h"
#include "storage/atomic_text_file.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"catalog.cache";
constexpr std::wstring_view kSchemaPrefix = L"schema=";
constexpr int kSchemaVersion = 2;

constexpr int kFieldCount = 7;  // stable_id, display_name, normalized_name,
                                // launch_identity, source_path, source,
                                // search_alias

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
    line += L'\t';
    line += EscapeText(entry.search_alias);
    return line;
}

// the cache is just merged entries; older/none handled by rebuild.
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

bool LoadCatalogCache(const std::wstring& directory, std::vector<AppEntry>& out,
                      bool* newer_schema) {
    std::vector<std::wstring> lines;
    switch (ReadVersionedLines(directory, kFileName, kSchemaVersion, lines)) {
    case VersionedReadStatus::Loaded:
        break;
    case VersionedReadStatus::Missing:
    case VersionedReadStatus::Unreadable:
        // The cache is a rebuildable snapshot, not user data: a missing or
        // unreadable file is simply rebuilt, never quarantined.
        return false;
    case VersionedReadStatus::Malformed:
        PreserveCorrupt(directory, kFileName);
        return false;
    case VersionedReadStatus::OlderSchema:
        // NR-047: an older schema is a valid file this build cannot read, not a
        // corrupt one. Leave it in place and rebuild over it; quarantining every
        // user's cache on a routine schema bump produces confusing .corrupt
        // files for a non-event.
        return false;
    case VersionedReadStatus::NewerSchema:
        // NR-079: a newer schema is another build's data. design-spec §10.4
        // forbids overwriting it, so report it (the host stops writing for the
        // rest of the run) while the file is left untouched.
        if (newer_schema) {
            *newer_schema = true;
        }
        return false;
    }

    out.clear();
    out.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            continue;
        }
        const std::vector<std::wstring_view> fields = SplitFields(lines[i]);
        if (fields.size() < kFieldCount) {
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
        entry.search_alias = UnescapeText(fields[6]);
        out.push_back(std::move(entry));
    }
    // The cache is a snapshot; run it through dedup so load never reintroduces
    // duplicates from a hand-edited or stale file.
    out = DeduplicateCatalog(out).entries;
    return true;
}

} // namespace nimblerun
