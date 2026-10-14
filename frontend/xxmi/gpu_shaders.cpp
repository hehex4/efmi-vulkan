#include "gpu_shaders.h"
#include "image_formats.h"
#include "globals.h"
#include <cmath>
#include <stdexcept>

namespace efmi::frontend {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::wstring widen(const char* text){std::string s(text);return {s.begin(),s.end()};}
}
GpuShaderRuntime::GpuShaderRuntime(gpu::ResourceDevice device,gpu::ShaderHost host,const std::filesystem::path& compiler,gpu::ResourceBudgets budgets,std::filesystem::path working):
    resources_(device,std::move(budgets)),compiler_(compiler),commands_(resources_.commands(),std::move(host)),api_version_(device.api_version),working_directory_(std::move(working)){
    require(working_directory_.is_absolute(),"Shader legacy working directory must be explicit and absolute");
}
std::vector<std::wstring> GpuShaderRuntime::install(DrawPlanAdapter& adapter){
    require(!adapter_,"GPU shader runtime cannot belong to two generations");adapter_=&adapter;
    resources_.draw_commands([this](auto& c,auto& s,auto& why){return draw(c,s,why);});
    auto boundaries=resources_.install(adapter);
    adapter.shader_commands([this](auto& c,auto& s,auto& why){return run(c,s,why);});
    RuntimeParseCustomShaders([this](const auto& declaration){compile(declaration);});return boundaries;
}
void GpuShaderRuntime::compile(const RuntimeShaderDeclaration& declaration){
    require(shaders_.size()<4096,"CustomShader declaration budget exceeded");Entry entry;
    for(unsigned i=0;i<6;++i){const auto& file=declaration.sources[i];if(file.empty())continue;entry.overrides[i]=true;
        if(!_wcsicmp(file.c_str(),L"null"))continue;
        gpu::HlslRequest request;request.source=declaration.root+declaration.namespace_path+file;
        if(declaration.namespace_path.empty() || !std::filesystem::exists(request.source))request.source=declaration.root+file;
        request.working_directory=working_directory_;request.recursive_include=declaration.recursive_include;
        request.flags=uint32_t(declaration.shader->compile_flags);request.stage="vhdgpc"[i];
        request.vulkan_version=api_version_>=VK_API_VERSION_1_2?VK_API_VERSION_1_2:VK_API_VERSION_1_1;
        auto program=std::make_shared<gpu::HlslProgram>();std::string why;
        if(!compiler_.compile(request,*program,why))throw std::runtime_error("CustomShader source compilation failed: "+why);
        entry.stages[i]=std::move(program);
    }
    shaders_.emplace(declaration.shader,std::move(entry));
}
bool GpuShaderRuntime::run(RunCustomShaderCommand& command,CommandListState& state,std::wstring& why){
    try{
        const auto found=shaders_.find(command.custom_shader);require(found!=shaders_.end(),"CustomShader was not compiled for this generation");
        auto& shader=*command.custom_shader;auto& entry=found->second;
        // Keep original counters so reset_per_frame_limits uses the same state.
        if(shader.max_executions_per_frame){
            if(shader.frame_no!=G->frame_no){shader.frame_no=G->frame_no;shader.executions_this_frame=1;}
            else{const auto previous=shader.executions_this_frame;if(previous<INT_MAX)++shader.executions_this_frame;
                if(previous>=shader.max_executions_per_frame)return true;}
        }
        for(unsigned i=0;i<5;++i)require(!entry.overrides[i],"CustomShader graphics stages need the graphics execution adapter");
        require(!shader.blend_override && !shader.depth_stencil_override && !shader.rs_override && !shader.sampler_override &&
            shader.topology==D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,"CustomShader fixed-function state needs the graphics execution adapter");
        if(entry.overrides[5] && entry.stages[5] && !entry.compute)entry.compute=commands_.compile(*entry.stages[5]);
        struct Restore {
            GpuResourceRuntime& resources;GpuResourceRuntime::OutputState outputs;
            gpu::Compute& current;gpu::Compute saved;
            ~Restore(){current=std::move(saved);resources.restore_outputs(std::move(outputs));}
        } restore{resources_,resources_.save_outputs(),compute_,compute_};
        if(entry.overrides[5])compute_=entry.compute;
        ++shader_calls_;return adapter_->invoke_shader(shader,state,why);
    }catch(const std::exception& e){why=widen(e.what());return false;}
}
bool GpuShaderRuntime::draw(DrawCommand& command,CommandListState& state,std::wstring& why){
    try{
        require(command.type==DrawCommandType::DISPATCH,"Custom draw/indirect dispatch needs another shader execution adapter");
        std::array<uint32_t,3> groups{};for(unsigned i=0;i<3;++i){const float value=command.args[i].evaluate(&state);
            require(std::isfinite(value) && double(value)>=0 && double(value)<=INT_MAX,"Dispatch expression exceeds original signed argument range");groups[i]=uint32_t(int(value));}
        // D3D dispatch with no compute shader, or any zero dimension, has no work.
        if(!compute_ || !groups[0] || !groups[1] || !groups[2])return true;
        std::vector<gpu::ShaderDescriptor> descriptors;
        for(const auto& binding:compute_->program().bindings)for(uint32_t element=0;element<binding.count;++element){
            require(!binding.counter && binding.source_type!='s',"CustomShader UAV counters/samplers need their source-state adapter");
            const auto target=binding.source_type=='b'?ResourceCopyTargetType::CONSTANT_BUFFER:
                binding.source_type=='t'?ResourceCopyTargetType::SHADER_RESOURCE:ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;
            const auto source=resources_.binding(L'c',target,binding.slot+element);
            require(bool(source),"CustomShader accesses a null source slot; null-resource execution requires an adapter");
            gpu::ShaderDescriptor value;value.binding=binding.binding;value.element=element;
            if(source.image)value.image=source.image_view;
            else{
                value.buffer=source.buffer;
                if(target==ResourceCopyTargetType::CONSTANT_BUFFER){value.offset=source.region?source.offset:0;value.size=source.region?source.visible_size:source.buffer->size();}
                else{value.offset=source.view_offset;value.size=source.view_size;
                    if(source.view_format!=DXGI_FORMAT_UNKNOWN && source.view_format!=DXGI_FORMAT_R32_TYPELESS)
                        value.format=source_image_format(source.view_format).format;
                    if(binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && source.description.StructureByteStride)
                        require(source.description.StructureByteStride==binding.stride,"Structured source view stride differs from the compiled shader");
                }
            }
            descriptors.push_back(std::move(value));
        }
        commands_.dispatch(compute_,descriptors,groups);++dispatches_;return true;
    }catch(const std::exception& e){why=widen(e.what());return false;}
}
}
