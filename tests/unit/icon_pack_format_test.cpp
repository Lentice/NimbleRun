#include "test_util.h"

#include "icons/icon_pack_format.h"
#include "catalog/stable_id.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using nimblerun::Crc32;
using nimblerun::DecodeEntry;
using nimblerun::DecodeHeader;
using nimblerun::EncodeEntry;
using nimblerun::EncodeHeader;
using nimblerun::EntryStatus;
using nimblerun::HashStableId;
using nimblerun::kHeaderSize;
using nimblerun::kIndexCapacity;
using nimblerun::kIndexEntrySize;
using nimblerun::kIndexOffset;
using nimblerun::kPackByteBudget;
using nimblerun::kPayloadStart;
using nimblerun::MakeEmptyPack;
using nimblerun::MakeSourceStamp;
using nimblerun::PackEntry;
using nimblerun::PackHeader;
using nimblerun::PackStatus;
using nimblerun::ParseStableIdHash;
using nimblerun::VerifyPayload;

namespace {

const std::uint8_t kPayloadBytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};

PackEntry ValidEntry() {
    PackEntry entry;
    entry.stable_id_hash = 0x0123456789ABCDEFull;
    entry.variant = 48;
    entry.in_use = true;
    entry.payload_len = sizeof(kPayloadBytes);
    entry.payload_offset = kPayloadStart;
    entry.payload_crc32 = Crc32(kPayloadBytes, sizeof(kPayloadBytes));
    entry.source_stamp = 0xAABBCCDD00112233ull;
    entry.fetched_utc = 1700000000ull;
    entry.last_used_utc = 1700000100ull;
    return entry;
}

// A valid pack: header slot A generation 2 / payload_end = kPayloadStart + 4,
// slot B generation 0, four payload bytes at kPayloadStart.
std::vector<std::uint8_t> PackWithPayload() {
    std::vector<std::uint8_t> pack(kPayloadStart + 4, 0);
    PackHeader newest;
    newest.generation = 2;
    newest.payload_end = kPayloadStart + 4;
    EncodeHeader(newest, pack.data());
    PackHeader older;
    older.generation = 0;
    EncodeHeader(older, pack.data() + kHeaderSize);
    for (std::size_t i = 0; i < sizeof(kPayloadBytes); ++i) {
        pack[kPayloadStart + i] = kPayloadBytes[i];
    }
    return pack;
}

void PutEntry(std::vector<std::uint8_t>& pack, std::size_t slot, const PackEntry& entry) {
    EncodeEntry(entry, pack.data() + kIndexOffset + slot * kIndexEntrySize);
}

void TestEmptyPack() {
    const std::vector<std::uint8_t> pack = MakeEmptyPack();
    Expect(pack.size() == kPayloadStart, "empty pack is exactly kPayloadStart bytes");

    PackHeader header;
    Expect(DecodeHeader(pack.data(), pack.size(), header) == PackStatus::Ok,
           "empty pack header decodes Ok");
    Expect(header.payload_end == kPayloadStart, "empty pack payload_end is kPayloadStart");
    Expect(header.generation == 1, "empty pack picks the generation-1 slot");

    for (std::size_t i = 0; i < kIndexCapacity; ++i) {
        PackEntry entry;
        Expect(DecodeEntry(pack.data(), pack.size(), header, i, entry) == EntryStatus::Free,
               "every empty slot is Free");
    }
}

void TestHeaderRoundTrip() {
    const PackHeader cases[] = {
        {},
        {nimblerun::kPackSchemaVersion, 12345, 512, kPayloadStart + 4},
        {nimblerun::kPackSchemaVersion, 0, 512, kPayloadStart},
    };
    for (const PackHeader& in : cases) {
        std::vector<std::uint8_t> file(kPayloadStart + 4, 0);
        EncodeHeader(in, file.data());
        PackHeader out;
        Expect(DecodeHeader(file.data(), file.size(), out) == PackStatus::Ok,
               "header round-trip decodes Ok");
        Expect(out.schema_version == in.schema_version, "schema_version round-trips");
        Expect(out.generation == in.generation, "generation round-trips");
        Expect(out.index_capacity == in.index_capacity, "index_capacity round-trips");
        Expect(out.payload_end == in.payload_end, "payload_end round-trips");
    }
}

