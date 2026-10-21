#ifndef DECRYPT_INNER_PUP_HPP
#define DECRYPT_INNER_PUP_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <decrypt/Crypto.hpp>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  Inner Sony PUP Container (Magic: 0xEEF51454)
//  Present inside SLB2 container (e.g. PS5UPDATE1.PUP)
// ──────────────────────────────────────────────────────────────────────

static constexpr uint32_t INNER_PUP_MAGIC = 0xEEF51454;
static constexpr uint64_t PUP_BLOCK_SIZE  = 512;
static constexpr size_t   AES_BLOCK_SIZE  = 16;

#pragma pack(push, 1)

struct InnerPUPHeader {
    uint32_t magic;           // 0xEEF51454 ('\x54\x14\xF5\xEE')
    uint32_t package_version; // e.g. 0x32010110
    uint32_t flags;           // flags / image version
    uint32_t header_size;     // header size / block count
};
static_assert(sizeof(InnerPUPHeader) == 0x10, "InnerPUPHeader must be 16 bytes");

struct InnerPUPEntry64 {
    uint32_t flags;
    uint32_t entry_id;
    uint64_t block_offset;
    uint64_t file_size;
    uint64_t uncompressed_size;
    uint8_t  digest[32];
};
static_assert(sizeof(InnerPUPEntry64) == 0x40, "InnerPUPEntry64 must be 64 bytes");

struct InnerPUPEntry32 {
    uint32_t flags;
    uint32_t entry_id;
    uint32_t block_offset;
    uint32_t file_size;
    uint8_t  digest[16];
};
static_assert(sizeof(InnerPUPEntry32) == 0x20, "InnerPUPEntry32 must be 32 bytes");

#pragma pack(pop)

enum class PartitionStatus {
    ValidMapped,
    EmptySlot,
    OutOfBounds,
    ZeroLength
};

struct InnerPUPComponent {
    uint32_t        slot_index;
    uint32_t        entry_id;
    std::string     name;
    uint64_t        block_offset;
    uint64_t        byte_offset;
    uint64_t        file_size;
    uint64_t        uncompressed_size;
    uint32_t        flags;
    bool            is_encrypted;
    bool            is_compressed;
    PartitionStatus status;
};

inline std::string get_pup_entry_name(uint32_t id) {
    switch (id) {
        case 0x01: return "firmware_version.bin";
        case 0x02: return "secure3.elf";
        case 0x03: return "kernel.elf";
        case 0x04: return "torus2.elf";
        case 0x05: return "eap_kbl.elf";
        case 0x06: return "cp_kbl.elf";
        case 0x07: return "secure_modules.bin";
        case 0x08: return "system.img";
        case 0x09: return "system_ex.img";
        case 0x0A: return "preinst.img";
        case 0x0B: return "preinst_ex.img";
        case 0x0C: return "recovery.img";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "partition_0x%04X.img", id);
            return std::string(buf);
        }
    }
}

inline const char* get_partition_status_str(PartitionStatus status) {
    switch (status) {
        case PartitionStatus::ValidMapped: return "VALID_MAPPED";
        case PartitionStatus::EmptySlot:   return "EMPTY_SLOT";
        case PartitionStatus::OutOfBounds: return "OUT_OF_BOUNDS";
        case PartitionStatus::ZeroLength:  return "ZERO_LENGTH";
    }
    return "UNKNOWN";
}

// ── Stream partition blob directly to disk (chunked to preserve RAM) ──

inline bool extract_partition_stream(
    const uint8_t* base_data, size_t total_size,
    const InnerPUPComponent& comp,
    const std::filesystem::path& out_path)
{
    if (comp.byte_offset + comp.file_size > total_size) {
        std::cerr << "[-] Error: Boundary overflow for component '" << comp.name
                  << "': offset 0x" << std::hex << comp.byte_offset
                  << " + size 0x" << comp.file_size
                  << " exceeds total image 0x" << total_size << std::dec << "\n";
        return false;
    }

    std::filesystem::create_directories(out_path.parent_path());
    std::ofstream out_file(out_path, std::ios::binary);
    if (!out_file) {
        std::cerr << "[-] Error: Failed to open output file: " << out_path << "\n";
        return false;
    }

    const size_t chunk_size = 4 * 1024 * 1024; // 4 MiB buffer
    const uint8_t* src = base_data + comp.byte_offset;
    size_t remaining = comp.file_size;

    while (remaining > 0) {
        size_t to_write = std::min(remaining, chunk_size);
        out_file.write(reinterpret_cast<const char*>(src), to_write);
        src += to_write;
        remaining -= to_write;
    }

    out_file.close();
    return true;
}

// ── Parse table and extract components with research bounds logging ──

