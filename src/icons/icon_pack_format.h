#pragma once

#include "icons/icon_cache.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nimblerun {

// Binary layout of the icons.cache pack (design-spec §10.2, NR-033). All
// integers are little-endian. Three fixed-offset blocks: two 32-byte header
// slots (torn writes kill at most one), a fixed-capacity index block, then a
// growing payload area. The fixed index capacity is what lets NR-035 append
// payload in place without moving the index.
inline constexpr std::uint8_t kPackMagic[4] = {'N', 'R', 'I', 'C'};
inline constexpr std::uint16_t kPackSchemaVersion = 1;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kHeaderSlotCount = 2;
inline constexpr std::size_t kIndexEntrySize = 56;
inline constexpr std::size_t kIndexCapacity = 512;
inline constexpr std::size_t kIndexOffset = kHeaderSize * kHeaderSlotCount;                       // 64
inline constexpr std::size_t kPayloadStart = kIndexOffset + kIndexCapacity * kIndexEntrySize;     // 28736

static_assert(kIndexOffset == 64);
static_assert(kPayloadStart == 28736);
static_assert(kHeaderSize * kHeaderSlotCount + kIndexCapacity * kIndexEntrySize == kPayloadStart);

// Byte budget for the whole pack (design-spec §NFR-001); the sole source of
// the 32 MiB figure. icon_store.h::kMaxPackBytes references this so the read
// side (DecodeHeader) and the write side (eviction) can never drift.
inline constexpr std::uint64_t kPackByteBudget = 32ull * 1024ull * 1024ull;

// IEEE 802.3 reflected CRC-32 (poly 0xEDB88320), the same checksum used for the
// header slots, index entries and payloads. Standard init/final XOR 0xFFFFFFFF.
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);

// Header slot, after CRC validation (see DecodeHeader).
struct PackHeader {
    std::uint16_t schema_version = kPackSchemaVersion;
    std::uint32_t generation = 0;
    std::uint32_t index_capacity = static_cast<std::uint32_t>(kIndexCapacity);
    std::uint64_t payload_end = kPayloadStart;
};

// Index entry. Ordinary copyable value; the CRC fields on disk are not stored
// here because DecodeEntry validates them and only fills the payload fields.
struct PackEntry {
    std::uint64_t stable_id_hash = 0;
    std::uint16_t variant = 0;
    bool in_use = false;
    std::uint32_t payload_len = 0;
    std::uint64_t payload_offset = 0;
    std::uint32_t payload_crc32 = 0;
    std::uint64_t source_stamp = 0;
    std::uint64_t fetched_utc = 0;
    std::uint64_t last_used_utc = 0;
};

// Writes a header slot / index entry into out, which must hold at least
// kHeaderSize / kIndexEntrySize bytes. Reserved bytes are zeroed and the CRC is
// filled in; byte-by-byte little-endian, no struct layout assumptions.
void EncodeHeader(const PackHeader& header, std::uint8_t* out);
void EncodeEntry(const PackEntry& entry, std::uint8_t* out);

// Decode results. PackStatus classifies the whole file (header selection);
// EntryStatus classifies one index slot, independent of its neighbors.
enum class PackStatus {
    Ok,
    Absent,          // file missing or shorter than kPayloadStart
    BadMagic,        // neither header slot has the magic
    NewerSchema,     // readable but schema_version > kPackSchemaVersion
    BothHeadersBad,  // magic ok but neither slot passes its CRC / capacity check
};

enum class EntryStatus {
    Ok,
    Free,            // flags bit0 = 0, a normal empty slot, not corruption
    CrcMismatch,     // entry_crc32 does not match (torn write)
    OutOfBounds,     // payload_offset < kPayloadStart, or offset + len > payload_end
    BadVariant,      // variant not in kIconVariants
};

// data/size is the whole file (or its mmap view). Selects the newest header
// slot with a valid CRC and fills out. Returns NewerSchema with out filled for
// diagnostics. Safe for any input: returns Absent before touching data when
// size < kPayloadStart, and never reads past the two header slots.
PackStatus DecodeHeader(const std::uint8_t* data, std::size_t size, PackHeader& out);

// slot < kIndexCapacity. Uses header.payload_end for the payload bounds check.
// Safe for any input and for any slot index; a torn entry only fails its own
// slot. Returns Free before payload checks when the slot is empty.
EntryStatus DecodeEntry(const std::uint8_t* data, std::size_t size,
                        const PackHeader& header, std::size_t slot, PackEntry& out);

// Payload content integrity: hashes the referenced bytes and compares with the
// entry's payload_crc32. Independent of DecodeEntry, which only checks the
// entry record itself. Caller uses this before actually consuming a payload.
bool VerifyPayload(const std::uint8_t* data, std::size_t size, const PackEntry& entry);

// stable ID is 16 lowercase hex characters (src/catalog/stable_id.h); this also
// accepts uppercase. Returns false on any malformed input and leaves out
// untouched, so the caller must skip the cache rather than treat 0 as valid.
bool ParseStableIdHash(const std::wstring& stable_id, std::uint64_t& out);

// Mixes a source file's last-write-time and size into one stamp so NR-035 can
// invalidate a cached icon when its source changes. 0 means "no file to stat",
// the caller then falls back to TTL.
std::uint64_t MakeSourceStamp(std::uint64_t last_write_time, std::uint64_t size);

// Produces a valid empty pack exactly kPayloadStart bytes long: header slot A
// with generation 1, slot B with generation 0, 512 empty index slots, and
// payload_end = kPayloadStart. NR-035 uses it to create a new file.
std::vector<std::uint8_t> MakeEmptyPack();

} // namespace nimblerun
