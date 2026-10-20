#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t SCE_HMD2_ERROR_UNSUPPORTED_FEATURE = static_cast<std::int32_t>(0x81110016);

}

extern "C" {

int APS5_VABI sceHmd2ReprojectionQuerySeeThroughBufferSizeAlign() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

std::int32_t APS5_VABI sceHmd2Initialize(const void* param) {
    (void)param;
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2GetFieldOfView() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2GetDeviceInformationByHandle() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2Terminate() {
    return 0;
}

int APS5_VABI sceHmd2Close() {
    return 0;
}

int APS5_VABI sceHmd2GazeGetResult() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2GazeGetResultForFoveatedRendering() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2GetDeviceInformation() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2GetFieldOfViewWithoutHandle() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2Open() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionBeginFrame() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionDisableMirroring() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionDisableVrMode() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionEnableMirroring() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionEnableVrMode() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionGetMirroringDisplayBufferSizeAlign() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionGetMirroringWorkMemorySizeAlign() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionGetPredictedDisplayTime() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionInitialize() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionQueryBufferSizeAlign() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionQueryDisplayBufferSizeAlign() {
    return SCE_HMD2_ERROR_UNSUPPORTED_FEATURE;
}

int APS5_VABI sceHmd2ReprojectionSetAllowPositionalReprojection() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetMirroringOption() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetParam() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetParamWithBuffer() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetRenderConfig() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionSetTiming() {
    return 0;
}

int APS5_VABI sceHmd2ReprojectionTerminate() {
    return 0;
}

int APS5_VABI sceHmd2SetVibration() {
    return 0;
}

}
