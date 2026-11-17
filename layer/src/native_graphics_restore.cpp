#include "native_graphics_restore.h"
#include "state.h"

namespace efvk {
namespace {
bool commands_available(const DeviceDispatch& d,const GraphicsDynamicState& s){
    if(s.viewport_mask && !d.CmdSetViewport)return false;
    if(s.scissor_mask && !d.CmdSetScissor)return false;
    if((s.known&(1u<<VK_DYNAMIC_STATE_LINE_WIDTH)) && !d.CmdSetLineWidth)return false;
    if((s.known&(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS)) && !d.CmdSetDepthBias)return false;
    if((s.known&(1u<<VK_DYNAMIC_STATE_BLEND_CONSTANTS)) && !d.CmdSetBlendConstants)return false;
    if((s.known&(1u<<VK_DYNAMIC_STATE_DEPTH_BOUNDS)) && !d.CmdSetDepthBounds)return false;
    if(s.stencil_faces[0] && !d.CmdSetStencilCompareMask)return false;
    if(s.stencil_faces[1] && !d.CmdSetStencilWriteMask)return false;
    if(s.stencil_faces[2] && !d.CmdSetStencilReference)return false;
    return true;
}
void replay(const DeviceDispatch& d,VkCommandBuffer cmd,const GraphicsDynamicState& s,VkDynamicState state){
    switch(state){
    case VK_DYNAMIC_STATE_VIEWPORT:
        for(uint32_t i=0;i<s.viewport_limit;++i)if(s.viewport_mask&(1u<<i))d.CmdSetViewport(cmd,i,1,&s.viewports[i]);break;
    case VK_DYNAMIC_STATE_SCISSOR:
        for(uint32_t i=0;i<s.viewport_limit;++i)if(s.scissor_mask&(1u<<i))d.CmdSetScissor(cmd,i,1,&s.scissors[i]);break;
    case VK_DYNAMIC_STATE_LINE_WIDTH:if(s.known&(1u<<state))d.CmdSetLineWidth(cmd,s.line_width);break;
    case VK_DYNAMIC_STATE_DEPTH_BIAS:if(s.known&(1u<<state))d.CmdSetDepthBias(cmd,s.depth_bias[0],s.depth_bias[1],s.depth_bias[2]);break;
    case VK_DYNAMIC_STATE_BLEND_CONSTANTS:if(s.known&(1u<<state))d.CmdSetBlendConstants(cmd,s.blend_constants.data());break;
    case VK_DYNAMIC_STATE_DEPTH_BOUNDS:if(s.known&(1u<<state))d.CmdSetDepthBounds(cmd,s.depth_bounds[0],s.depth_bounds[1]);break;
    case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:case VK_DYNAMIC_STATE_STENCIL_REFERENCE:{
        const unsigned kind=state-VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
        const PFN_vkCmdSetStencilReference function=kind==0?d.CmdSetStencilCompareMask:kind==1?d.CmdSetStencilWriteMask:d.CmdSetStencilReference;
        for(unsigned i=0;i<2;++i){const auto face=i?VK_STENCIL_FACE_BACK_BIT:VK_STENCIL_FACE_FRONT_BIT;
            if(s.stencil_faces[kind]&face)function(cmd,face,s.stencil[kind][i]);}break;
    }
    default:break;
    }
}
}
void NativeGraphicsRestore::record(Device* device,VkCommandBuffer command,VkPipeline selected) const {
    const auto& d=device->d;
    // Preserve values for future pipelines too. Rebinding the original static
    // pipeline invalidates exactly its static states; dynamic states replay last.
    for(int value=VK_DYNAMIC_STATE_VIEWPORT;value<=VK_DYNAMIC_STATE_STENCIL_REFERENCE;++value)
        replay(d,command,dynamic,VkDynamicState(value));
    if(selected || pipeline)d.CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,selected?selected:pipeline);
    for(const auto state:required)replay(d,command,dynamic,state);
    record_sets(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
    for(const auto& vertex:vertices)d.CmdBindVertexBuffers(command,vertex.slot,1,&vertex.buffer,&vertex.offset);
    if(index)d.CmdBindIndexBuffer(command,index,index_offset,index_type);
}
bool prepare_native_graphics_restore(Device* device,const CmdBuf* cb,NativeGraphicsRestore& output,std::string& why,bool resource_lock_held){
    auto refuse=[&](const char* text){why=text;return false;};
    if(!device || !cb || device->passive || !g_cfg.track_resources || !cb->graphics_sets_complete || !cb->graphics_dynamic.complete)
        return refuse("Graphics bindings and dynamic state are not completely tracked");
    if(cb->m2_extended_bind)return refuse("Graphics restoration needs exact effective vertex/index binding windows");
    if(cb->gfx_pipeline && !cb->gfx_bound)return refuse("Actual graphics pipeline is unknown");
    if(!device->d.CmdBindPipeline || !device->d.CmdBindVertexBuffers || !device->d.CmdBindIndexBuffer ||
       !commands_available(device->d,cb->graphics_dynamic))return refuse("Graphics restoration commands are unavailable");
    std::shared_lock<std::shared_mutex> lock(device->res_mx,std::defer_lock);if(!resource_lock_held)lock.lock();
    NativeGraphicsRestore next;next.pipeline=cb->gfx_bound;next.dynamic=cb->graphics_dynamic;
    if(cb->gfx_pipeline){
        const auto pipeline=device->pipes.find(cb->gfx_pipeline);
        if(pipeline==device->pipes.end() || !pipeline->second.recipe || !pipeline->second.recipe->geometry_complete || pipeline->second.recipe->compute)
            return refuse("Graphics restoration needs the original complete pipeline recipe");
        const auto& recipe=*pipeline->second.recipe;next.required=recipe.dynamic;
        next.viewport_count=recipe.viewport_count;next.scissor_count=recipe.scissor_count;
        for(const auto state:next.required){
            if(state==VK_DYNAMIC_STATE_VIEWPORT || state==VK_DYNAMIC_STATE_SCISSOR){
                const auto count=state==VK_DYNAMIC_STATE_VIEWPORT?next.viewport_count:next.scissor_count;
                const auto mask=state==VK_DYNAMIC_STATE_VIEWPORT?next.dynamic.viewport_mask:next.dynamic.scissor_mask;
                if(!count || count>GraphicsDynamicState::viewport_limit || (mask&((1u<<count)-1))!=((1u<<count)-1))
                    return refuse("Graphics restoration lacks the complete viewport/scissor array");
            }else if(state>=VK_DYNAMIC_STATE_LINE_WIDTH && state<=VK_DYNAMIC_STATE_DEPTH_BOUNDS){
                if(!(next.dynamic.known&(1u<<state)))return refuse("Graphics restoration lacks a required dynamic value");
            }else if(state>=VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK && state<=VK_DYNAMIC_STATE_STENCIL_REFERENCE){
                if(next.dynamic.stencil_faces[state-VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK]!=VK_STENCIL_FACE_FRONT_AND_BACK)
                    return refuse("Graphics restoration lacks a stencil face value");
            }else return refuse("Graphics restoration requires an extended dynamic-state adapter");
        }
    }
    auto private_buffer=[&](const std::shared_ptr<const efmi::frontend::NativeResource>& resource,bool vertex){
        if(!resource || !resource->bound || !resource->buffer || !resource->keep || resource->offset>=resource->size ||
           !(resource->usage&(vertex?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:VK_BUFFER_USAGE_INDEX_BUFFER_BIT)))return false;
        if(!vertex && ((resource->index_size!=2 && resource->index_size!=4) || resource->offset%resource->index_size))return false;
        next.resources.push_back(resource);return true;
    };
    for(uint32_t slot=0;slot<kMaxVb;++slot){
        if(cb->native_geometry){const auto found=cb->native_geometry->vertices.find(slot);
            if(found!=cb->native_geometry->vertices.end()){
                if(!private_buffer(found->second,true))return refuse("Graphics restoration needs a retained effective vertex binding");
                next.vertices.push_back({slot,VkBuffer(found->second->buffer),found->second->offset});continue;
            }
        }
        const auto& vertex=cb->vb[slot];if(!vertex.buf)continue;
        const auto found=device->bufs.find(vertex.buf);
        if(found==device->bufs.end() || vertex.off>=found->second.size || !(found->second.usage&VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
            return refuse("Graphics restoration vertex binding is unavailable");
        next.vertices.push_back({slot,vertex.buf,vertex.off});
    }
    if(cb->native_geometry && cb->native_geometry->index_overridden){const auto& index=cb->native_geometry->index;
        if(!private_buffer(index,false))return refuse("Graphics restoration needs a retained effective index binding");
        next.index=VkBuffer(index->buffer);next.index_offset=index->offset;next.index_type=index->index_size==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32;
    }else if(cb->ib){const auto found=device->bufs.find(cb->ib);
        if(found==device->bufs.end() || !(found->second.usage&VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
           (cb->index_size!=2 && cb->index_size!=4) || cb->ib_off%cb->index_size || cb->ib_off>=found->second.size)
            return refuse("Graphics restoration index binding is unavailable");
        next.index=cb->ib;next.index_offset=cb->ib_off;next.index_type=cb->index_size==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32;
    }
    if(!prepare_native_descriptor_restore(device,cb->sets,cb->dyn,next,why,true))return false;
    output=std::move(next);why.clear();return true;
}
}