void TestEntryRoundTrip() {
    const PackEntry cases[] = {
        {},
        ValidEntry(),
    };
    PackEntry not_in_use = ValidEntry();
    not_in_use.in_use = false;

    const auto check = [](const PackEntry& in) {
        std::vector<std::uint8_t> file(kPayloadStart + 4, 0);
        PackHeader header;
        header.payload_end = kPayloadStart + 4;
        EncodeHeader(header, file.data());
        EncodeEntry(in, file.data() + kIndexOffset);
        PackEntry out;
        const EntryStatus status = DecodeEntry(file.data(), file.size(), header, 0, out);
        Expect(status == (in.in_use ? EntryStatus::Ok : EntryStatus::Free),
               "entry round-trips to Ok/Free");
        Expect(out.stable_id_hash == in.stable_id_hash, "stable_id_hash round-trips");
        Expect(out.variant == in.variant, "variant round-trips");
        Expect(out.in_use == in.in_use, "in_use round-trips");
        Expect(out.payload_len == in.payload_len, "payload_len round-trips");
        Expect(out.payload_offset == in.payload_offset, "payload_offset round-trips");
        Expect(out.payload_crc32 == in.payload_crc32, "payload_crc32 round-trips");
        Expect(out.source_stamp == in.source_stamp, "source_stamp round-trips");
        Expect(out.fetched_utc == in.fetched_utc, "fetched_utc round-trips");
        Expect(out.last_used_utc == in.last_used_utc, "last_used_utc round-trips");
    };
    for (const PackEntry& in : cases) {
        check(in);
    }
    check(not_in_use);
}

void TestCrcKnownVectors() {
    Expect(Crc32(nullptr, 0) == 0, "crc of empty input is 0");
    const char check[] = "123456789";
    Expect(Crc32(reinterpret_cast<const std::uint8_t*>(check), 9) == 0xCBF43926u,
           "known crc32 vector '123456789'");
}

void TestAbsent() {
    const std::size_t sizes[] = {0, 1, kPayloadStart - 1};
    for (const std::size_t size : sizes) {
        const std::vector<std::uint8_t> file(size, 0xAB);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Absent,
               "file shorter than kPayloadStart is Absent");
    }
}

void TestBadMagic() {
    std::vector<std::uint8_t> file = MakeEmptyPack();
    file[0] ^= 0xFF;   // slot A magic byte 0
    file[32] ^= 0xFF;  // slot B magic byte 0
    PackHeader header;
    Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BadMagic,
           "both slots with wrong magic -> BadMagic");
}

void TestBothHeadersBadCrc() {
    std::vector<std::uint8_t> file = MakeEmptyPack();
    file[5] ^= 0x01;   // a bit inside slot A's crc-covered region
    file[37] ^= 0x01;  // the same byte of slot B
    PackHeader header;
    Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
           "magic ok but both slot CRCs broken -> BothHeadersBad");
}

void TestSingleSlotCorrupt() {
    // A valid (generation 5), B corrupted -> pick A.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader a;
        a.generation = 5;
        EncodeHeader(a, file.data());
        file[kHeaderSize + 5] ^= 0x01;  // corrupt slot B
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "A valid, B corrupt -> Ok");
        Expect(header.generation == 5, "picks generation 5 from A");
    }
    // B valid (generation 5), A corrupted -> pick B.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader b;
        b.generation = 5;
        EncodeHeader(b, file.data() + kHeaderSize);
        file[5] ^= 0x01;  // corrupt slot A
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "B valid, A corrupt -> Ok");
        Expect(header.generation == 5, "picks generation 5 from B");
    }
}

void TestBothValidPickNewer() {
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader a;
        a.generation = 7;
        EncodeHeader(a, file.data());
        PackHeader b;
        b.generation = 8;
        EncodeHeader(b, file.data() + kHeaderSize);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "both slots valid -> Ok");
        Expect(header.generation == 8, "picks the larger generation");
    }
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader a;
        a.generation = 8;
        EncodeHeader(a, file.data());
        PackHeader b;
        b.generation = 7;
        EncodeHeader(b, file.data() + kHeaderSize);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "larger generation wins regardless of slot order");
        Expect(header.generation == 8, "still picks generation 8");
    }
}

