// Focused checks for NR-035 (file-backed icon store).
//
// Drives IconStore against a temp pack under %TEMP%\NimbleRunTest\<pid> — never
// the real %LOCALAPPDATA%\NimbleRun. Payloads are synthetic bytes; time and
// source stamps are injected as parameters so no test depends on a wall clock.
// The pack layout is exercised through icon_pack_format's own encode functions
// to simulate torn writes and corruption without a second implementation.

#include "icons/icon_pack_format.h"
#include "icons/icon_store.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using nimblerun::Crc32;
using nimblerun::EncodeEntry;
using nimblerun::EncodeHeader;
using nimblerun::IconStore;
using nimblerun::kHeaderSize;
using nimblerun::kIndexCapacity;
using nimblerun::kIndexEntrySize;
using nimblerun::kIndexOffset;
using nimblerun::kPackByteBudget;
using nimblerun::kPayloadStart;
using nimblerun::MakeEmptyPack;
using nimblerun::PackEntry;
using nimblerun::PackHeader;
using StoreState = nimblerun::IconStore::StoreState;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

fs::path TempDir() {
    const fs::path dir =
        fs::temp_directory_path() / L"NimbleRunTest" / std::to_wstring(GetCurrentProcessId());
    fs::create_directories(dir);
    return dir;
}

fs::path PackPath(const fs::path& dir) {
    return dir / L"icons.cache";
}

std::wstring Id(std::size_t n) {
    wchar_t buf[24];
    std::swprintf(buf, 24, L"%016zx", n);
    return buf;
}

std::vector<std::uint8_t> Payload(int seed, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>(seed * 31 + i * 7);
    }
    return out;
}

std::vector<std::uint8_t> ReadFileBytes(const fs::path& path) {
    // The store maps the pack with PAGE_READWRITE; a concurrent read handle must
    // declare both read and write sharing or it hits a sharing violation.
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != FALSE && read > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    CloseHandle(file);
    return bytes;
}

void WriteFileBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Expect(false, "write file handle");
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                              &written, nullptr);
    CloseHandle(file);
    Expect(ok != FALSE && written == bytes.size(), "write file bytes");
}

// Locates the index slot and decoded entry for (stable_id, variant) inside a
// raw pack file. Pending iteration order is unspecified, so tests must not
// assume a particular stable_id lives at a particular slot.
bool FindEntry(const std::vector<std::uint8_t>& bytes, std::uint64_t hash,
               std::uint16_t variant, std::size_t& slot, nimblerun::PackEntry& entry) {
    nimblerun::PackHeader header;
    if (nimblerun::DecodeHeader(bytes.data(), bytes.size(), header) !=
        nimblerun::PackStatus::Ok) {
        return false;
    }
    for (std::size_t s = 0; s < kIndexCapacity; ++s) {
        if (nimblerun::DecodeEntry(bytes.data(), bytes.size(), header, s, entry) ==
                nimblerun::EntryStatus::Ok &&
            entry.stable_id_hash == hash && entry.variant == variant) {
            slot = s;
            return true;
        }
    }
    return false;
}

// Every section starts from a clean, valid pack file so tests are independent
// of each other and of the order they run in.
void ResetPack(const fs::path& dir) {
    WriteFileBytes(PackPath(dir), MakeEmptyPack());
}

void TestFreshOpen(const fs::path& dir) {
    const fs::path path = PackPath(dir);
    IconStore store(IconStore::IconStorePaths{path});
    Expect(store.Open() == StoreState::Ready, "fresh open ready");
    Expect(!store.Stats().recreated, "fresh open not recreated");
    Expect(store.Stats().entries == 0, "fresh open empty");
    Expect(ReadFileBytes(path).size() == kPayloadStart, "fresh file is empty pack size");
}

void TestPutFlushReload(const fs::path& dir) {
    ResetPack(dir);
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        Expect(store.Open() == StoreState::Ready, "open");
        store.Put(L"0000000000000001", 48, Payload(1, 64), 0x1234, 1000);
        Expect(store.Flush({}, 1000), "flush ok");
    }
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "reopen ready");
    Expect(store.Lookup(L"0000000000000001", 48, 0x1234, 1000) == Payload(1, 64),
           "round-trip bytes identical");
    Expect(store.Lookup(L"0000000000000001", 48, 0x1234, 1000) == Payload(1, 64),
           "round-trip bytes identical");
}

