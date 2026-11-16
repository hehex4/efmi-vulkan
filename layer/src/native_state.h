// Recording and cache data that survive a replacement-module exchange.
// This is a shared protocol. Planner functions and pipeline implementations are private.
#pragma once
#include "state.h"
#include "../../frontend/xxmi/native_bindings.h"
#include <set>
namespace efvk {
// Only the Vulkan handle crosses the boundary. The opaque owner keeps its creating
// module's PipelineVariant and destructor alive without exposing its representation.
struct NativeBoundPipeline {
    VkPipeline pipeline=VK_NULL_HANDLE;
    std::shared_ptr<const void> owner;
};
struct NativeClassicPipeline {
    std::shared_ptr<const GraphicsRecipe> source;
    std::shared_ptr<const PipelineLayoutInfo> layout;
    std::shared_ptr<const RenderPassRecipe> pass;
    std::shared_ptr<const NativeBoundPipeline> variant;
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
// Complete shared layout: the host finishes a scope that an exchanged module began.
struct NativeTargetState {
    struct Saved {
        TargetView view;
        VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
        std::shared_ptr<const efmi::frontend::NativeResource> resource;
        // A proven detached sample phase uses GENERAL; scope end restores layout.
        bool readable_when_detached=false;
    };
    struct Color {
        VkImage original_image=VK_NULL_HANDLE;
        ImageInfo original_storage; // Original contents remain live through null/output transitions.
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
// This cache is shared by the host and an exchanged backend, so its complete layout is stamped.
struct NativeImageCache {
    struct Resource {
        uint32_t set = 0, binding = 0, dim = 0, depth = 0;
        bool sampled = false, arrayed = false, multisampled = false, sampled_float32 = false;
        uint32_t scalar_type = 0, image_format = 0;
    };
    struct Use { Resource resource; bool depth_reference = false, read = false, write = false, atomic = false; };
    struct Access { std::vector<Use> images; std::string reason; };
    std::map<uint32_t, std::shared_ptr<const Access>> pipelines;
    std::map<uint32_t, VkFormatFeatureFlags2> storage_formats;
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
}
