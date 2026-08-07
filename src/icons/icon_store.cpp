#include "icons/icon_store.h"

#include "diagnostics/diagnostic_log.h"
#include "pins/pin_store.h"

#include <cstring>
#include <utility>

namespace nimblerun {
namespace {

constexpr std::uint64_t kGrowGranularity = 64u * 1024u;

std::wstring KeyFor(const std::wstring& stable_id, int variant) {
    return stable_id + L'|' + std::to_wstring(variant);
}

// The atomic replace can transiently fail with a sharing/access error when a
// real-time scanner opens the freshly written pack for a look. Retry a few
// times with a short pause; a persistent failure still degrades to "no cache"
// and the next Flush retries the compaction.
bool ReplaceFileWithRetry(const std::wstring& tmp_path, const std::wstring& pack_path) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(tmp_path.c_str(), pack_path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
            return true;
        }
        if (attempt < 4) {
            Sleep(10);
        }
    }
    return false;
}

} // namespace

IconStore::IconStore(IconStorePaths paths, std::uint64_t max_bytes, DiagnosticLog* log)
    : pack_path_(std::move(paths.pack)), max_bytes_(max_bytes), log_(log) {}

IconStore::~IconStore() {
    Unmap();
}

void IconStore::WriteLog(std::wstring_view stage, std::wstring_view detail) {
    if (log_ != nullptr) {
        log_->Write(stage, detail);
    }
}

bool IconStore::MapFile() {
    file_ = CreateFileW(pack_path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size;
    if (GetFileSizeEx(file_, &size) == FALSE) {
        Unmap();
        return false;
    }
    view_size_ = static_cast<std::size_t>(size.QuadPart);
    file_size_ = static_cast<std::uint64_t>(size.QuadPart);
    if (view_size_ == 0) {
        // An empty (or just-created) file: leave no mapping. DecodeHeader
        // classifies size < kPayloadStart as Absent without touching data.
        view_ = nullptr;
        return true;
    }
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping_ == nullptr) {
        Unmap();
        return false;
    }
    view_ = static_cast<std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, 0));
    if (view_ == nullptr) {
        Unmap();
        return false;
    }
    return true;
}

void IconStore::Unmap() {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    view_size_ = 0;
}

bool IconStore::CreateEmptyPack() {
    const std::wstring tmp_path = pack_path_.wstring() + L".tmp";
    const std::vector<std::uint8_t> pack = MakeEmptyPack();
    const HANDLE file = CreateFileW(tmp_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool wrote_ok = WriteFile(file, pack.data(), static_cast<DWORD>(pack.size()),
                                    &written, nullptr) != FALSE &&
                          written == pack.size() && FlushFileBuffers(file) != FALSE;
    const bool close_ok = CloseHandle(file) != FALSE;
    if (!wrote_ok || !close_ok) {
        DeleteFileW(tmp_path.c_str());
        return false;
    }
    if (!ReplaceFileWithRetry(tmp_path, pack_path_.wstring())) {
        DeleteFileW(tmp_path.c_str());
        return false;
    }
    return true;
}

void IconStore::ScanIndex() {
    const bool recreated = stats_.recreated;  // set by Open before a rescan
    entries_by_slot_ = {};
    in_use_ = {};
    key_to_slot_.clear();
    stats_ = StoreStats{};
    stats_.recreated = recreated;
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        PackEntry entry;
        const EntryStatus status = DecodeEntry(view_, view_size_, header_, slot, entry);
        if (status == EntryStatus::Ok) {
            entries_by_slot_[slot] = entry;
            in_use_[slot] = true;
            key_to_slot_[SlotKey{entry.stable_id_hash, entry.variant}] = slot;
            ++stats_.entries;
            stats_.payload_bytes += entry.payload_len;
        } else if (status != EntryStatus::Free) {
            ++stats_.dropped_entries;
        }
    }
    stats_.dead_bytes = file_size_ > kPayloadStart + stats_.payload_bytes
                            ? file_size_ - kPayloadStart - stats_.payload_bytes
                            : 0;
}

