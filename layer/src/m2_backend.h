// Experimental single-component integration; inactive unless a package is explicitly configured.
#pragma once
#include "command_owner.h"
#include "draw_snapshots.h"
#include "variant_cache.h"

namespace efvk {
struct Device;
struct PresentWork;
struct CmdBuf;
struct DrawRec;
class JsonWriter;
class M2Backend;
struct M2GpuMesh;
// The single-component backend's part of a command buffer.
struct M2Command : CommandOwner {
    std::shared_ptr<DrawSnapshots> snapshots;
    std::shared_ptr<const M2GpuMesh> mesh;
    std::vector<std::shared_ptr<const PipelineVariant>> variants;
    void reset_after_completion() override { used = false; variants.clear(); snapshots->recycle_completed(); }
    bool seal() override { if (snapshots->sealed()) return false; snapshots->seal(); return true; }
};
void m2_create(Device*,const VkPhysicalDeviceProperties&,const VkPhysicalDeviceMemoryProperties&);
void m2_stop(Device*);
void m2_retire(Device*,VkPipeline);
bool m2_record(Device*,CmdBuf*,const DrawRec&);
void m2_seal(Device*,uint32_t,const VkCommandBuffer*);
// Once per present; `had_draws` is false for an interval in which nothing was drawn.
void m2_frame(Device*,bool had_draws,PresentWork* present=nullptr);
// Before the application's submission goes down: uploads and publication of a character backend.
VkResult m2_submit(Device*,VkQueue);
void m2_status(Device*,JsonWriter&);
// `advance` ends a frame of the character backend, for a process that never presents (tests).
std::string probe_m2_set(int enabled, bool advance = false);
// Takes the replacement code from a module DLL (docs/hot-swap.md). The new backend is made and
// staged at once; it takes over at a frame boundary when it is ready. `package`: empty keeps the
// configured one.
std::string probe_m2_module(const std::string& path, const std::string& package);
} // namespace efvk
