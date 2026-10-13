// Experimental single-component integration; inactive unless a package is explicitly configured.
#pragma once
#include "draw_snapshots.h"
#include "variant_cache.h"

namespace efvk {
struct Device;
struct CmdBuf;
struct DrawRec;
class JsonWriter;
class M2Backend;
struct M2GpuMesh;
struct M2Command {
    std::shared_ptr<DrawSnapshots> snapshots;
    std::shared_ptr<const M2GpuMesh> mesh;
    std::shared_ptr<const void> keep; // the character backend's mesh owner
    std::vector<std::shared_ptr<const PipelineVariant>> variants;
    void reset_after_completion() { variants.clear(); snapshots->recycle_completed(); }
};
void m2_create(Device*,const VkPhysicalDeviceProperties&,const VkPhysicalDeviceMemoryProperties&);
void m2_stop(Device*);
void m2_retire(Device*,VkPipeline);
bool m2_record(Device*,CmdBuf*,const DrawRec&);
void m2_seal(Device*,uint32_t,const VkCommandBuffer*);
// Once per present; `had_draws` is false for an interval in which nothing was drawn.
void m2_frame(Device*,bool had_draws);
void m2_status(Device*,JsonWriter&);
// `advance` ends a frame of the character backend, for a process that never presents (tests).
std::string probe_m2_set(int enabled, bool advance = false);
} // namespace efvk
