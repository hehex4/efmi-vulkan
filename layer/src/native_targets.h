#pragma once
#include "state.h"
#include "command_owner.h"
#include "../../frontend/assets/file_plan.h"
namespace efvk {
inline bool native_output_binding(const efmi::frontend::NativeBinding& binding){
    return binding.kind==efmi::frontend::NativeBinding::ColorTarget || binding.kind==efmi::frontend::NativeBinding::DepthTarget;
}
struct NativeClassicTarget;
struct NativeTargetCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<NativeClassicTarget>> classic;
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
    Color depth;
    VkFormat stencil_format=VK_FORMAT_UNDEFINED;
    std::vector<Color> colors; // Original per-slot contracts survive temporary null views.
    std::map<VkImageView,Saved> restore;
};
struct NativeTargetPlan {
    struct Event {std::shared_ptr<NativeClassicTarget> classic;VkImageView target=VK_NULL_HANDLE;RenderTargets targets;std::shared_ptr<const efmi::frontend::NativeResource> resource;std::shared_ptr<NativeImageState> images;bool change=false;uint32_t slot=0;};
    std::vector<std::shared_ptr<Event>> events;
    std::shared_ptr<NativeTargetState> state;
    std::shared_ptr<CommandOwner> owner;
};
bool prepare_native_targets(Device*,CmdBuf*,const DrawRec&,const efmi::assets::FilePlan&,NativeTargetPlan&,std::string&,NativeTargetCache* = nullptr);
void record_native_target(Device*,CmdBuf*,const NativeTargetPlan&,size_t);
void native_targets_end(Device*,CmdBuf*);
}
