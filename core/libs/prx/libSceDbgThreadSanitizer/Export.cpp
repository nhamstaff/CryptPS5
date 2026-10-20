#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

extern "C" {

void APS5_VABI __tsan_acquire(void* ptr) {
    (void)ptr;
}

void APS5_VABI __tsan_release(void* ptr) {
    (void)ptr;
}

}