IconStore::StoreState IconStore::Open() {
    if (state_ != StoreState::Disabled) {
        return state_;
    }
    if (pack_path_.empty()) {
        state_ = StoreState::Disabled;
        return state_;
    }

    const bool mapped = MapFile();
    if (mapped) {
        PackHeader header;
        const PackStatus status = DecodeHeader(view_, view_size_, header);
        switch (status) {
        case PackStatus::Ok: {
            header_ = header;
            // Locate which header slot this came from so Flush commits to the
            // other one (the two slots alternate to survive torn writes).
            std::array<std::uint8_t, kHeaderSize> probe{};
            EncodeHeader(header_, probe.data());
            header_slot_ = 0;
            for (std::size_t slot = 0; slot < kHeaderSlotCount; ++slot) {
                if (std::memcmp(probe.data(), view_ + slot * kHeaderSize, kHeaderSize) == 0) {
                    header_slot_ = slot;
                    break;
                }
            }
            ScanIndex();
            if (stats_.dropped_entries > 0) {
                WriteLog(L"icon-store", L"entries-dropped:" + std::to_wstring(stats_.dropped_entries));
            }
            state_ = StoreState::Ready;
            return state_;
        }
        case PackStatus::NewerSchema:
            Unmap();
            WriteLog(L"icon-store", L"newer-schema");
            state_ = StoreState::Disabled;  // leave the file untouched
            return state_;
        case PackStatus::BadMagic:
        case PackStatus::BothHeadersBad:
            Unmap();
            DeleteFileW(pack_path_.c_str());
            stats_.recreated = true;
            WriteLog(L"icon-store", L"recreated");
            break;
        case PackStatus::Absent:
            Unmap();
            WriteLog(L"icon-store", L"created");
            break;
        }
    } else {
        // MapFile failed. Only a missing file is recoverable by creating one;
        // anything else (locked, permission) disables the store permanently.
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            WriteLog(L"icon-store", L"open-failed");
            state_ = StoreState::Disabled;
            return state_;
        }
        WriteLog(L"icon-store", L"created");
    }

    if (CreateEmptyPack() && MapFile()) {
        ScanIndex();
        state_ = StoreState::Ready;
        return state_;
    }
    state_ = StoreState::Disabled;
    return state_;
}

std::size_t IconStore::FindFreeSlot(const std::vector<bool>& touched) const {
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (!in_use_[slot] && !touched[slot]) {
            return slot;
        }
    }
    return kIndexCapacity;
}

std::uint64_t IconStore::LivePayloadBytes() const {
    std::uint64_t total = 0;
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (in_use_[slot]) {
            total += entries_by_slot_[slot].payload_len;
        }
    }
    return total;
}

bool IconStore::EvictOne(const std::unordered_set<std::uint64_t>& pinned_hashes,
                         const std::vector<bool>& touched, bool allow_pinned) {
    std::size_t best = kIndexCapacity;
    std::uint64_t best_used = 0;
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (!in_use_[slot] || touched[slot]) {
            continue;
        }
        const PackEntry& entry = entries_by_slot_[slot];
        if (!allow_pinned && pinned_hashes.count(entry.stable_id_hash) != 0) {
            continue;
        }
        if (best == kIndexCapacity || entry.last_used_utc < best_used ||
            (entry.last_used_utc == best_used && slot < best)) {
            best = slot;
            best_used = entry.last_used_utc;
        }
    }
    if (best == kIndexCapacity) {
        return false;
    }
    key_to_slot_.erase(SlotKey{entries_by_slot_[best].stable_id_hash,
                               entries_by_slot_[best].variant});
    in_use_[best] = false;
    return true;
}

bool IconStore::GrowView(std::uint64_t needed) {
    std::uint64_t target = needed;
    if (target % kGrowGranularity != 0) {
        target += kGrowGranularity - target % kGrowGranularity;
    }
    LARGE_INTEGER size;
    size.QuadPart = static_cast<LONGLONG>(target);
    if (SetFilePointerEx(file_, size, nullptr, FILE_BEGIN) == FALSE ||
        SetEndOfFile(file_) == FALSE) {
        return false;
    }
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
        // NR-050: view_ and view_size_ must move together. Leaving a stale
        // non-zero size behind a null pointer defeats every downstream bounds
        // check -- DecodeEntry's `size < kPayloadStart` guard passes on the old
        // size and then dereferences nullptr + kIndexOffset.
        view_size_ = 0;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (mapping_ == nullptr) {
        return false;
    }
    view_ = static_cast<std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, 0));
    if (view_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }
    view_size_ = target;
    file_size_ = target;
    return true;
}

