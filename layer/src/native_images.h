#pragma once
#include "state.h"
#include "../../frontend/xxmi/native_bindings.h"
#include <set>

namespace efvk {
// This cache is shared by the host and an exchanged backend, so its complete layout is stamped.
struct NativeImageCache {
    struct Resource {
        uint32_t set = 0, binding = 0, dim = 0, depth = 0;
        bool sampled = false, arrayed = false, multisampled = false, sampled_float32 = false;
        uint32_t scalar_type = 0, image_format = 0;
    };
    struct Use { Resource resource; bool depth_reference = false; };
    struct Access { std::vector<Use> images; std::string reason; };
    std::map<uint32_t, std::shared_ptr<const Access>> pipelines;
};
struct NativeImagePlan {
    struct Cell {
        std::shared_ptr<const efmi::frontend::NativeResource> resource;
        uint32_t mip = 0, layer = 0;
        VkImageLayout before = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout after = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bool writable = false;
    };
    using Key = std::tuple<uint64_t, uint32_t, uint32_t>;
    std::map<Key, Cell> cells;
    // Every descriptor's layout, including references requiring no transition.
    std::map<Key, VkImageLayout> layouts;
};
struct NativeImageState {
    bool compute = false, attachments_known = false;
    std::set<uint32_t> attachments;
    NativeImagePlan active;
    std::map<NativeImagePlan::Key, VkImageLayout> required;
};
void native_images_observe(Device*, CmdBuf*, const DrawRec&);
bool native_image_binding(Device*, CmdBuf*, const DrawRec&, const efmi::frontend::NativeBinding&,
                          const std::shared_ptr<const efmi::frontend::NativeResource>&,
                          NativeImagePlan&, VkImageLayout&, std::string&);
void record_native_images(Device*, CmdBuf*, const NativeImagePlan&);
void native_images_begin(Device*, CmdBuf*, const VkRenderPassBeginInfo*);
void native_images_begin(Device*, CmdBuf*, const VkRenderingInfo*);
void native_images_call(Device*, CmdBuf*, bool compute);
void native_images_end(Device*, CmdBuf*);
}
