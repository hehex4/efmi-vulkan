#include "native_targets.h"
#include "native_images.h"
namespace efvk {
namespace {
using Resource=efmi::frontend::NativeResource;
const char target_owner_key=0;
constexpr uint32_t depth_slot=8; // The original DSV binding represents both depth and stencil.
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

VkImageAspectFlags depth_aspects(VkFormat format){
    switch(format){
    case VK_FORMAT_D16_UNORM:case VK_FORMAT_X8_D24_UNORM_PACK32:case VK_FORMAT_D32_SFLOAT:return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_S8_UINT:return VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    default:return 0;
    }
}
bool output_context(const RenderTargets& targets,std::string& why){
    const auto count=targets.dynamic_colors.size();
    if(!targets.known || !targets.dynamic || targets.flags || targets.view_mask || targets.layers!=1 ||
       count>8 || targets.views.size()!=2*(count+size_t(targets.has_depth)+size_t(targets.has_stencil))){
        why="Native output destinations need an ordinary complete dynamic output pass";return false;
    }
    if(targets.area.offset.x<0 || targets.area.offset.y<0){why="Native output render area is invalid";return false;}
    for(size_t slot=0;slot<count;++slot){const auto& color=targets.dynamic_colors[slot];const auto& view=targets.views[2*slot];
        if(!color.imageView){
            if(color.resolveMode==VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID){why="Native external-format output resolve needs its own pass adapter";return false;}
            continue;
        }
        if(!view.known || color.imageView!=view.handle || view.image.samples!=VK_SAMPLE_COUNT_1_BIT ||
           color.resolveMode!=VK_RESOLVE_MODE_NONE || color.storeOp!=VK_ATTACHMENT_STORE_OP_STORE ||
           color.imageLayout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL){
            why="Native output pass must retain single-sample stored color contents without resolve";return false;
        }
    }
    size_t index=2*count;
    auto preserve=[&](bool present,const VkRenderingAttachmentInfo& attachment){
        if(!present)return true;const auto& actual=targets.views[index];index+=2;
        if(!attachment.imageView)return true;
        if(!actual.known || actual.handle!=attachment.imageView || actual.image.samples!=VK_SAMPLE_COUNT_1_BIT ||
           attachment.storeOp!=VK_ATTACHMENT_STORE_OP_STORE || attachment.resolveMode!=VK_RESOLVE_MODE_NONE ||
           attachment.imageLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL){
            why="Native output preserved depth/stencil needs known stored single-sample attachments without resolve";return false;
        }
        return true;
    };
    if(!preserve(targets.has_depth,targets.depth) || !preserve(targets.has_stencil,targets.stencil))return false;
    if(targets.has_depth && targets.has_stencil && targets.depth.imageView!=targets.stencil.imageView){
        why="Native output depth/stencil attachments must preserve their shared actual view";return false;
    }
    return true;
}
void continue_targets(RenderTargets& targets){
    for(auto& color:targets.dynamic_colors)if(color.imageView)color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    if(targets.has_depth && targets.depth.imageView)targets.depth.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    if(targets.has_stencil && targets.stencil.imageView)targets.stencil.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
}
std::shared_ptr<NativeImageState> target_images(const CmdBuf* cb,const RenderTargets& targets){
    if(!cb->native_image_state)return {};
    auto images=std::make_shared<NativeImageState>(*cb->native_image_state);images->attachments.clear();images->attachments_known=true;
    for(size_t slot=0;slot<targets.dynamic_colors.size();++slot)
        if(targets.dynamic_colors[slot].imageView)images->attachments.insert(targets.views[2*slot].image.id);
    if(targets.has_depth && targets.depth.imageView)images->attachments.insert(targets.views[2*targets.dynamic_colors.size()].image.id);
    if(targets.has_stencil && targets.stencil.imageView)images->attachments.insert(targets.views[2*(targets.dynamic_colors.size()+size_t(targets.has_depth))].image.id);
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
    auto targets=cb->target_contexts[cb->active_targets-1];if(!output_context(targets,why))return false;
    const auto end=device->d.CmdEndRendering?device->d.CmdEndRendering:device->d.CmdEndRenderingKHR;
    const auto begin=device->d.CmdBeginRendering?device->d.CmdBeginRendering:device->d.CmdBeginRenderingKHR;
    if(!begin || !end || !device->d.CmdPipelineBarrier)return refuse("Native output rendering entries are unavailable");
    if(cb->target_contexts.size()+plan.items.size()>4096)return refuse("Native output context budget exceeded");
    next.state=cb->native_target_state?std::make_shared<NativeTargetState>(*cb->native_target_state):std::make_shared<NativeTargetState>();
    const auto& proof=cb->image_layouts;
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto pipeline=device->pipes.find(draw.pipeline);
    const auto recipe=pipeline==device->pipes.end()?nullptr:pipeline->second.recipe;
    const size_t count=targets.dynamic_colors.size();const size_t depth_index=2*count,stencil_index=depth_index+2*size_t(targets.has_depth);
    if(!recipe || !recipe->geometry_complete || !recipe->dynamic_rendering || recipe->rendering_view_mask ||
       recipe->multisample.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT ||
       recipe->rendering_colors.size()!=count)
        return refuse("Native output pipeline attachment contract is unavailable");
    if(!next.state->initialized){
        next.state->initialized=true;next.state->depth.format=recipe->rendering_depth;next.state->stencil_format=recipe->rendering_stencil;
        const auto actual=targets.has_depth && targets.depth.imageView?depth_index:stencil_index;
        next.state->depth.original=targets.has_depth && targets.depth.imageView?targets.depth.imageView:
            targets.has_stencil?targets.stencil.imageView:VK_NULL_HANDLE;
        if(next.state->depth.original)next.state->depth.original_image=targets.views[actual].view.image;
        next.state->colors.resize(count);
        for(size_t slot=0;slot<count;++slot){auto& color=next.state->colors[slot];
            color.original=targets.dynamic_colors[slot].imageView;
            color.original_image=color.original?targets.views[2*slot].view.image:VK_NULL_HANDLE;
            color.format=recipe->rendering_colors[slot];
        }
    }
    if(next.state->colors.size()!=count || recipe->rendering_depth!=next.state->depth.format || recipe->rendering_stencil!=next.state->stencil_format)
        return refuse("Native output original attachment format contract changed");
    for(size_t slot=0;slot<count;++slot){
        if(recipe->rendering_colors[slot]!=next.state->colors[slot].format ||
           (targets.dynamic_colors[slot].imageView?recipe->rendering_colors[slot]!=targets.views[2*slot].view.format:
            (recipe->rendering_colors[slot]!=VK_FORMAT_UNDEFINED && !device->dynamic_unused_attachments)))
            return refuse("Native output pipeline color contract is unavailable");
    }
    const bool depth_bound=targets.has_depth && targets.depth.imageView;
    const bool stencil_bound=targets.has_stencil && targets.stencil.imageView;
    if(depth_bound?recipe->rendering_depth!=targets.views[depth_index].view.format:
       (recipe->rendering_depth!=VK_FORMAT_UNDEFINED && !device->dynamic_unused_attachments))
        return refuse("Native output pipeline depth contract is unavailable");
    if(stencil_bound?recipe->rendering_stencil!=targets.views[stencil_index].view.format:
       (recipe->rendering_stencil!=VK_FORMAT_UNDEFINED && !device->dynamic_unused_attachments))
        return refuse("Native output pipeline stencil contract is unavailable");
    for(size_t i=0;i<plan.items.size();++i){
        const auto& item=plan.items[i];if(item.operation!=Operation::NativeBind || no_op[i])continue;
        const auto& resource=item.native_resource;const bool empty=!resource || !resource->bound;
        const auto& binding=item.native_target;const bool depth=binding.kind==efmi::frontend::NativeBinding::DepthTarget;
        if(depth?binding.slot!=0:(binding.kind!=efmi::frontend::NativeBinding::ColorTarget || binding.slot>=count))
            return refuse("Native output destination slot needs a separate pass adapter");
        bool roles_changed=false;
        if(depth && !empty){
            if(!targets.has_depth && next.state->depth.format!=VK_FORMAT_UNDEFINED){
                targets.views.insert(targets.views.begin()+2*count,2,TargetView{});
                targets.depth=VkRenderingAttachmentInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};targets.has_depth=true;roles_changed=true;
            }
            if(!targets.has_stencil && next.state->stencil_format!=VK_FORMAT_UNDEFINED){
                targets.views.resize(targets.views.size()+2);
                targets.stencil=VkRenderingAttachmentInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};targets.has_stencil=true;roles_changed=true;
            }
        }
        const size_t depth_index=2*count,stencil_index=depth_index+2*size_t(targets.has_depth);
        const size_t slot=depth?depth_slot:binding.slot,view_index=depth?depth_index:2*slot;
        auto& original=depth?next.state->depth:next.state->colors[slot];
        const auto current=depth?(targets.has_depth && targets.depth.imageView?targets.depth.imageView:
            targets.has_stencil?targets.stencil.imageView:VK_NULL_HANDLE):targets.dynamic_colors[slot].imageView;
        if(empty && !item.resource){
            if(current && !device->dynamic_unused_attachments)
                return refuse("Native null outputs require the application's enabled unused attachments feature");
            auto event=std::make_shared<NativeTargetPlan::Event>();event->slot=uint32_t(slot);event->change=current!=VK_NULL_HANDLE;
            if(depth){
                if(targets.has_depth){targets.depth.imageView=VK_NULL_HANDLE;targets.views[depth_index]={};targets.views[depth_index+1]={};}
                if(targets.has_stencil){targets.stencil.imageView=VK_NULL_HANDLE;targets.views[stencil_index]={};targets.views[stencil_index+1]={};}
            }else{targets.dynamic_colors[slot].imageView=VK_NULL_HANDLE;targets.views[view_index]={};targets.views[view_index+1]={};}
            continue_targets(targets);event->images=target_images(cb,targets);
            event->targets=targets;next.events[i]=std::move(event);continue;
        }
        if(depth && !targets.has_depth && !targets.has_stencil)
            return refuse("Native DSV destination needs a declared pipeline depth or stencil format");
        const VkImageUsageFlags usage=depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if(!resource || !resource->bound || !resource->keep || !resource->image_safe || !resource->image_view ||
           resource->uploaded_readonly || resource->uploaded_mutable || !(resource->usage&usage))
            return refuse("Native output destination needs a retained compatible application attachment view");
        const auto view=device->image_views.find(VkImageView(resource->image_view));
        if(view==device->image_views.end() || view->second.id!=resource->image_view_id)return refuse("Native output destination view was replaced");
        const auto image=device->images.find(view->second.image);
        if(image==device->images.end() || image->second.id!=view->second.image_id || resource->image!=uint64_t(view->second.image) ||
           resource->identity!=((uint64_t(image->second.id)<<2)|2))return refuse("Native output destination image was replaced");
        auto range=view->second.range;
        if(range.levelCount==VK_REMAINING_MIP_LEVELS && range.baseMipLevel<image->second.levels)range.levelCount=image->second.levels-range.baseMipLevel;
        if(range.layerCount==VK_REMAINING_ARRAY_LAYERS && range.baseArrayLayer<image->second.layers)range.layerCount=image->second.layers-range.baseArrayLayer;
        if(depth){
            const auto aspects=depth_aspects(image->second.format);
            if(!aspects || view->second.format!=image->second.format ||
               (targets.has_depth && (!(aspects&VK_IMAGE_ASPECT_DEPTH_BIT) || view->second.format!=next.state->depth.format)) ||
               (targets.has_stencil && (!(aspects&VK_IMAGE_ASPECT_STENCIL_BIT) || view->second.format!=next.state->stencil_format)))
                return refuse("Native DSV destination must match every original depth/stencil role and format");
            // Attachment roles ignore the view aspect mask. Both format aspects move together.
            range.aspectMask=aspects;
        }
        if((!depth && (range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT || view->second.format!=original.format)) ||
           range.levelCount!=1 || range.layerCount!=1 || image->second.type!=VK_IMAGE_TYPE_2D || image->second.samples!=VK_SAMPLE_COUNT_1_BIT ||
           (view->second.type!=VK_IMAGE_VIEW_TYPE_2D && view->second.type!=VK_IMAGE_VIEW_TYPE_2D_ARRAY))
            return refuse("Native output destination format or subresources differ from the rendering contract");
        const auto width=std::max(1u,image->second.extent.width>>std::min(range.baseMipLevel,31u));
        const auto height=std::max(1u,image->second.extent.height>>std::min(range.baseMipLevel,31u));
        if(uint64_t(targets.area.offset.x)+targets.area.extent.width>width || uint64_t(targets.area.offset.y)+targets.area.extent.height>height)
            return refuse("Native output destination does not cover the render area");
        if(descriptor_conflict(device,cb,view->second.image,why))return false;
        if(!depth && ((targets.has_depth && targets.depth.imageView && view->second.image==targets.views[depth_index].view.image) ||
           (targets.has_stencil && targets.stencil.imageView && view->second.image==targets.views[stencil_index].view.image)))
            return refuse("Native output color destination aliases a current depth/stencil image");
        const auto handle=view->first;
        for(size_t other=0;other<count;++other)if(other!=slot && targets.dynamic_colors[other].imageView && targets.views[2*other].view.image==view->second.image)
            return refuse("Native output destination overlaps another active color image");
        for(const auto& source:next.state->colors)if(handle!=source.original && view->second.image==source.original_image)
            return refuse("Native output aliases of an original color image need subresource transition planning");
        if(handle!=next.state->depth.original && view->second.image==next.state->depth.original_image)
            return refuse("Native output aliases of the original depth/stencil image need subresource transition planning");
        for(const auto& saved:next.state->restore)if(saved.first!=handle && saved.second.view.view.image==view->second.image)
            return refuse("Native output view aliases need subresource transition planning");
        if(handle!=original.original && !next.state->restore.count(handle)){
            VkImageLayout before;
            if(!proof.get(image->second.id,image->second.levels,image->second.layers,range,before))
                return refuse("Native output destination all-aspect layout is not proven in this recording");
            const bool valid=before==VK_IMAGE_LAYOUT_GENERAL || before==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
                (depth?(before==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || before==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL):
                       (before==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || before==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
            if(!valid)return refuse("Native output destination layout cannot preserve contents for attachment LOAD");
            NativeTargetState::Saved saved;saved.view.handle=handle;saved.view.view=view->second;saved.view.image=image->second;saved.view.known=true;
            saved.view.view.lifetime.reset();saved.view.image.lifetime.reset();saved.view.view.range=range;
            saved.layout=before;saved.resource=resource;next.state->restore.emplace(handle,std::move(saved));
        }
        auto event=std::make_shared<NativeTargetPlan::Event>();event->resource=resource;event->slot=uint32_t(slot);event->change=roles_changed || handle!=current;
        auto publish=[&](VkRenderingAttachmentInfo& attachment,size_t index,VkImageLayout destination){
            attachment.imageView=handle;attachment.imageLayout=destination;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            attachment.resolveMode=VK_RESOLVE_MODE_NONE;attachment.resolveImageView=VK_NULL_HANDLE;
            attachment.resolveImageLayout=VK_IMAGE_LAYOUT_UNDEFINED;targets.views[index+1]={};
            auto& actual=targets.views[index];actual.handle=handle;actual.view=view->second;actual.image=image->second;actual.known=true;
            actual.view.lifetime.reset();actual.image.lifetime.reset();actual.view.range=range;
        };
        if(depth){
            if(targets.has_depth)publish(targets.depth,depth_index,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            if(targets.has_stencil)publish(targets.stencil,stencil_index,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }else publish(targets.dynamic_colors[slot],view_index,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        continue_targets(targets);event->images=target_images(cb,targets);
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
    auto& state=*plan.state;const bool depth=event->slot==depth_slot;
    auto& attachment=depth?state.depth:state.colors[event->slot];
    const auto attachment_layout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if(attachment.active){const auto& saved=state.restore.at(attachment.active);barriers[barrier_count++]=transition(saved,attachment_layout,saved.layout);layout(cb,saved,saved.layout);}
    const auto target=depth?(event->targets.has_depth && event->targets.depth.imageView?event->targets.depth.imageView:
        event->targets.has_stencil?event->targets.stencil.imageView:VK_NULL_HANDLE):event->targets.dynamic_colors[event->slot].imageView;
    attachment.active=target && target!=attachment.original?target:VK_NULL_HANDLE;
    if(attachment.active){const auto& saved=state.restore.at(attachment.active);barriers[barrier_count++]=transition(saved,saved.layout,attachment_layout);layout(cb,saved,attachment_layout);}
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,barrier_count,barriers);
    cb->target_contexts.push_back(std::move(event->targets));cb->active_targets=uint32_t(cb->target_contexts.size());
    cb->native_image_state=event->images;const auto& targets=cb->target_contexts.back();
    cb->native_graphics_ordered=true;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea=targets.area;info.layerCount=1;
    info.colorAttachmentCount=uint32_t(targets.dynamic_colors.size());info.pColorAttachments=targets.dynamic_colors.data();
    info.pDepthAttachment=targets.has_depth?&targets.depth:nullptr;
    info.pStencilAttachment=targets.has_stencil?&targets.stencil:nullptr;begin(cb->handle,&info);
}
void native_targets_end(Device* device,CmdBuf* cb)
{
    auto state=std::move(cb->native_target_state);if(!state)return;
    VkImageMemoryBarrier barriers[9]{};uint32_t count=0;
    auto restore=[&](const NativeTargetState::Color& attachment,VkImageLayout current){if(attachment.active){
        const auto& saved=state->restore.at(attachment.active);barriers[count++]=transition(saved,current,saved.layout);layout(cb,saved,saved.layout);
    }};
    for(const auto& color:state->colors)restore(color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    restore(state->depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if(count)device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,count,barriers);
}
}
