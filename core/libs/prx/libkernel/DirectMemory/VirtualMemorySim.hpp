#ifndef CORE_LIBS_PRX_LIBKERNEL_VIRTUAL_MEMORY_SIM_HPP
#define CORE_LIBS_PRX_LIBKERNEL_VIRTUAL_MEMORY_SIM_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <map>
#include <mutex>
#include <vector>

namespace LibkernelSim {

// ──────────────────────────────────────────────────────────────────────
//  Clean-room High-Level Emulation (HLE) Virtual Memory Simulator
//  Mimics PS5 libkernel virtual memory allocation, page protection,
//  and address space management semantics for development test binaries.
// ──────────────────────────────────────────────────────────────────────

static constexpr size_t PAGE_SIZE_16K = 0x4000; // 16 KiB PS5 page granularity
static constexpr size_t PAGE_SIZE_2M  = 0x200000; // 2 MiB superpage

enum MemoryProtection : uint32_t {
    PROT_NONE_SIM  = 0x0,
    PROT_READ_SIM  = 0x1,
    PROT_WRITE_SIM = 0x2,
    PROT_EXEC_SIM  = 0x4
};

enum MemoryFlags : uint32_t {
    MAP_ANON_SIM    = 0x1000,
    MAP_FIXED_SIM   = 0x0010,
    MAP_PRIVATE_SIM = 0x0002
};

struct VirtualPageBlock {
    uintptr_t   virtual_address;
    size_t      allocated_bytes;
    uint32_t    protection;
    uint32_t    flags;
    std::string tag_name;
    bool        is_committed;
};

class VirtualMemorySimulator {
public:
    static VirtualMemorySimulator& Instance();

    // Allocate / map virtual address space with given alignment and protection
    int AllocateVirtual(void** out_addr, size_t length, uint32_t prot, uint32_t flags, size_t alignment = PAGE_SIZE_16K, const std::string& name = "");

    // Release / unmap virtual address space
    int FreeVirtual(void* addr, size_t length);

    // Modify protection on page range
    int ProtectVirtual(const void* addr, size_t length, uint32_t new_prot);

    // Query virtual page state
    bool QueryVirtual(const void* addr, VirtualPageBlock* out_info);

    // Total active allocated bytes
    size_t GetTotalCommittedBytes();

private:
    VirtualMemorySimulator() = default;
    std::mutex mutex_;
    std::map<uintptr_t, VirtualPageBlock> active_blocks_;

    static size_t AlignUp(size_t val, size_t align) {
        return (val + align - 1) & ~(align - 1);
    }
};

} // namespace LibkernelSim

#endif // CORE_LIBS_PRX_LIBKERNEL_VIRTUAL_MEMORY_SIM_HPP