void TestVariantSeparate(const fs::path& dir) {
    ResetPack(dir);
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();
        store.Put(L"0000000000000001", 48, Payload(1, 32), 0x1000, 1);
        store.Put(L"0000000000000001", 96, Payload(2, 64), 0x1000, 2);
        store.Flush({}, 2);
    }
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    Expect(store.Lookup(L"0000000000000001", 48, 0x1000, 100) == Payload(1, 32),
           "variant 48 payload");
    Expect(store.Lookup(L"0000000000000001", 96, 0x1000, 100) == Payload(2, 64),
           "variant 96 payload");
}

void TestSourceStampStale(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    store.Put(L"0000000000000001", 48, Payload(1, 32), 0x1000, 1);
    store.Flush({}, 1);
    Expect(store.Lookup(L"0000000000000001", 48, 0x1000, 100).size() == 32, "matching stamp hits");
    Expect(store.Lookup(L"0000000000000001", 48, 0x9999, 100).empty(), "different stamp misses");
}

void TestTtl(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    store.Put(L"0000000000000001", 48, Payload(1, 32), 0, 1000);  // no file -> TTL entry
    store.Flush({}, 1000);
    const std::uint64_t day = 24ull * 60 * 60;
    Expect(store.Lookup(L"0000000000000001", 48, 0, 1000 + 29 * day).size() == 32,
           "within 30-day TTL hits");
    Expect(store.Lookup(L"0000000000000001", 48, 0, 1000 + 31 * day).empty(),
           "past 30-day TTL misses");
}

void TestLookupCopies(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    store.Put(L"0000000000000001", 48, Payload(1, 100), 0x1000, 1);
    store.Flush({}, 1);
    const std::vector<std::uint8_t> copy = store.Lookup(L"0000000000000001", 48, 0x1000, 100);
    Expect(copy == Payload(1, 100), "initial lookup");
    for (int round = 0; round < 5; ++round) {
        store.Put(L"0000000000000001", 48, Payload(1, 100), 0x1000, 10 + round);
        store.Flush({}, 10 + round);  // forces compaction, remaps the file
    }
    Expect(copy == Payload(1, 100), "copy survives remap/compaction");
}

// Simulates a crash between "payload + entry flushed" and "header committed":
// the header still points at the old payload_end, so the uncommitted entry is
// out of bounds and must be dropped while the committed entries survive.
void TestTornWrite(const fs::path& dir) {
    ResetPack(dir);
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();
        store.Put(L"0000000000000001", 48, Payload(1, 32), 0x1000, 1);
        store.Put(L"0000000000000002", 48, Payload(2, 32), 0x2000, 2);
        store.Flush({}, 2);
    }
    std::vector<std::uint8_t> torn = ReadFileBytes(PackPath(dir));
    const std::uint64_t payload_offset = torn.size();
    const std::vector<std::uint8_t> fake(16, 0xAA);
    torn.insert(torn.end(), fake.begin(), fake.end());
    PackEntry entry;
    entry.stable_id_hash = 0x00000000000000AA;
    entry.variant = 48;
    entry.in_use = true;
    entry.payload_len = static_cast<std::uint32_t>(fake.size());
    entry.payload_offset = payload_offset;
    entry.payload_crc32 = Crc32(fake.data(), fake.size());
    entry.source_stamp = 0;
    entry.fetched_utc = 99;
    entry.last_used_utc = 99;
    std::vector<std::uint8_t> slot_bytes(kIndexEntrySize, 0);
    EncodeEntry(entry, slot_bytes.data());
    std::copy(slot_bytes.begin(), slot_bytes.end(),
              torn.begin() + kIndexOffset + 5 * kIndexEntrySize);
    WriteFileBytes(PackPath(dir), torn);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "torn open ok");
    Expect(store.Stats().dropped_entries == 1, "torn entry dropped");
    Expect(store.Lookup(L"0000000000000001", 48, 0x1000, 100).size() == 32, "torn neighbor 1");
    Expect(store.Lookup(L"0000000000000002", 48, 0x2000, 100).size() == 32, "torn neighbor 2");
    Expect(store.Stats().dead_bytes > 0, "torn write leaves dead bytes");
}

