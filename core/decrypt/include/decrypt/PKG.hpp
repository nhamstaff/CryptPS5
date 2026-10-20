#ifndef DECRYPT_PKG_HPP
#define DECRYPT_PKG_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <decrypt/Crypto.hpp>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  PKG — PS5 game package container
//
//  Format:
//    [0x00..0x04]    magic: 0x7F434E54 ("\x7FCNT")
//    [0x04..0x08]    pkg_type
//    [0x08..0x0C]    pkg_0x8 (flags/revision)
//    [0x0C..0x10]    file_count
//    [0x10..0x18]    entry_count
//    [0x18..0x1C]    sc_entry_count
//    [0x1C..0x20]    entry_table_offset
//    [0x20..0x24]    main_ent_data_size
//    [0x24..0x28]    body_offset
//    [0x28..0x30]    body_size
//    [0x30..0x40]    content_id (36 bytes + padding)
//    [0x40..0x44]    drm_type
//    [0x44..0x48]    content_type
//    [0x48..0x4C]    content_flags
//    [...]           remainder of header
//    [entry_table]   PKG entry records
//    [body_offset]   encrypted/compressed payload data
// ──────────────────────────────────────────────────────────────────────

static constexpr uint32_t PKG_MAGIC = 0x544E437F;  // "\x7FCNT" LE
static constexpr uint32_t PKG_MAGIC_BE = 0x7F434E54;

// ── DRM types ──

enum class PKGDrmType : uint32_t {
    None     = 0x0,
    PS4      = 0x1,
    PS5      = 0xF,
    Unknown  = 0xFF
};

// ── Content types ──

enum class PKGContentType : uint32_t {
    GameData       = 0x1A,
    GamePatch      = 0x1B,
    AdditionalContent = 0x1C,
    Unknown        = 0xFF
};

#pragma pack(push, 1)

struct PKGHeader {
    uint32_t magic;
    uint32_t pkg_type;
    uint32_t pkg_revision;
    uint32_t file_count;
    uint32_t entry_count;
    uint16_t sc_entry_count;
    uint16_t entry_count_2;
    uint32_t entry_table_offset;
    uint32_t main_ent_data_size;
    uint64_t body_offset;
    uint64_t body_size;
    uint8_t  content_id[48];
    uint32_t drm_type;
    uint32_t content_type;
    uint32_t content_flags;
    uint32_t promote_size;
    uint32_t version_date;
    uint32_t version_hash;
    uint8_t  padding[0x30];          // align to 0xB0
    uint8_t  digest[32];
    uint8_t  pkg_digest[32];
};

struct PKGEntryRecord {
    uint32_t id;
    uint32_t filename_offset;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t data_offset;
    uint32_t data_size;
    uint64_t padding;
};

#pragma pack(pop)

// ── Parsed types ──

struct PKGEntry {
    uint32_t    id;
    std::string name;
    uint32_t    flags;
    uint64_t    offset;
    uint64_t    size;
    bool        encrypted;
};

struct ParsedPKG {
    PKGHeader             header;
    std::string           content_id;
    PKGDrmType            drm_type;
    PKGContentType        content_type;
    std::vector<PKGEntry> entries;
    std::vector<uint8_t>  raw_data;

    bool valid() const {
        return header.magic == PKG_MAGIC || header.magic == PKG_MAGIC_BE;
    }
};

// ── Known PKG entry IDs ──

static const std::map<uint32_t, std::string> KNOWN_ENTRY_IDS = {
    {0x0001, "digests"},
    {0x0010, "entry_keys"},
    {0x0020, "image_key"},
    {0x0080, "general_digests"},
    {0x0100, "metas"},
    {0x0200, "entry_names"},
    {0x0400, "license.dat"},
    {0x0401, "license.info"},
    {0x0402, "nptitle.dat"},
    {0x0403, "npbind.dat"},
    {0x0404, "selfinfo.dat"},
    {0x0406, "imageinfo.dat"},
    {0x0407, "target-deltainfo.dat"},
    {0x0408, "origin-deltainfo.dat"},
    {0x0409, "psreserved.dat"},
    {0x1000, "param.sfo"},
    {0x1001, "playgo-chunk.dat"},
    {0x1002, "playgo-chunk.sha"},
    {0x1003, "playgo-manifest.xml"},
    {0x1004, "pronunciation.xml"},
    {0x1005, "pronunciation.sig"},
    {0x1006, "pic1.png"},
    {0x1007, "pubtoolinfo.dat"},
    {0x1008, "app/playgo-chunk.dat"},
    {0x1009, "app/playgo-chunk.sha"},
    {0x100A, "app/playgo-manifest.xml"},
    {0x100B, "sharingpic.png"},
    {0x100C, "savedata.png"},
    {0x1200, "icon0.png"},
    {0x1220, "pic0.png"},
    {0x1240, "snd0.at9"},
    {0x1260, "changeinfo/changeinfo.xml"},
    {0x1280, "icon0.dds"},
    {0x12A0, "pic0.dds"},
    {0x12C0, "pic1.dds"},
};

