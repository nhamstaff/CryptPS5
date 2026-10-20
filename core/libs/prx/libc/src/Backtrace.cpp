#include <cstddef>
#include "prx/libc/include/General.hpp"

extern "C" {

int APS5_VABI sceLibcBacktraceGetBufferSize_nid_postfix(int, int*, std::size_t* size) {
    if (size) *size = 0;
    return 0;
}

int APS5_VABI sceLibcBacktraceSelf_nid_postfix(int, void*, std::size_t, int* count) {
    if (count) *count = 0;
    return 0;
}

}
