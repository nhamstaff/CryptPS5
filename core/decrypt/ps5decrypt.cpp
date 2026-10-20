// ──────────────────────────────────────────────────────────────────────
//  ps5decrypt — PS5 System Software & Game Package Decryptor
//
//  Unified tool for the AnyPS5 relinker pipeline.
//  Handles the complete extraction chain:
//    PUP → SLB2 → SELF segments → ELF unwrap
//    PKG → PFS image → PRX modules → ELF unwrap
//
//  Usage:
//    ps5decrypt --pup  <PS5UPDATE.PUP>  --output <dir>
//    ps5decrypt --pkg  <game.pkg>       --output <dir>
//    ps5decrypt --self <eboot.bin>      --output <dir>
//    ps5decrypt --scan <file>                           (identify format)
//    ps5decrypt --keys <keyfile>                        (load external keys)
//
//  Build:
//    g++ -std=c++20 -O2 -o ps5decrypt.exe ps5decrypt.cpp -I../include
//
//  No external dependencies — crypto is built from scratch.
// ──────────────────────────────────────────────────────────────────────

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <iomanip>

#include <decrypt/SLB2.hpp>
#include <decrypt/InnerPUP.hpp>
#include <decrypt/Self.hpp>
#include <decrypt/Crypto.hpp>
#include <decrypt/PFS.hpp>
#include <decrypt/PKG.hpp>

namespace fs = std::filesystem;
using namespace Decrypt;

// ─── Utilities ───────────────────────────────────────────────────────

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open: " + path);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

static void write_file(const std::string& path,
                        const std::vector<uint8_t>& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write: " + path);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

static std::string format_size(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < 4) { size /= 1024.0; unit++; }
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit]);
    return buf;
}

static std::string hex_string(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(len * 2);
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 0x0F];
    }
    return result;
}

// ─── Format detection ────────────────────────────────────────────────

enum class FileFormat {
    Unknown,
    SLB2_PUP,
    INNER_PUP,
    SELF,
    PKG,
    ELF,
    PFS
};

static FileFormat detect_format(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return FileFormat::Unknown;

    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), 4);

    if (magic == SLB2_MAGIC)        return FileFormat::SLB2_PUP;
    if (magic == INNER_PUP_MAGIC)   return FileFormat::INNER_PUP;
    if (magic == SELF_MAGIC)        return FileFormat::SELF;
    if (magic == PKG_MAGIC)         return FileFormat::PKG;
    if (magic == PKG_MAGIC_BE)      return FileFormat::PKG;

    // check ELF magic
    if (data.size() >= 4 && data[0] == 0x7F &&
        data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
        return FileFormat::ELF;

    // check PFS magic (8 bytes)
    if (data.size() >= 8) {
        uint64_t pfs_magic = 0;
        std::memcpy(&pfs_magic, data.data(), 8);
        if (pfs_magic == PFS_MAGIC) return FileFormat::PFS;
    }

    return FileFormat::Unknown;
}

static const char* format_name(FileFormat fmt) {
    switch (fmt) {
        case FileFormat::SLB2_PUP:  return "SLB2 (PUP firmware container)";
        case FileFormat::INNER_PUP: return "PUP (Sony inner firmware container)";
        case FileFormat::SELF:      return "SELF (Sony signed ELF)";
        case FileFormat::PKG:       return "PKG (game package)";
        case FileFormat::ELF:       return "ELF (standard executable)";
        case FileFormat::PFS:       return "PFS (PlayStation filesystem)";
        default:                    return "Unknown";
    }
}

// ─── Banner ──────────────────────────────────────────────────────────

static void print_banner() {
    std::cout << R"(
  ╔═══════════════════════════════════════════════════════════╗
  ║                                                           ║
  ║   ██████╗ ███████╗███████╗    ██████╗ ██████╗ ██╗   ██╗  ║
  ║   ██╔══██╗██╔════╝██╔════╝    ██╔══██╗██╔══██╗╚██╗ ██╔╝  ║
  ║   ██████╔╝███████╗███████╗    ██║  ██║██████╔╝ ╚████╔╝   ║
  ║   ██╔═══╝ ╚════██║╚════██║    ██║  ██║██╔══██╗  ╚██╔╝    ║
  ║   ██║     ███████║███████║    ██████╔╝██║  ██║   ██║     ║
  ║   ╚═╝     ╚══════╝╚══════╝    ╚═════╝ ╚═╝  ╚═╝   ╚═╝     ║
  ║                                                           ║
  ║   PS5 System Software Decryptor — AnyPS5 Pipeline         ║
  ║   SLB2 · SELF · PKG · PFS · AES-128-CBC · SHA-256         ║
  ║                                                           ║
  ╚═══════════════════════════════════════════════════════════╝
)" << std::endl;
}

