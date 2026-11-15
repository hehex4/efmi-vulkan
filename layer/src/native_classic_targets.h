// Classic pass preparation. Shared object layouts are defined in native_targets.h.
#pragma once
#include "native_targets.h"
#include "pipeline_variant.h"
namespace efvk {
namespace {
VkImageAspectFlags depth_aspects(VkFormat);
bool classic_recipe(const RenderPassRecipe& recipe){
    if(!recipe.complete || recipe.flags || recipe.attachments.size()>9 || recipe.subpasses.size()!=1 || !recipe.correlated.empty())return false;
    const auto& sub=recipe.subpasses[0];
    if(sub.flags || sub.point!=VK_PIPELINE_BIND_POINT_GRAPHICS || sub.view_mask || sub.colors.size()>8 ||
       !sub.inputs.empty() || !sub.resolves.empty() || !sub.preserves.empty())return false;
    std::vector<bool> used(recipe.attachments.size());
    auto check=[&](const VkAttachmentReference2& ref,bool depth){
        if(ref.attachment==VK_ATTACHMENT_UNUSED)return true;
        if(ref.attachment>=used.size() || used[ref.attachment])return false;used[ref.attachment]=true;
        const auto& a=recipe.attachments[ref.attachment];const auto aspects=depth_aspects(a.format);
        return !a.flags && a.samples==VK_SAMPLE_COUNT_1_BIT &&
            (depth?(aspects && ref.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                (!(aspects&VK_IMAGE_ASPECT_DEPTH_BIT) || a.storeOp==VK_ATTACHMENT_STORE_OP_STORE) &&
                (!(aspects&VK_IMAGE_ASPECT_STENCIL_BIT) || a.stencilStoreOp==VK_ATTACHMENT_STORE_OP_STORE)):
                (!aspects && ref.layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && a.storeOp==VK_ATTACHMENT_STORE_OP_STORE));
    };
    for(const auto& ref:sub.colors)if(!check(ref,false))return false;
    if(sub.has_depth && !check(sub.depth,true))return false;
    return std::all_of(used.begin(),used.end(),[](bool value){return value;});
}
VkImageLayout classic_work_layout(const RenderPassRecipe& recipe,size_t index){
    return depth_aspects(recipe.attachments[index].format)?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}
std::vector<VkFormat> classic_color_formats(const RenderPassRecipe& recipe){
    std::vector<VkFormat> formats;for(const auto& ref:recipe.subpasses[0].colors)
        formats.push_back(ref.attachment==VK_ATTACHMENT_UNUSED?VK_FORMAT_UNDEFINED:recipe.attachments[ref.attachment].format);return formats;
}
VkFormat classic_depth_format(const RenderPassRecipe& recipe,VkImageAspectFlags aspect){
    const auto& sub=recipe.subpasses[0];if(!sub.has_depth || sub.depth.attachment==VK_ATTACHMENT_UNUSED)return VK_FORMAT_UNDEFINED;
    const auto format=recipe.attachments[sub.depth.attachment].format;return depth_aspects(format)&aspect?format:VK_FORMAT_UNDEFINED;
}
bool classic_context(RenderTargets& targets,std::string& why){
    if(!targets.known || !targets.recipe || !classic_recipe(*targets.recipe) || targets.subpass ||
       !targets.framebuffer || !targets.framebuffer->known || targets.framebuffer->layers!=1 ||
       targets.framebuffer->attachment_count!=targets.recipe->attachments.size()){
        why="Native classic outputs require stored single-sample attachments in one ordinary subpass";return false;
    }
    auto views=targets.framebuffer->imageless?targets.views:targets.framebuffer->views;
    if(views.size()!=targets.recipe->attachments.size()){why="Native classic output actual views are unavailable";return false;}
    for(size_t i=0;i<views.size();++i){auto& view=views[i];auto& range=view.view.range;const auto aspects=depth_aspects(view.view.format);
        if(!view.known || !view.handle || !view.image.capture_safe || view.image.type!=VK_IMAGE_TYPE_2D ||
           view.view.format!=targets.recipe->attachments[i].format || (aspects && view.view.format!=view.image.format) || (!aspects && range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)){
            why="Native classic output actual attachment is unavailable";return false;
        }
        if(range.levelCount==VK_REMAINING_MIP_LEVELS && range.baseMipLevel<view.image.levels)range.levelCount=view.image.levels-range.baseMipLevel;
        if(range.layerCount==VK_REMAINING_ARRAY_LAYERS && range.baseArrayLayer<view.image.layers)range.layerCount=view.image.layers-range.baseArrayLayer;
        if(range.levelCount!=1 || range.layerCount!=1){why="Native classic output subresources need a separate adapter";return false;}
        if(aspects)range.aspectMask=aspects;
    }
    // Only local validation is canonicalized. Published contexts stay actual classic framebuffers.
    targets.dynamic=true;targets.layers=1;targets.views.clear();targets.dynamic_colors.clear();
    const auto& sub=targets.recipe->subpasses[0];
    auto attachment=[&](const VkAttachmentReference2& ref){
        VkRenderingAttachmentInfo out{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};out.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;out.storeOp=VK_ATTACHMENT_STORE_OP_STORE;out.imageLayout=ref.layout;
        if(ref.attachment!=VK_ATTACHMENT_UNUSED){out.imageView=views[ref.attachment].handle;targets.views.push_back(views[ref.attachment]);}
        else targets.views.push_back({});targets.views.push_back({});return out;
    };
    for(const auto& ref:sub.colors)targets.dynamic_colors.push_back(attachment(ref));
    targets.has_depth=classic_depth_format(*targets.recipe,VK_IMAGE_ASPECT_DEPTH_BIT)!=VK_FORMAT_UNDEFINED;
    targets.has_stencil=classic_depth_format(*targets.recipe,VK_IMAGE_ASPECT_STENCIL_BIT)!=VK_FORMAT_UNDEFINED;
    if(targets.has_depth)targets.depth=attachment(sub.depth);if(targets.has_stencil)targets.stencil=attachment(sub.depth);return true;
}
std::vector<TargetView> classic_views(const RenderTargets& targets,const RenderPassRecipe* mapping=nullptr){
    const auto& recipe=mapping?*mapping:*targets.recipe;
    std::vector<TargetView> views(recipe.attachments.size());const auto& sub=recipe.subpasses[0];
    for(size_t i=0;i<sub.colors.size();++i)if(sub.colors[i].attachment!=VK_ATTACHMENT_UNUSED)views[sub.colors[i].attachment]=targets.views[2*i];
    if(sub.has_depth && sub.depth.attachment!=VK_ATTACHMENT_UNUSED && (targets.has_depth || targets.has_stencil))
        views[sub.depth.attachment]=targets.views[2*sub.colors.size()];return views;
}
std::shared_ptr<NativeClassicTarget> classic_target(Device* device,NativeTargetCache& cache,
    const std::shared_ptr<const RenderPassRecipe>& original,const std::vector<TargetView>& views,VkRect2D area,std::string& why){
    std::vector<std::shared_ptr<ImageViewLifetime>> keep;
    {
        std::shared_lock<std::shared_mutex> lock(device->res_mx);
        for(const auto& view:views){
            if(!view.handle)continue;
            const auto live=device->image_views.find(view.handle);const auto image=device->images.find(view.view.image);
            if(live==device->image_views.end() || live->second.id!=view.view.id || live->second.image_id!=view.image.id ||
               image==device->images.end() || image->second.id!=view.image.id || !live->second.lifetime){
                why="Native classic framebuffer attachment lifetime or identity is unavailable";return {};
            }
            // Preserved originals remain inside the application's acquired/render-pass scope.
            // Their view alone may be retained; this never turns a swapchain image into a transferable resource.
            keep.push_back(live->second.lifetime);
        }
    }
    std::lock_guard<std::mutex> lock(cache.mutex);
    for(const auto& entry:cache.classic){
        if(entry->original!=original || entry->bindings.size()!=views.size() ||
           entry->info->width<uint64_t(area.offset.x)+area.extent.width || entry->info->height<uint64_t(area.offset.y)+area.extent.height)continue;
        bool same=true;for(size_t i=0;i<views.size();++i){const auto& previous=entry->bindings[i];
            same&=previous.view.id==views[i].view.id && previous.image.id==views[i].image.id && previous.handle==views[i].handle;}
        if(same)return entry;
    }
    auto entry=std::make_shared<NativeClassicTarget>();entry->device=device->handle;
    entry->destroy_framebuffer=device->d.DestroyFramebuffer;entry->destroy_pass=device->d.DestroyRenderPass;
    entry->original=original;entry->views=std::move(keep);entry->bindings=views;
    auto recipe=std::make_shared<RenderPassRecipe>(*original);
    // Unused descriptions still perform initial/final transitions. Remove them entirely,
    // retaining slot contracts in NativeTargetState and remapping only active references.
    recipe->attachments.clear();std::vector<uint32_t> remap(views.size(),VK_ATTACHMENT_UNUSED);
    std::vector<TargetView> active;
    for(size_t i=0;i<views.size();++i)if(views[i].handle){
        remap[i]=uint32_t(active.size());active.push_back(views[i]);recipe->attachments.push_back(original->attachments[i]);
    }
    auto relocate=[&](VkAttachmentReference2& ref){if(ref.attachment!=VK_ATTACHMENT_UNUSED)ref.attachment=remap[ref.attachment];};
    for(auto& ref:recipe->subpasses[0].colors)relocate(ref);
    if(recipe->subpasses[0].has_depth)relocate(recipe->subpasses[0].depth);
    for(size_t i=0;i<recipe->attachments.size();++i){auto& attachment=recipe->attachments[i];const auto aspects=depth_aspects(attachment.format);
        if(!aspects || (aspects&VK_IMAGE_ASPECT_DEPTH_BIT))attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        if(aspects&VK_IMAGE_ASPECT_STENCIL_BIT)attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.initialLayout=attachment.finalLayout=classic_work_layout(*recipe,i);
    }
    const auto create2=device->d.CreateRenderPass2?device->d.CreateRenderPass2:device->d.CreateRenderPass2KHR;
    if((recipe->version2?!create2:!device->d.CreateRenderPass) ||
       recipe->create(device->handle,device->d.CreateRenderPass,create2,&entry->pass)!=VK_SUCCESS){
        why="Native classic continuation render pass creation failed";return {};
    }
    auto info=std::make_shared<FramebufferInfo>();info->known=true;info->layers=1;info->attachment_count=uint32_t(active.size());
    info->width=active.empty()?uint32_t(area.offset.x)+area.extent.width:UINT32_MAX;
    info->height=active.empty()?uint32_t(area.offset.y)+area.extent.height:UINT32_MAX;info->views=active;std::vector<VkImageView> handles;
    for(const auto& view:active){handles.push_back(view.handle);
        info->width=std::min(info->width,std::max(1u,view.image.extent.width>>std::min(view.view.range.baseMipLevel,31u)));
        info->height=std::min(info->height,std::max(1u,view.image.extent.height>>std::min(view.view.range.baseMipLevel,31u)));}
    VkFramebufferCreateInfo create{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};create.renderPass=entry->pass;
    create.attachmentCount=uint32_t(handles.size());create.pAttachments=handles.data();create.width=info->width;create.height=info->height;create.layers=1;
    if(device->d.CreateFramebuffer(device->handle,&create,nullptr,&entry->framebuffer)!=VK_SUCCESS){
        why="Native classic continuation framebuffer creation failed";return {};
    }
    entry->recipe=std::move(recipe);entry->info=std::move(info);
    if(cache.classic.size()>=64)cache.classic.erase(cache.classic.begin());cache.classic.push_back(entry);return entry;
}
uint32_t classic_mask(const RenderPassRecipe& recipe){
    uint32_t mask=0;const auto& sub=recipe.subpasses[0];
    for(size_t i=0;i<sub.colors.size();++i)if(sub.colors[i].attachment!=VK_ATTACHMENT_UNUSED)mask|=1u<<i;
    if(sub.has_depth && sub.depth.attachment!=VK_ATTACHMENT_UNUSED)mask|=1u<<8;return mask;
}
std::shared_ptr<const NativeBoundPipeline> classic_pipeline(Device* device,NativeTargetState& state,
    const NativePipelineSelection& source,const std::shared_ptr<const RenderPassRecipe>& pass,std::string& why){
    if(!source.recipe || !source.layout || !source.recipe->geometry_complete || source.recipe->dynamic_rendering ||
       !source.recipe->render_pass || source.recipe->subpass || !classic_recipe(*source.recipe->render_pass) ||
       classic_color_formats(*source.recipe->render_pass)!=classic_color_formats(*state.classic_recipe) ||
       classic_depth_format(*source.recipe->render_pass,VK_IMAGE_ASPECT_DEPTH_BIT)!=state.depth.format ||
       classic_depth_format(*source.recipe->render_pass,VK_IMAGE_ASPECT_STENCIL_BIT)!=state.stencil_format){
        why="Native classic selected pipeline contract is unavailable";return {};
    }
    if(!state.compatibility)state.compatibility=std::make_shared<NativePipelineCache>();
    auto& cache=*state.compatibility;std::lock_guard<std::mutex> lock(cache.mutex);
    for(const auto& entry:cache.pipelines)if(entry->source==source.recipe && entry->layout==source.layout && entry->pass==pass)return entry->variant;
    VariantDispatch funcs;
#define X(n) funcs.n=device->d.n;
    EFVK_VARIANT_FUNCS(X)
#undef X
    if(!funcs.CreateShaderModule || !funcs.DestroyShaderModule || !funcs.CreatePipelineLayout || !funcs.DestroyPipelineLayout ||
       !funcs.CreateDescriptorSetLayout || !funcs.DestroyDescriptorSetLayout || !funcs.CreateGraphicsPipelines || !funcs.DestroyPipeline || !funcs.DestroyRenderPass){
        why="Native classic compatible pipeline creation entries are unavailable";return {};
    }
    if(!funcs.CreateRenderPass2)funcs.CreateRenderPass2=device->d.CreateRenderPass2KHR;
    auto recipe=*source.recipe;recipe.render_pass=pass;
    auto variant=std::make_shared<PipelineVariant>(device->handle,funcs);
    FragmentPatch patch;patch.keep_vertex=patch.native_only=true;
    if(!variant->build(recipe,*source.layout,recipe.vertex,device->physical_properties.limits.maxBoundDescriptorSets,patch,device->physical_properties.limits)){
        why="Native classic compatible pipeline creation failed: "+variant->reason;return {};
    }
    auto binding=std::make_shared<NativeBoundPipeline>();binding->pipeline=variant->pipeline;binding->owner=variant;
    auto entry=std::make_shared<NativeClassicPipeline>();entry->source=source.recipe;entry->layout=source.layout;entry->pass=pass;entry->variant=binding;
    if(cache.pipelines.size()>=64)cache.pipelines.erase(cache.pipelines.begin());cache.pipelines.push_back(entry);return binding;
}
}
}
