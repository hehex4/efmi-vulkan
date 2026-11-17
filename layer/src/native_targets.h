#pragma once
#include "native_state.h"
#include "command_owner.h"
#include "../../frontend/assets/file_plan.h"
namespace efvk {
class PipelineVariant;
struct NativeComputeRestore;
struct NativeGraphicsRestore;
inline bool native_output_binding(const efmi::frontend::NativeBinding& binding){
    return binding.kind==efmi::frontend::NativeBinding::ColorTarget || binding.kind==efmi::frontend::NativeBinding::DepthTarget;
}
struct NativeClassicTarget {
    VkDevice device=VK_NULL_HANDLE;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    PFN_vkDestroyFramebuffer destroy_framebuffer=nullptr;
    PFN_vkDestroyRenderPass destroy_pass=nullptr;
    std::shared_ptr<const RenderPassRecipe> original,recipe;
    std::shared_ptr<const FramebufferInfo> info;
    std::vector<TargetView> bindings; // Original attachment indices, including temporarily unused slots.
    std::vector<std::shared_ptr<ImageViewLifetime>> views;
    ~NativeClassicTarget(){
        if(framebuffer)destroy_framebuffer(device,framebuffer,nullptr);
        if(pass)destroy_pass(device,pass,nullptr);
    }
};
struct NativeTargetCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<NativeClassicTarget>> classic;
    std::shared_ptr<NativePipelineCache> pipeline_cache;
};
struct NativeTargetPlan {
    struct Event {
        std::shared_ptr<NativeClassicTarget> classic;
        std::shared_ptr<const NativeBoundPipeline> pipeline;
        VkPipeline bound=VK_NULL_HANDLE;
        VkImageView target=VK_NULL_HANDLE;
        RenderTargets targets;
        std::shared_ptr<const efmi::frontend::NativeResource> resource;
        std::shared_ptr<NativeImageState> images;
        // Work-only breaks retain every output, including depth-only/no-color passes.
        std::shared_ptr<const efmi::gpu::PreparedResourceCommands> work;
        std::shared_ptr<const NativeComputeRestore> compute;
        std::shared_ptr<const NativeGraphicsRestore> graphics;
        // Whole-image handoff around a packet, after End and before LOAD Begin.
        std::vector<VkImageMemoryBarrier> work_before,work_after;
        bool change=false;
        uint32_t slot=0;
    };
    std::vector<std::shared_ptr<Event>> events;
    // A prior output split establishes this layout before the corresponding input bind.
    std::vector<VkImageLayout> read_layouts;
    std::shared_ptr<NativeTargetState> state;
    std::shared_ptr<CommandOwner> owner;
};
bool prepare_native_targets(Device*,CmdBuf*,const DrawRec&,const efmi::assets::FilePlan&,NativeTargetPlan&,std::string&,NativeTargetCache* = nullptr,
                            bool resource_lock_held = false);
void record_native_target(Device*,CmdBuf*,const NativeTargetPlan&,size_t);
void native_targets_end(Device*,CmdBuf*);
// Resolve the exact selected stages, then adapt to an active private classic pass if needed.
VkPipeline native_target_bind(Device*,CmdBuf*,VkPipeline);
bool native_target_masked(const CmdBuf*);
// Prepare and retain a Mod variant for the active pass, or the pass after a prepared prefix,
// without recording output changes or changing the application's selected shaders.
VkPipeline native_target_variant(Device*,CmdBuf*,const PipelineVariant&,std::string&,
                                 const NativeTargetPlan* future = nullptr,size_t before = 0);
}
