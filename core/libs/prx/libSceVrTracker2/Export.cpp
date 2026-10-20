#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t SCE_VR_TRACKER2_ERROR_UNSUPPORTED = static_cast<std::int32_t>(0x81110016);
constexpr std::int32_t SCE_VR_TRACKER2_ERROR_NO_DEVICE = static_cast<std::int32_t>(0x81110001);

}

extern "C" {

int APS5_VABI sceVrTracker2Finalize() {
    return 0;
}

int APS5_VABI sceVrTracker2ResetLocalCoordinate() {
    return 0;
}

int APS5_VABI sceVrTracker2GetPlayAreaOrientedBoundingBox() {
    return SCE_VR_TRACKER2_ERROR_NO_DEVICE;
}

int APS5_VABI sceVrTracker2GetResult() {
    return SCE_VR_TRACKER2_ERROR_NO_DEVICE;
}

int APS5_VABI sceVrTracker2Initialize() {
    return SCE_VR_TRACKER2_ERROR_UNSUPPORTED;
}

int APS5_VABI sceVrTracker2LocateCoordinateSystem() {
    return 0;
}

int APS5_VABI sceVrTracker2QueryMemory() {
    return SCE_VR_TRACKER2_ERROR_UNSUPPORTED;
}

int APS5_VABI sceVrTracker2RegisterDevice() {
    return SCE_VR_TRACKER2_ERROR_NO_DEVICE;
}

int APS5_VABI sceVrTracker2SetCoordinateSystem() {
    return 0;
}

int APS5_VABI sceVrTracker2UnregisterDevice() {
    return 0;
}

}
