#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t SCE_PSML_ERROR_NOT_INITIALIZED = static_cast<std::int32_t>(0x8A810001);

}

extern "C" {

std::int32_t APS5_VABI scePsmlMfsrGetContextBufferRequirement1100(void* requirement, const void* param) {
    (void)requirement;
    (void)param;
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

std::int32_t APS5_VABI scePsmlMfsrCreateContext1100(void** context, const void* param) {
    (void)context;
    (void)param;
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

std::int32_t APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1100(void* context, void* commandBuffer, const void* param) {
    (void)context;
    (void)commandBuffer;
    (void)param;
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrCreateContext1300() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1300() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrGetSharedResourcesInitRequirement() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrInit() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrGetDispatchMfsrPacketSizeInDwords() {
    return 0;
}

int APS5_VABI scePsmlMfsrGetContextBufferRequirement800M3_2() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrSelectConfig() {
    return 0;
}

int APS5_VABI scePsmlMfsrGetMipmapBias() {
    return 0;
}

int APS5_VABI scePsmlMfsrRequestCapture() {
    return 0;
}

int APS5_VABI scePsmlMfsrReleaseContext() {
    return 0;
}

int APS5_VABI scePsmlMfsrIsCaptureInProgress() {
    return 0;
}

int APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1000() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrGetDispatchMfsrPacket900() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrCreateSharedResources() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrCreateContext800M3_2() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsrReleaseSharedResources() {
    return 0;
}

int APS5_VABI scePsmlMfsr2ReleaseSharedResources() {
    return 0;
}

int APS5_VABI scePsmlMfsr2GetSharedResourcesInitRequirement() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsr2GetContextInitRequirement() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsr2ReleaseContext() {
    return 0;
}

int APS5_VABI scePsmlMfsr2GetDispatchPacketsSizeInDwords() {
    return 0;
}

int APS5_VABI scePsmlMfsr2CreateSharedResources() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsr2GetDispatchPackets() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsr2CreateContext() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

int APS5_VABI scePsmlMfsr2Init() {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

APS5_EXPORT("xNZM9Y5lf-M", scePsmlUnknown01);
int APS5_VABI scePsmlUnknown01(void) {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

APS5_EXPORT("lcZD9tJraAw", scePsmlUnknown02);
int APS5_VABI scePsmlUnknown02(void) {
    return SCE_PSML_ERROR_NOT_INITIALIZED;
}

}