std::vector<std::uint8_t> IconStore::Lookup(const std::wstring& stable_id, int variant,
                                            std::uint64_t source_stamp, std::uint64_t now_utc) {
    if (state_ == StoreState::Disabled || view_ == nullptr) {
        return {};
    }
    std::uint64_t hash = 0;
    if (!ParseStableIdHash(stable_id, hash)) {
        return {};
    }
    const auto it = key_to_slot_.find(SlotKey{hash, static_cast<std::uint16_t>(variant)});
    if (it == key_to_slot_.end()) {
        return {};
    }
    const PackEntry& entry = entries_by_slot_[it->second];
    if (entry.source_stamp != 0) {
        // Real source file: the caller's current stamp must match the one the
        // icon was fetched under, otherwise the source changed (miss).
        if (entry.source_stamp != source_stamp) {
            return {};
        }
    } else {
        // No file to stat (AppsFolder / AUMID): fall back to a TTL, same
        // retention as pins (design-spec §FR-011, one constant source).
        if (now_utc > entry.fetched_utc + static_cast<std::uint64_t>(kPinRetentionSeconds)) {
            return {};
        }
    }
    if (!VerifyPayload(view_, view_size_, entry)) {
        return {};
    }
    return {view_ + entry.payload_offset, view_ + entry.payload_offset + entry.payload_len};
}

void IconStore::Put(const std::wstring& stable_id, int variant,
                    std::vector<std::uint8_t> payload,
                    std::uint64_t source_stamp, std::uint64_t now_utc) {
    // NR-068: only a Ready store accepts writes. Disabled and ReadOnly both
    // reject (icon_store.h: "readable, writes rejected"), and Flush digests
    // pending_ only when Ready -- accepting a Put in any other state would grow
    // pending_ without bound. Fire-and-forget: the worker never needs to know.
    if (state_ != StoreState::Ready || payload.empty()) {
        return;
    }
    std::uint64_t hash = 0;
    if (!ParseStableIdHash(stable_id, hash)) {
        return;
    }
    Pending pending;
    pending.stable_id_hash = hash;
    pending.variant = variant;
    pending.payload = std::move(payload);
    pending.source_stamp = source_stamp;
    pending.fetched_utc = now_utc;
    pending.last_used_utc = now_utc;
    pending_[KeyFor(stable_id, variant)] = std::move(pending);
}