void TestNewerSchema() {
    std::vector<std::uint8_t> file = MakeEmptyPack();
    PackHeader a;
    a.schema_version = 2;
    a.generation = 1;
    EncodeHeader(a, file.data());
    PackHeader b;
    b.schema_version = 2;
    b.generation = 0;
    EncodeHeader(b, file.data() + kHeaderSize);
    PackHeader header;
    Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::NewerSchema,
           "schema_version 2 -> NewerSchema");
    Expect(header.schema_version == 2, "newer schema still fills out for diagnostics");
}

void TestIndexCapacityMismatch() {
    std::vector<std::uint8_t> file = MakeEmptyPack();
    PackHeader a;
    a.index_capacity = 256;
    a.generation = 1;
    EncodeHeader(a, file.data());
    PackHeader b;
    b.index_capacity = 256;
    b.generation = 0;
    EncodeHeader(b, file.data() + kHeaderSize);
    PackHeader header;
    Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
           "index_capacity 256 -> BothHeadersBad");
}

void TestEntryCorruptionIsolation() {
    std::vector<std::uint8_t> file = PackWithPayload();
    PackHeader header;
    Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok, "pack header decodes Ok");
    PutEntry(file, 3, ValidEntry());
    PutEntry(file, 5, ValidEntry());

    PackEntry e3;
    PackEntry e5;
    Expect(DecodeEntry(file.data(), file.size(), header, 3, e3) == EntryStatus::Ok,
           "slot 3 decodes Ok before corruption");
    Expect(DecodeEntry(file.data(), file.size(), header, 5, e5) == EntryStatus::Ok,
           "slot 5 decodes Ok before corruption");

    // Flip one bit inside slot 3's entry record (byte 5, within the crc-covered
    // region 0..51). Only that slot may fail.
    file[kIndexOffset + 3 * kIndexEntrySize + 5] ^= 0x01;

    Expect(DecodeEntry(file.data(), file.size(), header, 3, e3) == EntryStatus::CrcMismatch,
           "flipped slot is CrcMismatch");
    Expect(DecodeEntry(file.data(), file.size(), header, 5, e5) == EntryStatus::Ok,
           "sibling slot in the same file stays Ok");
}

void TestOutOfBounds() {
    std::vector<std::uint8_t> file = PackWithPayload();
    PackHeader header;
    DecodeHeader(file.data(), file.size(), header);

    // payload_offset < kPayloadStart.
    {
        PackEntry entry = ValidEntry();
        entry.payload_offset = kPayloadStart - 1;
        PutEntry(file, 0, entry);
        PackEntry out;
        Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::OutOfBounds,
               "offset below kPayloadStart -> OutOfBounds");
    }
    // payload_offset + payload_len = payload_end + 1 (payload_end is kPayloadStart + 4).
    {
        PackEntry entry = ValidEntry();
        entry.payload_len = 5;
        PutEntry(file, 0, entry);
        PackEntry out;
        Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::OutOfBounds,
               "offset + len past payload_end -> OutOfBounds");
    }
    // payload_len = 0xFFFFFFFF.
    {
        PackEntry entry = ValidEntry();
        entry.payload_len = 0xFFFFFFFFu;
        PutEntry(file, 0, entry);
        PackEntry out;
        Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::OutOfBounds,
               "huge payload_len -> OutOfBounds");
    }
    // Sanity: a valid entry is Ok again after the corrupted cases.
    {
        PutEntry(file, 0, ValidEntry());
        PackEntry out;
        Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::Ok,
               "valid entry recovers after out-of-bounds cases");
    }
}

void TestBadVariant() {
    std::vector<std::uint8_t> file = PackWithPayload();
    PackHeader header;
    DecodeHeader(file.data(), file.size(), header);
    PackEntry entry = ValidEntry();
    entry.variant = 64;
    PutEntry(file, 0, entry);
    PackEntry out;
    Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::BadVariant,
           "variant 64 -> BadVariant");
}

void TestPayloadVerification() {
    std::vector<std::uint8_t> file = PackWithPayload();
    PackHeader header;
    DecodeHeader(file.data(), file.size(), header);
    PutEntry(file, 0, ValidEntry());

    PackEntry out;
    Expect(DecodeEntry(file.data(), file.size(), header, 0, out) == EntryStatus::Ok,
           "entry decodes Ok");
    Expect(VerifyPayload(file.data(), file.size(), out), "payload verifies");

    file[kPayloadStart + 1] ^= 0x01;  // flip a payload byte
    Expect(!VerifyPayload(file.data(), file.size(), out), "flipped payload fails verify");

    PackEntry after;
    Expect(DecodeEntry(file.data(), file.size(), header, 0, after) == EntryStatus::Ok,
           "entry record is independent of payload corruption");
}

