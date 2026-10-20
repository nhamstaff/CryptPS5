#include "VirtualMemorySim.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <iostream>
#include <algorithm>

namespace LibkernelSim {

VirtualMemorySimulator& VirtualMemorySimulator::Instance() {
    static VirtualMemorySimulator instance;
    return instance;
}

static DWORD TranslateToHostProtWin(uint32_t prot) {
#ifdef _WIN32
    if ((prot & PROT_EXEC_SIM) && (prot & PROT_WRITE_SIM)) return PAGE_EXECUTE_READWRITE;
    if (prot & PROT_EXEC_SIM) return PAGE_EXECUTE_READ;
    if (prot & PROT_WRITE_SIM) return PAGE_READWRITE;
    if (prot & PROT_READ_SIM) return PAGE_READONLY;
    return PAGE_NOACCESS;
#else
    return 0;
#endif
}

int VirtualMemorySimulator::AllocateVirtual(void** out_addr, size_t length, uint32_t prot, uint32_t flags, size_t alignment, const std::string& name) {
    if (!out_addr || length == 0) return -1;

    size_t aligned_len = AlignUp(length, alignment ? alignment : PAGE_SIZE_16K);
    void* allocated = nullptr;

#ifdef _WIN32
    DWORD host_prot = TranslateToHostProtWin(prot);
    allocated = VirtualAlloc(nullptr, aligned_len, MEM_RESERVE | MEM_COMMIT, host_prot);
#else
    int host_prot = 0;
    if (prot & PROT_READ_SIM) host_prot |= PROT_READ;
    if (prot & PROT_WRITE_SIM) host_prot |= PROT_WRITE;
    if (prot & PROT_EXEC_SIM) host_prot |= PROT_EXEC;
    allocated = mmap(nullptr, aligned_len, host_prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (allocated == MAP_FAILED) allocated = nullptr;
#endif

    if (!allocated) return -2;

    uintptr_t addr_val = reinterpret_cast<uintptr_t>(allocated);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        VirtualPageBlock block{};
        block.virtual_address = addr_val;
        block.allocated_bytes = aligned_len;
        block.protection      = prot;
        block.flags           = flags;
        block.tag_name        = name.empty() ? "libkernel_anon_heap" : name;
        block.is_committed    = true;
        active_blocks_[addr_val] = block;
    }

    *out_addr = allocated;
    return 0;
}

int VirtualMemorySimulator::FreeVirtual(void* addr, size_t length) {
    if (!addr) return -1;
    uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);

    size_t actual_length = length;
    bool is_direct = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_blocks_.find(addr_val);
        if (it != active_blocks_.end()) {
            actual_length = it->second.allocated_bytes;
            is_direct = it->second.is_direct_mapped;
            active_blocks_.erase(it);
        } else {
            for (auto bit = active_blocks_.begin(); bit != active_blocks_.end(); ++bit) {
                if (addr_val >= bit->first && addr_val < (bit->first + bit->second.allocated_bytes)) {
                    actual_length = bit->second.allocated_bytes;
                    is_direct = bit->second.is_direct_mapped;
                    addr = reinterpret_cast<void*>(bit->first);
                    active_blocks_.erase(bit);
                    break;
                }
            }
        }
    }

    if (is_direct) {
#ifdef _WIN32
        VirtualFree(addr, 0, MEM_RELEASE);
#else
        munmap(addr, actual_length ? actual_length : PAGE_SIZE_16K);
#endif
        return 0;
    }

#ifdef _WIN32
    (void)actual_length;
    if (!VirtualFree(addr, 0, MEM_RELEASE)) return -2;
#else
    if (munmap(addr, actual_length ? actual_length : PAGE_SIZE_16K) != 0) return -2;
#endif

    return 0;
}

int VirtualMemorySimulator::AllocateDirectMemory(
    size_t length, size_t alignment, uint64_t* out_phys_addr, const std::string& name)
{
    if (!out_phys_addr || length == 0) return -1;
    size_t align = alignment ? alignment : PAGE_SIZE_16K;
    size_t aligned_len = AlignUp(length, align);

    std::lock_guard<std::mutex> lock(mutex_);
    next_phys_addr_ = AlignUp(next_phys_addr_, align);
    uint64_t phys_addr = next_phys_addr_;
    next_phys_addr_ += aligned_len;

    DirectMemoryBlock block{};
    block.phys_address = phys_addr;
    block.size         = aligned_len;
    block.tag_name     = name.empty() ? "libkernel_direct_pool" : name;
    block.is_allocated = true;
    block.host_backing = nullptr;

    active_direct_blocks_[phys_addr] = block;
    *out_phys_addr = phys_addr;
    return 0;
}

