// Private classic-pass objects. NativeTargetState is the complete host/module boundary.
#pragma once
#include "native_targets.h"
namespace efvk {
struct NativeClassicTarget {
    VkDevice device=VK_NULL_HANDLE;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    PFN_vkDestroyFramebuffer destroy_framebuffer=nullptr;
    PFN_vkDestroyRenderPass destroy_pass=nullptr;
    std::shared_ptr<const RenderPassRecipe> original,recipe;
    std::shared_ptr<const FramebufferInfo> info;
    std::shared_ptr<const efmi::frontend::NativeResource> resource;
    ~NativeClassicTarget(){
        if(framebuffer)destroy_framebuffer(device,framebuffer,nullptr);
        if(pass)destroy_pass(device,pass,nullptr);
    }
};
namespace {
bool classic_recipe(const RenderPassRecipe& recipe){
    if(!recipe.complete || recipe.flags || recipe.attachments.size()!=1 || recipe.subpasses.size()!=1 || !recipe.correlated.empty())return false;
    const auto& sub=recipe.subpasses[0];const auto& attachment=recipe.attachments[0];
    return !sub.flags && sub.point==VK_PIPELINE_BIND_POINT_GRAPHICS && !sub.view_mask && sub.colors.size()==1 &&
        sub.colors[0].attachment==0 && sub.colors[0].layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        sub.inputs.empty() && sub.resolves.empty() && !sub.has_depth && sub.preserves.empty() &&
        !attachment.flags && attachment.samples==VK_SAMPLE_COUNT_1_BIT && attachment.storeOp==VK_ATTACHMENT_STORE_OP_STORE;
}
bool classic_context(RenderTargets& targets,std::string& why){
    if(!targets.known || !targets.recipe || !classic_recipe(*targets.recipe) || targets.subpass ||
       !targets.framebuffer || !targets.framebuffer->known || targets.framebuffer->layers!=1 || targets.framebuffer->attachment_count!=1){
        why="Native classic outputs require one stored color attachment in one ordinary subpass";return false;
    }
    const auto& views=targets.framebuffer->imageless?targets.views:targets.framebuffer->views;
    if(views.size()!=1 || !views[0].known || !views[0].handle || !views[0].image.capture_safe ||
       views[0].image.type!=VK_IMAGE_TYPE_2D || views[0].view.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT ||
       views[0].view.format!=targets.recipe->attachments[0].format){
        why="Native classic output actual attachment is unavailable";return false;
    }
    auto view=views[0];auto& range=view.view.range;
    if(range.levelCount==VK_REMAINING_MIP_LEVELS && range.baseMipLevel<view.image.levels)range.levelCount=view.image.levels-range.baseMipLevel;
    if(range.layerCount==VK_REMAINING_ARRAY_LAYERS && range.baseArrayLayer<view.image.layers)range.layerCount=view.image.layers-range.baseArrayLayer;
    if(range.levelCount!=1 || range.layerCount!=1){why="Native classic output subresources need a separate adapter";return false;}
    // Only the local common validator uses this representation. Published contexts stay classic.
    targets.dynamic=true;targets.layers=1;targets.views={view,TargetView{}};
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=view.handle;
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    targets.dynamic_colors={color};return true;
}
std::shared_ptr<NativeClassicTarget> classic_target(Device* device,NativeTargetCache& cache,
    const std::shared_ptr<const RenderPassRecipe>& original,const TargetView& view,
    const std::shared_ptr<const efmi::frontend::NativeResource>& resource,std::string& why){
    std::lock_guard<std::mutex> lock(cache.mutex);
    for(const auto& entry:cache.classic)if(entry->original==original && entry->info->views[0].view.id==view.view.id &&
        entry->info->views[0].image.id==view.image.id && entry->info->views[0].handle==view.handle)return entry;
    auto entry=std::make_shared<NativeClassicTarget>();entry->device=device->handle;
    entry->destroy_framebuffer=device->d.DestroyFramebuffer;entry->destroy_pass=device->d.DestroyRenderPass;
    entry->original=original;entry->resource=resource;
    auto recipe=std::make_shared<RenderPassRecipe>(*original);auto& attachment=recipe->attachments[0];
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const auto create2=device->d.CreateRenderPass2?device->d.CreateRenderPass2:device->d.CreateRenderPass2KHR;
    if((recipe->version2?!create2:!device->d.CreateRenderPass) ||
       recipe->create(device->handle,device->d.CreateRenderPass,create2,&entry->pass)!=VK_SUCCESS){
        why="Native classic continuation render pass creation failed";return {};
    }
    auto info=std::make_shared<FramebufferInfo>();info->known=true;info->layers=info->attachment_count=1;
    info->width=std::max(1u,view.image.extent.width>>std::min(view.view.range.baseMipLevel,31u));
    info->height=std::max(1u,view.image.extent.height>>std::min(view.view.range.baseMipLevel,31u));info->views={view};
    VkFramebufferCreateInfo create{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};create.renderPass=entry->pass;
    create.attachmentCount=1;create.pAttachments=&view.handle;create.width=info->width;create.height=info->height;create.layers=1;
    if(device->d.CreateFramebuffer(device->handle,&create,nullptr,&entry->framebuffer)!=VK_SUCCESS){
        why="Native classic continuation framebuffer creation failed";return {};
    }
    entry->recipe=std::move(recipe);entry->info=std::move(info);
    // Eviction drops only the cache reference. Recorded owners retain objects until completion.
    if(cache.classic.size()>=64)cache.classic.erase(cache.classic.begin());cache.classic.push_back(entry);return entry;
}
}
}
