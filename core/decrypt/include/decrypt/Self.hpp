#ifndef DECRYPT_SELF_HPP
#define DECRYPT_SELF_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <array>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  SELF (Signed ELF) — Sony's executable container format
//
//  Used for eboot.bin, *.prx, *.sprx, kernel blobs, etc.
//  The inner payload is a standard ELF64 whose segment offsets have
//  been scrambled by the SELF metadata insertion.
//
//  Magic:
//    0x1D3D154F  — standard SELF
//    0xEEF51454  — kernel SELF
//
//  Structure (simplified):
//    [0x00..0x20]   SELF header (magic, version, mode, header_size,
//                    meta_size, file_size, entry_count, flags)
//    [0x20..0x40]   Extended info (auth_id, vendor_id, self_type,
//                    version_major/minor, padding)
//    [0x40..var]    Segment info table (entry_count × 0x20)
//    [var..var]     SCE version info (optional)
//    [var..var]     Control info blocks (optional — digests, keys, caps)
//    [inner_elf..]  The actual ELF64 binary with shifted segments
// ──────────────────────────────────────────────────────────────────────

static constexpr uint32_t SELF_MAGIC        = 0x1D3D154F;

// ── SELF types ──

enum class SelfType : uint64_t {
    None           = 0,
    Kernel         = 0x0C,
    SecurityModule = 0x0E,
    SecureKernel   = 0x0F,
    Application    = 0x08,
    SystemModule   = 0x09,
    SystemDaemon   = 0x0A,
    Unknown        = 0xFF
};

static inline const char* self_type_name(SelfType type) {
    switch (type) {
        case SelfType::Kernel:         return "Kernel";
        case SelfType::SecurityModule: return "SecurityModule";
        case SelfType::SecureKernel:   return "SecureKernel";
        case SelfType::Application:    return "Application";
        case SelfType::SystemModule:   return "SystemModule";
        case SelfType::SystemDaemon:   return "SystemDaemon";
        default:                       return "Unknown";
    }
}

// ── Packed structures ──

#pragma pack(push, 1)

struct SelfHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  mode;
    uint8_t  endian;       // 1 = LE
    uint8_t  attribs;
    uint32_t key_type;
    uint16_t header_size;
    uint16_t meta_size;
    uint64_t file_size;
    uint16_t entry_count;
    uint16_t flags;
    uint8_t  padding[4];
};
static_assert(sizeof(SelfHeader) == 0x20, "SelfHeader = 32 bytes");

struct SelfExtendedInfo {
    uint64_t auth_id;
    uint64_t vendor_id;
    uint64_t self_type;
    uint64_t version;
    uint8_t  padding[0x20 - 32];
};
static_assert(sizeof(SelfExtendedInfo) == 0x20, "SelfExtendedInfo = 32 bytes");

struct SelfSegmentInfo {
    uint64_t flags;
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t decompressed_size;
};
static_assert(sizeof(SelfSegmentInfo) == 0x20, "SelfSegmentInfo = 32 bytes");

// ── Standard ELF64 structures ──

struct Elf64Header {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};
static_assert(sizeof(Elf64Header) == 64, "Elf64Header = 64 bytes");

struct Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};
static_assert(sizeof(Elf64ProgramHeader) == 56, "Elf64Phdr = 56 bytes");

#pragma pack(pop)

static constexpr uint32_t PT_LOAD              = 1;
static constexpr uint32_t PT_SCE_DYNLIBDATA    = 0x61000000;
static constexpr uint32_t PT_SCE_PROCPARAM     = 0x61000001;
static constexpr uint32_t PT_SCE_COMMENT       = 0x6FFFFF00;
static constexpr uint32_t PT_SCE_VERSION       = 0x6FFFFF01;

// ── Segment encryption flag bits ──

static constexpr uint64_t SEG_ENCRYPTED = 0x01;
static constexpr uint64_t SEG_SIGNED    = 0x02;
static constexpr uint64_t SEG_DEFLATED  = 0x08;

// ── Parsed SELF representation ──

struct SelfSegment {
    uint32_t index;
    uint64_t flags;
    uint64_t data_offset;     // where the segment data actually lives in file
    uint64_t compressed_size;
    uint64_t decompressed_size;
    bool     encrypted;
    bool     compressed;
    bool     signed_seg;
};

struct ParsedSelf {
    SelfHeader       header;
    SelfExtendedInfo extended;
    SelfType         type;

    uint64_t inner_elf_offset;      // file offset of inner \x7FELF
    Elf64Header elf_header;
    std::vector<Elf64ProgramHeader> program_headers;
    std::vector<SelfSegment>        segments;
    std::vector<uint8_t>            raw_data;

    bool is_encrypted() const {
        return std::any_of(segments.begin(), segments.end(),
                           [](const auto& s) { return s.encrypted; });
    }

    bool is_fake_self() const {
        // fSELF: auth_id is 0 or first control info indicates fake
        return extended.auth_id == 0 || !is_encrypted();
    }
};

// ── Entropy check ──

inline double byte_entropy(const uint8_t* data, size_t length) {
    if (length == 0) return 0.0;
    std::array<size_t, 256> counts{};
    for (size_t i = 0; i < length; ++i) counts[data[i]]++;
    double entropy = 0.0;
    for (auto c : counts) {
        if (c == 0) continue;
        double p = static_cast<double>(c) / static_cast<double>(length);
        entropy -= p * std::log2(p);
    }
    return entropy;
}

