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

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define EFVK_VERSION "0.5.1"
#define EFVK_PROTOCOL 1
#define EFVK_LAYER_NAME "VK_LAYER_EFMI_probe"

namespace efvk {

// What the layer does once it is loaded. Set once at start-up from the
// environment variable EFMI_VK_MODE.
enum class Mode {
    Passthrough, // forwards everything, hooks only instance / device creation and present
    Probe,       // read-only tracking, frame capture and the named pipe
};

// How the DLL got its place in front of the Vulkan calls of the process.
enum class Entry {
    Layer,  // the Vulkan loader loaded it as a layer and chains it in
    Inject, // something loaded it with LoadLibrary; it redirects the exports of vulkan-1.dll itself
};

struct Config {
    Mode mode = Mode::Probe;
    Entry entry = Entry::Layer;
    bool keep_log_open = true;
    uint64_t unmap_snapshot_limit = 16ull << 20; // largest staging buffer copied when its memory is unmapped
    uint32_t shadow_usage = 0;                   // VkBufferUsageFlags that get a CPU shadow (set in util.cpp)
    // Probe mode keeps descriptor, image and shader metadata by default. Switching this off leaves
    // draws, buffers and hashes, for the case that the extra tracking costs too much in a real process.
    bool track_resources = true;
    // Upper bound for SPIR-V kept in memory. Past it, modules keep their hash and size only.
    uint64_t shader_budget = 512ull << 20;
};

extern Config g_cfg;

// What happened while attaching; reported by the status command.
struct AttachInfo {
    std::atomic<uint32_t> loader_found{0};        // vulkan-1.dll was seen
    std::atomic<uint32_t> loader_was_loaded{0};   // it was already in the process when the DLL arrived
    std::atomic<uint32_t> exports_redirected{0};  // export table entries pointed at the hooks
    std::atomic<uint32_t> instances_created{0};
    std::atomic<uint32_t> devices_created{0};
    std::atomic<uint64_t> passive_calls{0};       // hooked calls for a device created before attaching
};

extern AttachInfo g_attach;

// One export of vulkan-1.dll to redirect: where it should lead, and where to
// store the address it had.
struct ExportHook {
    const char* name;
    void* hook;
    void** original;
};

// layer.cpp
void collect_export_hooks(std::vector<ExportHook>& out);
void exports_resolved();

// bootstrap.cpp
void bootstrap_start();

void util_init(HMODULE self);
void logf(const char* fmt, ...);
const std::wstring& module_dir();
const std::string& exe_name();

// CRC-32C over a byte range, continuing from a previous result (0 to start).
uint32_t crc32c(uint32_t seed, const void* data, size_t size);
bool crc32c_uses_hardware();

void ipc_start();

} // namespace efvk