// ─── PUP processing ──────────────────────────────────────────────────

static int process_pup(const std::string& input, const std::string& output,
                        KeyDatabase& keys) {
    std::cout << "[*] Loading PUP: " << input << "\n";
    auto data = read_file(input);
    std::cout << "[*] File size: " << format_size(data.size()) << "\n\n";

    auto container = parse_slb2(data);
    std::cout << "[+] SLB2 container parsed:\n"
              << "    Version:     " << container.header.version << "\n"
              << "    Flags:       0x" << std::hex << container.header.flags << std::dec << "\n"
              << "    Entries:     " << container.header.entry_count << "\n"
              << "    Block size:  " << container.block_size() << " bytes\n\n";

    std::cout << "[*] Component table:\n"
              << "    ┌──────┬─────────────────────────────────┬──────────────┬──────────────┐\n"
              << "    │ Idx  │ Name                            │ Offset       │ Size         │\n"
              << "    ├──────┼─────────────────────────────────┼──────────────┼──────────────┤\n";

    for (const auto& entry : container.entries) {
        printf("    │ %4u │ %-31s │ 0x%010llX │ %12s │\n",
               entry.index, entry.name.c_str(),
               (unsigned long long)entry.offset,
               format_size(entry.size).c_str());
    }
    std::cout << "    └──────┴─────────────────────────────────┴──────────────┴──────────────┘\n\n";

    // extract all components
    std::string out_dir = output.empty() ? "pup_extracted" : output;
    std::cout << "[*] Extracting to: " << out_dir << "\n";
    dump_slb2(container, out_dir);
    std::cout << "[+] Extracted " << container.entries.size() << " components.\n\n";

    // scan extracted components for SELF containers
    int self_count = 0;
    for (const auto& entry : container.entries) {
        auto blob = extract_component(container, entry);
        auto fmt = detect_format(blob);

        if (fmt == FileFormat::INNER_PUP) {
            std::cout << "[*] Inner Sony PUP detected: " << entry.name << "\n";
            process_inner_pup(blob.data(), blob.size(), out_dir, keys);
        } else if (fmt == FileFormat::SELF) {
            self_count++;
            std::cout << "[*] SELF detected: " << entry.name << "\n";

            try {
                auto self = parse_self(blob);
                std::cout << "    Type:       " << self_type_name(self.type) << "\n"
                          << "    Auth ID:    0x" << std::hex << self.extended.auth_id << std::dec << "\n"
                          << "    ELF offset: 0x" << std::hex << self.inner_elf_offset << std::dec << "\n"
                          << "    Segments:   " << self.segments.size() << "\n"
                          << "    PHDRs:      " << self.program_headers.size() << "\n";

                auto report = check_encryption(self);
                if (report.decrypted) {
                    std::cout << "    Encryption: [DECRYPTED] ✓ — can extract directly\n";

                    // extract inner ELF
                    std::string elf_path = out_dir + "/elfs/" + entry.name + ".elf";
                    // use unwrap logic from inner ELF offset
                    if (self.inner_elf_offset + sizeof(Elf64Header) < blob.size()) {
                        // write the raw inner ELF portion
                        std::vector<uint8_t> inner_elf(
                            blob.begin() + self.inner_elf_offset, blob.end());
                        write_file(elf_path, inner_elf);
                        std::cout << "    Extracted:  " << elf_path << " ("
                                  << format_size(inner_elf.size()) << ")\n";
                    }
                } else if (report.partially_encrypted) {
                    std::cout << "    Encryption: [PARTIAL] — some segments encrypted\n";
                    for (const auto& seg : report.segments) {
                        std::cout << "      seg[" << seg.index << "] entropy="
                                  << std::fixed << std::setprecision(2) << seg.entropy
                                  << (seg.looks_encrypted ? " [ENCRYPTED]" : " [CLEAR]")
                                  << "\n";
                    }

                    // try to decrypt with known keys
                    for (const auto& seg : report.segments) {
                        if (!seg.looks_encrypted) continue;
                        bool decrypted = false;
                        keys.for_each([&](const KeyEntry& key_entry) {
                            if (decrypted) return;
                            if (key_entry.type != KeyType::SEGMENT_KEY &&
                                key_entry.type != KeyType::FAKE_SELF_KEY) return;

                            auto& real_seg = self.segments[seg.index];
                            if (real_seg.data_offset + real_seg.compressed_size > blob.size()) return;

                            auto result = decrypt_segment(
                                blob.data() + real_seg.data_offset,
                                static_cast<size_t>(real_seg.compressed_size),
                                key_entry.key, key_entry.iv, seg.index);

                            if (result.success && result.entropy_after < 7.0) {
                                std::cout << "      → seg[" << seg.index
                                          << "] decrypted with key '" << key_entry.name
                                          << "' (entropy: " << result.entropy_before
                                          << " → " << result.entropy_after << ")\n";
                                decrypted = true;
                            }
                        });

                        if (!decrypted) {
                            // try derived keys
                            keys.for_each([&](const KeyEntry& key_entry) {
                                if (decrypted) return;
                                if (key_entry.type != KeyType::BOOTROM_L0 &&
                                    key_entry.type != KeyType::PUP_KEY) return;

                                auto derived_key = derive_segment_key(
                                    key_entry.key, self.header.key_type, seg.index);
                                auto derived_iv = derive_segment_iv(
                                    key_entry.key, seg.index);

                                auto& real_seg = self.segments[seg.index];
                                if (real_seg.data_offset + real_seg.compressed_size > blob.size()) return;

                                auto result = decrypt_segment(
                                    blob.data() + real_seg.data_offset,
                                    static_cast<size_t>(real_seg.compressed_size),
                                    derived_key, derived_iv, seg.index);

                                if (result.success && result.entropy_after < 7.0) {
                                    std::cout << "      → seg[" << seg.index
                                              << "] decrypted via PBKDF2 from '"
                                              << key_entry.name << "'\n";
                                    decrypted = true;
                                }
                            });
                        }

                        if (!decrypted) {
                            std::cout << "      → seg[" << seg.index
                                      << "] LOCKED — no matching key found\n";
                        }
                    }
                } else {
                    std::cout << "    Encryption: [FULLY ENCRYPTED] — need keys\n";
                    for (const auto& seg : report.segments) {
                        std::cout << "      seg[" << seg.index << "] entropy="
                                  << std::fixed << std::setprecision(2) << seg.entropy
                                  << " [ENCRYPTED]\n";
                    }
                }

            } catch (const std::exception& e) {
                std::cout << "    Parse error: " << e.what() << "\n";
            }
            std::cout << "\n";
        }
    }

    std::cout << "[+] PUP processing complete. Found " << self_count
              << " SELF containers.\n";
    return 0;
}