bool IconStore::Flush(const std::vector<std::wstring>& pinned_ids, std::uint64_t now_utc) {
    // NR-050: Ready without a live view is only reachable via a failed Compact
    // remap; reject instead of memcpy-ing into a null pointer.
    if (state_ != StoreState::Ready || view_ == nullptr) {
        return false;
    }
    if (pending_.empty()) {
        return true;
    }

    // A payload that fails its CRC on disk is reclaimed as a free slot here, so
    // the next Put can reuse it (NR-035 acceptance: payload corruption poisons
    // one entry, never the pack).
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (!in_use_[slot] || view_ == nullptr) {
            continue;
        }
        const PackEntry& entry = entries_by_slot_[slot];
        if (!VerifyPayload(view_, view_size_, entry)) {
            key_to_slot_.erase(SlotKey{entry.stable_id_hash, entry.variant});
            in_use_[slot] = false;
            ++stats_.dropped_entries;
        }
    }

    std::unordered_set<std::uint64_t> pinned_hashes;
    for (const std::wstring& id : pinned_ids) {
        std::uint64_t hash = 0;
        if (ParseStableIdHash(id, hash)) {
            pinned_hashes.insert(hash);
        }
    }

    struct Write {
        std::size_t slot = 0;
        PackEntry entry;
        std::vector<std::uint8_t> payload;
    };
    std::vector<Write> writes;
    writes.reserve(pending_.size());
    std::vector<bool> touched(kIndexCapacity, false);

    // 1. Assign a slot to every pending put (reusing the existing one on
    //    overwrite). Slot pressure evicts the oldest non-pinned entry first.
    for (auto& [key, pending] : pending_) {
        const SlotKey slot_key{pending.stable_id_hash, static_cast<std::uint16_t>(pending.variant)};
        std::size_t slot = kIndexCapacity;
        const auto existing = key_to_slot_.find(slot_key);
        if (existing != key_to_slot_.end()) {
            slot = existing->second;
        } else {
            slot = FindFreeSlot(touched);
            while (slot == kIndexCapacity) {
                if (!EvictOne(pinned_hashes, touched, /*allow_pinned=*/false) &&
                    !EvictOne(pinned_hashes, touched, /*allow_pinned=*/true)) {
                    break;
                }
                slot = FindFreeSlot(touched);
            }
            if (slot == kIndexCapacity) {
                continue;  // nothing to evict; drop this put (should not happen)
            }
        }
        touched[slot] = true;
        Write write;
        write.slot = slot;
        write.entry.stable_id_hash = pending.stable_id_hash;
        write.entry.variant = static_cast<std::uint16_t>(pending.variant);
        write.entry.in_use = true;
        write.entry.payload_len = static_cast<std::uint32_t>(pending.payload.size());
        write.entry.payload_offset = 0;
        write.entry.payload_crc32 = 0;
        write.entry.source_stamp = pending.source_stamp;
        write.entry.fetched_utc = pending.fetched_utc;
        write.entry.last_used_utc = now_utc;  // LRU moment is the flush, not the put
        write.payload = std::move(pending.payload);
        writes.push_back(std::move(write));
    }
    if (writes.empty()) {
        pending_.clear();
        return true;
    }

    // 2. Apply the staged writes to the in-memory index so the byte budget is
    //    computed against the result, not the pre-flush state.
    for (const Write& write : writes) {
        key_to_slot_[SlotKey{write.entry.stable_id_hash, write.entry.variant}] = write.slot;
        in_use_[write.slot] = true;
        entries_by_slot_[write.slot] = write.entry;
    }

    // 3. Evict until the live payload fits the budget. Pinned entries are
    //    exempt unless nothing else is left (hard limits still apply).
    std::uint64_t live = LivePayloadBytes();
    while (live > max_bytes_) {
        if (!EvictOne(pinned_hashes, touched, /*allow_pinned=*/false)) {
            break;
        }
        live = LivePayloadBytes();
    }
    while (live > max_bytes_) {
        if (!EvictOne(pinned_hashes, touched, /*allow_pinned=*/true)) {
            break;
        }
        live = LivePayloadBytes();
    }

    // 4. Grow the mapping once for the whole round.
    std::uint64_t new_bytes = 0;
    for (const Write& write : writes) {
        new_bytes += write.payload.size();
    }
    const std::uint64_t new_payload_end = header_.payload_end + new_bytes;
    if (new_payload_end > view_size_ && !GrowView(new_payload_end)) {
        // NR-050: the file has already been resized and the mapping is gone, so
        // there is nothing coherent left to write into. Disable the store for
        // this run rather than continuing with a null view; icons.cache is a
        // rebuildable accelerator (icon_store.h), so "no cache" is a complete
        // and safe degradation (§11) and the next launch starts clean.
        state_ = StoreState::Disabled;
        WriteLog(L"icon-store", L"grow-failed");
        return false;
    }

    // 5. Append payloads and rewrite entries in the fixed order, then commit
    //    the alternate header last. A crash before the header commit leaves the
    //    old header valid and the new payloads as dead bytes reclaimed by the
    //    next compaction -- a reader can never see a half-written entry.
    std::uint64_t offset = header_.payload_end;
    for (Write& write : writes) {
        if (!write.payload.empty()) {
            std::memcpy(view_ + offset, write.payload.data(), write.payload.size());
            if (FlushViewOfFile(view_ + offset, write.payload.size()) == FALSE) {
                ScanIndex();
                WriteLog(L"icon-store", L"flush-failed");
                pending_.clear();  // NR-068: never digestible again -- reject and forget
                state_ = StoreState::ReadOnly;
                return false;
            }
        }
        write.entry.payload_offset = offset;
        write.entry.payload_crc32 = Crc32(write.payload.data(), write.payload.size());
        EncodeEntry(write.entry, view_ + kIndexOffset + write.slot * kIndexEntrySize);
        entries_by_slot_[write.slot] = write.entry;
        if (FlushViewOfFile(view_ + kIndexOffset + write.slot * kIndexEntrySize,
                            kIndexEntrySize) == FALSE) {
            ScanIndex();
            WriteLog(L"icon-store", L"flush-failed");
            pending_.clear();  // NR-068: never digestible again -- reject and forget
            state_ = StoreState::ReadOnly;
            return false;
        }
        offset += write.payload.size();
    }

    PackHeader next = header_;
    next.generation += 1;
    next.payload_end = new_payload_end;
    std::array<std::uint8_t, kHeaderSize> slot_bytes{};
    EncodeHeader(next, slot_bytes.data());
    const std::size_t other_slot = 1 - header_slot_;
    std::memcpy(view_ + other_slot * kHeaderSize, slot_bytes.data(), kHeaderSize);
    if (FlushViewOfFile(view_ + other_slot * kHeaderSize, kHeaderSize) == FALSE) {
        ScanIndex();
        WriteLog(L"icon-store", L"flush-failed");
        pending_.clear();  // NR-068: never digestible again -- reject and forget
        state_ = StoreState::ReadOnly;
        return false;
    }
    header_ = next;
    header_slot_ = other_slot;
    pending_.clear();

    // 6. Refresh stats.
    stats_.entries = 0;
    stats_.payload_bytes = 0;
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (in_use_[slot]) {
            ++stats_.entries;
            stats_.payload_bytes += entries_by_slot_[slot].payload_len;
        }
    }
    stats_.dead_bytes = file_size_ > kPayloadStart + stats_.payload_bytes
                            ? file_size_ - kPayloadStart - stats_.payload_bytes
                            : 0;

    // 7. Compact when dead bytes pass half the live payload.
    if (stats_.dead_bytes > stats_.payload_bytes / 2 && stats_.payload_bytes > 0) {
        Compact();
    }
    return true;
}

