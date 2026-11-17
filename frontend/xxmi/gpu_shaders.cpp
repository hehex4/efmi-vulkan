#include "gpu_shaders.h"
#include "shader_assembly.h"
#include "../gpu/dxbc_bindings.h"
#include "image_formats.h"
#include "globals.h"
#include <chrono>
#include <cmath>
#include <cfloat>
#include <fstream>
#include <stdexcept>
#include <include/spirv/unified1/spirv.h>

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
    adapter.prepared_original([this]{return patched_compute_;},[this](auto& state,auto& why){return execute_original(state,why);});
    adapter.draw_prepare([this](auto& s,auto& why){try{apply_input_layout(s);return true;}catch(const std::exception& e){why=widen(e.what());return false;}});
    adapter.shader_commands([this](auto& c,auto& s,auto& why){return run(c,s,why);});
    RuntimeParseCustomShaders([this](const auto& declaration){compile(declaration);});return boundaries;
}
void GpuShaderRuntime::enable_cached_bytecode(const std::filesystem::path& library,gpu::DxbcFloat32Mode mode){
    require(!adapter_ && !cached_compiler_,"Cached bytecode translation is enabled once, before installation");
    cached_compiler_=std::make_unique<gpu::DxbcCompiler>(library);cached_mode_=mode;
}
void GpuShaderRuntime::exclude_shaders(std::vector<std::wstring> names){
    require(!adapter_,"Excluded CustomShaders are set before installation");excluded_=std::move(names);
}
namespace {
uint64_t elapsed_us(std::chrono::steady_clock::time_point began){
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count());
}
}
void GpuShaderRuntime::compile(const RuntimeShaderDeclaration& declaration){
    for(const auto& name:excluded_)if(!_wcsicmp(name.c_str(),declaration.name.c_str()))return;
    require(shaders_.size()<4096,"CustomShader declaration budget exceeded");
    // A declaration that fails stays known as failed: the parser skips it and its runs do nothing.
    try{compile_stages(declaration);}catch(...){Entry failed;failed.failed=true;shaders_.emplace(declaration.shader,std::move(failed));throw;}
}
void GpuShaderRuntime::compile_stages(const RuntimeShaderDeclaration& declaration){
    Entry entry;
    for(unsigned i=0;i<6;++i){const auto& file=declaration.sources[i];if(file.empty())continue;entry.overrides[i]=true;
        if(!_wcsicmp(file.c_str(),L"null"))continue;
        gpu::HlslRequest request;request.source=declaration.root+declaration.namespace_path+file;
        if(declaration.namespace_path.empty() || !std::filesystem::exists(request.source))request.source=declaration.root+file;
        request.working_directory=working_directory_;request.recursive_include=declaration.recursive_include;
        request.flags=uint32_t(declaration.shader->compile_flags);request.stage="vhdgpc"[i];
        request.vulkan_version=api_version_>=VK_API_VERSION_1_2?VK_API_VERSION_1_2:VK_API_VERSION_1_1;
        auto program=std::make_shared<gpu::HlslProgram>();std::string why;
        // The original cache name: the HLSL path without its extension, shader model and the
        // compile flags in lowercase hexadecimal. It writes caches only with cache_shaders, which a
        // generation never does beside installed files.
        wchar_t suffix[40];swprintf(suffix,40,L".%cs_5_0.%x.bin",request.stage,request.flags);
        auto cache=request.source;cache.replace_extension(suffix);
        std::error_code source_error,cache_error;
        const auto source_time=std::filesystem::last_write_time(request.source,source_error);
        const auto cache_time=std::filesystem::last_write_time(cache,cache_error);
        if(!source_error && !cache_error && std::filesystem::is_regular_file(cache) && source_time==cache_time){
            require(cached_compiler_!=nullptr,"CustomShader has an original cached bytecode, which needs the bytecode translation");
            const auto size=std::filesystem::file_size(cache);
            require(size>0 && size<=(4u<<20),"Original cached shader bytecode is outside its budget");
            std::vector<uint8_t> bytes(static_cast<size_t>(size));std::ifstream in(cache,std::ios::binary);
            in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));require(bool(in),"Original cached shader bytecode read failed");
            const auto key=cache_?cache_->bytecode_key(bytes,request.stage,uint32_t(cached_mode_)):std::string();
            if(!key.empty() && cache_->load(key,*program)){++shader_stats_.cached;++cached_programs_;entry.stages[i]=std::move(program);continue;}
            const auto translating=std::chrono::steady_clock::now();
            if(!cached_compiler_->compile(bytes,request.stage,*program,why,cached_mode_))
                throw std::runtime_error("CustomShader cached bytecode translation failed: "+why);
            ++shader_stats_.translated;shader_stats_.translate_us+=elapsed_us(translating);
            if(!key.empty())cache_->store(key,*program);
            ++cached_programs_;entry.stages[i]=std::move(program);continue;
        }
        // The program a load of the same source, includes, flags and tools made before (D-126).
        const auto key=cache_?cache_->hlsl_key(request):std::string();
        if(!key.empty() && cache_->load(key,*program)){++shader_stats_.cached;entry.stages[i]=std::move(program);continue;}
        std::vector<uint8_t> reference;
        const auto compiling=std::chrono::steady_clock::now();
        struct Timed {ShaderStats& stats;std::chrono::steady_clock::time_point began;
            ~Timed(){++stats.compiled;stats.compile_us+=elapsed_us(began);}} timed{shader_stats_,compiling};
        if(!compiler_.compile(request,*program,why,cached_compiler_?&reference:nullptr)){
            // DXC refused what the original compiler took (3DMigoto runs that bytecode, e.g. a
            // structured buffer's Length): the bytecode's translation runs instead.
            std::string translation;
            if(reference.empty() || !cached_compiler_->compile(reference,request.stage,*program,translation,cached_mode_))
                throw std::runtime_error("CustomShader source compilation failed: "+why+(translation.empty()?"":"; its bytecode translation: "+translation));
            ++reference_programs_;
        }
        if(!key.empty())cache_->store(key,*program);
        entry.stages[i]=std::move(program);
    }
    shaders_.emplace(declaration.shader,std::move(entry));
}
void GpuShaderRuntime::set_programs(const std::vector<gpu::HlslProgram>& programs){
    require(programs.size()<=6,"Too many original source shader stages");
    std::array<std::shared_ptr<gpu::HlslProgram>,6> next;
    const VkShaderStageFlagBits flags[]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,VK_SHADER_STAGE_GEOMETRY_BIT,VK_SHADER_STAGE_FRAGMENT_BIT,VK_SHADER_STAGE_COMPUTE_BIT};
    for(const auto& program:programs){unsigned i=0;while(i<6 && flags[i]!=program.stage)++i;
        require(i<6 && !next[i],"Original source shader stage is invalid or duplicated");next[i]=std::make_shared<gpu::HlslProgram>(program);}
    auto compute=next[5]?commands_.compile(*next[5]):gpu::Compute{};
    for(unsigned i=0;i<5;++i)stages_[i]=std::move(next[i]);stages_known_=31;compute_=std::move(compute);compute_known_=true;
}
void GpuShaderRuntime::application_compute(const RuntimeDraw& draw){
    compute_.reset();source_compute_.reset();compute_known_=false;patched_compute_=false;
    indirect_arguments_=draw.indirect_arguments;
    stages_.fill(nullptr);source_graphics_.fill(nullptr);stages_known_=0;graphics_known_=0;
    scissor_known_=pixel_samplers_known_=false;known_samplers_.clear();samplers_.clear();
    input_layout_.reset();original_layout_.reset();source_input_elements_=draw.input_elements;source_input_known_=draw.input_layout_known;
    if(!draw.compute && (draw.shader_stages&1))for(unsigned i=0;i<5;++i){
        if(draw.shader_stages&(1u<<i))source_graphics_[i]=draw.source_codes[i];
        else stages_known_|=uint8_t(1u<<i);
    }
    if(!draw.compute || !(draw.shader_stages&32))return;
    source_compute_=draw.source_codes[5];
}
std::shared_ptr<gpu::HlslProgram> GpuShaderRuntime::inherited_graphics_program(const std::shared_ptr<const SourceShaderCode>& code,unsigned stage){
    require(code && stage<5 && code->model()[0]=="vhdgp"[stage],"Inherited graphics bytecode conflicts with its stage");
    const auto found=inherited_graphics_.find(code);if(found!=inherited_graphics_.end())return found->second.program;
    require(cached_compiler_!=nullptr,"Inherited graphics needs bytecode translation");
    auto program=std::make_shared<gpu::HlslProgram>();std::string why;
    const auto key=cache_?cache_->bytecode_key(code->bytes(),"vhdgp"[stage],uint32_t(cached_mode_)|0x10000u):std::string();
    if(!key.empty() && cache_->load(key,*program))++shader_stats_.cached;
    else{
        const auto began=std::chrono::steady_clock::now();
        if(!cached_compiler_->compile(code->bytes(),"vhdgp"[stage],*program,why,cached_mode_))throw std::runtime_error("Inherited graphics translation failed: "+why);
        gpu::dxbc_storage_reads(*program);++shader_stats_.translated;shader_stats_.translate_us+=elapsed_us(began);
        if(!key.empty())cache_->store(key,*program);
    }
    const size_t bytes=code->bytes().size()+program->dxbc.size()+program->spirv.size()*4;
    require(bytes<=(64u<<20),"Inherited graphics exceeds its cache budget");
    while(!inherited_graphics_.empty() && (inherited_graphics_bytes_+bytes>(64u<<20) || inherited_graphics_.size()>=64)){
        const auto old=inherited_graphics_.begin();inherited_graphics_bytes_-=old->second.bytes;inherited_graphics_.erase(old);
    }
    inherited_graphics_.emplace(code,InheritedGraphics{program,bytes});inherited_graphics_bytes_+=bytes;return program;
}
void GpuShaderRuntime::inherit_graphics(){
    for(unsigned i=0;i<5;++i)if(!(stages_known_&(1u<<i)) && source_graphics_[i]){
        stages_[i]=inherited_graphics_program(source_graphics_[i],i);stages_known_|=uint8_t(1u<<i);
    }
    if(!input_layout_ && source_input_known_ && source_graphics_[0]){
        std::vector<SourceVertexElement> elements;
        for(const auto& v:source_input_elements_)elements.push_back({v.semantic,v.index,DXGI_FORMAT(v.format),v.slot,v.offset,D3D11_INPUT_CLASSIFICATION(v.classification),v.step});
        input_layout_=std::make_shared<SourceInputLayout>(std::move(elements),inherited_graphics_program(source_graphics_[0],0)->input_signature);
    }
}
void GpuShaderRuntime::source_patch(unsigned stage,std::shared_ptr<const SourceShaderCode> code){
    require(stage==5 && code && adapter_ && adapter_->planning_prepared(),"Source graphics patch requires inherited graphics execution");
    source_compute_=std::move(code);compute_known_=false;inherit_compute();
    patched_compute_=true;
}
void GpuShaderRuntime::inherit_compute(){
    if(compute_known_ || !source_compute_)return;
    require(source_compute_->model()[0]=='c',"Inherited application bytecode is not a compute shader");
    const auto found=inherited_compute_.find(source_compute_);
    if(found!=inherited_compute_.end()){compute_=found->second.pipeline;compute_known_=true;return;}
    require(cached_compiler_!=nullptr,"Inherited original compute needs bytecode translation");
    gpu::HlslProgram program;std::string why;const auto& code=source_compute_->bytes();
    const auto key=cache_?cache_->bytecode_key(code,'c',uint32_t(cached_mode_)|0x10000u):std::string();
    if(!key.empty() && cache_->load(key,program))++shader_stats_.cached;
    else{
        const auto began=std::chrono::steady_clock::now();
        if(!cached_compiler_->compile(code,'c',program,why,cached_mode_))throw std::runtime_error("Inherited original compute translation failed: "+why);
        gpu::dxbc_storage_reads(program);
        ++shader_stats_.translated;shader_stats_.translate_us+=elapsed_us(began);
        if(!key.empty())cache_->store(key,program);
    }
    const auto bytes=code.size()+program.dxbc.size()+program.spirv.size()*4;
    require(bytes<=(64u<<20),"Inherited original compute exceeds its cache budget");
    auto pipeline=commands_.compile(program);
    while(!inherited_compute_.empty() && (inherited_compute_bytes_+bytes>(64u<<20) || inherited_compute_.size()>=64)){
        const auto old=inherited_compute_.begin();inherited_compute_bytes_-=old->second.bytes;inherited_compute_.erase(old);
    }
    inherited_compute_.emplace(source_compute_,InheritedCompute{pipeline,bytes});inherited_compute_bytes_+=bytes;
    compute_=std::move(pipeline);compute_known_=true;
}
bool GpuShaderRuntime::execute_original(CommandListState& state,std::wstring& why){
    try{require(state.call_info!=nullptr,"Original operation lacks caller data");const auto& call=*state.call_info;
        if(call.type==DrawCall::DispatchIndirect){
            require(adapter_ && adapter_->planning_prepared() && compute_known_ && compute_,"Patched indirect dispatch needs a proven compute pipeline");
            const auto& args=indirect_arguments_;
            require(args.bound && args.buffer && args.keep && (args.usage&VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) &&
                args.offset%4==0 && args.offset<=args.size && args.size-args.offset>=12,"Patched indirect dispatch lacks retained argument range/usage");
            auto buffer=resources_.commands().import_buffer(VkBuffer(args.buffer),args.size,args.usage,VkDeviceMemory(args.memory),
                args.memory_offset,args.memory_size,args.keep);
            commands_.dispatch_indirect(compute_,descriptors({&compute_->program()}),buffer,args.offset);++dispatches_;return true;
        }
        if(call.type==DrawCall::Dispatch){
            if(compute_ && call.ThreadGroupCountX && call.ThreadGroupCountY && call.ThreadGroupCountZ){
                commands_.dispatch(compute_,descriptors({&compute_->program()}),{call.ThreadGroupCountX,call.ThreadGroupCountY,call.ThreadGroupCountZ});++dispatches_;}
            return true;
        }
        DrawCommand command;command.type=DrawCommandType::FROM_CALLER;return draw_graphics(command,state);
    }catch(const std::exception& e){why=widen(e.what());return false;}
}
bool GpuShaderRuntime::run(RunCustomShaderCommand& command,CommandListState& state,std::wstring& why){
    try{
        const auto found=shaders_.find(command.custom_shader);require(found!=shaders_.end(),"CustomShader was not compiled for this generation");
        auto& shader=*command.custom_shader;auto& entry=found->second;
        if(entry.failed)return true;
        // Keep original counters so reset_per_frame_limits uses the same state.
        if(shader.max_executions_per_frame){
            resources_.remember(shader);
            if(shader.frame_no!=G->frame_no){shader.frame_no=G->frame_no;shader.executions_this_frame=1;}
            else{const auto previous=shader.executions_this_frame;if(previous<INT_MAX)++shader.executions_this_frame;
                if(previous>=shader.max_executions_per_frame)return true;}
        }
        if(entry.overrides[5] && entry.stages[5] && !entry.compute)entry.compute=commands_.compile(*entry.stages[5]);
        struct Restore {
            GpuResourceRuntime& resources;GpuResourceRuntime::OutputState outputs;
            gpu::Compute& current;gpu::Compute saved;
            bool& known;bool saved_known;
            decltype(stages_)& stages;decltype(stages_) saved_stages;
            uint8_t& stage_known;uint8_t saved_stage_known;
            uint8_t& graphics_known;uint8_t saved_graphics_known;
            SourceGraphicsState& state;SourceGraphicsState saved_state;
            ~Restore(){current=std::move(saved);known=saved_known;stages=std::move(saved_stages);stage_known=saved_stage_known;
                graphics_known=saved_graphics_known;state=std::move(saved_state);resources.restore_outputs(std::move(outputs));}
        } restore{resources_,resources_.save_outputs(),compute_,compute_,compute_known_,compute_known_,stages_,stages_,stages_known_,stages_known_,graphics_known_,graphics_known_,graphics_,graphics_};
        if(entry.overrides[5]){compute_=entry.compute;compute_known_=true;}
        for(unsigned i=0;i<5;++i)if(entry.overrides[i]){stages_[i]=entry.stages[i];stages_known_|=uint8_t(1u<<i);}
        if(shader.blend_override==1)graphics_known_|=1;
        if(shader.depth_stencil_override==1)graphics_known_|=2;
        if(shader.rs_override==1)graphics_known_|=4;
        if(shader.topology!=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)graphics_known_|=8;
        graphics_=source_graphics_normalize(source_graphics_override(shader,graphics_));
        ++shader_calls_;return adapter_->invoke_shader(shader,state,why);
    }catch(const std::exception& e){why=widen(e.what());return false;}
}
void GpuShaderRuntime::set_sampler(wchar_t stage,unsigned slot,const VkSamplerCreateInfo* info){
    require(stage && wcschr(L"vhdgpc",stage) && slot<16,"Source sampler stage/slot is invalid");
    if(info)samplers_[{stage,slot}]=commands_.sampler(*info);else samplers_.erase({stage,slot});
    known_samplers_.emplace(stage,slot);
}
gpu::Sampler GpuShaderRuntime::sampler(wchar_t stage,unsigned slot){
    if(adapter_ && adapter_->planning_prepared())require(known_samplers_.count({stage,slot}) ||
        (stage==L'p' && (pixel_samplers_known_ || graphics_.pixel_samplers.at(slot).has_value())),
        "Prepared CustomShader inherits an unknown application sampler");
    if(stage==L'p' && graphics_.pixel_samplers.at(slot))return source_sampler(*graphics_.pixel_samplers[slot]);
    const auto found=samplers_.find({stage,slot});if(found!=samplers_.end())return found->second;
    if(!default_sampler_){
        // D3D11 null sampler: linear min/mag/mip, clamp, zero bias, no
        // comparison. Preserve the source LOD range, including its negative end.
        VkSamplerCreateInfo info={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.magFilter=info.minFilter=VK_FILTER_LINEAR;
        info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;info.addressModeU=info.addressModeV=info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxAnisotropy=1;info.compareOp=VK_COMPARE_OP_NEVER;info.minLod=-FLT_MAX;info.maxLod=FLT_MAX;info.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        default_sampler_=commands_.sampler(info);
    }
    return default_sampler_;
}
bool GpuShaderRuntime::draw(DrawCommand& command,CommandListState& state,std::wstring& why){
    try{
        const bool indirect=command.type==DrawCommandType::DISPATCH_INDIRECT;
        const bool prepared_plan=adapter_ && adapter_->planning_prepared();
        if(!indirect && command.type!=DrawCommandType::DISPATCH){
            return draw_graphics(command,state);
        }
        if(prepared_plan)inherit_compute();
        require(!prepared_plan || compute_known_,"Prepared CustomShader inherits an unknown application compute stage");
        gpu::Buffer arguments;uint32_t offset=0;std::array<uint32_t,3> groups{};
        if(indirect){const float value=command.args[0].evaluate(&state);
            require(std::isfinite(value) && double(value)>=0 && double(value)<=UINT32_MAX,"Indirect dispatch offset exceeds original unsigned range");offset=uint32_t(value);
            const auto source=resources_.resolve(command.indirect_buffer,state);if(!source)return true;
            require(bool(source.buffer),"Indirect dispatch source is not a buffer");arguments=source.buffer;
        }else for(unsigned i=0;i<3;++i){const float value=command.args[i].evaluate(&state);
            require(std::isfinite(value) && double(value)>=0 && double(value)<=INT_MAX,"Dispatch expression exceeds original signed argument range");groups[i]=uint32_t(int(value));}
        // D3D dispatch with no compute shader, or any zero dimension, has no work.
        if(!compute_ || (!indirect && (!groups[0] || !groups[1] || !groups[2])))return true;
        const auto descriptors=this->descriptors({&compute_->program()});
        if(indirect)commands_.dispatch_indirect(compute_,descriptors,arguments,offset);
        else commands_.dispatch(compute_,descriptors,groups);
        ++dispatches_;return true;
    }catch(const std::exception& e){why=widen(e.what());return false;}
}
std::vector<gpu::ShaderDescriptor> GpuShaderRuntime::descriptors(const std::vector<const gpu::HlslProgram*>& programs){
        std::vector<gpu::ShaderDescriptor> descriptors;
        for(const auto* program:programs){
            // Source comparison filters also serve ordinary Sample instructions.
            // Dref operations need a separate source comparison-view adapter.
            for(size_t pos=5;pos<program->spirv.size();){const auto word=program->spirv[pos],count=word>>16;const auto op=SpvOp(word&0xffff);
                require(count && count<=program->spirv.size()-pos,"Truncated source shader instruction");
                require(op!=SpvOpImageSampleDrefImplicitLod && op!=SpvOpImageSampleDrefExplicitLod &&
                    op!=SpvOpImageSampleProjDrefImplicitLod && op!=SpvOpImageSampleProjDrefExplicitLod && op!=SpvOpImageDrefGather,
                    "Source comparison sampling needs an instruction-aware image adapter");pos+=count;}
            const wchar_t stage=program->stage==VK_SHADER_STAGE_VERTEX_BIT?L'v':program->stage==VK_SHADER_STAGE_FRAGMENT_BIT?L'p':
                program->stage==VK_SHADER_STAGE_GEOMETRY_BIT?L'g':
                program->stage==VK_SHADER_STAGE_COMPUTE_BIT?L'c':L'\0';
            require(stage!=L'\0',"Source descriptors need another graphics stage adapter");
            for(const auto& binding:program->bindings)for(uint32_t element=0;element<binding.count;++element){
            require(!binding.counter,"CustomShader counter access needs an imported source counter view; original owned INI views have no counters");
            gpu::ShaderDescriptor value;value.binding=binding.binding;value.element=element;
            if(binding.source_type=='s'){value.sampler=sampler(stage,binding.slot+element);descriptors.push_back(std::move(value));continue;}
            const auto target=binding.source_type=='b'?ResourceCopyTargetType::CONSTANT_BUFFER:
                binding.source_type=='t'?ResourceCopyTargetType::SHADER_RESOURCE:ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;
            const auto source=resources_.binding(stage,target,binding.slot+element);
            // D3D11 reads a null view as zero with zero dimensions and drops writes to it.
            if(!source){value.null=true;descriptors.push_back(std::move(value));continue;}
            if(source.image)value.image=source.image_view;
            else{
                value.buffer=source.buffer;
                if(target==ResourceCopyTargetType::CONSTANT_BUFFER){value.offset=source.region?source.offset:0;value.size=source.region?source.visible_size:source.buffer->size();}
                else{value.offset=source.view_offset;value.size=source.view_size;
                    if(source.view_format!=DXGI_FORMAT_UNKNOWN && source.view_format!=DXGI_FORMAT_R32_TYPELESS)
                        value.format=source_image_format(source.view_format).format;
                    const auto kind=binding.source_buffer;
                    if(kind==gpu::ShaderBufferKind::Structured){
                        require(source.view_format==DXGI_FORMAT_UNKNOWN && (source.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
                            source.description.StructureByteStride==binding.source_stride,"Native structured buffer view differs from its bytecode declaration");
                        value.format=VK_FORMAT_R32_UINT;
                    }else if(kind==gpu::ShaderBufferKind::Raw){
                        require(source.view_format==DXGI_FORMAT_R32_TYPELESS && (source.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS),
                            "Native raw buffer declaration requires a raw source view");value.format=VK_FORMAT_R32_UINT;
                    }else if(kind==gpu::ShaderBufferKind::Typed){
                        require(source.view_format!=DXGI_FORMAT_UNKNOWN && source.view_format!=DXGI_FORMAT_R32_TYPELESS &&
                            !(source.description.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED),"Native typed buffer declaration requires a typed source view");
                        require(binding.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || binding.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                            "Typed bytecode buffers require native format conversion");
                    }
                    if(kind==gpu::ShaderBufferKind::Unspecified && binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && source.description.StructureByteStride)
                        require(source.description.StructureByteStride==binding.stride,"Structured source view stride differs from the compiled shader");
                    if(kind!=gpu::ShaderBufferKind::Unspecified && binding.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                        require(binding.stride==4,"Translated buffer storage is not a scalar word array");
                }
            }
            descriptors.push_back(std::move(value));
        }
        }
        return descriptors;
}

}