// ─── PKG processing ──────────────────────────────────────────────────

static int process_pkg(const std::string& input, const std::string& output,
                        KeyDatabase& keys) {
    (void)keys;
    std::cout << "[*] Loading PKG: " << input << "\n";
    auto data = read_file(input);
    std::cout << "[*] File size: " << format_size(data.size()) << "\n\n";

    auto pkg = parse_pkg(data);
    std::cout << "[+] PKG parsed:\n"
              << "    Content ID:   " << pkg.content_id << "\n"
              << "    DRM type:     0x" << std::hex << (uint32_t)pkg.drm_type << std::dec << "\n"
              << "    Content type: 0x" << std::hex << (uint32_t)pkg.content_type << std::dec << "\n"
              << "    Entries:      " << pkg.entries.size() << "\n"
              << "    Body offset:  0x" << std::hex << pkg.header.body_offset << std::dec << "\n"
              << "    Body size:    " << format_size(pkg.header.body_size) << "\n\n";

    std::cout << "[*] Entry table:\n"
              << "    ┌──────┬──────────────────────────────┬──────────────┬───────────┐\n"
              << "    │ ID   │ Name                         │ Size         │ Encrypted │\n"
              << "    ├──────┼──────────────────────────────┼──────────────┼───────────┤\n";

    for (const auto& entry : pkg.entries) {
        printf("    │ %04X │ %-28s │ %12s │ %-9s │\n",
               entry.id, entry.name.c_str(),
               format_size(entry.size).c_str(),
               entry.encrypted ? "YES" : "no");
    }
    std::cout << "    └──────┴──────────────────────────────┴──────────────┴───────────┘\n\n";

    // extract
    std::string out_dir = output.empty() ? "pkg_extracted" : output;
    std::cout << "[*] Extracting to: " << out_dir << "\n";
    dump_pkg(pkg, out_dir);
    std::cout << "[+] Extracted " << pkg.entries.size() << " entries.\n\n";

    // look for PFS image
    auto pfs_entry = find_pfs_image(pkg);
    if (pfs_entry && pfs_entry->size > 0) {
        std::cout << "[*] PFS image candidate: " << pfs_entry->name
                  << " (" << format_size(pfs_entry->size) << ")\n";

        if (pfs_entry->encrypted) {
            std::cout << "    [!] PFS image is encrypted — need content key\n";
        } else {
            try {
                auto pfs_data = extract_pkg_entry(pkg, *pfs_entry);
                auto pfs = parse_pfs(pfs_data);
                std::cout << "[+] PFS filesystem parsed:\n"
                          << "    Block size:   " << pfs.block_size << "\n"
                          << "    Block count:  " << pfs.superblock.block_count << "\n"
                          << "    Files:        " << pfs.files.size() << "\n\n";

                // list PRX modules
                auto prx_files = find_prx_modules(pfs);
                if (!prx_files.empty()) {
                    std::cout << "[*] System modules found:\n";
                    for (const auto* f : prx_files) {
                        std::cout << "    " << f->path << " ("
                                  << format_size(f->size) << ")\n";
                    }
                }

                // dump PFS
                std::string pfs_dir = out_dir + "/pfs";
                dump_pfs(pfs, pfs_dir);
                std::cout << "\n[+] PFS dumped to: " << pfs_dir << "\n";

            } catch (const std::exception& e) {
                std::cout << "    PFS parse error: " << e.what() << "\n";
            }
        }
    }

    return 0;
}