void TestParseStableIdHash() {
    std::uint64_t value = 0;
    Expect(ParseStableIdHash(L"0123456789abcdef", value), "16 lowercase hex parses");
    Expect(value == 0x0123456789ABCDEFull, "lowercase hex parses to the value");
    Expect(ParseStableIdHash(L"0123456789ABCDEF", value), "uppercase hex parses");
    Expect(value == 0x0123456789ABCDEFull, "uppercase hex parses to the value");
    Expect(ParseStableIdHash(L"FEDCBA9876543210", value), "mixed digits parse");
    Expect(value == 0xFEDCBA9876543210ull, "mixed digits parse to the value");
    Expect(!ParseStableIdHash(L"0123456789abcde", value), "15 chars rejected");
    Expect(!ParseStableIdHash(L"0123456789abcdef0", value), "17 chars rejected");
    Expect(!ParseStableIdHash(L"0123456789abcdeg", value), "non-hex rejected");
    Expect(!ParseStableIdHash(L"", value), "empty string rejected");

    // Interop with the real stable-id producer (src/catalog/stable_id.h).
    const std::wstring id = HashStableId(L"C:\\Windows\\System32\\notepad.exe");
    Expect(id.size() == 16, "HashStableId emits 16 characters");
    Expect(ParseStableIdHash(id, value), "HashStableId output parses");
    std::uint64_t other = 0;
    Expect(ParseStableIdHash(HashStableId(L"C:\\Windows\\System32\\calc.exe"), other),
           "a second HashStableId output parses");
    Expect(value != other, "different inputs produce different hashes");
}

void TestMakeSourceStamp() {
    Expect(MakeSourceStamp(100, 10) == MakeSourceStamp(100, 10), "stamp is deterministic");
    Expect(MakeSourceStamp(100, 10) != MakeSourceStamp(101, 10), "time change moves the stamp");
    Expect(MakeSourceStamp(100, 10) != MakeSourceStamp(100, 11), "size change moves the stamp");
    Expect(MakeSourceStamp(0, 0) == 0, "zero inputs give a zero stamp");
}

// NR-050: payload_end is the trust root of every downstream bounds check, so a
// CRC-valid header can still lie. Every case fixes the CRC on purpose so the
// rejection is provably the new bounds check, not the CRC gate.
void TestMaliciousPayloadEnd() {
    const std::uint64_t huge = 0x0000100000000000ull;
    const auto write_both = [](std::vector<std::uint8_t>& file, std::uint64_t payload_end) {
        PackHeader a;
        a.payload_end = payload_end;
        a.generation = 1;
        EncodeHeader(a, file.data());
        PackHeader b;
        b.payload_end = payload_end;
        b.generation = 0;
        EncodeHeader(b, file.data() + kHeaderSize);
    };

    // payload_end far beyond the file size.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        write_both(file, huge);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "huge payload_end with valid CRC -> BothHeadersBad");
    }
    // payload_end == kPayloadStart - 1.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        write_both(file, kPayloadStart - 1);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "payload_end below kPayloadStart with valid CRC -> BothHeadersBad");
    }
    // payload_end == 0.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        write_both(file, 0);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "zero payload_end with valid CRC -> BothHeadersBad");
    }
    // The empty-pack boundary value stays legal.
    {
        const std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "payload_end == kPayloadStart is Ok");
        Expect(header.payload_end == kPayloadStart, "empty pack payload_end round-trips");
    }
    // payload_end == size (payload fills the file exactly).
    {
        const std::vector<std::uint8_t> file = PackWithPayload();  // size == kPayloadStart + 4
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "payload_end == size is Ok");
        Expect(header.payload_end == kPayloadStart + 4, "full payload_end round-trips");
    }
    // Slot A has an absurd payload_end, slot B is sane but carries an older
    // generation: DecodeHeader must pick the sane slot B, not reject the file.
    {
        std::vector<std::uint8_t> file = MakeEmptyPack();
        PackHeader a;
        a.payload_end = huge;
        a.generation = 1;
        EncodeHeader(a, file.data());
        PackHeader b;
        b.generation = 0;  // default payload_end == kPayloadStart, sane
        EncodeHeader(b, file.data() + kHeaderSize);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "absurd slot A, sane older slot B -> Ok");
        Expect(header.generation == 0, "picks the sane slot B over the newer absurd A");
        Expect(header.payload_end == kPayloadStart, "sane slot's payload_end wins");
    }
}