// ── Parse ──

inline ParsedSelf parse_self(const std::vector<uint8_t>& data) {
    ParsedSelf self{};
    self.raw_data = data;

    if (data.size() < sizeof(SelfHeader) + sizeof(SelfExtendedInfo))
        throw std::runtime_error("SELF: file too small");

    std::memcpy(&self.header, data.data(), sizeof(SelfHeader));

    if (self.header.magic != SELF_MAGIC)
        throw std::runtime_error("SELF: bad magic");

    std::memcpy(&self.extended, data.data() + sizeof(SelfHeader), sizeof(SelfExtendedInfo));
    self.type = static_cast<SelfType>(self.extended.self_type);

    // ── Read segment info table ──
    const size_t seg_table_offset = sizeof(SelfHeader) + sizeof(SelfExtendedInfo);
    for (uint16_t i = 0; i < self.header.entry_count; ++i) {
        size_t off = seg_table_offset + i * sizeof(SelfSegmentInfo);
        if (off + sizeof(SelfSegmentInfo) > data.size())
            break;

        SelfSegmentInfo info{};
        std::memcpy(&info, data.data() + off, sizeof(SelfSegmentInfo));

        SelfSegment seg;
        seg.index             = i;
        seg.flags             = info.flags;
        seg.data_offset       = info.offset;
        seg.compressed_size   = info.compressed_size;
        seg.decompressed_size = info.decompressed_size;
        seg.encrypted         = (info.flags & SEG_ENCRYPTED) != 0;
        seg.compressed        = (info.flags & SEG_DEFLATED) != 0;
        seg.signed_seg        = (info.flags & SEG_SIGNED) != 0;
        self.segments.push_back(seg);
    }

    // ── Find inner ELF ──
    const uint8_t elf_magic[] = {0x7F, 'E', 'L', 'F'};
    auto it = std::search(data.begin(), data.end(),
                          std::begin(elf_magic), std::end(elf_magic));
    if (it == data.end())
        throw std::runtime_error("SELF: no inner ELF found");

    self.inner_elf_offset = static_cast<uint64_t>(std::distance(data.begin(), it));

    if (self.inner_elf_offset + sizeof(Elf64Header) > data.size())
        throw std::runtime_error("SELF: inner ELF header truncated");

    std::memcpy(&self.elf_header, data.data() + self.inner_elf_offset,
                sizeof(Elf64Header));

    // ── Read ELF program headers ──
    uint64_t phoff = self.inner_elf_offset + self.elf_header.phoff;
    for (uint16_t i = 0; i < self.elf_header.phnum; ++i) {
        uint64_t off = phoff + i * sizeof(Elf64ProgramHeader);
        if (off + sizeof(Elf64ProgramHeader) > data.size()) break;

        Elf64ProgramHeader phdr{};
        std::memcpy(&phdr, data.data() + off, sizeof(Elf64ProgramHeader));
        self.program_headers.push_back(phdr);
    }

    return self;
}

// ── Check encryption status of all segments ──

struct EncryptionReport {
    struct SegmentStatus {
        uint32_t index;
        bool     flag_encrypted;
        double   entropy;
        bool     looks_encrypted;  // entropy ≥ 7.5
    };
    std::vector<SegmentStatus> segments;
    bool fully_encrypted;
    bool partially_encrypted;
    bool decrypted;                // all segments are low-entropy
};

inline EncryptionReport check_encryption(const ParsedSelf& self) {
    EncryptionReport report{};
    int encrypted_count = 0;
    int total_checked   = 0;

    for (const auto& seg : self.segments) {
        if (seg.compressed_size == 0) continue;
        if (seg.data_offset + seg.compressed_size > self.raw_data.size()) continue;

        size_t sample_len = std::min(seg.compressed_size, uint64_t(0x10000));
        double ent = byte_entropy(self.raw_data.data() + seg.data_offset, sample_len);

        EncryptionReport::SegmentStatus status;
        status.index           = seg.index;
        status.flag_encrypted  = seg.encrypted;
        status.entropy         = ent;
        status.looks_encrypted = (ent >= 7.5);
        report.segments.push_back(status);

        if (status.looks_encrypted) encrypted_count++;
        total_checked++;
    }

    report.fully_encrypted     = (total_checked > 0 && encrypted_count == total_checked);
    report.partially_encrypted = (encrypted_count > 0 && encrypted_count < total_checked);
    report.decrypted           = (encrypted_count == 0);
    return report;
}

// ── Extract raw segment data (works on decrypted/fSELF containers) ──

inline std::vector<uint8_t> extract_segment(const ParsedSelf& self, uint32_t index) {
    if (index >= self.segments.size())
        throw std::runtime_error("SELF: segment index out of range");

    const auto& seg = self.segments[index];
    if (seg.encrypted)
        throw std::runtime_error("SELF: segment " + std::to_string(index) +
                                  " is encrypted — need decryption keys");

    if (seg.data_offset + seg.compressed_size > self.raw_data.size())
        throw std::runtime_error("SELF: segment data out of bounds");

    return {self.raw_data.begin() + seg.data_offset,
            self.raw_data.begin() + seg.data_offset + seg.compressed_size};
}

}  // namespace Decrypt

#endif  // DECRYPT_SELF_HPP