// ─── SELF processing ─────────────────────────────────────────────────

static int process_self(const std::string& input, const std::string& output,
                         KeyDatabase& keys) {
    std::cout << "[*] Loading SELF: " << input << "\n";
    auto data = read_file(input);
    std::cout << "[*] File size: " << format_size(data.size()) << "\n\n";

    auto self = parse_self(data);
    std::cout << "[+] SELF parsed:\n"
              << "    Magic:      0x" << std::hex << self.header.magic << std::dec << "\n"
              << "    Version:    " << (int)self.header.version << "\n"
              << "    Mode:       " << (int)self.header.mode << "\n"
              << "    Key type:   0x" << std::hex << self.header.key_type << std::dec << "\n"
              << "    Type:       " << self_type_name(self.type) << "\n"
              << "    Auth ID:    0x" << std::hex << self.extended.auth_id << std::dec << "\n"
              << "    Vendor ID:  0x" << std::hex << self.extended.vendor_id << std::dec << "\n"
              << "    Segments:   " << self.segments.size() << "\n"
              << "    ELF offset: 0x" << std::hex << self.inner_elf_offset << std::dec << "\n"
              << "    ELF entry:  0x" << std::hex << self.elf_header.entry << std::dec << "\n"
              << "    ELF PHDRs:  " << self.elf_header.phnum << "\n\n";

    // segment details
    std::cout << "[*] Segment table:\n"
              << "    ┌──────┬────────────┬────────────┬────────────┬───────────┬────────┐\n"
              << "    │ Idx  │ Offset     │ Comp Size  │ Decomp Sz  │ Encrypted │ Flags  │\n"
              << "    ├──────┼────────────┼────────────┼────────────┼───────────┼────────┤\n";

    for (const auto& seg : self.segments) {
        printf("    │ %4u │ 0x%08llX │ %10s │ %10s │ %-9s │ 0x%04llX │\n",
               seg.index,
               (unsigned long long)seg.data_offset,
               format_size(seg.compressed_size).c_str(),
               format_size(seg.decompressed_size).c_str(),
               seg.encrypted ? "YES" : "no",
               (unsigned long long)seg.flags);
    }
    std::cout << "    └──────┴────────────┴────────────┴────────────┴───────────┴────────┘\n\n";

    // ELF program headers
    std::cout << "[*] ELF program headers:\n";
    for (size_t i = 0; i < self.program_headers.size(); ++i) {
        const auto& ph = self.program_headers[i];
        const char* type_name = "UNKNOWN";
        switch (ph.type) {
            case 1: type_name = "PT_LOAD"; break;
            case 2: type_name = "PT_DYNAMIC"; break;
            case 4: type_name = "PT_NOTE"; break;
            case 6: type_name = "PT_PHDR"; break;
            case 7: type_name = "PT_TLS"; break;
            case 0x6474E550: type_name = "GNU_EH_FRAME"; break;
            case 0x6474E551: type_name = "GNU_STACK"; break;
            case 0x6474E552: type_name = "GNU_RELRO"; break;
            case 0x61000000: type_name = "SCE_DYNLIBDATA"; break;
            case 0x61000001: type_name = "SCE_PROCPARAM"; break;
            case 0x6FFFFF00: type_name = "SCE_COMMENT"; break;
            case 0x6FFFFF01: type_name = "SCE_VERSION"; break;
        }
        printf("    [%2zu] %-16s  vaddr=0x%010llX  filesz=0x%08llX  memsz=0x%08llX\n",
               i, type_name,
               (unsigned long long)ph.vaddr,
               (unsigned long long)ph.filesz,
               (unsigned long long)ph.memsz);
    }
    std::cout << "\n";

    // encryption analysis
    auto report = check_encryption(self);
    if (report.decrypted) {
        std::cout << "[+] All segments are DECRYPTED — extracting inner ELF\n";

        std::string out_path = output.empty()
            ? fs::path(input).stem().string() + ".elf" : output;

        std::vector<uint8_t> inner_elf(
            data.begin() + self.inner_elf_offset, data.end());
        write_file(out_path, inner_elf);
        std::cout << "[+] Wrote: " << out_path << " ("
                  << format_size(inner_elf.size()) << ")\n";
    } else {
        std::cout << "[!] Encrypted segments detected — attempting decryption\n";

        int decrypted_count = 0;
        for (const auto& status : report.segments) {
            if (!status.looks_encrypted) {
                decrypted_count++;
                continue;
            }

            bool found = false;
            keys.for_each([&](const KeyEntry& key_entry) {
                if (found) return;

                auto& seg = self.segments[status.index];
                if (seg.data_offset + seg.compressed_size > data.size()) return;

                // try direct key
                auto result = decrypt_segment(
                    data.data() + seg.data_offset,
                    static_cast<size_t>(seg.compressed_size),
                    key_entry.key, key_entry.iv, status.index);

                if (result.success && result.entropy_after < 7.0) {
                    std::cout << "    seg[" << status.index << "] ✓ decrypted with '"
                              << key_entry.name << "'\n";
                    found = true;
                    decrypted_count++;
                    return;
                }

                // try derived key
                auto dk = derive_segment_key(key_entry.key,
                                              self.header.key_type, status.index);
                auto div = derive_segment_iv(key_entry.key, status.index);

                result = decrypt_segment(
                    data.data() + seg.data_offset,
                    static_cast<size_t>(seg.compressed_size),
                    dk, div, status.index);

                if (result.success && result.entropy_after < 7.0) {
                    std::cout << "    seg[" << status.index
                              << "] ✓ decrypted via PBKDF2 from '"
                              << key_entry.name << "'\n";
                    found = true;
                    decrypted_count++;
                }
            });

            if (!found) {
                std::cout << "    seg[" << status.index
                          << "] ✗ LOCKED (entropy=" << std::fixed
                          << std::setprecision(2) << status.entropy << ")\n";
            }
        }

        std::cout << "\n[*] Decryption: " << decrypted_count << "/"
                  << report.segments.size() << " segments\n";
    }

    return 0;
}