void TestSingleEntryCorruption(const fs::path& dir) {
    ResetPack(dir);
    {
        WriteFileBytes(PackPath(dir), MakeEmptyPack());  // clean, deterministic slots
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();
        for (int i = 1; i <= 5; ++i) {
            store.Put(Id(i), 48, Payload(i, 32), 0x1000 + i, i);
        }
        store.Flush({}, 100);
    }
    std::vector<std::uint8_t> bytes = ReadFileBytes(PackPath(dir));
    std::size_t slot = 0;
    nimblerun::PackEntry entry;
    std::uint64_t hash = 0;
    Expect(nimblerun::ParseStableIdHash(Id(3), hash), "parse Id(3)");
    Expect(FindEntry(bytes, hash, 48, slot, entry), "locate Id(3) slot");
    bytes[kIndexOffset + slot * kIndexEntrySize + 8] ^= 0x01;  // variant byte
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "corrupt-entry open ok");
    Expect(store.Stats().dropped_entries == 1, "exactly one dropped");
    for (int i = 1; i <= 5; ++i) {
        if (i == 3) {
            continue;
        }
        Expect(store.Lookup(Id(i), 48, 0x1000 + i, 100).size() == 32, "neighbor entry survives");
    }
}

void TestPayloadCorruption(const fs::path& dir) {
    ResetPack(dir);
    {
        WriteFileBytes(PackPath(dir), MakeEmptyPack());  // clean, deterministic payload offsets
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();
        store.Put(Id(1), 48, Payload(1, 32), 0x1000, 1);
        store.Put(Id(2), 48, Payload(2, 32), 0x2000, 2);
        store.Put(Id(3), 48, Payload(3, 32), 0x3000, 3);
        store.Flush({}, 3);
    }
    std::vector<std::uint8_t> bytes = ReadFileBytes(PackPath(dir));
    std::size_t slot = 0;
    nimblerun::PackEntry entry;
    std::uint64_t hash = 0;
    Expect(nimblerun::ParseStableIdHash(Id(1), hash), "parse Id(1)");
    Expect(FindEntry(bytes, hash, 48, slot, entry), "locate Id(1) payload");
    bytes[entry.payload_offset + 2] ^= 0xFF;  // flip a byte inside Id(1)'s payload
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    Expect(store.Lookup(Id(1), 48, 0x1000, 100).empty(), "corrupt payload misses");
    Expect(store.Lookup(Id(2), 48, 0x2000, 100).size() == 32, "payload neighbor 2 hits");
    Expect(store.Lookup(Id(3), 48, 0x3000, 100).size() == 32, "payload neighbor 3 hits");
    // The next Flush reclaims the corrupt slot so a new put can reuse it.
    store.Put(Id(9), 48, Payload(9, 32), 0x9000, 900);
    Expect(store.Flush({}, 900), "flush after payload corruption");
    Expect(store.Lookup(Id(9), 48, 0x9000, 900).size() == 32, "corrupt slot reused");
    Expect(store.Lookup(Id(1), 48, 0x1000, 900).empty(), "corrupt entry stays gone");
}

void TestWholeFileCorruption(const fs::path& dir) {
    std::vector<std::uint8_t> bytes = MakeEmptyPack();
    bytes[0] ^= 0x01;  // slot A magic
    bytes[32] ^= 0x01;  // slot B magic
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "recreated ready");
    Expect(store.Stats().recreated, "recreated flag set");
    Expect(store.Stats().entries == 0, "recreated empty");
    Expect(!fs::exists(dir / L"icons.cache.corrupt"), "no .corrupt copy for a cache");
}

void TestNewerSchema(const fs::path& dir) {
    std::vector<std::uint8_t> bytes = MakeEmptyPack();
    for (std::size_t slot = 0; slot < 2; ++slot) {
        std::uint8_t* header = bytes.data() + slot * kHeaderSize;
        header[4] = 2;  // schema_version little-endian -> 2
        header[5] = 0;
        const std::uint32_t crc = Crc32(header, 24);
        header[24] = static_cast<std::uint8_t>(crc & 0xFF);
        header[25] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
        header[26] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
        header[27] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
    }
    WriteFileBytes(PackPath(dir), bytes);
    const std::vector<std::uint8_t> before = ReadFileBytes(PackPath(dir));

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Disabled, "newer schema disabled");
    Expect(store.Lookup(Id(1), 48, 0, 100).empty(), "disabled lookup empty");
    store.Put(Id(1), 48, Payload(1, 32), 0, 1);
    Expect(!store.Flush({}, 1), "disabled flush is a no-op failure");
    Expect(before == ReadFileBytes(PackPath(dir)), "newer schema file bytes untouched");
}

