// Restore the tracked pipeline-based compute state after a prepared GPU packet.
#pragma once
#include "native_descriptor_restore.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace efvk {
struct Device;
struct CmdBuf;
struct NativeComputeRestore : NativeDescriptorRestore {
    VkPipeline pipeline=VK_NULL_HANDLE; // The actual selected pipeline, including native replacements.
    PFN_vkCmdBindPipeline bind_pipeline=nullptr;

    // Prepared compute binds a pipeline and set zero, but never writes push constants.
    // Ascending compatible sets repair every defined binding disturbed by that bind.
    // Vulkan cannot unbind a pipeline/set; formerly undefined state need not be restored.
    void record(VkCommandBuffer command) const {
        if(pipeline)bind_pipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        record_sets(command,VK_PIPELINE_BIND_POINT_COMPUTE);
    }
};
// Prepare and record within the same intercepted call. Normal application pipeline/
// set lifetime rules still apply. This does not authorize pass splitting or submission.
// Failure leaves output untouched and records no Vulkan command.
bool prepare_native_compute_restore(Device*,const CmdBuf*,NativeComputeRestore&,std::string&,bool resource_lock_held=false);
}
