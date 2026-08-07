#include "usage/usage_store.h"

#include "storage/atomic_text_file.h"

#include <windows.h>

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

} // namespace

UsageStore::UsageStore(std::wstring directory)
    : directory_(std::move(directory)) {
}

UsageLoadResult UsageStore::Load() {
    records_.clear();

    std::vector<std::wstring> lines;
    switch (ReadVersionedLines(directory_, kFileName, kSchemaVersion, lines)) {
    case VersionedReadStatus::Loaded:
        break;
    case VersionedReadStatus::Missing:
        return UsageLoadResult::Missing;
    case VersionedReadStatus::NewerSchema:
        return UsageLoadResult::NewerSchema;  // original untouched (design-spec §10.4)
    default:  // Unreadable / Malformed / OlderSchema
        PreserveCorrupt(directory_, kFileName);
        return UsageLoadResult::Corrupt;
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
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

bool UsageStore::Forget(std::wstring_view stable_id) {
    if (stable_id.empty()) {
        return false;
    }
    auto it = std::find_if(records_.begin(), records_.end(),
        [&](const UsageRecord& r) { return r.stable_id == stable_id; });
    if (it == records_.end()) {
        return false;
    }
    records_.erase(it);
    return true;
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

int UsageScore(const UsageRecord& record, std::int64_t now_utc) {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    // Compare timestamps rather than subtracting them: usage.tsv can carry any
    // int64_t, and `now_utc - INT64_MIN` is signed overflow. A last_launch in the
    // future (a clock that moved backwards) lands in the top tier, which is the
    // right answer for "most recently launched".
    int bonus = 0;
    if (record.last_launch_utc > now_utc - kDay) {
        bonus = 8;
    } else if (record.last_launch_utc > now_utc - 7 * kDay) {
        bonus = 4;
    } else if (record.last_launch_utc > now_utc - 30 * kDay) {
        bonus = 1;
    }
    // Clamp the count term: a corrupt or absurd total must not overflow int and
    // flip the comparison in the search tie-break.
    const std::uint64_t clamped =
        record.total_launches < 1000000u ? record.total_launches : 1000000u;
    return static_cast<int>(clamped) + bonus;
}

} // namespace nimblerun