void TestEviction(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)},
                    /*max_bytes=*/kPayloadStart + 80);
    store.Open();
    store.Put(Id(1), 48, Payload(1, 40), 0x1000, 1);
    store.Flush({}, 1);
    store.Put(Id(2), 48, Payload(2, 40), 0x2000, 2);
    store.Flush({}, 2);
    store.Put(Id(3), 48, Payload(3, 40), 0x3000, 3);
    store.Flush({}, 3);
    Expect(store.Lookup(Id(1), 48, 0x1000, 100).empty(), "oldest evicted");
    Expect(store.Lookup(Id(2), 48, 0x2000, 100).size() == 40, "second survives");
    Expect(store.Lookup(Id(3), 48, 0x3000, 100).size() == 40, "newest survives");
}

void TestPinnedExemption(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)},
                    /*max_bytes=*/kPayloadStart + 80);
    store.Open();
    store.Put(Id(1), 48, Payload(1, 40), 0x1000, 1);
    store.Flush({}, 1);
    store.Put(Id(2), 48, Payload(2, 40), 0x2000, 2);
    store.Flush({}, 2);
    store.Put(Id(3), 48, Payload(3, 40), 0x3000, 3);
    store.Flush({Id(1)}, 3);  // the oldest entry is pinned
    Expect(store.Lookup(Id(1), 48, 0x1000, 100).size() == 40, "pinned oldest survives");
    Expect(store.Lookup(Id(2), 48, 0x2000, 100).empty(), "next-oldest evicted instead");
    Expect(store.Lookup(Id(3), 48, 0x3000, 100).size() == 40, "newest survives");
}

void TestSlotCapacity(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});  // default byte budget
    store.Open();
    for (std::size_t i = 1; i <= kIndexCapacity + 1; ++i) {
        store.Put(Id(i), 48, Payload(i, 8), 0x1000 + i, i);
        store.Flush({}, i);
    }
    Expect(store.Stats().entries == kIndexCapacity, "entries capped at index capacity");
    Expect(store.Lookup(Id(1), 48, 0x1001, 100000).empty(), "oldest dropped at capacity");
    Expect(store.Lookup(Id(kIndexCapacity + 1), 48, 0x1000 + kIndexCapacity + 1, 100000).size() == 8,
           "newest survives capacity");
}

void TestCompaction(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    for (int i = 1; i <= 3; ++i) {
        store.Put(Id(i), 48, Payload(i, 100), 0x1000 + i, i);
    }
    store.Flush({}, 10);
    const std::uint64_t mid_size = fs::file_size(PackPath(dir));
    for (int round = 0; round < 10; ++round) {
        for (int i = 1; i <= 3; ++i) {
            store.Put(Id(i), 48, Payload(i, 100), 0x1000 + i, 100 + round);
        }
        store.Flush({}, 100 + round);
    }
    const std::uint64_t final_size = fs::file_size(PackPath(dir));
    Expect(final_size <= mid_size + 512, "file did not grow despite repeated overwrites");
    Expect(store.Stats().dead_bytes < 500, "dead bytes reclaimed");
    for (int i = 1; i <= 3; ++i) {
        Expect(store.Lookup(Id(i), 48, 0x1000 + i, 1000).size() == 100, "record survives compaction");
    }
    Expect(!fs::exists(dir / L"icons.cache.tmp"), "no .tmp leftover after compaction");
}

void TestCompactionFailure(const fs::path& dir) {
    ResetPack(dir);
    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    store.Open();
    store.Put(Id(1), 48, Payload(1, 100), 0x1000, 1);
    store.Flush({}, 1);
    store.Put(Id(2), 48, Payload(2, 100), 0x2000, 2);
    store.Flush({}, 2);
    store.Put(Id(1), 48, Payload(1, 100), 0x1000, 3);
    store.Flush({}, 3);  // dead = 100 (half of live 200 is 100, not yet over)
    // Make the .tmp path a directory so compaction's CreateFileW fails.
    fs::create_directory(dir / L"icons.cache.tmp");
    store.Put(Id(2), 48, Payload(2, 100), 0x2000, 4);
    Expect(store.Flush({}, 4), "flush itself succeeds; compaction fails non-fatally");
    Expect(store.Lookup(Id(1), 48, 0x1000, 100).size() == 100, "record 1 still readable");
    Expect(store.Lookup(Id(2), 48, 0x2000, 100).size() == 100, "record 2 still readable");
    Expect(store.Stats().dead_bytes >= 100, "dead bytes retained when compaction fails");
    fs::remove(dir / L"icons.cache.tmp");
}

