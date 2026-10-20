#include <cstdint>
#include <cstddef>
#include <mutex>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

namespace {

constexpr std::int32_t VOICE_QOS_APP_TYPE_GAME = 0x20000000;
constexpr std::int32_t VOICE_QOS_APP_TYPE_10000000 = 0x10000000;
constexpr std::uint32_t VOICE_QOS_MEMORY_SIZE = 0x40000;
constexpr int SCE_VOICE_ERROR_LIBVOICEQOS_NOT_INIT = static_cast<int>(0x804E0901);
constexpr int SCE_VOICE_ERROR_LIBVOICEQOS_ARGUMENT_INVALID = static_cast<int>(0x804E0902);
constexpr int SCE_VOICE_ERROR_LIBVOICEQOS_INITIALIZED = static_cast<int>(0x804E0905);

std::mutex g_mutex;
bool g_initialized = false;

}

extern "C" {

int APS5_VABI sceVoiceQoSInit(void* mem_block, uint32_t mem_size, int32_t app_type) {
    std::lock_guard lock(g_mutex);
    if (g_initialized) return SCE_VOICE_ERROR_LIBVOICEQOS_INITIALIZED;
    if (!mem_block || mem_size != VOICE_QOS_MEMORY_SIZE || (app_type != VOICE_QOS_APP_TYPE_GAME && app_type != VOICE_QOS_APP_TYPE_10000000)) return SCE_VOICE_ERROR_LIBVOICEQOS_ARGUMENT_INVALID;
    g_initialized = true;
    return 0;
}

int APS5_VABI sceVoiceQoSEnd() {
    std::lock_guard lock(g_mutex);
    if (!g_initialized) return SCE_VOICE_ERROR_LIBVOICEQOS_NOT_INIT;
    g_initialized = false;
    return 0;
}

int APS5_VABI sceVoiceQoSCreateLocalEndpoint(uint32_t* id) {
    if (id) *id = 1;
    return 0;
}

int APS5_VABI sceVoiceQoSDeleteLocalEndpoint(uint32_t id) {
    (void)id;
    return 0;
}

int APS5_VABI sceVoiceQoSCreateRemoteEndpoint(uint32_t* id) {
    if (id) *id = 2;
    return 0;
}

int APS5_VABI sceVoiceQoSDeleteRemoteEndpoint(uint32_t id) {
    (void)id;
    return 0;
}

int APS5_VABI sceVoiceQoSConnect(uint32_t localId, uint32_t remoteId) {
    (void)localId;
    (void)remoteId;
    return 0;
}

int APS5_VABI sceVoiceQoSDisconnect(uint32_t localId, uint32_t remoteId) {
    (void)localId;
    (void)remoteId;
    return 0;
}

int APS5_VABI sceVoiceQoSSetConnectionAttribute(uint32_t localId, uint32_t remoteId, const void* attr) {
    (void)localId;
    (void)remoteId;
    (void)attr;
    return 0;
}

int APS5_VABI sceVoiceQoSSetLocalEndpointAttribute(uint32_t id, const void* attr) {
    (void)id;
    (void)attr;
    return 0;
}

int APS5_VABI sceVoiceQoSSetRemoteEndpointAttribute(uint32_t id, const void* attr) {
    (void)id;
    (void)attr;
    return 0;
}

int APS5_VABI sceVoiceQoSReadPacket(uint32_t id, void* packet, uint32_t* size) {
    (void)id;
    (void)packet;
    if (size) *size = 0;
    return 0;
}

int APS5_VABI sceVoiceQoSWritePacket(uint32_t id, const void* packet, uint32_t size) {
    (void)id;
    (void)packet;
    (void)size;
    return 0;
}

int APS5_VABI sceVoiceQoSGetLocalEndpointAttribute(uint32_t id, void* attr) {
    (void)id;
    (void)attr;
    return 0;
}

}
