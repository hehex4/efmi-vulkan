#pragma once
#include "state.h"
#include "pipeline_variant.h"
#include "command_owner.h"
#include "../../frontend/assets/file_plan.h"
namespace efvk {
inline bool native_output_binding(const efmi::frontend::NativeBinding& binding){
    return binding.kind==efmi::frontend::NativeBinding::ColorTarget || binding.kind==efmi::frontend::NativeBinding::DepthTarget;
}
// The host reads these cache objects after module exchange: their complete layouts are shared ABI.
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
struct NativeClassicPipeline {
    std::shared_ptr<const GraphicsRecipe> source;
    std::shared_ptr<const PipelineLayoutInfo> layout;
    std::shared_ptr<const RenderPassRecipe> pass;
    std::shared_ptr<const PipelineVariant> variant;
};
struct NativePipelineSelection {
    VkPipeline source=VK_NULL_HANDLE,bound=VK_NULL_HANDLE;
    std::shared_ptr<const GraphicsRecipe> recipe;
    std::shared_ptr<const PipelineLayoutInfo> layout;
};
struct NativePipelineCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<NativeClassicPipeline>> pipelines;
};
struct NativeTargetCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<NativeClassicTarget>> classic;
    std::shared_ptr<NativePipelineCache> pipeline_cache;
};
// Complete shared layout: the host finishes a scope that an exchanged module began.
struct NativeTargetState {
    struct Saved {TargetView view;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;std::shared_ptr<const efmi::frontend::NativeResource> resource;};
    struct Color {
        VkImage original_image=VK_NULL_HANDLE;
        VkFormat format=VK_FORMAT_UNDEFINED;
        VkImageView original=VK_NULL_HANDLE,active=VK_NULL_HANDLE;
    };
    bool initialized=false;
    bool classic=false,private_pass=false;
    std::vector<Saved> classic_originals;
    std::shared_ptr<const RenderPassRecipe> classic_recipe;
    std::shared_ptr<NativePipelineCache> compatibility;
    Color depth;
    VkFormat stencil_format=VK_FORMAT_UNDEFINED;
    std::vector<Color> colors; // Original per-slot contracts survive temporary null views.
    std::map<VkImageView,Saved> restore;
};
struct NativeTargetPlan {
    struct Event {std::shared_ptr<NativeClassicTarget> classic;std::shared_ptr<const PipelineVariant> pipeline;VkPipeline bound=VK_NULL_HANDLE;VkImageView target=VK_NULL_HANDLE;RenderTargets targets;std::shared_ptr<const efmi::frontend::NativeResource> resource;std::shared_ptr<NativeImageState> images;bool change=false;uint32_t slot=0;};
    std::vector<std::shared_ptr<Event>> events;
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