void TestLockedFile(const fs::path& dir) {
    ResetPack(dir);
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();
        store.Put(Id(1), 48, Payload(1, 8), 0x1000, 1);
        store.Flush({}, 1);
    }
    // Hold the file with FILE_SHARE_NONE so any later open requiring write
    // access fails with a sharing violation.
    const HANDLE lock = CreateFileW(PackPath(dir).c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(lock != INVALID_HANDLE_VALUE, "lock handle acquired");
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        Expect(store.Open() == StoreState::Disabled, "locked file disables store");
        Expect(store.Lookup(Id(1), 48, 0x1000, 100).empty(), "disabled lookup empty");
    }
    CloseHandle(lock);
}

// NR-050: an icons.cache whose header claims a huge payload_end -- with the CRC
// recomputed so it passes the old CRC gate -- must not let the store grow the
// file to disk-filling size. Open degrades to a rebuilt pack.
void TestMaliciousPayloadEnd(const fs::path& dir) {
    std::vector<std::uint8_t> bytes = MakeEmptyPack();
    const std::uint64_t huge = 0x0000100000000000ull;
    for (std::size_t slot = 0; slot < 2; ++slot) {
        PackHeader header;
        header.generation = static_cast<std::uint32_t>(1 - slot);
        header.payload_end = huge;
        EncodeHeader(header, bytes.data() + slot * kHeaderSize);
    }
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    const StoreState state = store.Open();
    const std::uint64_t size_after_open = fs::file_size(PackPath(dir));
    Expect(state == StoreState::Disabled || store.Stats().recreated,
           "malicious header degrades to no cache (rebuilt)");
    Expect(size_after_open <= kPayloadStart + 1024,
           "Open does not grow the file past the empty-pack size");
    store.Put(Id(1), 48, Payload(1, 32), 0x1000, 1);
    (void)store.Flush({}, 1);
    const std::uint64_t size_after_flush = fs::file_size(PackPath(dir));
    Expect(size_after_flush <= kPayloadStart + 1024,
           "Put + Flush after a malicious header keeps the file bounded");
}

// NR-075: a CRC-valid pack whose payload_end exceeds the 32 MiB pack budget
// must not be accepted as Ready even when the file itself is that large -- a
// single Lookup could otherwise copy close to 4 GiB into a vector. Open
// classifies it via DecodeHeader as a rebuildable corruption and recreates a
// bounded empty pack instead of mapping a potentially GB-sized read.
void TestOverBudgetPack(const fs::path& dir) {
    std::vector<std::uint8_t> bytes(kPackByteBudget + 1, 0);
    for (std::size_t slot = 0; slot < 2; ++slot) {
        PackHeader header;
        header.generation = static_cast<std::uint32_t>(1 - slot);
        header.payload_end = kPackByteBudget + 1;
        EncodeHeader(header, bytes.data() + slot * kHeaderSize);
    }
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "over-budget pack recreated ready");
    Expect(store.Stats().recreated, "over-budget pack marked recreated");
    Expect(store.Stats().entries == 0, "recreated pack empty");
    Expect(ReadFileBytes(PackPath(dir)).size() == kPayloadStart,
           "over-budget pack rebuilt to the bounded empty-pack size");
}

// NR-114: a CRC-valid pack whose PHYSICAL size exceeds the whole-pack budget --
// e.g. valid headers with payload_end == kPayloadStart plus trailing garbage
// bytes -- must not be accepted as Ready. MapFile rejects the physical file
// before any mapping is created; Open recreates a bounded empty pack and marks
// the store recreated so diagnostics can tell this input apart.
void TestOverBudgetPhysicalFile(const fs::path& dir) {
    std::vector<std::uint8_t> bytes(kPackByteBudget + 1, 0);
    for (std::size_t slot = 0; slot < 2; ++slot) {
        PackHeader header;
        header.generation = static_cast<std::uint32_t>(1 - slot);
        header.payload_end = kPayloadStart;
        EncodeHeader(header, bytes.data() + slot * kHeaderSize);
    }
    WriteFileBytes(PackPath(dir), bytes);

    IconStore store(IconStore::IconStorePaths{PackPath(dir)});
    Expect(store.Open() == StoreState::Ready, "over-budget physical file recreated ready");
    Expect(store.Stats().recreated, "over-budget physical file marked recreated");
    Expect(store.Stats().entries == 0, "recreated pack empty");
    Expect(ReadFileBytes(PackPath(dir)).size() == kPayloadStart,
           "over-budget physical file rebuilt to the bounded empty-pack size");
}

