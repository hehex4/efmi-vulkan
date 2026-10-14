#include "gpu_shaders.h"
#include "../gpu/image_formats.h"
#include <set>
#include <stdexcept>

bool FrontendApplyInputLayoutOverrides(std::vector<D3D11_INPUT_ELEMENT_DESC>&,CommandListState&);
namespace efmi::frontend {
namespace {void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
void GpuShaderRuntime::set_input_layout(InputLayout layout){
    require(!original_layout_,"Host input layout cannot replace an active original draw override");input_layout_=std::move(layout);
}
void GpuShaderRuntime::after_draw() noexcept {
    if(original_layout_){input_layout_=std::move(original_layout_);original_layout_.reset();}
}
void GpuShaderRuntime::apply_input_layout(CommandListState& state){
    if(state.input_layout_overrides.empty() || !input_layout_)return;
    auto elements=input_layout_->descriptions();
    if(FrontendApplyInputLayoutOverrides(elements,state)){
        try{auto replacement=std::make_shared<SourceInputLayout>(copy_vertex_elements(elements),input_layout_->signature());
            if(!original_layout_)original_layout_=input_layout_;input_layout_=std::move(replacement);
        }catch(const InvalidInputLayout&){/* Native failed creation preserves the current layout. */}
    }
    state.input_layout_overrides.clear();
}
void GpuShaderRuntime::vertex_inputs(const gpu::HlslProgram& vertex,gpu::GraphicsDescription& config,gpu::GraphicsDraw& draw){
    std::map<uint32_t,VkVertexInputBindingDescription> streams;std::map<uint32_t,gpu::GraphicsVertexBuffer> buffers;
    for(const auto& input:vertex.inputs)if(input.builtin==UINT32_MAX){require(bool(input_layout_),"Source null input layout needs null-fetch lowering");
        const auto semantic=input_semantic(input.semantic);const gpu::ShaderSignature* signature=nullptr;
        for(const auto& candidate:vertex.input_signature)if(!candidate.system_value && input_semantic(candidate.semantic,candidate.index)==semantic){
            require(!signature,"Vertex input has an ambiguous native signature");signature=&candidate;}
        require(signature!=nullptr,"Vertex input lacks its original native signature");
        const auto found=input_layout_->registers().find(signature->register_index);
        require(found!=input_layout_->registers().end(),"Source layout has no fetched value for this native register");const auto& element=found->second;
        const auto format=vertex_format(element.format);require(uint32_t(input.format)<185,"Source vertex input format requires an adapter");
        const auto scalar=gpu::image_formats[format].scalar_type;const bool alpha=element.format==DXGI_FORMAT_B8G8R8X8_UNORM;
        if(alpha || scalar!=gpu::image_formats[input.format].scalar_type)config.input_conversions.push_back({input.location,scalar,alpha});
        const auto source=resources_.binding(L'\0',ResourceCopyTargetType::VERTEX_BUFFER,element.slot);
        require(bool(source.buffer),"Source null vertex buffer needs null-fetch lowering");
        const bool instanced=element.classification==D3D11_INPUT_PER_INSTANCE_DATA;const auto& host=commands_.host();
        const bool divisor=instanced && element.step!=1 && host.vertex_divisor && (element.step || host.vertex_zero_divisor);
        require(!instanced || element.step<=1 || divisor,"Source instance divisor feature was not enabled");
        const uint32_t stride=instanced && !element.step && !divisor?0:source.stride;
        uint64_t offset=source.offset;
        if(instanced && !element.step && !divisor){
            require(!draw.arguments,"Indirect zero-divisor instance fetch requires GPU argument lowering");
            // Native StartInstanceLocation is added after division, including
            // step0. A zero Vulkan binding stride alone would lose that base.
            offset+=uint64_t(draw.first_instance)*source.stride;
        }
        if(streams.emplace(element.slot,VkVertexInputBindingDescription{element.slot,stride,instanced?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX}).second && divisor)
            config.divisors.push_back({element.slot,element.step});
        buffers.emplace(element.slot,gpu::GraphicsVertexBuffer{element.slot,source.buffer,offset});config.attributes.push_back({input.location,element.slot,format,element.offset});
    }
    for(const auto& pair:streams)config.streams.push_back(pair.second);for(const auto& pair:buffers)draw.vertices.push_back(pair.second);
}
}
