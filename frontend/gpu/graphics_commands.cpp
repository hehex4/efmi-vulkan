#include "shader_commands_internal.h"
#include "graphics_link.h"
#include "image_formats.h"
#include <spirv_reflect.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){shader_require(ok,why);}
void checked(VkResult r,const char* why){shader_checked(r,why);}
const ImageFormatInfo& format(VkFormat f){require(f>0 && uint32_t(f)<185,"Graphics format needs an extension adapter");return image_formats[uint32_t(f)];}
void state_valid(const GraphicsDescription& s,const ShaderDevice& d){
    const auto& l=d.host.properties.limits;const auto& f=d.resource.enabled_features;
    require(s.colors.size()<=l.maxColorAttachments && s.colors.size()<=8 && s.blend.size()==s.colors.size(),"Graphics color attachment count invalid");
    require(s.samples && !(uint32_t(s.samples)&(uint32_t(s.samples)-1)) && uint32_t(s.samples)<=64,"Graphics sample count invalid");
    require(s.topology==VK_PRIMITIVE_TOPOLOGY_POINT_LIST || s.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || s.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
        s.topology==VK_PRIMITIVE_TOPOLOGY_LINE_LIST || s.topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,"Graphics topology requires another stage/raster adapter");
    require(!s.primitive_restart || s.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP || s.topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,"Primitive restart requires strip topology");
    const auto& r=s.raster;
    require(r.sType==VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO && !r.pNext && !r.flags &&
        r.lineWidth==1 && r.polygonMode>=VK_POLYGON_MODE_FILL && r.polygonMode<=VK_POLYGON_MODE_LINE &&
        !(r.cullMode&~VK_CULL_MODE_FRONT_AND_BACK) && r.frontFace>=VK_FRONT_FACE_COUNTER_CLOCKWISE && r.frontFace<=VK_FRONT_FACE_CLOCKWISE &&
        std::isfinite(r.depthBiasConstantFactor) && std::isfinite(r.depthBiasClamp) && std::isfinite(r.depthBiasSlopeFactor),"Unsupported graphics raster state");
    require((r.polygonMode==VK_POLYGON_MODE_FILL || f.fillModeNonSolid) && (!r.depthClampEnable || f.depthClamp) &&
        (!r.depthBiasClamp || f.depthBiasClamp),"Graphics raster feature was not enabled");
    const auto& z=s.depth;
    require(z.sType==VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO && !z.pNext && !z.flags && !z.depthBoundsTestEnable &&
        z.depthCompareOp>=VK_COMPARE_OP_NEVER && z.depthCompareOp<=VK_COMPARE_OP_ALWAYS,"Graphics depth bounds/extensions need an adapter");
    for(const auto& face:{z.front,z.back})require(face.failOp>=VK_STENCIL_OP_KEEP && face.failOp<=VK_STENCIL_OP_DECREMENT_AND_WRAP &&
        face.passOp>=VK_STENCIL_OP_KEEP && face.passOp<=VK_STENCIL_OP_DECREMENT_AND_WRAP &&
        face.depthFailOp>=VK_STENCIL_OP_KEEP && face.depthFailOp<=VK_STENCIL_OP_DECREMENT_AND_WRAP &&
        face.compareOp>=VK_COMPARE_OP_NEVER && face.compareOp<=VK_COMPARE_OP_ALWAYS,"Invalid graphics stencil operation");
    auto same=[](const auto& a,const auto& b){return a.blendEnable==b.blendEnable && a.srcColorBlendFactor==b.srcColorBlendFactor &&
        a.dstColorBlendFactor==b.dstColorBlendFactor && a.colorBlendOp==b.colorBlendOp && a.srcAlphaBlendFactor==b.srcAlphaBlendFactor &&
        a.dstAlphaBlendFactor==b.dstAlphaBlendFactor && a.alphaBlendOp==b.alphaBlendOp && a.colorWriteMask==b.colorWriteMask;};
    for(size_t i=0;i<s.colors.size();++i){const auto& b=s.blend[i];
        require(!i || f.independentBlend || same(b,s.blend[0]),"Independent blending was not enabled");
        require(!(b.colorWriteMask&~15u) && b.colorBlendOp>=VK_BLEND_OP_ADD && b.colorBlendOp<=VK_BLEND_OP_MAX &&
            b.alphaBlendOp>=VK_BLEND_OP_ADD && b.alphaBlendOp<=VK_BLEND_OP_MAX,"Invalid graphics color blend operation");
        for(auto factor:{b.srcColorBlendFactor,b.dstColorBlendFactor,b.srcAlphaBlendFactor,b.dstAlphaBlendFactor})
            require(factor>=VK_BLEND_FACTOR_ZERO && factor<=VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,"Dual-source blending needs output-index lowering");
        if(s.colors[i]){const auto& info=format(s.colors[i]);require(info.aspects==VK_IMAGE_ASPECT_COLOR_BIT && !info.compression,"Invalid color attachment format");
            VkFormatProperties properties{};d.resource.GetFormatProperties(d.resource.physical,s.colors[i],&properties);
            require(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,"Color attachment format unsupported");
            require(!b.blendEnable || (properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT),"Attachment format cannot blend");}
    }
    if(s.depth_format){const auto& info=format(s.depth_format);require(info.aspects&(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT),"Invalid depth attachment format");
        VkFormatProperties properties{};d.resource.GetFormatProperties(d.resource.physical,s.depth_format,&properties);
        require(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,"Depth attachment format unsupported");}
    bool any_color=false;for(auto color:s.colors)if(color)any_color=true;
    if(any_color)require(l.framebufferColorSampleCounts&s.samples,"Color sample count unsupported");
    if(s.depth_format){const auto aspects=format(s.depth_format).aspects;
        if(aspects&VK_IMAGE_ASPECT_DEPTH_BIT)require(l.framebufferDepthSampleCounts&s.samples,"Depth sample count unsupported");
        if(aspects&VK_IMAGE_ASPECT_STENCIL_BIT)require(l.framebufferStencilSampleCounts&s.samples,"Stencil sample count unsupported");}
    if(!any_color && !s.depth_format)require(l.framebufferNoAttachmentsSampleCounts&s.samples,"Attachment-free sample count unsupported");
    for(auto v:s.blend_constants)require(std::isfinite(v),"Non-finite blend constants need a native conversion policy");
    require(s.streams.size()<=l.maxVertexInputBindings && s.attributes.size()<=l.maxVertexInputAttributes,"Vertex input count exceeds device limit");
    std::set<uint32_t> slots,locations;
    for(const auto& stream:s.streams)require(stream.binding<l.maxVertexInputBindings && stream.stride<=l.maxVertexInputBindingStride &&
        (stream.inputRate==VK_VERTEX_INPUT_RATE_VERTEX || stream.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE) && slots.insert(stream.binding).second,"Invalid vertex stream");
    require(s.divisors.size()<=s.streams.size(),"Vertex divisor count exceeds stream count");std::set<uint32_t> divisor_slots;
    for(const auto& divisor:s.divisors){const auto stream=std::find_if(s.streams.begin(),s.streams.end(),[&](const auto& b){return b.binding==divisor.binding;});
        require(stream!=s.streams.end() && stream->inputRate==VK_VERTEX_INPUT_RATE_INSTANCE && divisor_slots.insert(divisor.binding).second,"Invalid or duplicate instance divisor binding");
        require(d.host.vertex_divisor && (divisor.divisor || d.host.vertex_zero_divisor) && divisor.divisor<=d.host.max_vertex_divisor,
            "Vertex divisor feature was not enabled or exceeds the device limit");}
    for(const auto& a:s.attributes){require(a.location<l.maxVertexInputAttributes && locations.insert(a.location).second &&
        slots.count(a.binding) && a.offset<=l.maxVertexInputAttributeOffset,"Invalid vertex attribute location/offset");
        const auto& info=format(a.format);require(info.aspects==VK_IMAGE_ASPECT_COLOR_BIT && !info.compression,"Invalid vertex attribute format");
        VkFormatProperties properties{};d.resource.GetFormatProperties(d.resource.physical,a.format,&properties);
        require(properties.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT,"Vertex format unsupported");}
}
bool overlap(const ImageView& a,const ImageView& b){
    if(a->owner()!=b->owner())return false;const auto& x=a->range();const auto& y=b->range();
    return x.baseMipLevel<y.baseMipLevel+y.levelCount && y.baseMipLevel<x.baseMipLevel+x.levelCount &&
        x.baseArrayLayer<y.baseArrayLayer+y.layerCount && y.baseArrayLayer<x.baseArrayLayer+x.layerCount;
}
}
GraphicsPipeline::GraphicsPipeline(std::shared_ptr<ShaderPipelineState> p):impl_(std::move(p)){}
GraphicsPipeline::~GraphicsPipeline()=default;
const std::vector<HlslProgram>& GraphicsPipeline::programs() const {return impl_->programs;}
Graphics ShaderCommands::compile_graphics(const std::vector<HlslProgram>& programs,const GraphicsDescription& state){
    const auto d=impl_->device;require(d->resource.queue_flags&VK_QUEUE_GRAPHICS_BIT,"Graphics requires an owned graphics queue");
    state_valid(state,*d);auto linked=link_graphics(programs,d->host.maintenance4);
    bool geometry=false;for(const auto& program:linked){
        require(program.stage==VK_SHADER_STAGE_VERTEX_BIT || program.stage==VK_SHADER_STAGE_GEOMETRY_BIT || program.stage==VK_SHADER_STAGE_FRAGMENT_BIT,
            "Tessellation execution needs another graphics adapter");
        if(program.stage==VK_SHADER_STAGE_GEOMETRY_BIT){geometry=true;
            require(d->resource.enabled_features.geometryShader,"Geometry shader feature was not enabled");
            validate_geometry(program,state.topology,d->host.properties.limits);}
    }
    require(geometry || state.topology!=VK_PRIMITIVE_TOPOLOGY_POINT_LIST,"Vertex point output needs source point-size lowering");
    const auto& raster_stage=linked.back().stage==VK_SHADER_STAGE_FRAGMENT_BIT?linked[linked.size()-2]:linked.back();
    require(state.raster.rasterizerDiscardEnable || std::any_of(raster_stage.outputs.begin(),raster_stage.outputs.end(),
        [](const auto& value){return value.builtin==SpvBuiltInPosition;}),"Rasterized graphics requires position from the last pre-raster stage");
    auto result=std::make_shared<ShaderPipelineState>(d);result->programs=std::move(linked);result->graphics=state;result->is_graphics=true;
    for(const auto& program:result->programs){require(program.vulkan_version<=d->resource.api_version,"Graphics shader target exceeds this device");
        shader_capabilities(program,*d);d->validate(program);auto usage=shader_usage(program);
        require(!usage.workgroup_bytes,"Graphics stage declares compute workgroup storage");
        result->usage.sampled_pairs.insert(result->usage.sampled_pairs.end(),usage.sampled_pairs.begin(),usage.sampled_pairs.end());
        for(const auto& b:program.bindings)if(b.source_type=='u')require(program.stage==VK_SHADER_STAGE_FRAGMENT_BIT?
            d->resource.enabled_features.fragmentStoresAndAtomics:d->resource.enabled_features.vertexPipelineStoresAndAtomics,
            "Graphics UAV use requires enabled stage storage features");
        if(program.stage==VK_SHADER_STAGE_FRAGMENT_BIT)for(const auto& output:program.outputs)
            if(output.builtin==UINT32_MAX && output.location<state.colors.size() && state.colors[output.location])
                require(format(output.format).scalar_type==format(state.colors[output.location]).scalar_type,"Fragment output scalar class differs from attachment");
    }
    const auto& vertex=result->programs.front();std::set<uint32_t> input_locations;
    for(const auto& input:vertex.inputs)if(input.builtin==UINT32_MAX){input_locations.insert(input.location);
        const auto a=std::find_if(state.attributes.begin(),state.attributes.end(),[&](const auto& item){return item.location==input.location;});
        require(a!=state.attributes.end() && format(a->format).scalar_type==format(input.format).scalar_type,"Graphics vertex input lacks a compatible source attribute");}
    require(input_locations.size()==state.attributes.size(),"Unused vertex attributes need source-layout filtering");
    uint32_t fragment_outputs=uint32_t(state.colors.size());
    for(const auto& program:result->programs)if(program.stage==VK_SHADER_STAGE_FRAGMENT_BIT)for(const auto& b:program.bindings)
        if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || b.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)fragment_outputs+=b.count;
    require(fragment_outputs<=d->host.properties.limits.maxFragmentCombinedOutputResources,"Fragment combined output count exceeds device limit");
    layout(*result);const auto dev=d->resource.device;
    std::vector<VkAttachmentDescription> attachments;std::vector<VkAttachmentReference> colors;
    auto attachment=[&](VkFormat f,bool depth){VkAttachmentDescription a{};a.format=f;a.samples=state.samples;
        a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_GENERAL;attachments.push_back(a);
        return VkAttachmentReference{uint32_t(attachments.size()-1),depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};};
    for(auto color:state.colors)colors.push_back(color?attachment(color,false):VkAttachmentReference{VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_UNDEFINED});
    VkAttachmentReference depth{VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_UNDEFINED};if(state.depth_format)depth=attachment(state.depth_format,true);
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=uint32_t(colors.size());subpass.pColorAttachments=colors.data();
    if(state.depth_format)subpass.pDepthStencilAttachment=&depth;
    VkSubpassDependency deps[2]={{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0},
        {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0}};
    VkRenderPassCreateInfo pass={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=uint32_t(attachments.size());pass.pAttachments=attachments.data();
    pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=2;pass.pDependencies=deps;
    checked(d->CreateRenderPass(dev,&pass,nullptr,&result->pass),"Create owned graphics render pass");
    std::vector<VkPipelineShaderStageCreateInfo> stages;result->modules.reserve(result->programs.size());
    for(const auto& program:result->programs){VkShaderModuleCreateInfo module={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=program.spirv.size()*4;module.pCode=program.spirv.data();
        VkShaderModule handle;checked(d->CreateShaderModule(dev,&module,nullptr,&handle),"Create graphics shader module");result->modules.push_back(handle);
        VkPipelineShaderStageCreateInfo stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stage.stage=program.stage;stage.module=handle;stage.pName="main";stages.push_back(stage);}
    VkPipelineVertexInputStateCreateInfo inputs={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};inputs.vertexBindingDescriptionCount=uint32_t(state.streams.size());inputs.pVertexBindingDescriptions=state.streams.data();
    inputs.vertexAttributeDescriptionCount=uint32_t(state.attributes.size());inputs.pVertexAttributeDescriptions=state.attributes.data();
    VkPipelineVertexInputDivisorStateCreateInfo divisors={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO};
    if(!state.divisors.empty()){divisors.vertexBindingDivisorCount=uint32_t(state.divisors.size());divisors.pVertexBindingDivisors=state.divisors.data();inputs.pNext=&divisors;}
    VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=state.topology;assembly.primitiveRestartEnable=state.primitive_restart;
    VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineMultisampleStateCreateInfo samples={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=state.samples;samples.alphaToCoverageEnable=state.alpha_to_coverage;
    const uint32_t mask[2]={state.sample_mask,state.sample_mask};samples.pSampleMask=mask;
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=uint32_t(state.blend.size());blend.pAttachments=state.blend.data();
    std::copy(state.blend_constants.begin(),state.blend_constants.end(),blend.blendConstants);
    const VkDynamicState dynamic_states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_states;
    VkGraphicsPipelineCreateInfo pipeline={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline.stageCount=uint32_t(stages.size());pipeline.pStages=stages.data();
    pipeline.pVertexInputState=&inputs;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&state.raster;
    pipeline.pMultisampleState=&samples;pipeline.pDepthStencilState=&state.depth;pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;pipeline.layout=result->layout;pipeline.renderPass=result->pass;
    checked(d->CreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&pipeline,nullptr,&result->pipeline),"Create owned graphics pipeline");
    return std::make_shared<GraphicsPipeline>(std::move(result));
}
void ShaderCommands::draw(const Graphics& pipeline,const std::vector<ShaderDescriptor>& bindings,const GraphicsDraw& draw){
    const auto d=impl_->device;auto& commands=impl_->commands;const auto& limits=d->host.properties.limits;
    require(pipeline && pipeline->impl_ && pipeline->impl_->device->domain==d->domain && pipeline->impl_->is_graphics,"Graphics pipeline belongs to another execution domain");
    if(!draw.arguments && (!draw.count || !draw.instances))return;
    const auto& state=pipeline->impl_->graphics;
    require(draw.extent.width && draw.extent.height && draw.layers && draw.extent.width<=limits.maxFramebufferWidth &&
        draw.extent.height<=limits.maxFramebufferHeight && draw.layers<=limits.maxFramebufferLayers && draw.colors.size()==state.colors.size(),"Invalid graphics framebuffer dimensions/count");
    const auto& v=draw.viewport;
    require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width) && std::isfinite(v.height) && v.width>0 && v.height!=0 &&
        v.width<=limits.maxViewportDimensions[0] && std::abs(v.height)<=limits.maxViewportDimensions[1] &&
        v.x>=limits.viewportBoundsRange[0] && v.x+v.width<=limits.viewportBoundsRange[1] &&
        std::min(v.y,v.y+v.height)>=limits.viewportBoundsRange[0] && std::max(v.y,v.y+v.height)<=limits.viewportBoundsRange[1] &&
        std::isfinite(v.minDepth) && std::isfinite(v.maxDepth) && v.minDepth>=0 && v.minDepth<=1 && v.maxDepth>=0 && v.maxDepth<=1,"Invalid graphics viewport");
    require(draw.scissor.offset.x>=0 && draw.scissor.offset.y>=0 && uint64_t(draw.scissor.offset.x)+draw.scissor.extent.width<=INT32_MAX &&
        uint64_t(draw.scissor.offset.y)+draw.scissor.extent.height<=INT32_MAX,"Invalid graphics scissor");
    std::vector<VkImageView> views;std::vector<ImageView> outputs;
    auto target=[&](const ImageView& view,VkFormat expected,bool depth){require(bool(view)==bool(expected),"Graphics target presence differs from pipeline");if(!view)return;
        commands.validate(view->owner());const auto& image=view->owner()->description();const auto& range=view->range();const auto extent=view->owner()->extent(range.baseMipLevel);
        require(view->format()==expected && image.samples==state.samples && range.levelCount==1 && range.layerCount>=draw.layers &&
            (view->type()==VK_IMAGE_VIEW_TYPE_2D || view->type()==VK_IMAGE_VIEW_TYPE_2D_ARRAY) && extent.width>=draw.extent.width && extent.height>=draw.extent.height &&
            (view->usage()&(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)),"Graphics attachment view is incompatible");
        for(const auto& previous:outputs)require(!overlap(previous,view),"Graphics attachment subresources overlap");
        for(const auto& binding:bindings)if(binding.image)require(!overlap(binding.image,view),"Graphics descriptor overlaps an attachment");
        outputs.push_back(view);views.push_back(view->view());};
    for(size_t i=0;i<draw.colors.size();++i)target(draw.colors[i],state.colors[i],false);target(draw.depth,state.depth_format,true);
    std::map<uint32_t,const GraphicsVertexBuffer*> vertices;
    for(const auto& buffer:draw.vertices){require(vertices.emplace(buffer.slot,&buffer).second,"Duplicate graphics vertex buffer");commands.validate(buffer.buffer,buffer.offset,1);
        require(buffer.buffer->usage()&VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,"Graphics stream lacks vertex buffer usage");}
    require(vertices.size()==state.streams.size(),"Graphics vertex stream count differs from pipeline");
    for(const auto& divisor:state.divisors)if(divisor.divisor!=1 && (draw.arguments || draw.first_instance))
        require(d->host.divisor_first_instance,"Nonzero or GPU-produced instance base requires divisor device support");
    for(const auto& stream:state.streams){const auto found=vertices.find(stream.binding);require(found!=vertices.end(),"Missing graphics vertex stream");
        const auto& buffer=*found->second;
        for(const auto& attribute:state.attributes)if(attribute.binding==stream.binding){const uint64_t first=uint64_t(buffer.offset)+attribute.offset;
            require(first<buffer.buffer->size() && format(attribute.format).bytes<=buffer.buffer->size()-first,"Vertex attribute begins outside its buffer");}
        // Indexed and indirect GPU indices remain the caller's domain contract.
        if(!draw.arguments && (!draw.indexed || stream.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE) && stream.stride){
            uint32_t divisor=1;for(const auto& value:state.divisors)if(value.binding==stream.binding)divisor=value.divisor;
            const uint64_t index=stream.inputRate==VK_VERTEX_INPUT_RATE_VERTEX?uint64_t(draw.first)+draw.count-1:
                uint64_t(draw.first_instance)+(divisor?(draw.instances-1)/divisor:0);
            for(const auto& attribute:state.attributes)if(attribute.binding==stream.binding)require(index<=(UINT64_MAX-buffer.offset-attribute.offset)/stream.stride &&
                uint64_t(buffer.offset)+index*stream.stride+attribute.offset<=buffer.buffer->size()-format(attribute.format).bytes,"Direct vertex fetch exceeds source allocation");}}
    if(draw.indexed){require(draw.index_type==VK_INDEX_TYPE_UINT16 || draw.index_type==VK_INDEX_TYPE_UINT32,"Graphics index type unsupported");const uint32_t size=draw.index_type==VK_INDEX_TYPE_UINT16?2:4;
        commands.validate(draw.indices,draw.index_offset,draw.arguments?size:(uint64_t(draw.first)+draw.count)*size);
        require(draw.index_offset%size==0 && (draw.indices->usage()&VK_BUFFER_USAGE_INDEX_BUFFER_BIT),"Graphics index usage/alignment invalid");}
    if(draw.arguments){require(d->resource.enabled_features.drawIndirectFirstInstance,"Owned indirect graphics requires enabled nonzero first-instance support");
        commands.validate(draw.arguments,draw.argument_offset,draw.indexed?sizeof(VkDrawIndexedIndirectCommand):sizeof(VkDrawIndirectCommand));
        require(draw.argument_offset%4==0 && (draw.arguments->usage()&VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT),"Graphics indirect usage/alignment invalid");}
    auto invocation=descriptors(pipeline->impl_,bindings);invocation->graphics=draw;
    VkFramebufferCreateInfo framebuffer={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=pipeline->impl_->pass;
    framebuffer.attachmentCount=uint32_t(views.size());framebuffer.pAttachments=views.data();framebuffer.width=draw.extent.width;framebuffer.height=draw.extent.height;framebuffer.layers=draw.layers;
    checked(d->CreateFramebuffer(d->resource.device,&framebuffer,nullptr,&invocation->framebuffer),"Create retained graphics framebuffer");
    const auto command=commands.command();commands.pending_->shader_owners_.push_back(invocation);
    for(const auto& binding:bindings)if(binding.image)commands.transition(binding.image->owner(),VK_IMAGE_LAYOUT_GENERAL);
    for(const auto& view:outputs)commands.transition(view->owner(),VK_IMAGE_LAYOUT_GENERAL);
    commands.barrier(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|(draw.arguments?VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT:0),VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    VkRenderPassBeginInfo pass={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};pass.renderPass=pipeline->impl_->pass;pass.framebuffer=invocation->framebuffer;pass.renderArea.extent=draw.extent;
    d->CmdBeginRenderPass(command,&pass,VK_SUBPASS_CONTENTS_INLINE);d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->impl_->pipeline);
    d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->impl_->layout,0,1,&invocation->set,0,nullptr);
    d->CmdSetViewport(command,0,1,&draw.viewport);d->CmdSetScissor(command,0,1,&draw.scissor);
    for(const auto& entry:vertices){const auto handle=entry.second->buffer->buffer();d->CmdBindVertexBuffers(command,entry.first,1,&handle,&entry.second->offset);}
    if(draw.indexed)d->CmdBindIndexBuffer(command,draw.indices->buffer(),draw.index_offset,draw.index_type);
    if(draw.arguments){if(draw.indexed)d->CmdDrawIndexedIndirect(command,draw.arguments->buffer(),draw.argument_offset,1,sizeof(VkDrawIndexedIndirectCommand));
        else d->CmdDrawIndirect(command,draw.arguments->buffer(),draw.argument_offset,1,sizeof(VkDrawIndirectCommand));}
    else if(draw.indexed)d->CmdDrawIndexed(command,draw.count,draw.instances,draw.first,draw.vertex_offset,draw.first_instance);
    else d->CmdDraw(command,draw.count,draw.instances,draw.first,draw.first_instance);
    d->CmdEndRenderPass(command);
}
}
