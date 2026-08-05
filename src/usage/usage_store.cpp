#include "usage/usage_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nimblerun {
namespace {

constexpr std::wstring_view kFileName = L"usage.tsv";
constexpr std::wstring_view kSchemaPrefix = L"schema=";
constexpr int kSchemaVersion = 1;

std::wstring Trim(std::wstring_view value) {
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

std::vector<std::wstring> SplitLines(std::wstring_view text) {
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

bool ParseInt64(std::wstring_view text, std::int64_t& out) {
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

bool ParseUint64(std::wstring_view text, std::uint64_t& out) {
    const std::wstring value = Trim(text);
    if (value.empty()) {
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

} // namespace

UsageStore::UsageStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

UsageLoadResult UsageStore::Load() {
    records_.clear();

    const std::wstring path = JoinPath(directory_, kFileName);
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return UsageLoadResult::Missing;
        }
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }

    std::wstring text;
    if (!DecodeUtf8(bytes, text) || text.empty()) {
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }
    if (text.front() == L'\uFEFF') {
        text.erase(text.begin());
    }

    const std::vector<std::wstring> lines = SplitLines(text);
    if (lines.empty()) {
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }

    const std::wstring schema_line = Trim(lines[0]);
    if (schema_line.size() <= kSchemaPrefix.size() ||
        schema_line.compare(0, kSchemaPrefix.size(), kSchemaPrefix) != 0) {
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }
    std::int64_t schema = 0;
    if (!ParseInt64(schema_line.substr(kSchemaPrefix.size()), schema)) {
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }
    if (schema > kSchemaVersion) {
        return UsageLoadResult::NewerSchema;
    }
    if (schema != kSchemaVersion) {
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::wstring line = Trim(lines[i]);
        if (line.empty()) {
            continue;
        }
        const std::vector<std::wstring_view> fields = SplitFields(line);
        if (fields.size() != 3) {
            PreserveCorrupt(directory_, kFileName);
            return UsageLoadResult::Corrupt;
        }
        UsageRecord record;
        record.stable_id = UnescapeText(fields[0]);
        if (record.stable_id.empty() ||
            !ParseUint64(fields[1], record.total_launches) ||
            !ParseInt64(fields[2], record.last_launch_utc)) {
            PreserveCorrupt(directory_, kFileName);
            return UsageLoadResult::Corrupt;
        }
        // When the same stable id appears twice, the last line wins.
        auto it = std::find_if(records_.begin(), records_.end(),
            [&](const UsageRecord& r) { return r.stable_id == record.stable_id; });
        if (it != records_.end()) {
            *it = std::move(record);
        } else {
            records_.push_back(std::move(record));
        }
    }
    return UsageLoadResult::Loaded;
}

bool UsageStore::Save() const {
    std::wstring text;
    text += kSchemaPrefix;
    text += std::to_wstring(kSchemaVersion);
    text += L"\n";

    // Write in ascending stable_id order so repeated saves are byte-identical.
    std::vector<const UsageRecord*> ordered;
    ordered.reserve(records_.size());
    for (const UsageRecord& record : records_) {
        ordered.push_back(&record);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const UsageRecord* a, const UsageRecord* b) { return a->stable_id < b->stable_id; });
    for (const UsageRecord* record : ordered) {
        text += EscapeText(record->stable_id);
        text += L'\t';
        text += std::to_wstring(record->total_launches);
        text += L'\t';
        text += std::to_wstring(record->last_launch_utc);
        text += L'\n';
    }

    return AtomicWriteUtf8Text(directory_, kFileName, text);
}

bool UsageStore::Clear() {
    std::vector<UsageRecord> previous = records_;
    records_.clear();
    if (Save()) {
        return true;
    }
    records_ = std::move(previous);
    return false;
}

bool UsageStore::RecordLaunch(std::wstring stable_id, std::int64_t last_launch_utc) {
    if (stable_id.empty()) {
        return false;
    }
    for (UsageRecord& record : records_) {
        if (record.stable_id == stable_id) {
            ++record.total_launches;
            record.last_launch_utc = last_launch_utc;
            return true;
        }
    }
    UsageRecord record;
    record.stable_id = std::move(stable_id);
    record.total_launches = 1;
    record.last_launch_utc = last_launch_utc;
    records_.push_back(std::move(record));
    return true;
}

std::vector<UsageRecord> UsageStore::Recent(int cap) const {
    if (cap <= 0) {
        return {};
    }
    std::vector<UsageRecord> result = records_;
    std::sort(result.begin(), result.end(),
        [](const UsageRecord& a, const UsageRecord& b) {
            if (a.last_launch_utc != b.last_launch_utc) {
                return a.last_launch_utc > b.last_launch_utc;  // newest first
            }
            return a.stable_id < b.stable_id;  // deterministic tie-breaker
        });
    if (result.size() > static_cast<std::size_t>(cap)) {
        result.resize(static_cast<std::size_t>(cap));
    }
    return result;
}

} // namespace nimblerun
