#include "native_targets.h"
#include "resource_memory.h"
#include "native_images.h"
#include "native_classic_targets.h"
#include "native_image_phases.h"
namespace efvk {
namespace {
using Resource=efmi::frontend::NativeResource;
const char target_owner_key=0;
constexpr uint32_t depth_slot=8; // The original DSV binding represents both depth and stencil.
struct TargetOwner:CommandOwner {
    std::vector<std::shared_ptr<const Resource>> resources;
    std::vector<std::shared_ptr<NativeClassicTarget>> classic;
    std::vector<std::shared_ptr<const NativeBoundPipeline>> pipelines;
    bool sealed=false;
    void reset_after_completion()override{resources.clear();classic.clear();pipelines.clear();used=false;sealed=false;}
    bool seal()override{if(!used || sealed)return false;sealed=true;return true;}
};
std::shared_ptr<TargetOwner> target_owner(CmdBuf* cb){
    // Binding a twin must not discard another package's unused snapshot arena (BUG-089).
    for(const auto& owner:cb->owners)if(owner && owner->backend==&target_owner_key)return std::static_pointer_cast<TargetOwner>(owner);
    auto owner=std::make_shared<TargetOwner>();owner->backend=&target_owner_key;cb->owners.push_back(owner);return owner;
}
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
std::shared_ptr<NativeImageState> target_images(const RenderTargets& targets){
    auto images=std::make_shared<NativeImageState>();images->attachments_known=true;
    for(size_t slot=0;slot<targets.dynamic_colors.size();++slot)
        if(targets.dynamic_colors[slot].imageView)images->attachments.insert(targets.views[2*slot].image.id);
    if(targets.has_depth && targets.depth.imageView)images->attachments.insert(targets.views[2*targets.dynamic_colors.size()].image.id);
    if(targets.has_stencil && targets.stencil.imageView)images->attachments.insert(targets.views[2*(targets.dynamic_colors.size()+size_t(targets.has_depth))].image.id);
    return images;
}
void continue_images(CmdBuf* cb,const std::shared_ptr<NativeImageState>& next){
    // Descriptor preparation/recording follows target preflight. Transfer the live
    // requests at emission time, never overwrite them with an earlier snapshot.
    if(cb->native_image_state){
        next->active=cb->native_image_state->active;
        next->required=cb->native_image_state->required;
    }
    cb->native_image_state=next;
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
VkImageLayout detached_layout(const NativeTargetState::Saved& saved){
    return saved.readable_when_detached?VK_IMAGE_LAYOUT_GENERAL:saved.layout;
}
VkImageLayout attachment_source(CmdBuf* cb,const NativeTargetState::Saved& saved){
    auto current=saved.layout;
    if(saved.readable_when_detached){
        auto range=saved.view.view.range;range.levelCount=range.layerCount=1;
        // Preflight proved this cell; every introduced transition updates it.
        cb->image_layouts.get(saved.view.image.id,saved.view.image.levels,saved.view.image.layers,range,current);
    }
    return current;
}
bool descriptor_conflict(Device* device,CmdBuf* cb,const DrawRec& draw,const efmi::assets::FilePlan& plan,
                         const std::vector<bool>& read_phases,VkImage image,const ImageInfo& info,std::string& why){
    if(!image)return false;
    const auto target=native_memory(info);const auto granularity=device->physical_properties.limits.bufferImageGranularity;
    auto conflict=[&](const NativeMemoryRange& input){
        if(!input.memory){why="Native output input memory identity is unavailable";return true;}
        if(native_memory_overlap(target,input,granularity)){why="Native output destination overlaps input memory";return true;}
        return false;
    };
    auto resource_conflict=[&](const Resource& resource){
        if(!resource.bound)return false;
        // Private file allocations are owned separately and never exposed as application outputs.
        if(resource.keep && (resource.uploaded_readonly || resource.uploaded_mutable))return false;
        if(resource.image==uint64_t(image)){why="Native output destination overlaps a bound descriptor";return true;}
        return conflict(native_memory(resource));
    };
    auto buffer_conflict=[&](VkBuffer handle,uint32_t id=0){
        if(!handle)return false;const auto found=device->bufs.find(handle);
        if(found==device->bufs.end() || (id && found->second.id!=id)){why="Native output buffer identity is unavailable";return true;}
        return conflict(native_memory(found->second));
    };
    auto element_conflict=[&](const DescriptorElement& value){
        if(value.native && resource_conflict(*std::static_pointer_cast<const Resource>(value.native)))return true;
        if(value.native_request && resource_conflict(*std::static_pointer_cast<const Resource>(value.native_request)))return true;
        if(!value.buffer && !value.buffer_view && !value.image_view)return false;
        if(!value.known){why="Native output descriptor identity is unavailable";return true;}
        if(value.native)return false; // Its retained allocation survives logical destruction.
        if(value.buffer_view){
            const auto view=device->buffer_views.find(value.buffer_view);
            if(view==device->buffer_views.end() || view->second.id!=value.buffer_view_id){why="Native output texel view identity is unavailable";return true;}
            return buffer_conflict(view->second.buffer,view->second.buffer_id);
        }
        if(value.buffer)return buffer_conflict(value.buffer,value.buffer_id);
        const auto view=device->image_views.find(value.image_view);
        if(view==device->image_views.end() || view->second.id!=value.image_view_id){why="Native output descriptor image identity is unavailable";return true;}
        if(view->second.image==image){why="Native output destination overlaps a bound descriptor";return true;}
        const auto input=device->images.find(view->second.image);
        if(input==device->images.end() || input->second.id!=view->second.image_id){why="Native output sampled image identity is unavailable";return true;}
        return conflict(native_memory(input->second));
    };
    for(const auto& bound:cb->sets){
        if(!bound.set && !bound.pushed)continue;
        const DescriptorSetInfo* contents=bound.pushed.get();
        const auto found=device->descriptor_sets.find(bound.set);
        if(!contents && found!=device->descriptor_sets.end() && found->second.id==bound.id)contents=&found->second;
        if(!contents || bound.unsupported || contents->unsupported){why="Native output descriptor alias context is unavailable";return true;}
        for(const auto& entry:contents->values){const auto& value=entry.second;
            if(element_conflict(value))return true;
            // Suspended requests and the application fallback remain future inputs.
            if(value.application && element_conflict(*value.application))return true;
        }
    }
    // Layout restoration is an access even after its descriptor has been replaced.
    if(cb->native_image_state)for(const auto& cell:cb->native_image_state->active.cells){
        if(!cell.second.resource){why="Native image restoration resource is unavailable";return true;}
        if(resource_conflict(*cell.second.resource))return true;
    }
    // Explicit future reads may use the same image in disjoint output phases.
    // Its existing sampled layout must still pass descriptor preflight, and the
    // pass split's barriers must restore that layout before the read. Different
    // objects aliasing the allocation, saved fallbacks and pending restoration
    // remain conflicts; a binding interval alone cannot prove those safe.
    for(size_t i=0;i<plan.items.size();++i){const auto& item=plan.items[i];
        if(item.operation!=efmi::assets::FileOperation::NativeBind || native_output_binding(item.native_target) || !item.native_resource)continue;
        const auto& resource=*item.native_resource;
        if(read_phases[i] && resource.image==uint64_t(image) && resource.identity==((uint64_t(info.id)<<2)|2))continue;
        if(resource_conflict(resource))return true;
    }
    for(const auto& binding:draw.vb)if(buffer_conflict(binding.buf))return true;
    return buffer_conflict(draw.ib) || buffer_conflict(draw.indirect) || buffer_conflict(draw.indirect_count);
}
}
bool prepare_native_targets(Device* device,CmdBuf* cb,const DrawRec& draw,const efmi::assets::FilePlan& plan,NativeTargetPlan& output,std::string& why,
                            NativeTargetCache* cache,bool resource_lock_held)
{
    using Operation=efmi::assets::FileOperation;
    NativeTargetPlan next;next.events.resize(plan.items.size());bool needed=false;
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
        }
    }
    if(!needed){output=std::move(next);return true;}
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(!cb->in_rp || cb->secondary || cb->inherited_rp || !cb->execution_scopes.splittable())
        return refuse("Native output pass splitting requires a primary with no active query or transform feedback");
    if(device->synchronization2_enabled || device->application_queue_count!=1 || device->material_queues.size()!=1)
        return refuse("Native output destinations require one application queue and legacy synchronization");
    if(!cb->active_targets || cb->active_targets>cb->target_contexts.size())return refuse("Native output context is unavailable");
    auto targets=cb->target_contexts[cb->active_targets-1];const bool classic=!targets.dynamic;
    if(classic && !classic_context(targets,why))return false;
    if(!output_context(targets,why))return false;
    const auto initial_targets=targets;
    const auto read_phases=native_image_read_phases(targets,plan);
    const auto end=device->d.CmdEndRendering?device->d.CmdEndRendering:device->d.CmdEndRenderingKHR;
    const auto begin=device->d.CmdBeginRendering?device->d.CmdBeginRendering:device->d.CmdBeginRenderingKHR;
    if(!device->d.CmdPipelineBarrier || (classic?(!cache || !device->d.CmdBeginRenderPass || !device->d.CmdEndRenderPass ||
       !device->d.CreateFramebuffer || !device->d.DestroyFramebuffer || !device->d.DestroyRenderPass):(!begin || !end)))
        return refuse("Native output rendering entries or object cache are unavailable");
    if(cb->target_contexts.size()+plan.items.size()>4096)return refuse("Native output context budget exceeded");
    next.state=cb->native_target_state?std::make_shared<NativeTargetState>(*cb->native_target_state):std::make_shared<NativeTargetState>();
    const auto& proof=cb->image_layouts;
    std::shared_lock<std::shared_mutex> lock(device->res_mx,std::defer_lock);
    if(!resource_lock_held)lock.lock();
    const auto pipeline=device->pipes.find(draw.pipeline);
    const auto recipe=pipeline==device->pipes.end()?nullptr:pipeline->second.recipe;
    const size_t count=targets.dynamic_colors.size();const size_t depth_index=2*count,stencil_index=depth_index+2*size_t(targets.has_depth);
    if(!recipe || !recipe->geometry_complete || recipe->multisample.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT ||
       (classic?(recipe->dynamic_rendering || recipe->subpass || !recipe->render_pass || !classic_recipe(*recipe->render_pass)):
                 (!recipe->dynamic_rendering || recipe->rendering_view_mask || recipe->rendering_colors.size()!=count)))
        return refuse("Native output pipeline attachment contract is unavailable");
    const std::vector<VkFormat> classic_formats=classic?classic_color_formats(*recipe->render_pass):std::vector<VkFormat>{};
    const auto& color_formats=classic?classic_formats:recipe->rendering_colors;
    const auto depth_format=classic?classic_depth_format(*recipe->render_pass,VK_IMAGE_ASPECT_DEPTH_BIT):recipe->rendering_depth;
    const auto stencil_format=classic?classic_depth_format(*recipe->render_pass,VK_IMAGE_ASPECT_STENCIL_BIT):recipe->rendering_stencil;
    if(!next.state->initialized){
        next.state->classic=classic;
        if(classic){
            next.state->classic_recipe=targets.recipe;const auto views=classic_views(targets);
            for(size_t i=0;i<views.size();++i){NativeTargetState::Saved original;original.view=views[i];
                original.layout=targets.recipe->attachments[i].finalLayout;next.state->classic_originals.push_back(std::move(original));}
        }
        next.state->initialized=true;next.state->depth.format=depth_format;next.state->stencil_format=stencil_format;
        const auto actual=targets.has_depth && targets.depth.imageView?depth_index:stencil_index;
        next.state->depth.original=targets.has_depth && targets.depth.imageView?targets.depth.imageView:
            targets.has_stencil?targets.stencil.imageView:VK_NULL_HANDLE;
        if(next.state->depth.original){next.state->depth.original_image=targets.views[actual].view.image;
            next.state->depth.original_storage=targets.views[actual].image;next.state->depth.original_storage.lifetime.reset();}
        next.state->colors.resize(count);
        for(size_t slot=0;slot<count;++slot){auto& color=next.state->colors[slot];
            color.original=targets.dynamic_colors[slot].imageView;
            color.original_image=color.original?targets.views[2*slot].view.image:VK_NULL_HANDLE;
            if(color.original){color.original_storage=targets.views[2*slot].image;color.original_storage.lifetime.reset();}
            color.format=color_formats[slot];
        }
    }
    if(next.state->colors.size()!=count || depth_format!=next.state->depth.format || stencil_format!=next.state->stencil_format)
        return refuse("Native output original attachment format contract changed");
    for(size_t slot=0;slot<count;++slot){
        if(color_formats[slot]!=next.state->colors[slot].format ||
           (targets.dynamic_colors[slot].imageView?color_formats[slot]!=targets.views[2*slot].view.format:
            (color_formats[slot]!=VK_FORMAT_UNDEFINED && !classic && !device->dynamic_unused_attachments)))
            return refuse("Native output pipeline color contract is unavailable");
    }
    const bool depth_bound=targets.has_depth && targets.depth.imageView;
    const bool stencil_bound=targets.has_stencil && targets.stencil.imageView;
    if(depth_bound?depth_format!=targets.views[depth_index].view.format:
       (depth_format!=VK_FORMAT_UNDEFINED && !classic && !device->dynamic_unused_attachments))
        return refuse("Native output pipeline depth contract is unavailable");
    if(stencil_bound?stencil_format!=targets.views[stencil_index].view.format:
       (stencil_format!=VK_FORMAT_UNDEFINED && !classic && !device->dynamic_unused_attachments))
        return refuse("Native output pipeline stencil contract is unavailable");
    // Existing outputs and restoration storage also survive output-only/null events.
    // A future descriptor must not overwrite their retained contents in either order.
    for(const auto& active:targets.views)if(active.known && descriptor_conflict(device,cb,draw,plan,read_phases,active.view.image,active.image,why))return false;
    for(const auto& source:next.state->colors)if(descriptor_conflict(device,cb,draw,plan,read_phases,source.original_image,source.original_storage,why))return false;
    if(descriptor_conflict(device,cb,draw,plan,read_phases,next.state->depth.original_image,next.state->depth.original_storage,why))return false;
    for(const auto& saved:next.state->restore)if(descriptor_conflict(device,cb,draw,plan,read_phases,saved.second.view.view.image,saved.second.view.image,why))return false;
    for(size_t i=0;i<plan.items.size();++i){
        const auto& item=plan.items[i];if(item.operation!=Operation::NativeBind || !native_output_binding(item.native_target) || no_op[i])continue;
        const auto& resource=item.native_resource;const bool empty=!resource || !resource->bound;
        const auto& binding=item.native_target;const bool depth=binding.kind==efmi::frontend::NativeBinding::DepthTarget;
        if(depth?binding.slot!=0:(binding.kind!=efmi::frontend::NativeBinding::ColorTarget || binding.slot>=count))
            return refuse("Native output destination slot needs a separate pass adapter");
        if(classic && empty && (!cb->native_pipeline || cb->native_pipeline->bound!=draw.bound ||
           !cb->native_pipeline->recipe || !cb->native_pipeline->layout))
            return refuse("Native classic null outputs require the actual selected pipeline snapshot");
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
            if(current && !classic && !device->dynamic_unused_attachments)
                return refuse("Native null outputs require the application's enabled unused attachments feature");
            auto event=std::make_shared<NativeTargetPlan::Event>();event->slot=uint32_t(slot);event->change=current!=VK_NULL_HANDLE;
            if(depth){
                if(targets.has_depth){targets.depth.imageView=VK_NULL_HANDLE;targets.views[depth_index]={};targets.views[depth_index+1]={};}
                if(targets.has_stencil){targets.stencil.imageView=VK_NULL_HANDLE;targets.views[stencil_index]={};targets.views[stencil_index+1]={};}
            }else{targets.dynamic_colors[slot].imageView=VK_NULL_HANDLE;targets.views[view_index]={};targets.views[view_index+1]={};}
            continue_targets(targets);event->images=target_images(targets);
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
        if(!image->second.mem || !image->second.mem_size)return refuse("Native output destination memory range is unavailable");
        if(descriptor_conflict(device,cb,draw,plan,read_phases,view->second.image,image->second,why))return false;
        const auto backing=native_memory(image->second);const auto granularity=device->physical_properties.limits.bufferImageGranularity;
        auto image_alias=[&](VkImage other,const ImageInfo& metadata){
            return other && other!=view->second.image && native_memory_overlap(backing,native_memory(metadata),granularity);
        };
        for(const auto& active:targets.views)if(active.known && image_alias(active.view.image,active.image))
            return refuse("Native output destination overlaps active attachment memory");
        for(const auto& source:next.state->colors)if(image_alias(source.original_image,source.original_storage))
            return refuse("Native output destination overlaps preserved original memory");
        if(image_alias(next.state->depth.original_image,next.state->depth.original_storage))
            return refuse("Native output destination overlaps preserved depth memory");
        for(const auto& saved:next.state->restore)if(image_alias(saved.second.view.view.image,saved.second.view.image))
            return refuse("Native output destination overlaps retained restoration memory");
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
        auto event=std::make_shared<NativeTargetPlan::Event>();event->target=handle;event->resource=resource;event->slot=uint32_t(slot);event->change=roles_changed || handle!=current;
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
        continue_targets(targets);event->images=target_images(targets);
        event->targets=targets;next.events[i]=std::move(event);
    }
    native_image_phase_layouts(initial_targets,plan,read_phases,next);
    if(lock.owns_lock())lock.unlock();
    if(classic){
        if(!next.state->compatibility){
            std::lock_guard<std::mutex> cache_lock(cache->mutex);
            if(!cache->pipeline_cache)cache->pipeline_cache=std::make_shared<NativePipelineCache>();
            next.state->compatibility=cache->pipeline_cache;
        }
        // All binding proofs precede allocation, and all allocations precede the first End.
        for(auto& event:next.events)if(event && event->change){
            event->classic=classic_target(device,*cache,next.state->classic_recipe,classic_views(event->targets,next.state->classic_recipe.get()),event->targets.area,why);
            if(!event->classic)return false;
            if(classic_mask(*event->classic->recipe)!=classic_mask(*next.state->classic_recipe)){
                if(!cb->native_pipeline || cb->native_pipeline->bound!=draw.bound)return refuse("Native classic selected pipeline identity is unavailable");
                event->pipeline=classic_pipeline(device,*next.state,*cb->native_pipeline,event->classic->recipe,why);
                if(!event->pipeline)return false;event->bound=event->pipeline->pipeline;
            }else if(cb->native_pipeline)event->bound=cb->native_pipeline->source;
            auto& actual=event->targets;actual.dynamic=false;actual.dynamic_colors.clear();actual.views.clear();
            actual.recipe=event->classic->recipe;actual.framebuffer=event->classic->info;
        }
    }
    auto owner=target_owner(cb);
    if(owner->sealed || owner->resources.size()+plan.items.size()>4096)return refuse("Native output retention budget or sealed recording");
    owner->resources.reserve(owner->resources.size()+plan.items.size());cb->target_contexts.reserve(cb->target_contexts.size()+plan.items.size());
    owner->classic.reserve(owner->classic.size()+plan.items.size());
    owner->pipelines.reserve(owner->pipelines.size()+plan.items.size());
    next.owner=owner;output=std::move(next);return true;
}
namespace {
const NativeTargetState::Saved& classic_saved(const NativeTargetState& state,VkImageView handle){
    // An original image can move to another slot after its first slot is vacated.
    // Its implicit final transition belongs to the image, not to the new slot.
    for(const auto& original:state.classic_originals)if(handle==original.view.handle)return original;
    return state.restore.at(handle);
}
void record_classic_target(Device* device,CmdBuf* cb,const NativeTargetPlan& plan,const std::shared_ptr<NativeTargetPlan::Event>& event){
    auto& state=*plan.state;device->d.CmdEndRenderPass(cb->handle);
    VkImageMemoryBarrier barriers[18]{};uint32_t count=0;
    auto move=[&](const NativeTargetState::Saved& saved,VkImageLayout before,VkImageLayout after){
        barriers[count++]=transition(saved,before,after);layout(cb,saved,after);
    };
    const auto& next=event->classic->info->views;
    const auto previous=state.private_pass?cb->pass.info->views:std::vector<TargetView>{};
    auto work=[](const TargetView& view){return depth_aspects(view.view.format)?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;};
    auto contains=[](const std::vector<TargetView>& views,VkImageView handle){return std::any_of(views.begin(),views.end(),[&](const TargetView& view){return view.handle==handle;});};
    if(!state.private_pass)for(const auto& original:state.classic_originals)layout(cb,original,original.layout);
    for(const auto& old:previous)if(!contains(next,old.handle)){
        const auto& saved=classic_saved(state,old.handle);move(saved,work(old),detached_layout(saved));
    }
    for(const auto& view:next)if(!contains(previous,view.handle)){
        const auto& saved=classic_saved(state,view.handle);move(saved,attachment_source(cb,saved),work(view));
    }
    auto& attachment=event->slot==depth_slot?state.depth:state.colors[event->slot];
    attachment.active=event->target!=attachment.original?event->target:VK_NULL_HANDLE;
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,count,barriers);
    cb->target_contexts.push_back(std::move(event->targets));cb->active_targets=uint32_t(cb->target_contexts.size());
    continue_images(cb,event->images);const auto& targets=cb->target_contexts.back();cb->native_graphics_ordered=true;
    state.private_pass=true;cb->pass.render_pass=event->classic->pass;cb->pass.framebuffer=event->classic->framebuffer;
    cb->pass.recipe=targets.recipe;cb->pass.info=targets.framebuffer;
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};info.renderPass=cb->pass.render_pass;
    info.framebuffer=cb->pass.framebuffer;info.renderArea=targets.area;device->d.CmdBeginRenderPass(cb->handle,&info,VK_SUBPASS_CONTENTS_INLINE);
    if(event->bound){
        if(event->bound!=cb->gfx_bound)device->d.CmdBindPipeline(cb->handle,VK_PIPELINE_BIND_POINT_GRAPHICS,event->bound);
        cb->gfx_bound=event->bound;if(cb->native_pipeline)cb->native_pipeline->bound=event->bound;
    }
}
}