bool IconStore::Compact() {
    // Rebuild the whole pack in memory: headers plus the surviving entries with
    // payloads renumbered in slot order. The original file is untouched until
    // the replacement succeeds; any failure deletes only the .tmp.
    std::vector<std::uint8_t> content(kPayloadStart + stats_.payload_bytes, 0);
    std::uint64_t offset = kPayloadStart;
    for (std::size_t slot = 0; slot < kIndexCapacity; ++slot) {
        if (!in_use_[slot]) {
            continue;
        }
        const PackEntry& entry = entries_by_slot_[slot];
        if (entry.payload_offset + entry.payload_len > view_size_) {
            continue;  // can't copy (shouldn't happen); leave it out
        }
        std::memcpy(content.data() + offset, view_ + entry.payload_offset, entry.payload_len);
        PackEntry rewritten = entry;
        rewritten.payload_offset = offset;
        EncodeEntry(rewritten, content.data() + kIndexOffset + slot * kIndexEntrySize);
        // NOTE: entries_by_slot_ is deliberately left untouched here. A failed
        // compaction must not rewrite the in-memory index, because eviction is
        // in-memory-only (the evicted slot still reads in_use on disk) and a
        // rescan would resurrect it. On success ScanIndex rebuilds from the new
        // file; on failure the existing index (with evictions) stays correct.
        offset += entry.payload_len;
    }
    content.resize(offset);

    PackHeader newest = header_;
    newest.generation += 1;
    newest.payload_end = offset;
    EncodeHeader(newest, content.data());
    EncodeHeader(header_, content.data() + kHeaderSize);  // older generation wins tie-break

    // The pack is mapped without FILE_SHARE_DELETE, so it cannot be replaced
    // while a view is live: release the mapping first, then swap the file.
    Unmap();

    const std::wstring tmp_path = pack_path_.wstring() + L".tmp";
    const HANDLE file = CreateFileW(tmp_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        MapFile();  // restore the mapping; keep the in-memory index as-is
        return false;
    }
    DWORD written = 0;
    const bool wrote_ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                                    &written, nullptr) != FALSE &&
                          written == content.size() && FlushFileBuffers(file) != FALSE;
    const bool close_ok = CloseHandle(file) != FALSE;
    if (!wrote_ok || !close_ok) {
        DeleteFileW(tmp_path.c_str());
        MapFile();
        return false;
    }
    if (!ReplaceFileWithRetry(tmp_path, pack_path_.wstring())) {
        DeleteFileW(tmp_path.c_str());
        MapFile();  // the original file is still there; remap, keep index as-is
        return false;
    }

    if (!MapFile()) {
        return false;
    }
    header_ = newest;
    header_slot_ = 0;  // slot A carries the newest generation
    ScanIndex();
    return true;
}

} // namespace nimblerun
