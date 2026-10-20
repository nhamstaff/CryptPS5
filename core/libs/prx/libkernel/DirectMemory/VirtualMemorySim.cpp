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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_blocks_.find(addr_val);
        if (it != active_blocks_.end()) {
            active_blocks_.erase(it);
        }
    }

#ifdef _WIN32
    (void)length;
    if (!VirtualFree(addr, 0, MEM_RELEASE)) return -2;
#else
    if (munmap(addr, length) != 0) return -2;
#endif

    return 0;
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