// ─── Scan (identify format) ──────────────────────────────────────────

static int scan_file(const std::string& input) {
    auto data = read_file(input);
    auto fmt = detect_format(data);

    std::cout << "[*] File: " << input << "\n"
              << "    Size: " << format_size(data.size()) << "\n"
              << "    Format: " << format_name(fmt) << "\n";

    // quick entropy scan
    size_t sample = std::min(data.size(), size_t(0x10000));
    double ent = byte_entropy(data.data(), sample);
    std::cout << "    Entropy: " << std::fixed << std::setprecision(2) << ent
              << " bits/byte"
              << (ent >= 7.5 ? " [likely encrypted]" : " [likely plaintext]")
              << "\n";

    // magic bytes
    std::cout << "    Magic: " << hex_string(data.data(), std::min(data.size(), size_t(16)))
              << "\n";

    if (fmt == FileFormat::SELF) {
        auto self = parse_self(data);
        std::cout << "    SELF type: " << self_type_name(self.type) << "\n"
                  << "    Segments: " << self.segments.size() << "\n"
                  << "    fSELF: " << (self.is_fake_self() ? "yes" : "no") << "\n";
    } else if (fmt == FileFormat::SLB2_PUP) {
        auto container = parse_slb2(data);
        std::cout << "    SLB2 entries: " << container.entries.size() << "\n";
    } else if (fmt == FileFormat::PKG) {
        auto pkg = parse_pkg(data);
        std::cout << "    Content ID: " << pkg.content_id << "\n"
                  << "    Entries: " << pkg.entries.size() << "\n";
    }

    return 0;
}

