#include "relinker/ElfSymbolMapper.hpp"
#include <cstring>
#include <iostream>

namespace Relinker {

static inline uint32_t GetRelocSymbol(uint64_t info) { return static_cast<uint32_t>(info >> 32); }
static inline uint32_t GetRelocType(uint64_t info)   { return static_cast<uint32_t>(info & 0xFFFFFFFF); }

bool ElfSymbolResolver::ParseSymbols(
    const uint8_t* elf_data, size_t file_size,
    std::vector<ParsedElfSymbol>& out_symbols,
    std::map<std::string, uint64_t>& out_symbol_map)
{
    if (!elf_data || file_size < sizeof(RawElf64_Ehdr)) return false;

    const auto* ehdr = reinterpret_cast<const RawElf64_Ehdr*>(elf_data);
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        return false;
    }

    if (ehdr->e_shoff + ehdr->e_shnum * sizeof(RawElf64_Shdr) > file_size) return false;
    const auto* shdrs = reinterpret_cast<const RawElf64_Shdr*>(elf_data + ehdr->e_shoff);

    // Locate section string table
    if (ehdr->e_shstrndx >= ehdr->e_shnum) return false;
    const auto& shstr_hdr = shdrs[ehdr->e_shstrndx];
    if (shstr_hdr.sh_offset + shstr_hdr.sh_size > file_size) return false;
    (void)shstr_hdr;

    // Find symbol table section
    const RawElf64_Shdr* symtab_shdr = nullptr;
    const RawElf64_Shdr* strtab_shdr = nullptr;

    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        // SHT_SYMTAB = 2, SHT_DYNSYM = 11
        if (shdrs[i].sh_type == 2 || (!symtab_shdr && shdrs[i].sh_type == 11)) {
            symtab_shdr = &shdrs[i];
            if (symtab_shdr->sh_link < ehdr->e_shnum) {
                strtab_shdr = &shdrs[symtab_shdr->sh_link];
            }
        }
    }

    if (!symtab_shdr || !strtab_shdr) return false;
    if (symtab_shdr->sh_offset + symtab_shdr->sh_size > file_size) return false;
    if (strtab_shdr->sh_offset + strtab_shdr->sh_size > file_size) return false;

    const char* strtab = reinterpret_cast<const char*>(elf_data + strtab_shdr->sh_offset);
    size_t strtab_size = strtab_shdr->sh_size;

    size_t sym_count = symtab_shdr->sh_size / sizeof(RawElf64_Sym);
    const auto* syms = reinterpret_cast<const RawElf64_Sym*>(elf_data + symtab_shdr->sh_offset);

    out_symbols.clear();
    out_symbol_map.clear();
    out_symbols.reserve(sym_count);

    for (size_t i = 0; i < sym_count; ++i) {
        ParsedElfSymbol sym{};
        sym.value         = syms[i].st_value;
        sym.size          = syms[i].st_size;
        sym.type          = syms[i].st_info & 0x0F;
        sym.binding       = syms[i].st_info >> 4;
        sym.section_index = syms[i].st_shndx;

        if (syms[i].st_name < strtab_size) {
            sym.name = std::string(strtab + syms[i].st_name);
        }

        if (!sym.name.empty() && sym.value > 0) {
            out_symbol_map[sym.name] = sym.value;
        }
        out_symbols.push_back(std::move(sym));
    }

    return true;
}

