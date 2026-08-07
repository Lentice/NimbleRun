#pragma once

#include "icons/icon_pack_format.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nimblerun {

class DiagnosticLog;

// File-backed decoded-icon cache at %LOCALAPPDATA%\NimbleRun\icons.cache
// (design-spec §10.1). The on-disk layout is defined and owned by
// icon_pack_format (NR-033); this class is the only one that touches the file.
//
// Design rules (NR-035, §10.2):
//  - Reads go through a memory map so payloads never sit in the working set
//    (§NFR-001); Lookup copies bytes out, callers never hold a view pointer.
//  - Payloads are appended in place; compaction and full rewrites use the
//    .tmp + replace scheme. Append is a deliberate exception to the user-data
//    atomic-write rule because this file is a fully rebuildable accelerator,
//    not a source of truth (§10.2, NR-030).
//  - Unlike settings.ini / usage.tsv / favorites.txt, corruption is NOT
//    preserved as a .corrupt copy: the pack is a cache, and keeping the copy
//    only accumulates useless bytes on disk. Any corruption degrades to "no
//    cache" and the app keeps working (which is exactly why it is safe to
//    delete rather than preserve).
//  - Diagnostics go through DiagnosticLog with event names and counts only;
//    never paths, app names, or search text.
//
// This class is only ever driven from the background icon worker thread
// (NR-036). It calls no UI or window APIs.
class IconStore {
public:
    // Injectable seam so tests can point at a temp directory instead of
    // %LOCALAPPDATA%. Empty path => the store is disabled (all Lookup miss,
    // all Put no-op), which is also the state after an unrecoverable failure.
    struct IconStorePaths {
        std::filesystem::path pack;  // ...\icons.cache
    };

    enum class StoreState {
        Ready,        // readable and writable
        ReadOnly,     // readable, writes rejected (e.g. disk full)
        Disabled,     // off entirely (newer schema, cannot create, empty path)
    };

    struct StoreStats {  // for diagnostics and test assertions, never shown to users
        std::size_t entries = 0;
        std::size_t dropped_entries = 0;   // corrupt slots dropped on this load
        std::uint64_t payload_bytes = 0;
        std::uint64_t dead_bytes = 0;
        bool recreated = false;            // this load rebuilt the whole file
    };

    // Byte budget for icons.cache (design-spec §NFR-001): target ≤ 32 MiB.
    // Single source of the 32 MiB figure is icon_pack_format.h (NR-075); the
    // read side (DecodeHeader) and the eviction side both derive from it.
    static constexpr std::uint64_t kMaxPackBytes = kPackByteBudget;

    // max_bytes overrides the byte budget for eviction tests; production
    // callers leave it at the default.
    explicit IconStore(IconStorePaths paths,
                       std::uint64_t max_bytes = kMaxPackBytes,
                       DiagnosticLog* log = nullptr);
    ~IconStore();

    IconStore(const IconStore&) = delete;
    IconStore& operator=(const IconStore&) = delete;

    // Open (or create) the pack and classify its integrity. Safe to call once
    // at worker startup. Never throws.
    StoreState Open();
    StoreState State() const { return state_; }
    StoreStats Stats() const { return stats_; }

    // Returns the stored payload bytes for (stable_id, variant) when present,
    // intact, and not stale. Copies out of the mapped view so the caller never
    // holds a pointer into the mapping. Empty vector = miss. now_utc and
    // source_stamp come from the caller (injectable clock / stat).
    std::vector<std::uint8_t> Lookup(const std::wstring& stable_id, int variant,
                                     std::uint64_t source_stamp, std::uint64_t now_utc);

    // Queue a payload for persistence. Buffers in memory; nothing touches disk
    // until Flush(). Overwrites any pending or stored entry for the same key.
    void Put(const std::wstring& stable_id, int variant,
             std::vector<std::uint8_t> payload,
             std::uint64_t source_stamp, std::uint64_t now_utc);

    // Commit buffered puts: evict as needed (pinned_ids exempt), append
    // payloads, write index entries, then commit the alternate header.
    // Compacts when dead bytes exceed 50%. Returns false on any write failure
    // (state may drop to ReadOnly); the in-memory LRU above is unaffected
    // either way.
    bool Flush(const std::vector<std::wstring>& pinned_ids, std::uint64_t now_utc);

private:
    struct Pending {
        std::uint64_t stable_id_hash = 0;
        int variant = 0;
        std::vector<std::uint8_t> payload;
        std::uint64_t source_stamp = 0;
        std::uint64_t fetched_utc = 0;
        std::uint64_t last_used_utc = 0;
    };
    // stable_id_hash + variant identify a (stable_id, variant) slot. std::pair
    // has no std::hash, so this is a tiny struct with its own hasher.
    struct SlotKey {
        std::uint64_t stable_id_hash = 0;
        std::uint16_t variant = 0;
        bool operator==(const SlotKey&) const = default;
        struct Hasher {
            std::size_t operator()(const SlotKey& key) const {
                std::size_t h = std::hash<std::uint64_t>{}(key.stable_id_hash);
                return h ^ (static_cast<std::size_t>(key.variant) << 1);
            }
        };
    };

    bool MapFile();
    void Unmap();
    bool CreateEmptyPack();
    void ScanIndex();
    std::size_t FindFreeSlot(const std::vector<bool>& touched) const;
    std::uint64_t LivePayloadBytes() const;
    bool EvictOne(const std::unordered_set<std::uint64_t>& pinned_hashes,
                  const std::vector<bool>& touched, bool allow_pinned);
    bool GrowView(std::uint64_t needed);
    bool Compact();
    void WriteLog(std::wstring_view stage, std::wstring_view detail);

    std::filesystem::path pack_path_;
    std::uint64_t max_bytes_;
    DiagnosticLog* log_;

    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    std::uint8_t* view_ = nullptr;
    std::size_t view_size_ = 0;
    std::uint64_t file_size_ = 0;

    StoreState state_ = StoreState::Disabled;
    StoreStats stats_;
    PackHeader header_;
    std::size_t header_slot_ = 0;

    std::array<PackEntry, kIndexCapacity> entries_by_slot_{};
    std::array<bool, kIndexCapacity> in_use_{};
    std::unordered_map<SlotKey, std::size_t, SlotKey::Hasher> key_to_slot_;
    std::unordered_map<std::wstring, Pending> pending_;
};

} // namespace nimblerun
