#ifndef DECRYPT_SLB2_HPP
#define DECRYPT_SLB2_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  SLB2 — PS5 outer firmware container (wraps PUP payloads)
//
//  Layout:
//    offset 0x00: magic   "SLB2" → 0x32424C53 little-endian
//    offset 0x04: version (uint32)
//    offset 0x08: flags   (uint32)
//    offset 0x0C: entry_count (uint32)
//    offset 0x10: total_blocks (uint32)
//    offset 0x14: reserved[12]
//    offset 0x20: entry_table[entry_count × 0x30]
// ──────────────────────────────────────────────────────────────────────

static constexpr uint32_t SLB2_MAGIC = 0x32424C53;  // 'SLB2' LE

#pragma pack(push, 1)

struct SLB2Header {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t entry_count;
    uint32_t total_blocks;
    uint8_t  reserved[12];
};
static_assert(sizeof(SLB2Header) == 0x20, "SLB2Header must be 32 bytes");

struct SLB2Entry {
    uint32_t block_offset;
    uint32_t file_size;
    uint8_t  reserved[8];
    char     filename[32];
};
static_assert(sizeof(SLB2Entry) == 0x30, "SLB2Entry must be 48 bytes");

#pragma pack(pop)

struct SLB2Component {
    uint32_t    index;
    std::string name;
    uint64_t    offset;
    uint64_t    size;
};

struct SLB2Container {
    SLB2Header                header;
    std::vector<SLB2Component> entries;
    std::vector<uint8_t>       raw_data;

    bool valid() const { return header.magic == SLB2_MAGIC; }
    uint32_t block_size() const { return 512; }
};

// ── Parse ────────────────────────────────────────────────────────────

inline SLB2Container parse_slb2(const std::vector<uint8_t>& data) {
    SLB2Container container{};
    container.raw_data = data;

    if (data.size() < sizeof(SLB2Header))
        throw std::runtime_error("SLB2: file too small for header");

    std::memcpy(&container.header, data.data(), sizeof(SLB2Header));

    if (container.header.magic != SLB2_MAGIC)
        throw std::runtime_error("SLB2: bad magic — not a PUP container");

    const size_t entry_table_offset = sizeof(SLB2Header);
    const size_t entry_table_end    = entry_table_offset +
                                      container.header.entry_count * sizeof(SLB2Entry);

    if (entry_table_end > data.size())
        throw std::runtime_error("SLB2: entry table overflows file");

    for (uint32_t i = 0; i < container.header.entry_count; ++i) {
        SLB2Entry raw{};
        std::memcpy(&raw, data.data() + entry_table_offset + i * sizeof(SLB2Entry),
                     sizeof(SLB2Entry));

        SLB2Component component;
        component.index  = i;
        component.name   = std::string(raw.filename,
                               strnlen(raw.filename, sizeof(raw.filename)));
        component.offset = (uint64_t)raw.block_offset * container.block_size();
        component.size   = raw.file_size;

        // sanity: does the blob actually fit in the file?
        if (component.offset + component.size > data.size()) {
            // truncated entry — keep metadata but flag the size
            component.size = (component.offset < data.size())
                             ? data.size() - component.offset : 0;
        }

        container.entries.push_back(std::move(component));
    }

    return container;
}

// ── Extract a single component blob ──────────────────────────────────

inline std::vector<uint8_t> extract_component(const SLB2Container& container,
                                               const SLB2Component& entry) {
    if (entry.offset + entry.size > container.raw_data.size())
        throw std::runtime_error("SLB2: component '" + entry.name +
                                  "' overflows container data");
    return {container.raw_data.begin() + entry.offset,
            container.raw_data.begin() + entry.offset + entry.size};
}

// ── Dump everything to a directory ───────────────────────────────────

inline void dump_slb2(const SLB2Container& container,
                      const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);
    for (const auto& entry : container.entries) {
        if (entry.offset + entry.size > container.raw_data.size())
            throw std::runtime_error("SLB2: component '" + entry.name +
                                      "' overflows container data");
        auto path = output_dir / entry.name;
        std::filesystem::create_directories(path.parent_path());

        FILE* fp = fopen(path.string().c_str(), "wb");
        if (!fp) throw std::runtime_error("SLB2: cannot write " + path.string());
        fwrite(container.raw_data.data() + entry.offset, 1, entry.size, fp);
        fclose(fp);
    }
}

}  // namespace Decrypt

#endif  // DECRYPT_SLB2_HPP