bool ElfSymbolResolver::ParseRelocations(
    const uint8_t* elf_data, size_t file_size,
    std::vector<ParsedElfRelocation>& out_relocations)
{
    if (!elf_data || file_size < sizeof(RawElf64_Ehdr)) return false;

    const auto* ehdr = reinterpret_cast<const RawElf64_Ehdr*>(elf_data);
    if (ehdr->e_shoff + ehdr->e_shnum * sizeof(RawElf64_Shdr) > file_size) return false;
    const auto* shdrs = reinterpret_cast<const RawElf64_Shdr*>(elf_data + ehdr->e_shoff);

    out_relocations.clear();

    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        // SHT_RELA = 4
        if (shdrs[i].sh_type == 4) {
            if (shdrs[i].sh_offset + shdrs[i].sh_size > file_size) continue;
            size_t count = shdrs[i].sh_size / sizeof(RawElf64_Rela);
            const auto* relas = reinterpret_cast<const RawElf64_Rela*>(elf_data + shdrs[i].sh_offset);

            for (size_t r = 0; r < count; ++r) {
                ParsedElfRelocation reloc{};
                reloc.offset       = relas[r].r_offset;
                reloc.symbol_index = GetRelocSymbol(relas[r].r_info);
                reloc.type         = GetRelocType(relas[r].r_info);
                reloc.addend       = relas[r].r_addend;
                out_relocations.push_back(reloc);
            }
        }
    }

    return !out_relocations.empty();
}

size_t ElfSymbolResolver::ApplyRelocations(
    uint8_t* mapped_image, size_t image_size,
    uint64_t load_base,
    const std::vector<ParsedElfRelocation>& relocations,
    const std::vector<ParsedElfSymbol>& symbols)
{
    if (!mapped_image || image_size == 0) return 0;
    size_t applied_count = 0;

    for (const auto& rel : relocations) {
        uint64_t patch_offset = 0;
        if (rel.offset >= load_base && load_base > 0) {
            patch_offset = rel.offset - load_base;
        } else {
            patch_offset = rel.offset;
        }

        uint64_t sym_val = 0;
        if (rel.symbol_index < symbols.size()) {
            sym_val = symbols[rel.symbol_index].value;
        }

        switch (rel.type) {
            case R_X86_64_64:
                if (patch_offset + sizeof(uint64_t) <= image_size) {
                    *reinterpret_cast<uint64_t*>(mapped_image + patch_offset) = sym_val + rel.addend;
                    applied_count++;
                }
                break;

            case R_X86_64_RELATIVE:
                if (patch_offset + sizeof(uint64_t) <= image_size) {
                    *reinterpret_cast<uint64_t*>(mapped_image + patch_offset) = load_base + rel.addend;
                    applied_count++;
                }
                break;

            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                if (patch_offset + sizeof(uint64_t) <= image_size) {
                    *reinterpret_cast<uint64_t*>(mapped_image + patch_offset) = sym_val + rel.addend;
                    applied_count++;
                }
                break;

            case R_X86_64_PC32:
                if (patch_offset + sizeof(uint32_t) <= image_size) {
                    uint64_t final_target_va = sym_val + rel.addend;
                    uint64_t current_pc = load_base + patch_offset;
                    int64_t pc_disp = static_cast<int64_t>(final_target_va - current_pc);
                    *reinterpret_cast<int32_t*>(mapped_image + patch_offset) = static_cast<int32_t>(pc_disp);
                    applied_count++;
                }
                break;

            case R_X86_64_32:
                if (patch_offset + sizeof(uint32_t) <= image_size) {
                    *reinterpret_cast<uint32_t*>(mapped_image + patch_offset) = static_cast<uint32_t>(sym_val + rel.addend);
                    applied_count++;
                }
                break;

            case R_X86_64_32S:
                if (patch_offset + sizeof(int32_t) <= image_size) {
                    *reinterpret_cast<int32_t*>(mapped_image + patch_offset) = static_cast<int32_t>(sym_val + rel.addend);
                    applied_count++;
                }
                break;

            case R_X86_64_DTPMOD64:
            case R_X86_64_DTPOFF64:
            case R_X86_64_TPOFF64:
                if (patch_offset + sizeof(uint64_t) <= image_size) {
                    *reinterpret_cast<uint64_t*>(mapped_image + patch_offset) = sym_val + rel.addend;
                    applied_count++;
                }
                break;

            default:
                break;
        }
    }

    return applied_count;
}

} // namespace Relinker
