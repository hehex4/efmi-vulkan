// Restore the tracked pipeline-based compute state after a prepared GPU packet.
#pragma once
#include "common.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace efvk {
struct Device;
struct CmdBuf;
struct NativeComputeRestore {
    struct Set {
        uint32_t slot=0;
        VkPipelineLayout layout=VK_NULL_HANDLE;
        VkDescriptorSet handle=VK_NULL_HANDLE;
        std::vector<uint32_t> offsets;
    };
    VkPipeline pipeline=VK_NULL_HANDLE; // The actual selected pipeline, including native replacements.
    std::vector<Set> sets;
    std::vector<std::shared_ptr<const void>> layouts;
    PFN_vkCmdBindPipeline bind_pipeline=nullptr;
    PFN_vkCmdBindDescriptorSets bind_sets=nullptr;

    // Prepared compute binds a pipeline and set zero, but never writes push constants.
    // Ascending compatible sets repair every defined binding disturbed by that bind.
    // Vulkan cannot unbind a pipeline/set; formerly undefined state need not be restored.
    void record(VkCommandBuffer command) const {
        if(pipeline)bind_pipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        for(const auto& set:sets)bind_sets(command,VK_PIPELINE_BIND_POINT_COMPUTE,set.layout,
            set.slot,1,&set.handle,uint32_t(set.offsets.size()),set.offsets.data());
    }
};
// Prepare and record within the same intercepted call. Normal application pipeline/
// set lifetime rules still apply. This does not authorize pass splitting or submission.
// Failure leaves output untouched and records no Vulkan command.
bool prepare_native_compute_restore(Device*,const CmdBuf*,NativeComputeRestore&,std::string&,bool resource_lock_held=false);
}