// ── Parse PKG ──

inline ParsedPKG parse_pkg(const std::vector<uint8_t>& data) {
    ParsedPKG pkg{};
    pkg.raw_data = data;

    if (data.size() < sizeof(PKGHeader))
        throw std::runtime_error("PKG: file too small for header");

    std::memcpy(&pkg.header, data.data(), sizeof(PKGHeader));

    // handle big-endian magic
    if (pkg.header.magic != PKG_MAGIC && pkg.header.magic != PKG_MAGIC_BE)
        throw std::runtime_error("PKG: bad magic");

    pkg.content_id = std::string(
        reinterpret_cast<const char*>(pkg.header.content_id),
        strnlen(reinterpret_cast<const char*>(pkg.header.content_id), 48));

    pkg.drm_type     = static_cast<PKGDrmType>(pkg.header.drm_type);
    pkg.content_type = static_cast<PKGContentType>(pkg.header.content_type);

    // read entry table
    uint64_t table_off = pkg.header.entry_table_offset;
    for (uint32_t i = 0; i < pkg.header.entry_count; ++i) {
        uint64_t off = table_off + i * sizeof(PKGEntryRecord);
        if (off + sizeof(PKGEntryRecord) > data.size()) break;

        PKGEntryRecord rec{};
        std::memcpy(&rec, data.data() + off, sizeof(PKGEntryRecord));

        PKGEntry entry;
        entry.id        = rec.id;
        entry.flags     = rec.flags1;
        entry.offset    = rec.data_offset;
        entry.size      = rec.data_size;
        entry.encrypted = (rec.flags2 & 0x80000000) != 0;

        // resolve name from known IDs
        auto it = KNOWN_ENTRY_IDS.find(rec.id);
        entry.name = (it != KNOWN_ENTRY_IDS.end())
                     ? it->second
                     : "entry_" + std::to_string(rec.id);

        pkg.entries.push_back(entry);
    }

    return pkg;
}

// ── Extract PKG entry data ──

inline std::vector<uint8_t> extract_pkg_entry(
    const ParsedPKG& pkg, const PKGEntry& entry)
{
    uint64_t body_base = pkg.header.body_offset;
    uint64_t abs_offset = body_base + entry.offset;

    if (abs_offset + entry.size > pkg.raw_data.size())
        throw std::runtime_error("PKG: entry '" + entry.name +
                                  "' overflows package data");

    return {pkg.raw_data.begin() + abs_offset,
            pkg.raw_data.begin() + abs_offset + entry.size};
}

// ── Find PFS image inside PKG ──

inline const PKGEntry* find_pfs_image(const ParsedPKG& pkg) {
    for (const auto& entry : pkg.entries) {
        if (entry.name.find("pfs_image") != std::string::npos ||
            entry.name.find("imageinfo") != std::string::npos)
            return &entry;
    }
    // fallback: find largest entry (usually the PFS payload)
    const PKGEntry* largest = nullptr;
    for (const auto& entry : pkg.entries) {
        if (!largest || entry.size > largest->size)
            largest = &entry;
    }
    return largest;
}

// ── Dump PKG contents to disk ──

inline void dump_pkg(const ParsedPKG& pkg,
                     const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);

    for (const auto& entry : pkg.entries) {
        if (entry.size == 0) continue;
        try {
            auto blob = extract_pkg_entry(pkg, entry);
            auto path = output_dir / entry.name;
            std::filesystem::create_directories(path.parent_path());

            FILE* fp = fopen(path.string().c_str(), "wb");
            if (!fp) continue;
            fwrite(blob.data(), 1, blob.size(), fp);
            fclose(fp);
        } catch (...) {
            // skip entries we can't extract
        }
    }
}

}  // namespace Decrypt

#endif  // DECRYPT_PKG_HPP