void record_native_target(Device* device,CmdBuf* cb,const NativeTargetPlan& plan,size_t index)
{
    const auto& event=plan.events[index];if(!event)return;
    auto owner=std::static_pointer_cast<TargetOwner>(plan.owner);owner->used=true;owner->resources.push_back(event->resource);
    cb->native_target_state=plan.state;if(!event->change)return;
    if(event->classic){owner->classic.push_back(event->classic);if(event->pipeline)owner->pipelines.push_back(event->pipeline);record_classic_target(device,cb,plan,event);return;}
    const auto end=device->d.CmdEndRendering?device->d.CmdEndRendering:device->d.CmdEndRenderingKHR;
    const auto begin=device->d.CmdBeginRendering?device->d.CmdBeginRendering:device->d.CmdBeginRenderingKHR;
    end(cb->handle);
    VkImageMemoryBarrier barriers[2]{};uint32_t barrier_count=0;
    auto& state=*plan.state;const bool depth=event->slot==depth_slot;
    auto& attachment=depth?state.depth:state.colors[event->slot];
    const auto attachment_layout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if(attachment.active){const auto& saved=state.restore.at(attachment.active);const auto after=detached_layout(saved);barriers[barrier_count++]=transition(saved,attachment_layout,after);layout(cb,saved,after);}
    const auto target=event->target;
    attachment.active=target && target!=attachment.original?target:VK_NULL_HANDLE;
    if(attachment.active){const auto& saved=state.restore.at(attachment.active);barriers[barrier_count++]=transition(saved,attachment_source(cb,saved),attachment_layout);layout(cb,saved,attachment_layout);}
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,barrier_count,barriers);
    cb->target_contexts.push_back(std::move(event->targets));cb->active_targets=uint32_t(cb->target_contexts.size());
    continue_images(cb,event->images);const auto& targets=cb->target_contexts.back();
    cb->native_graphics_ordered=true;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea=targets.area;info.layerCount=1;
    info.colorAttachmentCount=uint32_t(targets.dynamic_colors.size());info.pColorAttachments=targets.dynamic_colors.data();
    info.pDepthAttachment=targets.has_depth?&targets.depth:nullptr;
    info.pStencilAttachment=targets.has_stencil?&targets.stencil:nullptr;begin(cb->handle,&info);
}
void native_targets_end(Device* device,CmdBuf* cb)
{
    auto state=std::move(cb->native_target_state);if(!state)return;
    std::vector<VkImageMemoryBarrier> barriers;
    auto move=[&](const NativeTargetState::Saved& saved,VkImageLayout current){
        barriers.push_back(transition(saved,current,saved.layout));layout(cb,saved,saved.layout);
    };
    auto restore=[&](const NativeTargetState::Color& attachment,VkImageLayout current){if(attachment.active){
        move(state->restore.at(attachment.active),current);
    }};
    if(state->classic){
        if(state->private_pass)for(size_t i=0;i<cb->pass.info->views.size();++i){
            const auto& saved=classic_saved(*state,cb->pass.info->views[i].handle);
            move(saved,depth_aspects(saved.view.view.format)?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
    }else{
        for(const auto& color:state->colors)restore(color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        restore(state->depth,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    // Detached sampled targets also need restoration even though they are no
    // longer active outputs. Active targets above already have their old layout.
    for(const auto& entry:state->restore)if(entry.second.readable_when_detached){
        const auto current=attachment_source(cb,entry.second);
        if(current!=entry.second.layout)move(entry.second,current);
    }
    if(!barriers.empty())device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,uint32_t(barriers.size()),barriers.data());
    if(state->classic && state->private_pass){
        const auto source=cb->native_pipeline?cb->native_pipeline->source:cb->gfx_pipeline;
        if(source && source!=cb->gfx_bound){device->d.CmdBindPipeline(cb->handle,VK_PIPELINE_BIND_POINT_GRAPHICS,source);cb->gfx_bound=source;}
        if(cb->native_pipeline)cb->native_pipeline->bound=source;cb->native_target_blocked=false;
    }
}
VkPipeline native_target_bind(Device* device,CmdBuf* cb,VkPipeline pipeline){
    if(!cb)return pipeline;
    auto selected=cb->native_pipeline;
    if(!selected || selected->source!=pipeline){
        selected=std::make_shared<NativePipelineSelection>();selected->source=selected->bound=pipeline;
        std::shared_lock<std::shared_mutex> lock(device->res_mx);const auto found=device->pipes.find(pipeline);
        if(found!=device->pipes.end()){selected->recipe=found->second.recipe;selected->layout=found->second.layout;}
    }
    cb->native_pipeline=selected;
    const auto state=cb->native_target_state;
    if(!state || !state->classic || !state->private_pass ||
       classic_mask(*cb->pass.recipe)==classic_mask(*state->classic_recipe)){
        cb->native_target_blocked=false;selected->bound=pipeline;return pipeline;
    }
    std::string why;const auto variant=classic_pipeline(device,*state,*selected,cb->pass.recipe,why);
    auto owner=target_owner(cb);
    const bool held=std::find(owner->pipelines.begin(),owner->pipelines.end(),variant)!=owner->pipelines.end();
    if(!variant || owner->sealed || (!held && owner->pipelines.size()>=4096)){
        if(!cb->native_target_blocked)logf("Native classic draw blocked: %s",why.empty()?"pipeline retention budget or sealed recording":why.c_str());
        cb->native_target_blocked=true;selected->bound=VK_NULL_HANDLE;return VK_NULL_HANDLE;
    }
    owner->used=true;
    if(!held)owner->pipelines.push_back(variant);
    cb->native_target_blocked=false;selected->bound=variant->pipeline;return variant->pipeline;
}
VkPipeline native_target_variant(Device* device,CmdBuf* cb,const PipelineVariant& source,std::string& why,
                                 const NativeTargetPlan* future,size_t before){
    if(!cb)return source.pipeline;
    auto state=cb->native_target_state;auto pass=cb->pass.recipe;
    if(future)for(size_t i=0;i<std::min(before,future->events.size());++i){
        const auto& event=future->events[i];
        if(event && event->classic){state=future->state;pass=event->classic->recipe;}
    }
    if(!state || !state->classic || !pass || !state->classic_recipe || classic_mask(*pass)==classic_mask(*state->classic_recipe))return source.pipeline;
    NativePipelineSelection selected;selected.source=selected.bound=source.pipeline;
    selected.recipe=source.effective_recipe;selected.layout=source.effective_layout;
    const auto variant=classic_pipeline(device,*state,selected,pass,why);
    if(!variant)return VK_NULL_HANDLE;
    auto owner=target_owner(cb);
    const bool held=std::find(owner->pipelines.begin(),owner->pipelines.end(),variant)!=owner->pipelines.end();
    const size_t pending=future?future->events.size():0;
    if(owner->sealed || owner->pipelines.size()+size_t(!held)+pending>4096){
        why="Native classic Mod pipeline retention budget or sealed recording";return VK_NULL_HANDLE;
    }
    // Retaining this Mod clone must not consume the capacity reserved for later output events.
    owner->pipelines.reserve(owner->pipelines.size()+size_t(!held)+pending);
    owner->used=true;if(!held)owner->pipelines.push_back(variant);return variant->pipeline;
}
bool native_target_masked(const CmdBuf* cb){
    return cb && cb->native_target_state && cb->native_target_state->classic && cb->native_target_state->private_pass &&
        classic_mask(*cb->pass.recipe)!=classic_mask(*cb->native_target_state->classic_recipe);
}

}
