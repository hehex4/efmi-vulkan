#pragma once
#include "state.h"
#include "command_owner.h"
#include "../../frontend/assets/file_plan.h"
namespace efvk {
inline bool native_output_binding(const efmi::frontend::NativeBinding& binding){
    return binding.kind==efmi::frontend::NativeBinding::ColorTarget || binding.kind==efmi::frontend::NativeBinding::DepthTarget;
}
// Complete shared layout: the host finishes a scope that an exchanged module began.
struct NativeTargetState {
    struct Saved {TargetView view;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;std::shared_ptr<const efmi::frontend::NativeResource> resource;};
    VkImage original_image=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_UNDEFINED; // Compatibility survives a temporarily unbound actual output.
    VkImageView original=VK_NULL_HANDLE, active=VK_NULL_HANDLE;
    std::map<VkImageView,Saved> restore;
};
struct NativeTargetPlan {
    struct Event {RenderTargets targets;std::shared_ptr<const efmi::frontend::NativeResource> resource;std::shared_ptr<NativeImageState> images;bool change=false;};
    std::vector<std::shared_ptr<Event>> events;
    std::shared_ptr<NativeTargetState> state;
    std::shared_ptr<CommandOwner> owner;
};
bool prepare_native_targets(Device*,CmdBuf*,const DrawRec&,const efmi::assets::FilePlan&,NativeTargetPlan&,std::string&);
void record_native_target(Device*,CmdBuf*,const NativeTargetPlan&,size_t);
void native_targets_end(Device*,CmdBuf*);
}
