#include "icons/icon_pack_format.h"

#include <array>

namespace nimblerun {

namespace {

std::uint16_t ReadLe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t ReadLe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t ReadLe64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return value;
}

void WriteLe16(std::uint8_t* p, std::uint16_t value) {
    p[0] = static_cast<std::uint8_t>(value & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void WriteLe32(std::uint8_t* p, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

void WriteLe64(std::uint8_t* p, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

bool HasMagic(const std::uint8_t* slot) {
    return slot[0] == kPackMagic[0] && slot[1] == kPackMagic[1] &&
           slot[2] == kPackMagic[2] && slot[3] == kPackMagic[3];
}

bool HeaderCrcValid(const std::uint8_t* slot) {
    return Crc32(slot, 24) == ReadLe32(slot + 24);
}

} // namespace

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (std::uint32_t bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void EncodeHeader(const PackHeader& header, std::uint8_t* out) {
    out[0] = kPackMagic[0];
    out[1] = kPackMagic[1];
    out[2] = kPackMagic[2];
    out[3] = kPackMagic[3];
    WriteLe16(out + 4, header.schema_version);
    WriteLe16(out + 6, 0);  // reserved
    WriteLe32(out + 8, header.generation);
    WriteLe32(out + 12, header.index_capacity);
    WriteLe64(out + 16, header.payload_end);
    WriteLe32(out + 24, Crc32(out, 24));
    WriteLe32(out + 28, 0);  // reserved
}

void EncodeEntry(const PackEntry& entry, std::uint8_t* out) {
    WriteLe64(out + 0, entry.stable_id_hash);
    WriteLe16(out + 8, entry.variant);
    WriteLe16(out + 10, entry.in_use ? 1u : 0u);
    WriteLe32(out + 12, entry.payload_len);
    WriteLe64(out + 16, entry.payload_offset);
    WriteLe32(out + 24, entry.payload_crc32);
    WriteLe64(out + 28, entry.source_stamp);
    WriteLe64(out + 36, entry.fetched_utc);
    WriteLe64(out + 44, entry.last_used_utc);
    WriteLe32(out + 52, Crc32(out, 52));
}

PackStatus DecodeHeader(const std::uint8_t* data, std::size_t size, PackHeader& out) {
    if (size < kPayloadStart) {
        return PackStatus::Absent;
    }
    bool any_magic = false;
    bool any_valid = false;
    std::size_t best_slot = 0;
    for (std::size_t s = 0; s < kHeaderSlotCount; ++s) {
        const std::uint8_t* slot = data + s * kHeaderSize;
        if (!HasMagic(slot)) {
            continue;
        }
        any_magic = true;
        if (!HeaderCrcValid(slot)) {
            continue;
        }
        // NR-050: a valid CRC proves the field was not corrupted in transit; it
        // does not prove the value is sane. payload_end is the trust root of the
        // whole format -- DecodeEntry bounds every payload against it, and Flush
        // grows the file to it -- so an absurd value turns every downstream check
        // into a rubber stamp. Rejecting the slot here (rather than after
        // selection) lets a sane sibling slot win, which is exactly what the dual
        // header slot exists for. Two failure modes this closes: payload_end far
        // beyond the file makes SetEndOfFile expand icons.cache until the disk is
        // full, and payload_end below kPayloadStart makes an append memcpy over
        // the header slots and the index.
        const std::uint64_t payload_end = ReadLe64(slot + 16);
        // NR-075: a pack budget cap on top of NR-050's file-size bound. A CRC
        // correct but bloated file (a bit flip, an old bug, or a hand-edited
        // icons.cache in %LOCALAPPDATA%) would otherwise be accepted as Ready
        // and a single Lookup could copy close to 4 GiB into a vector
        // (payload_len is u32). Legit packs never exceed kPackByteBudget --
        // Flush's eviction keeps payload_end within it -- so this rejects only
        // pathological files. design-spec §NFR-001 caps the pack at 32 MiB.
        if (payload_end < kPayloadStart || payload_end > size ||
            payload_end > kPackByteBudget) {
            continue;
        }
        if (!any_valid || ReadLe32(slot + 8) > ReadLe32(data + best_slot * kHeaderSize + 8)) {
            best_slot = s;
            any_valid = true;
        }
    }
    if (!any_magic) {
        return PackStatus::BadMagic;
    }
    if (!any_valid) {
        return PackStatus::BothHeadersBad;
    }
    const std::uint8_t* slot = data + best_slot * kHeaderSize;
    out.schema_version = ReadLe16(slot + 4);
    out.generation = ReadLe32(slot + 8);
    out.index_capacity = ReadLe32(slot + 12);
    out.payload_end = ReadLe64(slot + 16);
    if (out.schema_version > kPackSchemaVersion) {
        return PackStatus::NewerSchema;
    }
    if (out.index_capacity != kIndexCapacity) {
        return PackStatus::BothHeadersBad;
    }
    return PackStatus::Ok;
}

EntryStatus DecodeEntry(const std::uint8_t* data, std::size_t size,
                        const PackHeader& header, std::size_t slot, PackEntry& out) {
    if (size < kPayloadStart || slot >= kIndexCapacity) {
        return EntryStatus::OutOfBounds;
    }
    const std::uint8_t* e = data + kIndexOffset + slot * kIndexEntrySize;
    out.stable_id_hash = ReadLe64(e + 0);
    out.variant = ReadLe16(e + 8);
    out.in_use = (ReadLe16(e + 10) & 1u) != 0;
    out.payload_len = ReadLe32(e + 12);
    out.payload_offset = ReadLe64(e + 16);
    out.payload_crc32 = ReadLe32(e + 24);
    out.source_stamp = ReadLe64(e + 28);
    out.fetched_utc = ReadLe64(e + 36);
    out.last_used_utc = ReadLe64(e + 44);

    // Free is decided by the in-use flag before the CRC check: a never-written
    // (all-zero) slot has a nonzero CRC, so CRC-first would misclassify it as
    // corruption. A torn write that clears the flag degrades to Free -- the
    // entry is simply lost, its neighbors untouched (NR-033).
    if (!out.in_use) {
        return EntryStatus::Free;
    }
    if (Crc32(e, 52) != ReadLe32(e + 52)) {
        return EntryStatus::CrcMismatch;
    }
    bool valid_variant = false;
    for (const int variant : kIconVariants) {
        if (out.variant == static_cast<std::uint16_t>(variant)) {
            valid_variant = true;
            break;
        }
    }
    if (!valid_variant) {
        return EntryStatus::BadVariant;
    }
    if (out.payload_offset < kPayloadStart) {
        return EntryStatus::OutOfBounds;
    }
    // Overflow-safe offset + len > payload_end (payload_len is bounded u32).
    if (out.payload_offset > header.payload_end ||
        out.payload_len > header.payload_end - out.payload_offset) {
        return EntryStatus::OutOfBounds;
    }
    return EntryStatus::Ok;
}

bool VerifyPayload(const std::uint8_t* data, std::size_t size, const PackEntry& entry) {
    if (entry.payload_offset < kPayloadStart || entry.payload_offset > size) {
        return false;
    }
    if (entry.payload_len > size - entry.payload_offset) {
        return false;
    }
    return Crc32(data + entry.payload_offset, entry.payload_len) == entry.payload_crc32;
}

bool ParseStableIdHash(const std::wstring& stable_id, std::uint64_t& out) {
    if (stable_id.size() != 16) {
        return false;
    }
    std::uint64_t value = 0;
    for (const wchar_t c : stable_id) {
        std::uint32_t digit = 16;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<std::uint32_t>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            digit = static_cast<std::uint32_t>(c - L'a') + 10;
        } else if (c >= L'A' && c <= L'F') {
            digit = static_cast<std::uint32_t>(c - L'A') + 10;
        }
        if (digit >= 16) {
            return false;
        }
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

std::uint64_t MakeSourceStamp(std::uint64_t last_write_time, std::uint64_t size) {
    // Odd multiplier is a bijection on u64, so a size change always moves the
    // stamp; the XOR keeps a time change visible for any size.
    return last_write_time ^ (size * 0x9E3779B97F4A7C15ull);
}

std::vector<std::uint8_t> MakeEmptyPack() {
    std::vector<std::uint8_t> pack(kPayloadStart, 0);
    PackHeader newest;
    newest.generation = 1;
    EncodeHeader(newest, pack.data());
    PackHeader older;
    older.generation = 0;
    EncodeHeader(older, pack.data() + kHeaderSize);
    return pack;
}

} // namespace nimblerun
