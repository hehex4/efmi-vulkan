#pragma once
#include "native_state.h"
namespace efvk {
// Direct compute ordering for original descriptors or private published resources.
void native_compute_order(Device*, CmdBuf*, PFN_vkCmdPipelineBarrier);
void native_images_observe(Device*, CmdBuf*, const DrawRec&);
void native_images_observe_locked(Device*, CmdBuf*, const DrawRec&);
bool native_image_binding(Device*, CmdBuf*, const DrawRec&, const efmi::frontend::NativeBinding&,
                          const std::shared_ptr<const efmi::frontend::NativeResource>&,
                          NativeImagePlan&, VkImageLayout&, std::string&);
void record_native_images(Device*, CmdBuf*, const NativeImagePlan&);
void native_images_begin(Device*, CmdBuf*, const VkRenderPassBeginInfo*);
void native_images_begin(Device*, CmdBuf*, const VkRenderingInfo*);
void native_images_call(Device*, CmdBuf*, bool compute);
void native_images_end(Device*, CmdBuf*);
}
