// Shared declarations of the EFMI Vulkan layer.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The layer defines its own entry points and calls down through function
// pointers only, so the prototypes of the loader are not wanted.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <cstdint>
#include <cstring>
#include <string>

#define EFVK_VERSION "0.1.0"
#define EFVK_PROTOCOL 1
#define EFVK_LAYER_NAME "VK_LAYER_EFMI_probe"

namespace efvk {

// What the layer does once it is loaded. Set once at start-up from the
// environment variable EFMI_VK_MODE.
enum class Mode {
    Passthrough, // forwards everything, hooks only instance / device creation and present
    Probe,       // read-only tracking, frame capture and the named pipe
};

struct Config {
    Mode mode = Mode::Probe;
    bool keep_log_open = true;
    uint64_t unmap_snapshot_limit = 16ull << 20; // largest staging buffer copied when its memory is unmapped
    uint32_t shadow_usage = 0;                   // VkBufferUsageFlags that get a CPU shadow (set in util.cpp)
};

extern Config g_cfg;

void util_init(HMODULE self);
void logf(const char* fmt, ...);
const std::wstring& module_dir();
const std::string& exe_name();

// CRC-32C over a byte range, continuing from a previous result (0 to start).
uint32_t crc32c(uint32_t seed, const void* data, size_t size);
bool crc32c_uses_hardware();

void ipc_start();

} // namespace efvk
