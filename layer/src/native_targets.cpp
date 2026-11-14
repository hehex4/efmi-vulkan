#include "native_targets.h"
#include "native_images.h"
namespace efvk {
namespace {
using Resource=efmi::frontend::NativeResource;
const char target_owner_key=0;
struct TargetOwner:CommandOwner {
    std::vector<std::shared_ptr<const Resource>> resources;
    bool sealed=false;
    void reset_after_completion()override{resources.clear();used=false;sealed=false;}
    bool seal()override{if(!used || sealed)return false;sealed=true;return true;}
};
bool absent_output(const CmdBuf* cb,const efmi::frontend::NativeBinding& binding){
    const bool depth=binding.kind==efmi::frontend::NativeBinding::DepthTarget;
    if((depth && binding.slot) || (!depth && (binding.kind!=efmi::frontend::NativeBinding::ColorTarget || binding.slot>=8)))return false;
    if(cb->inherited_rp)return false;
    if(!cb->in_rp)return !cb->active_targets;
    if(!cb->execution_scopes.known || !cb->active_targets || cb->active_targets>cb->target_contexts.size())return false;
    const auto& targets=cb->target_contexts[cb->active_targets-1];if(!targets.known)return false;
    if(targets.dynamic){
        if(targets.dynamic_colors.size()>8 || targets.views.size()!=2*(targets.dynamic_colors.size()+targets.has_depth+targets.has_stencil))return false;
        // External-format downsample can use a null source while the resolve target remains active.
        for(const auto& color:targets.dynamic_colors)if(color.resolveMode==VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID)return false;
        if(depth)return !(targets.has_depth && targets.depth.imageView) && !(targets.has_stencil && targets.stencil.imageView);
        return binding.slot>=targets.dynamic_colors.size() || !targets.dynamic_colors[binding.slot].imageView;
    }
    if(!targets.recipe || !targets.recipe->complete || targets.subpass>=targets.recipe->subpasses.size())return false;
    const auto& sub=targets.recipe->subpasses[targets.subpass];
    return depth?(!sub.has_depth || sub.depth.attachment==VK_ATTACHMENT_UNUSED):
        (binding.slot>=sub.colors.size() || sub.colors[binding.slot].attachment==VK_ATTACHMENT_UNUSED);
}
bool null_output(const efmi::assets::FilePlanItem& item){
    return !item.resource && (!item.native_resource || !item.native_resource->bound);
}

bool color_context(const RenderTargets& targets,const NativeTargetState* state,std::string& why){
    if(!targets.known || !targets.dynamic || targets.flags || targets.view_mask || targets.layers!=1 ||
       targets.has_stencil || targets.dynamic_colors.size()!=1 || targets.views.size()!=2+2*size_t(targets.has_depth)){
        why="Native output destinations need an ordinary single-color dynamic pass";return false;
    }
    const auto& color=targets.dynamic_colors[0];const auto& view=targets.views[0];
    const bool known_view=color.imageView?(view.known && color.imageView==view.handle && view.image.samples==VK_SAMPLE_COUNT_1_BIT):
        (state && state->original && state->format!=VK_FORMAT_UNDEFINED);
    if(!known_view || color.resolveMode!=VK_RESOLVE_MODE_NONE || color.storeOp!=VK_ATTACHMENT_STORE_OP_STORE ||
       color.imageLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || targets.area.offset.x<0 || targets.area.offset.y<0){
        why="Native output pass must retain single-sample stored color contents without resolve";return false;
    }
    if(targets.has_depth && targets.depth.imageView){
        const auto& depth=targets.depth;const auto& actual=targets.views[2];
        if(!actual.known || actual.handle!=depth.imageView || actual.image.samples!=VK_SAMPLE_COUNT_1_BIT ||
           depth.storeOp!=VK_ATTACHMENT_STORE_OP_STORE || depth.resolveMode!=VK_RESOLVE_MODE_NONE ||
           depth.imageLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL){
            why="Native output preserved depth needs a known stored single-sample attachment without resolve";return false;
        }
    }
    return true;
}
void continue_targets(RenderTargets& targets){
    targets.dynamic_colors[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    if(targets.has_depth && targets.depth.imageView)targets.depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
}
std::shared_ptr<NativeImageState> target_images(const CmdBuf* cb,const RenderTargets& targets){
    if(!cb->native_image_state)return {};
    auto images=std::make_shared<NativeImageState>(*cb->native_image_state);images->attachments.clear();images->attachments_known=true;
    if(targets.dynamic_colors[0].imageView)images->attachments.insert(targets.views[0].image.id);
    if(targets.has_depth && targets.depth.imageView)images->attachments.insert(targets.views[2].image.id);
    return images;
}
VkImageMemoryBarrier transition(const NativeTargetState::Saved& saved,VkImageLayout before,VkImageLayout after){
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=saved.view.view.image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout=before;barrier.newLayout=after;barrier.subresourceRange=saved.view.view.range;
    barrier.subresourceRange.levelCount=barrier.subresourceRange.layerCount=1;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;return barrier;
}
void layout(CmdBuf* cb,const NativeTargetState::Saved& saved,VkImageLayout value){
    auto range=saved.view.view.range;range.levelCount=range.layerCount=1;
    cb->image_layouts.set(saved.view.image.id,saved.view.image.levels,saved.view.image.layers,range,value);
}
bool descriptor_conflict(Device* device,CmdBuf* cb,VkImage image,std::string& why){
    for(const auto& bound:cb->sets){
        if(!bound.set && !bound.pushed)continue;
        const DescriptorSetInfo* contents=bound.pushed.get();
        const auto found=device->descriptor_sets.find(bound.set);
        if(!contents && found!=device->descriptor_sets.end() && found->second.id==bound.id)contents=&found->second;
        if(!contents || bound.unsupported || contents->unsupported){why="Native output descriptor alias context is unavailable";return true;}
        for(const auto& value:contents->values){
            if(!value.second.image_view)continue;
            if(value.second.native){
                const auto& resource=*std::static_pointer_cast<const Resource>(value.second.native);
                if(resource.image==uint64_t(image)){why="Native output destination overlaps a bound descriptor";return true;}
            }else{
                const auto view=device->image_views.find(value.second.image_view);
                if(!value.second.known || view==device->image_views.end() || view->second.id!=value.second.image_view_id){
                    why="Native output descriptor image identity is unavailable";return true;
                }
                if(view->second.image==image){why="Native output destination overlaps a bound descriptor";return true;}
            }
        }
    }
    return false;
}
}
bool prepare_native_targets(Device* device,CmdBuf* cb,const DrawRec& draw,const efmi::assets::FilePlan& plan,NativeTargetPlan& output,std::string& why)
{
    using Operation=efmi::assets::FileOperation;
    NativeTargetPlan next;next.events.resize(plan.items.size());bool needed=false,mixed=false;
    std::vector<bool> no_op(plan.items.size());uint32_t written=0;
    auto slot=[](const efmi::frontend::NativeBinding& binding){
        return binding.kind==efmi::frontend::NativeBinding::DepthTarget?(binding.slot?9u:8u):std::min(binding.slot,9u);
    };
    // An initially absent slot can become bound earlier in this same ordered plan.
    for(const auto& item:plan.items)if(item.operation==Operation::NativeBind && native_output_binding(item.native_target) && !null_output(item))
        written|=1u<<slot(item.native_target);
    for(size_t i=0;i<plan.items.size();++i){const auto& item=plan.items[i];if(item.operation!=Operation::NativeBind)continue;
        if(native_output_binding(item.native_target)){
            no_op[i]=null_output(item) && !(written&(1u<<slot(item.native_target))) && absent_output(cb,item.native_target);
            needed|=!no_op[i];
        }else mixed=true;
    }
    if(!needed){output=std::move(next);return true;}
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(mixed)return refuse("Mixed output and descriptor changes need sequential alias preparation");
    if(!cb->in_rp || cb->secondary || cb->inherited_rp || !cb->execution_scopes.splittable())
        return refuse("Native output pass splitting requires a primary with no active query or transform feedback");
    if(device->synchronization2_enabled || device->application_queue_count!=1 || device->material_queues.size()!=1)
        return refuse("Native output destinations require one application queue and legacy synchronization");
    if(cb->native_images || (cb->native_image_state && !cb->native_image_state->active.cells.empty()))
        return refuse("Native output changes with persistent image bindings need an alias restoration adapter");
    if(!cb->active_targets || cb->active_targets>cb->target_contexts.size())return refuse("Native output context is unavailable");
    auto targets=cb->target_contexts[cb->active_targets-1];if(!color_context(targets,cb->native_target_state.get(),why))return false;
    const auto end=device->d.CmdEndRendering?device->d.CmdEndRendering:device->d.CmdEndRenderingKHR;
    const auto begin=device->d.CmdBeginRendering?device->d.CmdBeginRendering:device->d.CmdBeginRenderingKHR;
    if(!begin || !end || !device->d.CmdPipelineBarrier)return refuse("Native output rendering entries are unavailable");
    if(cb->target_contexts.size()+plan.items.size()>4096)return refuse("Native output context budget exceeded");
    next.state=cb->native_target_state?std::make_shared<NativeTargetState>(*cb->native_target_state):std::make_shared<NativeTargetState>();
    if(!next.state->original){next.state->original=targets.dynamic_colors[0].imageView;next.state->original_image=targets.views[0].view.image;next.state->format=targets.views[0].view.format;}
    const auto& proof=cb->image_layouts;
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto pipeline=device->pipes.find(draw.pipeline);
    const auto recipe=pipeline==device->pipes.end()?nullptr:pipeline->second.recipe;
    if(!recipe || !recipe->geometry_complete || !recipe->dynamic_rendering || recipe->rendering_view_mask ||
       recipe->rendering_colors.size()!=1 || recipe->rendering_colors[0]!=next.state->format ||
       recipe->rendering_stencil!=VK_FORMAT_UNDEFINED)
        return refuse("Native output pipeline attachment contract is unavailable");
    const bool depth_bound=targets.has_depth && targets.depth.imageView;
    if(depth_bound?recipe->rendering_depth!=targets.views[2].view.format:
       (recipe->rendering_depth!=VK_FORMAT_UNDEFINED && !device->dynamic_unused_attachments))
        return refuse("Native output pipeline depth contract is unavailable");
    for(size_t i=0;i<plan.items.size();++i){
        const auto& item=plan.items[i];if(item.operation!=Operation::NativeBind || no_op[i])continue;
        const auto& resource=item.native_resource;
        const bool empty=!resource || !resource->bound;
        const auto& binding=item.native_target;
        if(binding.kind!=efmi::frontend::NativeBinding::ColorTarget || binding.slot!=0){
            // Only entries proven absent in this complete context are original built-in no-ops.
            const bool absent=(binding.kind==efmi::frontend::NativeBinding::ColorTarget && binding.slot>0 && binding.slot<8) ||
                              (binding.kind==efmi::frontend::NativeBinding::DepthTarget && binding.slot==0 && !depth_bound);
            if(empty && !item.resource && absent)continue;
            return refuse("Native output destination slot needs a separate pass adapter");
        }
        if(empty && !item.resource){
            if(targets.dynamic_colors[0].imageView && !device->dynamic_unused_attachments)
                return refuse("Native null outputs require the application's enabled unused attachments feature");
            auto event=std::make_shared<NativeTargetPlan::Event>();event->change=targets.dynamic_colors[0].imageView!=VK_NULL_HANDLE;
            targets.dynamic_colors[0].imageView=VK_NULL_HANDLE;continue_targets(targets);
            targets.views[0]=TargetView{};targets.views[1]=TargetView{};
            event->images=target_images(cb,targets);
            event->targets=targets;next.events[i]=std::move(event);continue;
        }
        if(!resource || !resource->bound || !resource->keep || !resource->image_safe || !resource->image_view ||
           resource->uploaded_readonly || resource->uploaded_mutable || !(resource->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            return refuse("Native output destination needs a retained owned application color view");
        const auto view=device->image_views.find(VkImageView(resource->image_view));
        if(view==device->image_views.end() || view->second.id!=resource->image_view_id)return refuse("Native output destination view was replaced");
        const auto image=device->images.find(view->second.image);
        if(image==device->images.end() || image->second.id!=view->second.image_id || resource->identity!=((uint64_t(image->second.id)<<2)|2))
            return refuse("Native output destination image was replaced");
        auto range=view->second.range;
        if(range.levelCount==VK_REMAINING_MIP_LEVELS && range.baseMipLevel<image->second.levels)range.levelCount=image->second.levels-range.baseMipLevel;
        if(range.layerCount==VK_REMAINING_ARRAY_LAYERS && range.baseArrayLayer<image->second.layers)range.layerCount=image->second.layers-range.baseArrayLayer;
        if(range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT || range.levelCount!=1 || range.layerCount!=1 ||
           image->second.type!=VK_IMAGE_TYPE_2D || image->second.samples!=VK_SAMPLE_COUNT_1_BIT ||
           view->second.format!=next.state->format || (view->second.type!=VK_IMAGE_VIEW_TYPE_2D && view->second.type!=VK_IMAGE_VIEW_TYPE_2D_ARRAY))
            return refuse("Native output destination format or subresources differ from the rendering contract");
        const auto width=std::max(1u,image->second.extent.width>>std::min(range.baseMipLevel,31u));
        const auto height=std::max(1u,image->second.extent.height>>std::min(range.baseMipLevel,31u));
        if(uint64_t(targets.area.offset.x)+targets.area.extent.width>width || uint64_t(targets.area.offset.y)+targets.area.extent.height>height)
            return refuse("Native output destination does not cover the render area");
        if(descriptor_conflict(device,cb,view->second.image,why))return false;
        if(depth_bound && view->second.image==targets.views[2].view.image)
            return refuse("Native output color destination aliases the preserved depth image");
        const auto handle=view->first;
        if(handle!=next.state->original && view->second.image==next.state->original_image)
            return refuse("Native output aliases of the original image need subresource transition planning");
        for(const auto& saved:next.state->restore)if(saved.first!=handle && saved.second.view.view.image==view->second.image)
            return refuse("Native output view aliases need subresource transition planning");
        if(handle!=next.state->original && !next.state->restore.count(handle)){
            VkImageLayout before;
            if(!proof.get(image->second.id,image->second.levels,image->second.layers,range,before) ||
               (before!=VK_IMAGE_LAYOUT_GENERAL && before!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && before!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && before!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
                return refuse("Native output destination layout is not proven in this recording");
            NativeTargetState::Saved saved;saved.view.handle=handle;saved.view.view=view->second;saved.view.image=image->second;saved.view.known=true;
            saved.view.view.lifetime.reset();saved.view.image.lifetime.reset();saved.view.view.range=range;
            saved.layout=before;saved.resource=resource;next.state->restore.emplace(handle,std::move(saved));
        }
        auto event=std::make_shared<NativeTargetPlan::Event>();event->resource=resource;
        event->change=handle!=targets.dynamic_colors[0].imageView;
        targets.dynamic_colors[0].imageView=handle;continue_targets(targets);
        targets.views[0].handle=handle;targets.views[0].view=view->second;targets.views[0].image=image->second;targets.views[0].known=true;
        targets.views[0].view.lifetime.reset();targets.views[0].image.lifetime.reset();targets.views[0].view.range=range;
        event->images=target_images(cb,targets);
        event->targets=targets;next.events[i]=std::move(event);
    }
    lock.unlock();
    auto owner=std::static_pointer_cast<TargetOwner>(owner_for(cb->owners,&target_owner_key,[]{auto owner=std::make_shared<TargetOwner>();owner->backend=&target_owner_key;return owner;}));
    if(owner->sealed || owner->resources.size()+plan.items.size()>4096)return refuse("Native output retention budget or sealed recording");
    owner->resources.reserve(owner->resources.size()+plan.items.size());cb->target_contexts.reserve(cb->target_contexts.size()+plan.items.size());
    next.owner=owner;output=std::move(next);return true;
}
void record_native_target(Device* device,CmdBuf* cb,const NativeTargetPlan& plan,size_t index)
{
    const auto& event=plan.events[index];if(!event)return;
    auto owner=std::static_pointer_cast<TargetOwner>(plan.owner);owner->used=true;owner->resources.push_back(event->resource);
    cb->native_target_state=plan.state;if(!event->change)return;
    const auto end=device->d.CmdEndRendering?device->d.CmdEndRendering:device->d.CmdEndRenderingKHR;
    const auto begin=device->d.CmdBeginRendering?device->d.CmdBeginRendering:device->d.CmdBeginRenderingKHR;
    end(cb->handle);
    VkImageMemoryBarrier barriers[2]{};uint32_t barrier_count=0;
    auto& state=*plan.state;
    if(state.active){const auto& saved=state.restore.at(state.active);barriers[barrier_count++]=transition(saved,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,saved.layout);layout(cb,saved,saved.layout);}
    const auto target=event->targets.dynamic_colors[0].imageView;state.active=target && target!=state.original?target:VK_NULL_HANDLE;
    if(state.active){const auto& saved=state.restore.at(state.active);barriers[barrier_count++]=transition(saved,saved.layout,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);layout(cb,saved,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,barrier_count,barriers);
    cb->target_contexts.push_back(std::move(event->targets));cb->active_targets=uint32_t(cb->target_contexts.size());
    cb->native_image_state=event->images;const auto& targets=cb->target_contexts.back();
    cb->native_graphics_ordered=true;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea=targets.area;info.layerCount=1;
    info.colorAttachmentCount=1;info.pColorAttachments=targets.dynamic_colors.data();
    info.pDepthAttachment=targets.has_depth?&targets.depth:nullptr;begin(cb->handle,&info);
}
void native_targets_end(Device* device,CmdBuf* cb)
{
    auto state=std::move(cb->native_target_state);if(!state || !state->active)return;
    const auto& saved=state->restore.at(state->active);auto barrier=transition(saved,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,saved.layout);
    device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    layout(cb,saved,saved.layout);
}
}