// NR-108: the fixed prefix counts toward the budget, and a rejected batch
// must leave the committed header and physical file untouched.
void TestWholePackBudget(const fs::path& dir) {
    ResetPack(dir);
    {
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        Expect(store.Open() == StoreState::Ready, "budget test open");
        const std::vector<std::uint8_t> before = ReadFileBytes(PackPath(dir));
        PackHeader before_header;
        Expect(nimblerun::DecodeHeader(before.data(), before.size(), before_header) ==
                   nimblerun::PackStatus::Ok,
               "budget test reads initial header");

        store.Put(Id(1), 48, Payload(1, kPackByteBudget - kPayloadStart + 1), 0x1000, 1);
        Expect(store.Flush({}, 1), "oversized payload is dropped safely");
        const std::vector<std::uint8_t> after = ReadFileBytes(PackPath(dir));
        PackHeader after_header;
        Expect(after.size() == before.size(), "oversized payload does not grow the pack");
        Expect(nimblerun::DecodeHeader(after.data(), after.size(), after_header) ==
                   nimblerun::PackStatus::Ok,
               "oversized payload leaves a valid header");
        Expect(after_header.payload_end == before_header.payload_end,
               "oversized payload does not commit a new payload_end");
        Expect(store.Lookup(Id(1), 48, 0x1000, 1).empty(), "oversized payload is not cached");
    }

    ResetPack(dir);
    IconStore small(IconStore::IconStorePaths{PackPath(dir)}, kPayloadStart + 80);
    Expect(small.Open() == StoreState::Ready, "batch budget test open");
    small.Put(Id(1), 48, Payload(1, 40), 0x1000, 1);
    small.Put(Id(2), 48, Payload(2, 40), 0x2000, 1);
    small.Put(Id(3), 48, Payload(3, 40), 0x3000, 1);
    Expect(small.Flush({Id(1)}, 1), "oversized batch is dropped safely");
    const std::vector<std::uint8_t> batch = ReadFileBytes(PackPath(dir));
    PackHeader batch_header;
    Expect(batch.size() <= kPayloadStart + 80, "batch respects whole-pack budget");
    Expect(nimblerun::DecodeHeader(batch.data(), batch.size(), batch_header) ==
               nimblerun::PackStatus::Ok,
           "batch leaves a valid header");
    Expect(batch_header.payload_end <= kPayloadStart + 80,
           "batch header respects whole-pack budget");
    Expect(small.Lookup(Id(1), 48, 0x1000, 1).empty(), "oversized batch does not cache pinned input");
}

void TestRandomFuzz(const fs::path& dir) {
    std::mt19937 rng(20260805);
    for (int i = 0; i < 50; ++i) {
        const std::size_t length = rng() % (kPayloadStart + 1024);
        std::vector<std::uint8_t> bytes(length);
        for (std::uint8_t& byte : bytes) {
            byte = static_cast<std::uint8_t>(rng());
        }
        WriteFileBytes(PackPath(dir), bytes);
        IconStore store(IconStore::IconStorePaths{PackPath(dir)});
        store.Open();  // must classify any input without crashing
    }
}

void RunAll(const fs::path& dir) {
    TestFreshOpen(dir);
    TestPutFlushReload(dir);
    TestVariantSeparate(dir);
    TestSourceStampStale(dir);
    TestTtl(dir);
    TestLookupCopies(dir);
    TestTornWrite(dir);
    TestSingleEntryCorruption(dir);
    TestPayloadCorruption(dir);
    TestWholeFileCorruption(dir);
    TestNewerSchema(dir);
    TestEviction(dir);
    TestPinnedExemption(dir);
    TestSlotCapacity(dir);
    TestCompaction(dir);
    TestCompactionFailure(dir);
    TestLockedFile(dir);
    TestMaliciousPayloadEnd(dir);
    TestOverBudgetPack(dir);
    TestOverBudgetPhysicalFile(dir);
    TestWholePackBudget(dir);
    TestRandomFuzz(dir);
}

} // namespace

int wmain() {
    const fs::path dir = TempDir();
    // Each section starts from a clean pack file.
    fs::remove(PackPath(dir));
    fs::remove(dir / L"icons.cache.tmp");
    RunAll(dir);
    fs::remove_all(dir);
    std::wprintf(L"nimblerun_icon_store_test: all checks passed\n");
    return 0;
}