// ─── Usage ───────────────────────────────────────────────────────────

static void print_usage(const char* argv0) {
    std::cout << "Usage:\n"
              << "  " << argv0 << " --pup  <PS5UPDATE.PUP>  [--output <dir>] [--keys <file>]\n"
              << "  " << argv0 << " --pkg  <game.pkg>       [--output <dir>] [--keys <file>]\n"
              << "  " << argv0 << " --self <eboot.bin>      [--output <dir>] [--keys <file>]\n"
              << "  " << argv0 << " --scan <file>\n"
              << "  " << argv0 << " --auto <file>           (auto-detect and process)\n"
              << "\nOptions:\n"
              << "  --output <dir>   Output directory or file path\n"
              << "  --keys <file>    External key database file\n"
              << "  --verbose        Extra diagnostic output\n"
              << "  --help           This message\n";
}

// ─── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    print_banner();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode;
    std::string input;
    std::string output;
    std::string keyfile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--pup" || arg == "--pkg" || arg == "--self" ||
            arg == "--scan" || arg == "--auto") {
            mode = arg;
            if (i + 1 < argc) input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--keys" && i + 1 < argc) {
            keyfile = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (mode.empty() || input.empty()) {
        std::cerr << "[!] Missing mode or input file.\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // load key database
    KeyDatabase keys;
    keys.load_known_keys();
    std::cout << "[*] Loaded " << keys.key_count() << " known keys\n";

    if (!keyfile.empty()) {
        if (keys.load_from_file(keyfile)) {
            std::cout << "[+] Loaded external keys from: " << keyfile << "\n";
        } else {
            std::cout << "[!] Could not load key file: " << keyfile << "\n";
        }
    }
    std::cout << "\n";

    auto start = std::chrono::high_resolution_clock::now();

    int result = 0;
    try {
        if (mode == "--pup") {
            auto data = read_file(input);
            auto fmt = detect_format(data);
            if (fmt == FileFormat::INNER_PUP) {
                std::string out_dir = output.empty() ? "pup_extracted" : output;
                result = process_inner_pup(data.data(), data.size(), out_dir, keys);
            } else {
                result = process_pup(input, output, keys);
            }
        } else if (mode == "--pkg") {
            result = process_pkg(input, output, keys);
        } else if (mode == "--self") {
            result = process_self(input, output, keys);
        } else if (mode == "--scan") {
            result = scan_file(input);
        } else if (mode == "--auto") {
            auto data = read_file(input);
            auto fmt = detect_format(data);
            std::cout << "[*] Auto-detected: " << format_name(fmt) << "\n\n";
            switch (fmt) {
                case FileFormat::SLB2_PUP:  result = process_pup(input, output, keys); break;
                case FileFormat::INNER_PUP: result = process_inner_pup(data.data(), data.size(), output.empty() ? "pup_extracted" : output, keys); break;
                case FileFormat::PKG:       result = process_pkg(input, output, keys); break;
                case FileFormat::SELF:      result = process_self(input, output, keys); break;
                default:
                    std::cerr << "[!] Cannot auto-process format: "
                              << format_name(fmt) << "\n";
                    result = 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[FATAL] " << e.what() << "\n";
        result = 2;
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cout << "\n[*] Done in " << ms << " ms.\n";

    return result;
}
