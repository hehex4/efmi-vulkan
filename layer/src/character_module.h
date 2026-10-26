// The boundary between the loaded layer (the host: hooks and tracker) and the code that decides
// and records character replacements (the module). The host has that code built in and can take
// another copy of it from a DLL while the process runs (docs/hot-swap.md).
//
// Both sides are built from the same headers with the same compiler settings; EFVK_ABI is a stamp
// of those headers, and a module with another stamp is refused. Objects cross the boundary as they
// are (tracker structures, strings, vectors): both sides allocate from the process heap. Code of a
// module that was ever used stays loaded until the process ends, because objects it made may
// outlive the backend that made them.
#pragma once

#include "character_backend.h"

#ifndef EFVK_ABI
#define EFVK_ABI 0ull // set by scripts/build.py from the shared headers
#endif

namespace efvk {

struct BufInfo;
struct SetBind;
struct BoundRange;
struct Config;

// What a module calls back for: the tracker's readers and the host's log.
struct HostApi {
    uint32_t size = 0;
    uint64_t abi = 0;
    const Config* config = nullptr; // read once while the entry function runs
    void (*log)(const char* text) = nullptr;
    uint32_t (*crc32c)(uint32_t seed, const void* data, size_t size) = nullptr;
    uint8_t (*read_buffer_locked)(Device*, const BufInfo&, uint64_t off, uint64_t size, uint8_t* out) = nullptr;
    void (*collect_buffer_ranges_locked)(Device*, const SetBind&, const uint32_t* dyn, uint32_t dyn_count, uint32_t slot,
                                         std::vector<BoundRange>& out) = nullptr;
    // The settings the probe changes in a running process (`set`), read live: the module's copy of the
    // configuration is the one its entry made (BUG-055).
    bool (*gpu_bones)() = nullptr;
};

// The presentation a frame ends with, when the host saw one (vkQueuePresentKHR on `queue`). The
// module may draw into the image of the first swap chain before it is shown: it then waits for
// `waits` itself, replaces them with the semaphores the presentation is to wait for and sets
// `replaced`. Called on the presenting thread, which owns `queue` for the duration.
struct PresentWork {
    VkQueue queue = VK_NULL_HANDLE;
    const VkPresentInfoKHR* info = nullptr;
    std::vector<VkSemaphore> waits;
    bool replaced = false;
};

// The functions of one copy of the replacement code.
struct CharacterModule {
    uint32_t size = 0;
    uint64_t abi = 0;
    const char* version = "";
    std::shared_ptr<CharacterBackend> (*create)(Device*, const VkPhysicalDeviceProperties&, const VkPhysicalDeviceMemoryProperties&) = nullptr;
    // False when the package or the device was refused; `reason` says why.
    bool (*usable)(CharacterBackend&, std::string& reason) = nullptr;
    // Mesh, arenas and images are in place: draws can be replaced from the first one on.
    bool (*ready)(CharacterBackend&) = nullptr;
    void (*stop)(CharacterBackend&) = nullptr;
    bool (*record)(CharacterBackend&, CmdBuf*, const DrawRec&) = nullptr;
    void (*retire)(CharacterBackend&, uint32_t pipeline_id) = nullptr;
    void (*frame)(CharacterBackend&, bool had_draws, PresentWork* present) = nullptr;
    void (*status)(CharacterBackend&, JsonWriter&) = nullptr;
    void (*enable)(CharacterBackend&, bool) = nullptr;
    // Before the application's submission of `cbs` on `queue`: the work those recordings need first
    // (the texture uploads' publication, the bone dispatches of their draws, owned draw work).
    VkResult (*submit)(CharacterBackend&, VkQueue, uint32_t count, CmdBuf* const* cbs) = nullptr;
    // After the application's vkCmdEndRenderPass of the instance `cb->pass` names (D-113): work that
    // belongs to that pass and needs targets of its own (the translucent captures and composite).
    void (*pass_end)(CharacterBackend&, CmdBuf*) = nullptr;
};

// The copy this binary was built with.
const CharacterModule* builtin_character_module();

// Exported by a module DLL: `const CharacterModule* efmi_vk_character_module(const HostApi*)`.
// Null when the host's stamp or structure size is not the module's.
#define EFVK_MODULE_ENTRY "efmi_vk_character_module"
using CharacterModuleEntry = const CharacterModule* (*)(const HostApi*);

} // namespace efvk
