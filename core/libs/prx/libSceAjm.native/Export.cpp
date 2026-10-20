#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <cstdio>

extern "C" {

int APS5_VABI sceAjmBatchJobEncode(AjmBatchInfo* info, uint32_t instance, const void* pcm_input, size_t pcm_input_size, void* bitstream_output, size_t bitstream_output_size, void* result) {
    (void)info;
    (void)instance;
    (void)pcm_input;
    (void)pcm_input_size;
    (void)bitstream_output;
    (void)bitstream_output_size;
    (void)result;
    return 0;
}

const char* APS5_VABI sceAjmStrError(int error) {
    (void)error;
    return "OK";
}

int APS5_VABI sceAjmDecWVorbisCreateHeaderPacket(void) {
    return 0;
}

int APS5_VABI sceAjmDecWVorbisCreateSetupPacket(void) {
    return 0;
}

}
