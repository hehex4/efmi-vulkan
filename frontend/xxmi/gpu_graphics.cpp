#include "gpu_shaders.h"
#include "image_formats.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace efmi::frontend {
namespace {
using Target=ResourceCopyTargetType;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint32_t bits(float value){uint32_t result;memcpy(&result,&value,4);return result;}
bool compare_valid(D3D11_COMPARISON_FUNC v){return v>=D3D11_COMPARISON_NEVER && v<=D3D11_COMPARISON_ALWAYS;}
bool stencil_valid(const D3D11_DEPTH_STENCILOP_DESC& s){return compare_valid(s.StencilFunc) &&
    s.StencilFailOp>=1 && s.StencilFailOp<=8 && s.StencilDepthFailOp>=1 && s.StencilDepthFailOp<=8 && s.StencilPassOp>=1 && s.StencilPassOp<=8;}
VkCompareOp compare(D3D11_COMPARISON_FUNC value){require(compare_valid(value),"Source comparison function is invalid");return VkCompareOp(value-1);}
VkStencilOp stencil(D3D11_STENCIL_OP value){switch(value){
    case D3D11_STENCIL_OP_KEEP:return VK_STENCIL_OP_KEEP;case D3D11_STENCIL_OP_ZERO:return VK_STENCIL_OP_ZERO;
    case D3D11_STENCIL_OP_REPLACE:return VK_STENCIL_OP_REPLACE;case D3D11_STENCIL_OP_INCR_SAT:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case D3D11_STENCIL_OP_DECR_SAT:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;case D3D11_STENCIL_OP_INVERT:return VK_STENCIL_OP_INVERT;
    case D3D11_STENCIL_OP_INCR:return VK_STENCIL_OP_INCREMENT_AND_WRAP;case D3D11_STENCIL_OP_DECR:return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:throw std::runtime_error("Source stencil operation is invalid");}}
VkBlendFactor factor(D3D11_BLEND value,bool alpha){switch(value){
    case D3D11_BLEND_ZERO:return VK_BLEND_FACTOR_ZERO;case D3D11_BLEND_ONE:return VK_BLEND_FACTOR_ONE;
    case D3D11_BLEND_SRC_COLOR:return VK_BLEND_FACTOR_SRC_COLOR;case D3D11_BLEND_INV_SRC_COLOR:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3D11_BLEND_SRC_ALPHA:return VK_BLEND_FACTOR_SRC_ALPHA;case D3D11_BLEND_INV_SRC_ALPHA:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3D11_BLEND_DEST_ALPHA:return VK_BLEND_FACTOR_DST_ALPHA;case D3D11_BLEND_INV_DEST_ALPHA:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3D11_BLEND_DEST_COLOR:return VK_BLEND_FACTOR_DST_COLOR;case D3D11_BLEND_INV_DEST_COLOR:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3D11_BLEND_SRC_ALPHA_SAT:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3D11_BLEND_BLEND_FACTOR:return alpha?VK_BLEND_FACTOR_CONSTANT_ALPHA:VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3D11_BLEND_INV_BLEND_FACTOR:return alpha?VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default:throw std::runtime_error("Source dual-source blending needs output-index lowering");}}
VkBlendOp blend_op(D3D11_BLEND_OP value){require(value>=1 && value<=5,"Source blend operation is invalid");return VkBlendOp(value-1);}
gpu::GraphicsDescription description(const SourceGraphicsState& source,const gpu::GraphicsDraw& draw,bool pixel,bool geometry){
    gpu::GraphicsDescription result;
    for(const auto& color:draw.colors)result.colors.push_back(color?color->format():VK_FORMAT_UNDEFINED);
    if(draw.depth)result.depth_format=draw.depth->format();
    for(const auto& color:draw.colors)if(color)result.samples=color->owner()->description().samples;
    if(draw.depth)result.samples=draw.depth->owner()->description().samples;
    require(result.samples==VK_SAMPLE_COUNT_1_BIT,"Source multisample raster rules need a native reference adapter");
    if(source.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)result.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    else if(source.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP){result.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;result.primitive_restart=true;}
    else if(geometry && source.topology==D3D11_PRIMITIVE_TOPOLOGY_POINTLIST)result.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    else if(geometry && source.topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST)result.topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if(geometry && source.topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP){result.topology=VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;result.primitive_restart=true;}
    else throw std::runtime_error("Source point/line/adjacency/patch topology requires another raster adapter");
    const auto& r=source.raster;require(r.FillMode==D3D11_FILL_SOLID || r.FillMode==D3D11_FILL_WIREFRAME,"Source fill mode invalid");
    require(r.CullMode>=D3D11_CULL_NONE && r.CullMode<=D3D11_CULL_BACK,"Source cull mode invalid");
    require(!r.DepthBias && !r.DepthBiasClamp && !r.SlopeScaledDepthBias,"Source depth bias needs format-dependent native validation");
    result.raster.polygonMode=r.FillMode==D3D11_FILL_SOLID?VK_POLYGON_MODE_FILL:VK_POLYGON_MODE_LINE;
    result.raster.cullMode=r.CullMode==D3D11_CULL_NONE?VK_CULL_MODE_NONE:r.CullMode==D3D11_CULL_FRONT?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT;
    result.raster.frontFace=r.FrontCounterClockwise?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
    result.raster.depthClampEnable=!r.DepthClipEnable;
    const auto& z=source.depth;result.depth.depthTestEnable=z.DepthEnable;result.depth.depthWriteEnable=z.DepthEnable && z.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL;
    result.depth.depthCompareOp=compare(z.DepthFunc);result.depth.stencilTestEnable=z.StencilEnable;
    auto face=[&](const D3D11_DEPTH_STENCILOP_DESC& f){return VkStencilOpState{stencil(f.StencilFailOp),stencil(f.StencilPassOp),stencil(f.StencilDepthFailOp),compare(f.StencilFunc),z.StencilReadMask,z.StencilWriteMask,source.stencil_ref};};
    result.depth.front=face(z.FrontFace);result.depth.back=face(z.BackFace);
    result.sample_mask=source.sample_mask;result.alpha_to_coverage=source.blend.AlphaToCoverageEnable;result.blend_constants=source.blend_factor;
    for(size_t i=0;i<draw.colors.size();++i){const auto& b=source.blend.RenderTarget[source.blend.IndependentBlendEnable?i:0];
        VkPipelineColorBlendAttachmentState out{};out.blendEnable=b.BlendEnable;out.srcColorBlendFactor=factor(b.SrcBlend,false);out.dstColorBlendFactor=factor(b.DestBlend,false);
        out.colorBlendOp=blend_op(b.BlendOp);out.srcAlphaBlendFactor=factor(b.SrcBlendAlpha,true);out.dstAlphaBlendFactor=factor(b.DestBlendAlpha,true);out.alphaBlendOp=blend_op(b.BlendOpAlpha);
        // Vulkan without a fragment shader has undefined color outputs. Native
        // null PS preserves color while still executing depth/stencil tests.
        out.colorWriteMask=pixel?b.RenderTargetWriteMask:0;result.blend.push_back(out);}
    return result;
}
std::vector<uint64_t> pipeline_key(const std::array<std::shared_ptr<gpu::HlslProgram>,5>& stages,const gpu::GraphicsDescription& s){
    std::vector<uint64_t> key;for(const auto& stage:stages)key.push_back(reinterpret_cast<uintptr_t>(stage.get()));
    auto add=[&](std::initializer_list<int64_t> words){key.insert(key.end(),words.begin(),words.end());};
    add({int64_t(s.colors.size())});for(auto color:s.colors)key.push_back(color);
    const auto& r=s.raster;const auto& z=s.depth;
    add({s.depth_format,s.samples,s.topology,s.primitive_restart,s.alpha_to_coverage,s.sample_mask,
        r.polygonMode,r.cullMode,r.frontFace,r.depthClampEnable,z.depthTestEnable,z.depthWriteEnable,z.depthCompareOp,z.stencilTestEnable});
    for(const auto& f:{z.front,z.back})add({f.failOp,f.passOp,f.depthFailOp,f.compareOp,f.compareMask,f.writeMask,f.reference});
    for(const auto& b:s.blend)add({b.blendEnable,b.srcColorBlendFactor,b.dstColorBlendFactor,b.colorBlendOp,b.srcAlphaBlendFactor,b.dstAlphaBlendFactor,b.alphaBlendOp,b.colorWriteMask});
    for(auto v:s.blend_constants)key.push_back(bits(v));
    key.push_back(s.streams.size());for(const auto& b:s.streams)add({b.binding,b.stride,b.inputRate});
    key.push_back(s.divisors.size());for(const auto& b:s.divisors)add({b.binding,b.divisor});
    key.push_back(s.attributes.size());for(const auto& a:s.attributes)add({a.location,a.binding,a.format,a.offset});return key;
}
}
SourceGraphicsState source_graphics_normalize(SourceGraphicsState s){
    const auto defaults=source_graphics_defaults();
    for(auto& b:s.blend.RenderTarget)if(!b.BlendEnable){const auto mask=b.RenderTargetWriteMask;b=defaults.blend.RenderTarget[0];b.RenderTargetWriteMask=mask;}
    if(!s.blend.IndependentBlendEnable)for(unsigned i=1;i<8;++i)s.blend.RenderTarget[i]=s.blend.RenderTarget[0];
    if(!s.depth.DepthEnable){s.depth.DepthWriteMask=defaults.depth.DepthWriteMask;s.depth.DepthFunc=defaults.depth.DepthFunc;}
    if(!s.depth.StencilEnable){s.depth.StencilReadMask=defaults.depth.StencilReadMask;s.depth.StencilWriteMask=defaults.depth.StencilWriteMask;
        s.depth.FrontFace=defaults.depth.FrontFace;s.depth.BackFace=defaults.depth.BackFace;}
    else if(!stencil_valid(s.depth.FrontFace) || !stencil_valid(s.depth.BackFace))s.depth=defaults.depth;
    return s;
}
void GpuShaderRuntime::set_graphics_state(const SourceGraphicsState& state){graphics_=source_graphics_normalize(state);}
gpu::Sampler GpuShaderRuntime::source_sampler(const D3D11_SAMPLER_DESC& source){
    const uint32_t filter=uint32_t(source.Filter);
    require(!(filter&~0xd5u) && ((filter&0x40u)==0 || (filter&0x55u)==0x55u),"Source sampler reduction/filter needs another adapter");
    std::vector<uint64_t> key{filter,uint64_t(source.AddressU),uint64_t(source.AddressV),uint64_t(source.AddressW),bits(source.MipLODBias),source.MaxAnisotropy,
        uint64_t(source.ComparisonFunc),bits(source.MinLOD),bits(source.MaxLOD)};for(float v:source.BorderColor)key.push_back(bits(v));
    const auto found=source_samplers_.find(key);if(found!=source_samplers_.end())return found->second;
    require(source_samplers_.size()<128,"Source sampler cache budget exceeded");
    VkSamplerCreateInfo info={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.minFilter=filter&0x10?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
    info.magFilter=filter&4?VK_FILTER_LINEAR:VK_FILTER_NEAREST;info.mipmapMode=filter&1?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    auto address=[](D3D11_TEXTURE_ADDRESS_MODE mode){switch(mode){case D3D11_TEXTURE_ADDRESS_WRAP:return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case D3D11_TEXTURE_ADDRESS_MIRROR:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;case D3D11_TEXTURE_ADDRESS_CLAMP:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case D3D11_TEXTURE_ADDRESS_BORDER:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;default:throw std::runtime_error("Source mirror-once sampler needs an enabled address adapter");}};
    info.addressModeU=address(source.AddressU);info.addressModeV=address(source.AddressV);info.addressModeW=address(source.AddressW);
    info.mipLodBias=source.MipLODBias;info.minLod=source.MinLOD;info.maxLod=source.MaxLOD;info.anisotropyEnable=(filter&0x40)!=0;info.maxAnisotropy=float(source.MaxAnisotropy);
    const auto* border=source.BorderColor;
    if(border[0]==0 && border[1]==0 && border[2]==0 && border[3]==0)info.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if(border[0]==0 && border[1]==0 && border[2]==0 && border[3]==1)info.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else if(border[0]==1 && border[1]==1 && border[2]==1 && border[3]==1)info.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    else require(info.addressModeU!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER && info.addressModeV!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER && info.addressModeW!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,"Source custom border color needs an enabled sampler adapter");
    // Executable ordinary sample operations ignore the source comparison bit.
    // Comparison instructions are explicitly refused before descriptor creation.
    info.compareEnable=VK_FALSE;info.compareOp=VK_COMPARE_OP_ALWAYS;
    auto sampler=commands_.sampler(info);source_samplers_.emplace(std::move(key),sampler);return sampler;
}
bool GpuShaderRuntime::draw_graphics(DrawCommand& command,CommandListState& state){
    gpu::GraphicsDraw draw;
    auto arg=[&](unsigned i){const float value=command.args[i].evaluate(&state);require(std::isfinite(value) && double(value)>=INT_MIN && double(value)<=INT_MAX,"Draw expression exceeds original signed argument range");return int32_t(value);};
    auto positive=[&](unsigned i){const auto value=arg(i);require(value>=0,"Draw count or start exceeds supported unsigned range");return uint32_t(value);};
    auto auto_count=[&](Target target){const auto source=resources_.binding(L'\0',target,0);if(!source)return 0u;
        require(bool(source.buffer),"Automatic draw count requires a buffer");const uint32_t stride=target==Target::VERTEX_BUFFER?source.stride:
            source.format==DXGI_FORMAT_R16_UINT?2u:source.format==DXGI_FORMAT_R32_UINT?4u:0u;
        if(!stride || source.offset>=source.description.ByteWidth)return 0u;return (source.description.ByteWidth-source.offset)/stride;};
    switch(command.type){
    case DrawCommandType::DRAW:draw.count=positive(0);draw.first=positive(1);break;
    case DrawCommandType::DRAW_INSTANCED:draw.count=positive(0);draw.instances=positive(1);draw.first=positive(2);draw.first_instance=positive(3);break;
    case DrawCommandType::DRAW_INDEXED:draw.indexed=true;draw.count=positive(0);draw.first=positive(1);draw.vertex_offset=arg(2);break;
    case DrawCommandType::DRAW_INDEXED_INSTANCED:draw.indexed=true;draw.count=positive(0);draw.instances=positive(1);draw.first=positive(2);draw.vertex_offset=arg(3);draw.first_instance=positive(4);break;
    case DrawCommandType::DRAW_INSTANCED_INDIRECT:case DrawCommandType::DRAW_INDEXED_INSTANCED_INDIRECT:{
        draw.indexed=command.type==DrawCommandType::DRAW_INDEXED_INSTANCED_INDIRECT;draw.argument_offset=positive(0);const auto source=resources_.resolve(command.indirect_buffer,state);
        if(!source)return true;require(bool(source.buffer),"Indirect draw arguments must be a buffer");draw.arguments=source.buffer;break;}
    case DrawCommandType::AUTO_VERTEX_COUNT:draw.count=auto_count(Target::VERTEX_BUFFER);break;
    case DrawCommandType::AUTO_INDEX_COUNT:draw.indexed=true;draw.count=auto_count(Target::INDEX_BUFFER);break;
    case DrawCommandType::AUTO_INDEX_INSTANCE_COUNT:if(!state.call_info)return true;draw.indexed=true;draw.count=auto_count(Target::INDEX_BUFFER);draw.instances=state.call_info->InstanceCount;draw.first_instance=state.call_info->FirstInstance;break;
    case DrawCommandType::FROM_CALLER:{const auto* c=state.call_info;if(!c)return true;
        if(c->type==DrawCall::Invalid)return true;
        if(c->type==DrawCall::Draw || c->type==DrawCall::DrawInstanced){draw.count=c->VertexCount;draw.first=c->FirstVertex;}
        else if(c->type==DrawCall::DrawIndexed || c->type==DrawCall::DrawIndexedInstanced){draw.indexed=true;draw.count=c->IndexCount;draw.first=c->FirstIndex;memcpy(&draw.vertex_offset,&c->FirstVertex,4);}
        else throw std::runtime_error("Source caller requires stream-output or imported indirect/dispatch state");
        if(c->type==DrawCall::DrawInstanced || c->type==DrawCall::DrawIndexedInstanced){draw.instances=c->InstanceCount;draw.first_instance=c->FirstInstance;}break;}
    default:throw std::runtime_error("Source DrawAuto requires a stream-output count adapter");}
    if(!stages_[0] || (!draw.arguments && (!draw.count || !draw.instances)) || graphics_.topology==D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)return true;
    require(!stages_[1] && !stages_[2],"Source tessellation execution needs another graphics adapter");
    const auto& viewport=resources_.viewport();if(viewport.Width==0 || viewport.Height==0)return true;
    require(viewport.Width>0 && viewport.Height>0,"Source viewport dimensions are invalid");
    draw.viewport={viewport.TopLeftX,viewport.TopLeftY+viewport.Height,viewport.Width,-viewport.Height,viewport.MinDepth,viewport.MaxDepth};
    auto extent=[&](const gpu::ImageView& image){if(!image)return;const auto size=image->owner()->extent(image->range().baseMipLevel);
        if(!draw.extent.width){draw.extent={size.width,size.height};draw.layers=image->range().layerCount;}
        else{draw.extent.width=std::min(draw.extent.width,size.width);draw.extent.height=std::min(draw.extent.height,size.height);}};
    for(unsigned i=0;i<8;++i){const auto target=resources_.binding(L'\0',Target::RENDER_TARGET,i);
        if(target){require(bool(target.image_view),"Graphics output needs an image attachment");draw.colors.resize(i+1);draw.colors[i]=target.image_view;extent(target.image_view);}}
    const auto depth=resources_.binding(L'\0',Target::DEPTH_STENCIL_TARGET,0);if(depth){require(bool(depth.image_view),"Graphics depth needs an image attachment");draw.depth=depth.image_view;extent(draw.depth);}
    require(draw.extent.width && draw.extent.height,"Attachment-free source draws need an explicit raster extent adapter");
    draw.scissor={{0,0},draw.extent};if(graphics_.raster.ScissorEnable){const auto left=std::max<LONG>(0,scissor_.left),top=std::max<LONG>(0,scissor_.top);
        const auto right=std::max(left,std::min<LONG>(LONG(draw.extent.width),scissor_.right)),bottom=std::max(top,std::min<LONG>(LONG(draw.extent.height),scissor_.bottom));
        draw.scissor={{left,top},{uint32_t(right-left),uint32_t(bottom-top)}};}
    if(draw.indexed){const auto index=resources_.binding(L'\0',Target::INDEX_BUFFER,0);require(bool(index.buffer),"Source null index buffer needs null-fetch emulation");
        require(index.format==DXGI_FORMAT_R16_UINT || index.format==DXGI_FORMAT_R32_UINT,"Source index format is invalid");draw.indices=index.buffer;draw.index_offset=index.offset;
        draw.index_type=index.format==DXGI_FORMAT_R16_UINT?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32;}
    auto config=description(graphics_,draw,bool(stages_[4]),bool(stages_[3]));vertex_inputs(*stages_[0],config,draw);auto key=pipeline_key(stages_,config);
    auto found=graphics_pipelines_.find(key);gpu::Graphics pipeline;
    if(found!=graphics_pipelines_.end())pipeline=found->second;
    else{require(graphics_pipelines_.size()<512,"Source graphics pipeline cache budget exceeded");std::vector<gpu::HlslProgram> programs;
        for(const auto& stage:stages_)if(stage)programs.push_back(*stage);pipeline=commands_.compile_graphics(programs,config);graphics_pipelines_.emplace(std::move(key),pipeline);}
    std::vector<const gpu::HlslProgram*> programs;for(const auto& program:pipeline->programs())programs.push_back(&program);
    commands_.draw(pipeline,descriptors(programs),draw);++draws_;return true;
}
}
