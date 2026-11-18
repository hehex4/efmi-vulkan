#pragma once
#include "pipeline_recipe.h"
#include "../../frontend/xxmi/native_graphics_state.h"

namespace efvk {
inline std::shared_ptr<const efmi::frontend::NativeGraphicsState> source_graphics_state(const CmdBuf& cb,const GraphicsRecipe& recipe){
    if(recipe.compute || !recipe.geometry_complete || !cb.graphics_dynamic.complete)return {};
    auto state=std::make_shared<efmi::frontend::NativeGraphicsState>();
    state->known=15;state->topology=recipe.assembly.topology;state->restart=recipe.assembly.primitiveRestartEnable!=0;
    state->raster=recipe.raster;state->raster.pNext=nullptr;
    state->depth=recipe.depth;state->depth.pNext=nullptr;
    state->blend=recipe.blend_attachments;
    std::copy_n(recipe.blend.blendConstants,4,state->blend_constants.begin());
    state->sample_mask=recipe.sample_mask.empty()?UINT32_MAX:recipe.sample_mask[0];
    state->alpha_to_coverage=recipe.multisample.alphaToCoverageEnable!=0;
    if(recipe.blend.logicOpEnable || recipe.multisample.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT ||
       recipe.multisample.sampleShadingEnable || recipe.multisample.alphaToOneEnable)state->known&=~1u;
    if(recipe.viewport_count==1 && recipe.viewports.size()==1){state->viewport=recipe.viewports[0];state->viewport_known=true;}
    if(recipe.scissor_count==1 && recipe.scissors.size()==1){state->scissor=recipe.scissors[0];state->scissor_known=true;}
    const auto& dynamic=cb.graphics_dynamic;
    for(const auto value:recipe.dynamic){
        switch(value){
        case VK_DYNAMIC_STATE_VIEWPORT:
            state->viewport_known=recipe.viewport_count==1 && (dynamic.viewport_mask&1);
            if(state->viewport_known)state->viewport=dynamic.viewports[0];break;
        case VK_DYNAMIC_STATE_SCISSOR:
            state->scissor_known=recipe.scissor_count==1 && (dynamic.scissor_mask&1);
            if(state->scissor_known)state->scissor=dynamic.scissors[0];break;
        case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:break;
        case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
            if(cb.topology==UINT32_MAX)state->known&=~8u;else state->topology=VkPrimitiveTopology(cb.topology);break;
        case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
            if(cb.primitive_restart==UINT32_MAX)state->known&=~8u;else state->restart=cb.primitive_restart!=0;break;
        case VK_DYNAMIC_STATE_LINE_WIDTH:
            if(!(dynamic.known&(1u<<value)))state->known&=~4u;
            else state->raster.lineWidth=dynamic.line_width;break;
        case VK_DYNAMIC_STATE_DEPTH_BIAS:
            if(!(dynamic.known&(1u<<value)))state->known&=~4u;
            else {state->raster.depthBiasConstantFactor=dynamic.depth_bias[0];state->raster.depthBiasClamp=dynamic.depth_bias[1];state->raster.depthBiasSlopeFactor=dynamic.depth_bias[2];}break;
        case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
            if(!(dynamic.known&(1u<<value)))state->known&=~1u;
            else state->blend_constants=dynamic.blend_constants;break;
        case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
            if(!(dynamic.known&(1u<<value)))state->known&=~2u;
            else {state->depth.minDepthBounds=dynamic.depth_bounds[0];state->depth.maxDepthBounds=dynamic.depth_bounds[1];}break;
        case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:case VK_DYNAMIC_STATE_STENCIL_REFERENCE:{
            const auto index=value-VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
            if(dynamic.stencil_faces[index]!=VK_STENCIL_FACE_FRONT_AND_BACK){state->known&=~2u;break;}
            auto set=[&](VkStencilOpState& face,uint32_t v){if(index==0)face.compareMask=v;else if(index==1)face.writeMask=v;else face.reference=v;};
            set(state->depth.front,dynamic.stencil[index][0]);set(state->depth.back,dynamic.stencil[index][1]);break;}
        default:state->known=0;state->viewport_known=state->scissor_known=false;break;
        }
    }
    return state;
}
}
