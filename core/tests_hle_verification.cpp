#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

#include "prx/libkernel/DirectMemory/VirtualMemorySim.hpp"
#include "relinker/ElfSymbolMapper.hpp"

int main() {
    std::cout << "[*] Starting HLE Environment Verification Suite...\n\n";

    // ── Test 1: Virtual Memory Management Simulation ──
    std::cout << "[1] Testing VirtualMemorySimulator...\n";
    auto& vmem = LibkernelSim::VirtualMemorySimulator::Instance();

    void* mapped_ptr = nullptr;
    int alloc_res = vmem.AllocateVirtual(&mapped_ptr, 64 * 1024,
                                         LibkernelSim::PROT_READ_SIM | LibkernelSim::PROT_WRITE_SIM,
                                         LibkernelSim::MAP_ANON_SIM,
                                         LibkernelSim::PAGE_SIZE_16K,
                                         "game_primary_heap");
    if (alloc_res != 0 || !mapped_ptr) {
        std::cerr << "[-] Virtual memory allocation failed!\n";
        return 1;
    }
    std::cout << "    [+] Allocated 64 KiB at: " << mapped_ptr << "\n";

    // Write and read test
    std::memset(mapped_ptr, 0xCC, 64 * 1024);
    assert(static_cast<uint8_t*>(mapped_ptr)[0] == 0xCC);

    // Query virtual page state
    LibkernelSim::VirtualPageBlock block{};
    bool query_res = vmem.QueryVirtual(mapped_ptr, &block);
    if (!query_res || block.allocated_bytes < 64 * 1024) {
        std::cerr << "[-] Virtual memory query failed!\n";
        return 2;
    }
    std::cout << "    [+] Query verified: tag='" << block.tag_name
              << "', bytes=" << block.allocated_bytes << "\n";

    // Protect virtual pages
    int prot_res = vmem.ProtectVirtual(mapped_ptr, 64 * 1024, LibkernelSim::PROT_READ_SIM);
    if (prot_res != 0) {
        std::cerr << "[-] Virtual memory protect failed!\n";
        return 3;
    }
    std::cout << "    [+] Protection updated to PROT_READ\n";

    // Free virtual pages
    int free_res = vmem.FreeVirtual(mapped_ptr, 64 * 1024);
    if (free_res != 0) {
        std::cerr << "[-] Virtual memory free failed!\n";
        return 4;
    }
    std::cout << "    [+] Virtual memory cleanly released.\n\n";

    // ── Test 2: ELF Symbol & Relocation Mapping ──
    std::cout << "[2] Testing ElfSymbolResolver...\n";

    // Synthesize a minimal ELF64 binary with .shstrtab, .symtab, .strtab, .rela.dyn
    std::vector<uint8_t> dummy_elf(4096, 0);
    auto* ehdr = reinterpret_cast<Relinker::RawElf64_Ehdr*>(dummy_elf.data());
    ehdr->e_ident[0] = 0x7F;
    ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';
    ehdr->e_ident[3] = 'F';
    ehdr->e_shoff = 128;
    ehdr->e_shentsize = sizeof(Relinker::RawElf64_Shdr);
    ehdr->e_shnum = 4;
    ehdr->e_shstrndx = 1;

    auto* shdrs = reinterpret_cast<Relinker::RawElf64_Shdr*>(dummy_elf.data() + ehdr->e_shoff);

    // Section 1: .shstrtab
    shdrs[1].sh_type = 3; // SHT_STRTAB
    shdrs[1].sh_offset = 512;
    shdrs[1].sh_size = 64;

    // Section 2: .strtab
    shdrs[2].sh_type = 3; // SHT_STRTAB
    shdrs[2].sh_offset = 600;
    shdrs[2].sh_size = 128;
    const char sample_str[] = "\0sceKernelExit\0sceKernelAllocate\0";
    std::memcpy(dummy_elf.data() + 600, sample_str, sizeof(sample_str));

    // Section 3: .symtab
    shdrs[3].sh_type = 2; // SHT_SYMTAB
    shdrs[3].sh_offset = 800;
    shdrs[3].sh_size = sizeof(Relinker::RawElf64_Sym) * 2;
    shdrs[3].sh_link = 2; // links to .strtab

    auto* syms = reinterpret_cast<Relinker::RawElf64_Sym*>(dummy_elf.data() + 800);
    syms[1].st_name = 1; // "sceKernelExit"
    syms[1].st_value = 0x401000;
    syms[1].st_info = 0x12; // STB_GLOBAL, STT_FUNC

    std::vector<Relinker::ParsedElfSymbol> parsed_syms;
    std::map<std::string, uint64_t> sym_map;
    bool sym_res = Relinker::ElfSymbolResolver::ParseSymbols(dummy_elf.data(), dummy_elf.size(), parsed_syms, sym_map);
    if (!sym_res || sym_map.find("sceKernelExit") == sym_map.end()) {
        std::cerr << "[-] ElfSymbolResolver symbol parsing failed!\n";
        return 5;
    }
    std::cout << "    [+] Symbol table parsed: found '" << parsed_syms[1].name
              << "' -> 0x" << std::hex << parsed_syms[1].value << std::dec << "\n";

    // Test relocation application
    std::vector<uint8_t> target_image(1024, 0);
    std::vector<Relinker::ParsedElfRelocation> relocs = {
        { 0x100, 1, Relinker::R_X86_64_64, 0x20 } // offset 0x100, sym 1, addend 0x20
    };
    size_t applied = Relinker::ElfSymbolResolver::ApplyRelocations(
        target_image.data(), target_image.size(), 0x0, relocs, parsed_syms);

    uint64_t patched_val = *reinterpret_cast<uint64_t*>(target_image.data() + 0x100);
    if (applied != 1 || patched_val != (0x401000 + 0x20)) {
        std::cerr << "[-] Relocation application mismatch!\n";
        return 6;
    }
    std::cout << "    [+] Relocation applied at offset 0x100: 0x" << std::hex << patched_val << std::dec << "\n\n";

    std::cout << "[+] All HLE environment simulation components verified successfully!\n";
    return 0;
}
