// Entry of the replacement module DLL, and the few functions of the host that the replacement
// code calls: here they forward to the host that loaded the module. Not part of the layer DLL.
#include "../src/character_module.h"
#include "../src/state.h"

#include <cstdarg>
#include <cstdio>

namespace efvk {

Config g_cfg;
static const HostApi* g_host = nullptr;

void logf(const char* fmt, ...)
{
    if (!g_host) return;
    char text[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    g_host->log(text);
}

uint32_t crc32c(uint32_t seed, const void* data, size_t size) { return g_host->crc32c(seed, data, size); }
bool live_gpu_bones() { return g_host && g_host->gpu_bones ? g_host->gpu_bones() : g_cfg.m2_gpu_bones; }

ReadSrc read_buffer_locked(Device* dev, const BufInfo& b, uint64_t off, uint64_t size, uint8_t* out)
{
    return static_cast<ReadSrc>(g_host->read_buffer_locked(dev, b, off, size, out));
}

void collect_buffer_ranges_locked(Device* dev, const SetBind& bind, const uint32_t* dyn, uint32_t dyn_count, uint32_t slot,
                                  std::vector<BoundRange>& out)
{
    g_host->collect_buffer_ranges_locked(dev, bind, dyn, dyn_count, slot, out);
}

} // namespace efvk

extern "C" __declspec(dllexport) const efvk::CharacterModule* efmi_vk_character_module(const efvk::HostApi* host)
{
    using namespace efvk;
    if (!host || host->size != sizeof(HostApi) || host->abi != EFVK_ABI || !host->config || !host->log || !host->crc32c ||
        !host->read_buffer_locked || !host->collect_buffer_ranges_locked) return nullptr;
    g_host = host;
    g_cfg = *host->config;
    return builtin_character_module();
}