int VirtualMemorySimulator::MapDirectMemory(
    void** out_addr, size_t length, uint32_t prot, uint64_t phys_addr, size_t alignment)
{
    if (!out_addr || length == 0) return -1;

    size_t align = alignment ? alignment : PAGE_SIZE_16K;
    size_t aligned_len = AlignUp(length, align);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_direct_blocks_.find(phys_addr);
    if (it == active_direct_blocks_.end() || it->second.size < aligned_len) {
        return -2; // Direct physical block not found or too small
    }

    void* allocated = nullptr;
#ifdef _WIN32
    DWORD host_prot = TranslateToHostProtWin(prot);
    allocated = VirtualAlloc(nullptr, aligned_len, MEM_RESERVE | MEM_COMMIT, host_prot);
#else
    int host_prot = 0;
    if (prot & PROT_READ_SIM) host_prot |= PROT_READ;
    if (prot & PROT_WRITE_SIM) host_prot |= PROT_WRITE;
    if (prot & PROT_EXEC_SIM) host_prot |= PROT_EXEC;
    allocated = mmap(nullptr, aligned_len, host_prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (allocated == MAP_FAILED) allocated = nullptr;
#endif

    if (!allocated) return -3;

    uintptr_t addr_val = reinterpret_cast<uintptr_t>(allocated);
    VirtualPageBlock vblock{};
    vblock.virtual_address     = addr_val;
    vblock.allocated_bytes     = aligned_len;
    vblock.protection          = prot;
    vblock.flags               = MAP_FIXED_SIM;
    vblock.tag_name            = it->second.tag_name + "_vmap";
    vblock.is_committed        = true;
    vblock.is_direct_mapped    = true;
    vblock.phys_address_mapped = phys_addr;

    active_blocks_[addr_val] = vblock;
    *out_addr = allocated;
    return 0;
}

int VirtualMemorySimulator::ReleaseDirectMemory(uint64_t phys_addr, size_t length) {
    (void)length;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_direct_blocks_.find(phys_addr);
    if (it == active_direct_blocks_.end()) return -1;

    active_direct_blocks_.erase(it);
    return 0;
}

size_t VirtualMemorySimulator::GetTotalDirectMemoryBytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& [phys, block] : active_direct_blocks_) {
        if (block.is_allocated) total += block.size;
    }
    return total;
}

int VirtualMemorySimulator::ProtectVirtual(const void* addr, size_t length, uint32_t new_prot) {
    if (!addr || length == 0) return -1;
    uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);

#ifdef _WIN32
    DWORD old_prot = 0;
    DWORD host_prot = TranslateToHostProtWin(new_prot);
    if (!VirtualProtect(const_cast<void*>(addr), length, host_prot, &old_prot)) return -2;
#else
    int host_prot = 0;
    if (new_prot & PROT_READ_SIM) host_prot |= PROT_READ;
    if (new_prot & PROT_WRITE_SIM) host_prot |= PROT_WRITE;
    if (new_prot & PROT_EXEC_SIM) host_prot |= PROT_EXEC;
    if (mprotect(const_cast<void*>(addr), length, host_prot) != 0) return -2;
#endif

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_blocks_.find(addr_val);
        if (it != active_blocks_.end()) {
            it->second.protection = new_prot;
        }
    }
    return 0;
}

bool VirtualMemorySimulator::QueryVirtual(const void* addr, VirtualPageBlock* out_info) {
    if (!addr || !out_info) return false;
    uintptr_t query_addr = reinterpret_cast<uintptr_t>(addr);

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [base, block] : active_blocks_) {
        if (query_addr >= base && query_addr < (base + block.allocated_bytes)) {
            *out_info = block;
            return true;
        }
    }
    return false;
}

size_t VirtualMemorySimulator::GetTotalCommittedBytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& [base, block] : active_blocks_) {
        if (block.is_committed) total += block.allocated_bytes;
    }
    return total;
}

} // namespace LibkernelSim
