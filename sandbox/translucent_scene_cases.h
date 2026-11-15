// Capture references survive later output changes and the application's destruction requests.
#include "../layer/src/translucency.h"
namespace {
std::vector<int> scene_release_order;
VKAPI_ATTR void VKAPI_CALL scene_destroy_image(VkDevice, VkImage image, const VkAllocationCallbacks*) {
    scene_release_order.push_back(int(uintptr_t(image)));
}
VKAPI_ATTR void VKAPI_CALL scene_destroy_view(VkDevice, VkImageView view, const VkAllocationCallbacks*) {
    scene_release_order.push_back(int(uintptr_t(view)));
}
}
static void translucent_scene_cases()
{
    Device device; std::unique_lock<std::shared_mutex> lock(device.res_mx);
    auto framebuffer = std::make_shared<FramebufferInfo>();
    framebuffer->known = true; framebuffer->layers = 1; framebuffer->attachment_count = 2;
    auto recipe = std::make_shared<RenderPassRecipe>(); recipe->complete = true;
    recipe->attachments.resize(2); recipe->subpasses.resize(1);
    auto& sub = recipe->subpasses[0]; sub.has_depth = true;
    sub.depth = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2}; sub.depth.attachment = 0;
    VkAttachmentReference2 colour{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2}; colour.attachment = 1;
    sub.colors.push_back(colour);
    scene_release_order.clear();
    for (unsigned i = 0; i < 2; ++i) {
        const auto image_handle = h<VkImage>(901 + i * 2);
        const auto view_handle = h<VkImageView>(902 + i * 2);
        ImageInfo image; image.id = 41 + i; image.capture_safe = true;
        image.format = i ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_D32_SFLOAT_S8_UINT;
        image.extent = {64, 64, 1}; image.levels = image.layers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (i ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        image.lifetime = std::make_shared<ImageLifetime>(); image.lifetime->handle = image_handle; image.lifetime->destroy = scene_destroy_image;
        image.lifetime->dependencies.emplace_back(new int(-int(image.id)), [](int* value) { scene_release_order.push_back(*value); delete value; });
        ImageViewInfo view; view.id = 51 + i; view.image_id = image.id; view.image = image_handle;
        view.native_safe = true; view.type = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
        view.range = {VkImageAspectFlags(i ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        view.lifetime = std::make_shared<ImageViewLifetime>(); view.lifetime->handle = view_handle; view.lifetime->destroy = scene_destroy_view;
        view.lifetime->dependencies.push_back(image.lifetime);
        device.images.emplace(image_handle, image); device.image_views.emplace(view_handle, view);
        TargetView target; target.known = true; target.handle = view_handle; target.image = image; target.view = view;
        target.image.lifetime.reset(); target.view.lifetime.reset(); framebuffer->views.push_back(std::move(target));
    }
    RenderTargets targets; targets.known = true; targets.framebuffer = framebuffer; targets.recipe = recipe;
    std::string why;
    auto retained = translucent_scene(&device, targets, why);
    check(retained && retained->has_depth && retained->colour.image.id == 42 && retained->depth.image.id == 41 &&
          retained->depth.view.range.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
          "Capture snapshot resolves subpass slots and retains both depth/stencil aspects");
    sub.depth.attachment = VK_ATTACHMENT_UNUSED;
    auto absent = translucent_scene(&device, targets, why);
    check(absent && !absent->has_depth && retained && retained->has_depth && retained->depth.image.id == 41,
          "A later null output leaves an earlier capture's retained scene unchanged");
    absent.reset(); sub.depth.attachment = 0;
    auto& colour_view = device.image_views.at(h<VkImageView>(904));
    ++colour_view.id;
    check(!translucent_scene(&device, targets, why), "Capture rejects a recycled view handle with a different identity");
    --colour_view.id; colour_view.range.baseMipLevel = 1;
    check(!translucent_scene(&device, targets, why), "Capture rejects subresources its compositor cannot address");
    colour_view.range.baseMipLevel = 0;
    auto owner = colour_view.lifetime; colour_view.lifetime.reset();
    check(!translucent_scene(&device, targets, why), "Capture cannot retain a view without an allocation owner");
    colour_view.lifetime = std::move(owner);
    for (auto& pair : device.image_views) pair.second.lifetime->requested = true;
    for (auto& pair : device.images) pair.second.lifetime->requested = true;
    device.image_views.clear(); device.images.clear();
    check(scene_release_order.empty(), "Application destruction requests wait for the capture's retained references");
    retained.reset();
    auto at = [](int value) { return std::find(scene_release_order.begin(), scene_release_order.end(), value) - scene_release_order.begin(); };
    check(scene_release_order.size() == 6 && at(902) < at(901) && at(901) < at(-41) && at(904) < at(903) && at(903) < at(-42),
          "Releasing capture owners destroys views before images and images before allocations");
}