inline std::vector<InnerPUPComponent> parse_pup_table(
    const uint8_t* table_data, size_t table_len, size_t total_pup_size)
{
    std::vector<InnerPUPComponent> all_slots;

    if (!table_data || table_len == 0) {
        std::cerr << "[-] Warning: Null or empty descriptor table buffer passed to parser.\n";
        return all_slots;
    }

    // Detect entry stride (64-byte vs 32-byte layout)
    size_t stride = sizeof(InnerPUPEntry64);
    if (table_len >= sizeof(InnerPUPEntry64)) {
        const auto* e = reinterpret_cast<const InnerPUPEntry64*>(table_data);
        if (e->entry_id > 0 && e->entry_id < 0x1000 &&
            (e->block_offset * PUP_BLOCK_SIZE) < total_pup_size) {
            stride = sizeof(InnerPUPEntry64);
        } else {
            stride = sizeof(InnerPUPEntry32);
        }
    }

    size_t num_entries = table_len / stride;
    size_t active_count = 0;

    std::cout << "[*] Partition Index: Processing " << num_entries
              << " slots using " << stride << "-byte descriptor stride.\n";

    for (size_t i = 0; i < num_entries; ++i) {
        InnerPUPComponent comp{};
        comp.slot_index = static_cast<uint32_t>(i);

        if (stride == sizeof(InnerPUPEntry64)) {
            const auto* raw = reinterpret_cast<const InnerPUPEntry64*>(table_data + i * stride);
            comp.entry_id          = raw->entry_id;
            comp.block_offset      = raw->block_offset;
            comp.byte_offset       = raw->block_offset * PUP_BLOCK_SIZE;
            comp.file_size         = raw->file_size;
            comp.uncompressed_size = raw->uncompressed_size;
            comp.flags             = raw->flags;
        } else {
            const auto* raw = reinterpret_cast<const InnerPUPEntry32*>(table_data + i * stride);
            comp.entry_id          = raw->entry_id;
            comp.block_offset      = raw->block_offset;
            comp.byte_offset       = static_cast<uint64_t>(raw->block_offset) * PUP_BLOCK_SIZE;
            comp.file_size         = raw->file_size;
            comp.uncompressed_size = raw->file_size;
            comp.flags             = raw->flags;
        }

        comp.name          = get_pup_entry_name(comp.entry_id);
        comp.is_encrypted  = (comp.flags & 0x01) != 0;
        comp.is_compressed = (comp.flags & 0x02) != 0;

        if (comp.entry_id == 0 && comp.file_size == 0) {
            comp.status = PartitionStatus::EmptySlot;
        } else if (comp.file_size == 0) {
            comp.status = PartitionStatus::ZeroLength;
        } else if (comp.byte_offset >= total_pup_size || comp.byte_offset + comp.file_size > total_pup_size) {
            comp.status = PartitionStatus::OutOfBounds;
        } else {
            comp.status = PartitionStatus::ValidMapped;
            active_count++;
        }

        all_slots.push_back(comp);
    }

    std::cout << "[*] Partition Index: Mapped " << active_count << " active partitions across "
              << all_slots.size() << " total slots.\n\n";

    return all_slots;
}

