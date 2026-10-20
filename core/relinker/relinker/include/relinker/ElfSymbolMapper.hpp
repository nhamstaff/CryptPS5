#ifndef RELINKER_ELF_SYMBOL_MAPPER_HPP
#define RELINKER_ELF_SYMBOL_MAPPER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace Relinker {

// ──────────────────────────────────────────────────────────────────────
//  Unencrypted ELF Symbol & Relocation Mapping Layer
//  Provides safe bounds-checked symbol resolution and relocation patching
//  for development test binaries.
// ──────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)

struct RawElf64_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct RawElf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct RawElf64_Sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct RawElf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

#pragma pack(pop)

// Standard Relocation Types for x86-64
static constexpr uint32_t R_X86_64_NONE      = 0;
static constexpr uint32_t R_X86_64_64        = 1;
static constexpr uint32_t R_X86_64_COPY      = 5;
static constexpr uint32_t R_X86_64_GLOB_DAT  = 6;
static constexpr uint32_t R_X86_64_JUMP_SLOT = 7;
static constexpr uint32_t R_X86_64_RELATIVE  = 8;

struct ParsedElfSymbol {
    std::string name;
    uint64_t    value;
    uint64_t    size;
    uint8_t     type;
    uint8_t     binding;
    uint16_t    section_index;
};

struct ParsedElfRelocation {
    uint64_t offset;
    uint32_t symbol_index;
    uint32_t type;
    int64_t  addend;
};

class ElfSymbolResolver {
public:
    // Safely parse symbol tables (.symtab / .dynsym)
    static bool ParseSymbols(
        const uint8_t* elf_data, size_t file_size,
        std::vector<ParsedElfSymbol>& out_symbols,
        std::map<std::string, uint64_t>& out_symbol_map);

    // Safely parse relocation tables (.rela.dyn / .rela.plt)
    static bool ParseRelocations(
        const uint8_t* elf_data, size_t file_size,
        std::vector<ParsedElfRelocation>& out_relocations);

    // Apply relocations onto mapped memory buffer
    static size_t ApplyRelocations(
        uint8_t* mapped_image, size_t image_size,
        uint64_t load_base,
        const std::vector<ParsedElfRelocation>& relocations,
        const std::vector<ParsedElfSymbol>& symbols);
};

} // namespace Relinker

#endif // RELINKER_ELF_SYMBOL_MAPPER_HPP