// NR-075: payload_end is capped by the pack byte budget (design-spec §NFR-001)
// on top of NR-050's file-size bound. Every header keeps a recomputed valid CRC
// so the rejection is provably the new budget check, and the fixture file is
// large enough that the file-size bound is not the deciding factor.
void TestPackByteBudget() {
    const auto make_pack = [](std::uint64_t a_end, std::uint64_t b_end, std::size_t size) {
        std::vector<std::uint8_t> file(size, 0);
        PackHeader a;
        a.payload_end = a_end;
        a.generation = 1;
        EncodeHeader(a, file.data());
        PackHeader b;
        b.payload_end = b_end;
        b.generation = 0;
        EncodeHeader(b, file.data() + kHeaderSize);
        return file;
    };

    // payload_end one byte over the budget -> both slots rejected.
    {
        const auto file = make_pack(kPackByteBudget + 1, kPackByteBudget + 1,
                                    kPackByteBudget + 1);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "payload_end over the budget with valid CRC -> BothHeadersBad");
    }
    // payload_end exactly at the budget stays legal.
    {
        const auto file = make_pack(kPackByteBudget, kPackByteBudget, kPackByteBudget);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::Ok,
               "payload_end == budget is Ok");
        Expect(header.payload_end == kPackByteBudget, "budget boundary payload_end round-trips");
    }
    // Slot A over the budget, slot B sane but older. The physical file itself
    // is kPackByteBudget + 1 bytes, so the NR-114 physical-size rule rejects it
    // outright -- this overrides the NR-075 dual-header "sane sibling wins"
    // assumption. The sane-sibling rule still applies for in-budget files: a
    // file whose size is ≤ budget with one corrupt/over-budget slot still lets
    // the sane slot win; only the physical file size rule is new.
    {
        const auto file = make_pack(kPackByteBudget + 1, kPayloadStart, kPackByteBudget + 1);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "over-budget physical size -> BothHeadersBad (NR-114 overrides sane sibling)");
    }
    // NR-114: a physical file of budget+1 whose headers are entirely sane
    // (payload_end == kPayloadStart, valid CRC) but carries trailing bytes past
    // the budget is rejected outright -- trailing bytes must not be accepted.
    {
        const auto file = make_pack(kPayloadStart, kPayloadStart, kPackByteBudget + 1);
        PackHeader header;
        Expect(DecodeHeader(file.data(), file.size(), header) == PackStatus::BothHeadersBad,
               "sane headers with over-budget trailing bytes -> BothHeadersBad");
    }
}

void TestFuzz() {
    std::mt19937 rng(20260805u);
    std::uniform_int_distribution<std::size_t> len_dist(0, kPayloadStart + 1024);
    std::uniform_int_distribution<unsigned> byte_dist(0, 255u);
    std::uniform_int_distribution<std::size_t> slot_dist(0, kIndexCapacity + 16);
    for (int iter = 0; iter < 400; ++iter) {
        const std::size_t len = len_dist(rng);
        std::vector<std::uint8_t> data(len);
        for (std::uint8_t& byte : data) {
            byte = static_cast<std::uint8_t>(byte_dist(rng));
        }
        PackHeader header;
        (void)DecodeHeader(data.data(), data.size(), header);
        for (int k = 0; k < 8; ++k) {
            PackEntry entry;
            (void)DecodeEntry(data.data(), data.size(), header, slot_dist(rng), entry);
            (void)VerifyPayload(data.data(), data.size(), entry);
        }
    }
}

} // namespace

int wmain() {
    TestEmptyPack();
    TestHeaderRoundTrip();
    TestEntryRoundTrip();
    TestCrcKnownVectors();
    TestAbsent();
    TestBadMagic();
    TestBothHeadersBadCrc();
    TestSingleSlotCorrupt();
    TestBothValidPickNewer();
    TestNewerSchema();
    TestIndexCapacityMismatch();
    TestEntryCorruptionIsolation();
    TestOutOfBounds();
    TestBadVariant();
    TestPayloadVerification();
    TestParseStableIdHash();
    TestMakeSourceStamp();
    TestMaliciousPayloadEnd();
    TestPackByteBudget();
    TestFuzz();
    std::printf("NR-033 icon pack format check PASSED\n");
    return 0;
}