inline int process_inner_pup(const uint8_t* data, size_t size,
                            const std::string& out_dir,
                            KeyDatabase& keys) {
    if (size < sizeof(InnerPUPHeader)) {
        std::cerr << "[-] Error: Inner PUP image (0x" << std::hex << size
                  << " bytes) too small for 16-byte header.\n" << std::dec;
        return 1;
    }

    InnerPUPHeader header{};
    std::memcpy(&header, data, sizeof(InnerPUPHeader));

    if (header.magic != INNER_PUP_MAGIC) {
        std::cerr << "[-] Error: Bad inner PUP magic 0x" << std::hex << header.magic
                  << " (expected 0x" << INNER_PUP_MAGIC << ")\n" << std::dec;
        return 1;
    }

    std::cout << "[+] Inner PUP Container Detected:\n"
              << "    Magic:           0x" << std::hex << header.magic << std::dec << " (Sony PUP)\n"
              << "    Package Version: 0x" << std::hex << header.package_version << std::dec << "\n"
              << "    Flags:           0x" << std::hex << header.flags << std::dec << "\n"
              << "    Header Size:     0x" << std::hex << header.header_size << std::dec << " bytes\n\n";

    // Entropy calculation over initial table block
    size_t sample_len = std::min<size_t>(1024, size - 0x10);
    double table_entropy = 0.0;
    if (sample_len > 0) {
        std::array<size_t, 256> counts{};
        for (size_t i = 0; i < sample_len; ++i) counts[data[0x10 + i]]++;
        for (auto c : counts) {
            if (c > 0) {
                double p = (double)c / sample_len;
                table_entropy -= p * std::log2(p);
            }
        }
    }

    bool is_table_encrypted = (table_entropy > 7.5);
    std::cout << "[*] Partition table entropy: " << std::fixed << std::setprecision(2)
              << table_entropy << " bits/byte "
              << (is_table_encrypted ? "[ENCRYPTED]" : "[PLAINTEXT]") << "\n";

    std::vector<uint8_t> table_bytes;
    size_t table_len = std::min<size_t>(header.header_size ? header.header_size : 0x10000, size - 0x10);

    if (is_table_encrypted) {
        // Dynamic AES-CBC input descriptor validation
        if (table_len < AES_BLOCK_SIZE) {
            std::cerr << "[-] Error: Encrypted descriptor table length (0x" << std::hex << table_len
                      << ") is less than AES block size (16 bytes).\n" << std::dec;
            return 1;
        }

        size_t alignment_remainder = table_len % AES_BLOCK_SIZE;
        size_t enc_table_len = table_len - alignment_remainder;
        if (alignment_remainder != 0) {
            std::cout << "[!] AES-CBC Alignment: Table length " << table_len
                      << " is unaligned (" << alignment_remainder << " bytes remainder). Truncating to "
                      << enc_table_len << " bytes for block cipher safety.\n";
        }

        // Look for valid PUP key
        const KeyEntry* pup_key = nullptr;
        keys.for_each([&](const KeyEntry& k) {
            if (k.type == KeyType::PUP_KEY) {
                bool non_zero = false;
                for (auto b : k.key) { if (b != 0) { non_zero = true; break; } }
                if (non_zero) pup_key = &k;
            }
        });

        if (!pup_key) {
            std::cout << "\n[!] RETAIL PUP IS ENCRYPTED:\n"
                      << "    The partition table is locked with Sony's retail PUP key.\n"
                      << "    To extract 'system.img' (holding all 3,600+ retail PRXs):\n"
                      << "    1. Create a keys.txt file\n"
                      << "    2. Add: <key_id> PUP_KEY pup_retail_key 1.00+ <32_hex_key> <32_hex_iv>\n"
                      << "    3. Run: ps5decrypt.exe --pup extracted_pup\\PS5UPDATE1.PUP --keys keys.txt --output " << out_dir << "\n\n";
            return 0;
        }

        std::cout << "[+] Found PUP decryption key: " << pup_key->name << "\n";
        std::cout << "[*] Decrypting descriptor table using AES-128-CBC (" << enc_table_len << " bytes)...\n";

        try {
            table_bytes = aes128_cbc_decrypt(data + 0x10, enc_table_len, pup_key->key, pup_key->iv);
            std::cout << "[+] Descriptor table decrypted successfully.\n";
        } catch (const std::exception& ex) {
            std::cerr << "[-] Error during AES-128-CBC table decryption: " << ex.what() << "\n";
            return 1;
        }
    } else {
        table_bytes.assign(data + 0x10, data + 0x10 + table_len);
    }

    std::filesystem::create_directories(out_dir);

    // Save table binary for research analysis
    std::string table_path = out_dir + "/partition_table.bin";
    std::ofstream tbl(table_path, std::ios::binary);
    if (tbl) tbl.write(reinterpret_cast<const char*>(table_bytes.data()), table_bytes.size());
    std::cout << "[*] Partition table saved to: " << table_path << "\n";

    // Parse component entries from table
    auto all_slots = parse_pup_table(table_bytes.data(), table_bytes.size(), size);

    std::cout << "    ┌──────┬──────┬───────────────────────────────┬────────────────┬────────────────┬──────────────┐\n"
              << "    │ Slot │ ID   │ Component Name                │ Offset         │ Size (bytes)   │ Status       │\n"
              << "    ├──────┼──────┼───────────────────────────────┼────────────────┼────────────────┼──────────────┤\n";

    std::vector<InnerPUPComponent> active_partitions;
    for (const auto& slot : all_slots) {
        if (slot.status == PartitionStatus::ValidMapped) {
            active_partitions.push_back(slot);
        }
        // Log all mapped or named entries for research analysis
        if (slot.status != PartitionStatus::EmptySlot || slot.slot_index < 16) {
            printf("    │ %4u │ 0x%02X │ %-29s │ 0x%012llX │ %14llu │ %-12s │\n",
                   slot.slot_index, slot.entry_id, slot.name.c_str(),
                   (unsigned long long)slot.byte_offset,
                   (unsigned long long)slot.file_size,
                   get_partition_status_str(slot.status));
        }
    }
    std::cout << "    └──────┴──────┴───────────────────────────────┴────────────────┴────────────────┴──────────────┘\n\n";

    if (active_partitions.empty()) {
        std::cout << "[*] Partition indexing complete. No active partitions available for extraction.\n";
        return 0;
    }

    // Extract partitions
    for (const auto& comp : active_partitions) {
        std::filesystem::path comp_path = std::filesystem::path(out_dir) / comp.name;
        std::cout << "[*] Extracting: " << comp.name << " (" << (comp.file_size / (1024 * 1024)) << " MiB)...\n";
        if (extract_partition_stream(data, size, comp, comp_path)) {
            std::cout << "    [+] Extracted to: " << comp_path.string() << "\n";
        }
    }

    std::cout << "\n[+] Extraction sequence completed successfully.\n";
    return 0;
}

}  // namespace Decrypt

#endif  // DECRYPT_INNER_PUP_HPP
