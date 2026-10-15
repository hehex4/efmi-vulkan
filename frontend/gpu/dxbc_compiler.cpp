#include "dxbc_compiler.h"
#include "dxbc_float32.h"
#include "dxbc_bindings.h"
#include <windows.h>
#include <vkd3d_shader.h>
#include <spirv_reflect.h>
#include <map>
#include <set>
#include <stdexcept>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
VkShaderStageFlagBits stage_flag(char stage){
    switch(stage){case 'v':return VK_SHADER_STAGE_VERTEX_BIT;case 'p':return VK_SHADER_STAGE_FRAGMENT_BIT;
    case 'g':return VK_SHADER_STAGE_GEOMETRY_BIT;case 'h':return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case 'd':return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;case 'c':return VK_SHADER_STAGE_COMPUTE_BIT;
    default:throw std::runtime_error("Unknown bytecode shader stage");}
}
char resource_class(vkd3d_shader_descriptor_type type){
    switch(type){case VKD3D_SHADER_DESCRIPTOR_TYPE_SRV:return 't';case VKD3D_SHADER_DESCRIPTOR_TYPE_UAV:return 'u';
    case VKD3D_SHADER_DESCRIPTOR_TYPE_CBV:return 'b';case VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER:return 's';
    default:throw std::runtime_error("Unknown bytecode descriptor class");}
}
void signatures(const vkd3d_shader_signature& source,std::vector<ShaderSignature>& out,bool output){
    require(source.element_count<=4096,"Bytecode signature exceeds budget");
    for(unsigned i=0;i<source.element_count;++i){const auto& e=source.elements[i];
        require(e.semantic_name && strnlen(e.semantic_name,1025)<=1024 && e.component_type<=VKD3D_SHADER_COMPONENT_FLOAT,
            "Bytecode signature needs another component adapter");
        out.push_back({e.semantic_name,e.semantic_index,e.register_index,unsigned(e.component_type),unsigned(e.sysval_semantic),
            e.stream_index,e.mask,output?((~e.used_mask)&15u):e.used_mask});
    }
}
void interfaces(const SpvReflectInterfaceVariable* v,const std::vector<ShaderSignature>& signature,
                std::vector<ShaderInterface>& out,bool pixel_output){
    require(!v->member_count,"Bytecode interface blocks need a member adapter");
    ShaderInterface entry{v->name?v->name:"","",v->location,uint32_t(v->built_in),VkFormat(v->format),v->spirv_id};
    if(v->built_in<0){
        require(v->component==0 || v->component==UINT32_MAX,"Packed bytecode interface needs component-aware linking");
        const ShaderSignature* match=nullptr;
        for(const auto& s:signature){const auto location=pixel_output && s.system_value==VKD3D_SHADER_SV_TARGET?s.index:s.register_index;
            if(location!=v->location)continue;
            require(!match,"Ambiguous bytecode semantic register needs component-aware linking");match=&s;}
        require(match!=nullptr,"Bytecode interface lacks an original signature register");
        entry.semantic=match->semantic+std::to_string(match->index);
    }
    out.push_back(std::move(entry));
}
struct Reflection {
    SpvReflectShaderModule module{};bool valid=false;
    ~Reflection(){if(valid)spvReflectDestroyShaderModule(&module);}
};
}
struct DxbcCompiler::Impl {
    HMODULE library=nullptr;
    PFN_vkd3d_shader_compile compile=nullptr;
    PFN_vkd3d_shader_scan scan=nullptr;
    PFN_vkd3d_shader_free_shader_code free_code=nullptr;
    PFN_vkd3d_shader_free_messages free_messages=nullptr;
    PFN_vkd3d_shader_free_scan_descriptor_info free_descriptors=nullptr;
    PFN_vkd3d_shader_free_scan_signature_info free_signatures=nullptr;
    ~Impl(){if(library)FreeLibrary(library);}
};
DxbcCompiler::DxbcCompiler(const std::filesystem::path& path):impl_(std::make_unique<Impl>()){
    require(path.is_absolute(),"Bytecode compiler path must be explicit and absolute");
    require(!GetEnvironmentVariableA("VKD3D_SHADER_DUMP_PATH",nullptr,0) && !GetEnvironmentVariableA("VKD3D_SHADER_CONFIG",nullptr,0),
        "Bytecode compiler ambient dump/config overrides are outside the owned contract");
    impl_->library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(impl_->library!=nullptr,"Cannot load configured bytecode compiler");
#define LOAD(field,name) impl_->field=reinterpret_cast<decltype(impl_->field)>(GetProcAddress(impl_->library,"vkd3d_shader_" #name));require(impl_->field,"Missing bytecode compiler entry " #name);
    LOAD(compile,compile) LOAD(scan,scan) LOAD(free_code,free_shader_code) LOAD(free_messages,free_messages)
    LOAD(free_descriptors,free_scan_descriptor_info) LOAD(free_signatures,free_scan_signature_info)
#undef LOAD
    auto version=reinterpret_cast<PFN_vkd3d_shader_get_version>(GetProcAddress(impl_->library,"vkd3d_shader_get_version"));
    unsigned major=0,minor=0;require(version!=nullptr,"Bytecode compiler version unavailable");version(&major,&minor);
    require(major==2 && minor==1,"Bytecode compiler differs from the verified release");
}
DxbcCompiler::~DxbcCompiler()=default;
bool DxbcCompiler::compile(const std::vector<uint8_t>& bytes,char stage,HlslProgram& output,std::string& reason,DxbcFloat32Mode mode){
    reason.clear();try{
        require(bytes.size()>=32 && bytes.size()<=(4u<<20) && !memcmp(bytes.data(),"DXBC",4),"Bytecode input is not a bounded DXBC container");
        require(mode==DxbcFloat32Mode::Hardware || mode==DxbcFloat32Mode::Software,"Unknown bytecode float32 policy");
        HlslProgram next;next.stage=stage_flag(stage);next.dxbc=bytes;next.vulkan_version=VK_API_VERSION_1_1;
        // Full source instructions and signatures, not optional RDEF/debug chunks,
        // define descriptors after signature-aware original assembly.
        struct Scan {
            Impl& api;vkd3d_shader_scan_descriptor_info descriptors{VKD3D_SHADER_STRUCTURE_TYPE_SCAN_DESCRIPTOR_INFO};
            vkd3d_shader_scan_signature_info signatures{VKD3D_SHADER_STRUCTURE_TYPE_SCAN_SIGNATURE_INFO};
            char* messages=nullptr;vkd3d_shader_code code{};bool scanned=false,compiled=false;
            ~Scan(){api.free_messages(messages);if(compiled)api.free_code(&code);
                if(scanned){api.free_descriptors(&descriptors);api.free_signatures(&signatures);}}
        } scan{*impl_};
        auto messages=[&]{if(scan.messages){const auto size=strnlen(scan.messages,(1u<<20)+1);
            require(size<=(1u<<20) && next.diagnostics.size()<=(1u<<20)-size,"Bytecode compiler diagnostics exceed budget");
            next.diagnostics.append(scan.messages,size);impl_->free_messages(scan.messages);scan.messages=nullptr;}};
        vkd3d_shader_compile_option options[]={
            {VKD3D_SHADER_COMPILE_OPTION_API_VERSION,VKD3D_SHADER_API_VERSION_2_1},
            {VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV,VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV_STORAGE_BUFFER},
            {VKD3D_SHADER_COMPILE_OPTION_WRITE_TESS_GEOM_POINT_SIZE,0},
            {VKD3D_SHADER_COMPILE_OPTION_FEATURE,0},
            {VKD3D_SHADER_COMPILE_OPTION_DENORMAL_MODE_F32,unsigned(mode==DxbcFloat32Mode::Software?
                VKD3D_SHADER_DENORMAL_MODE_ANY:VKD3D_SHADER_DENORMAL_MODE_FLUSH_TO_ZERO)},
            {VKD3D_SHADER_COMPILE_OPTION_TYPED_UAV,VKD3D_SHADER_COMPILE_OPTION_TYPED_UAV_READ_FORMAT_UNKNOWN}};
        vkd3d_shader_compile_info info{VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO};
        info.source={bytes.data(),bytes.size()};info.source_type=VKD3D_SHADER_SOURCE_DXBC_TPF;
        info.target_type=VKD3D_SHADER_TARGET_SPIRV_BINARY;info.options=options;info.option_count=unsigned(std::size(options));
        info.log_level=VKD3D_SHADER_LOG_WARNING;info.source_name="owned-source.dxbc";
        scan.descriptors.next=&scan.signatures;info.next=&scan.descriptors;
        auto result=impl_->scan(&info,&scan.messages);scan.scanned=result>=0;messages();
        if(result<0)throw std::runtime_error("Bytecode scan failed: "+next.diagnostics);
        const auto buffers=dxbc_buffer_contracts(bytes);
        // Scalar SSBOs cannot represent arbitrary typed-format conversion. Use
        // texel views for the complete program when any typed buffer UAV occurs.
        for(const auto& b:buffers)if(b.first.first=='u' && b.second.kind==ShaderBufferKind::Typed)
            options[1].value=VKD3D_SHADER_COMPILE_OPTION_BUFFER_UAV_STORAGE_TEXEL_BUFFER;
        signatures(scan.signatures.input,next.input_signature,false);signatures(scan.signatures.output,next.output_signature,true);
        signatures(scan.signatures.patch_constant,next.patch_signature,true);
        require(scan.descriptors.descriptor_count<=512,"Bytecode descriptors exceed budget");
        std::vector<vkd3d_shader_resource_binding> mapping;std::vector<vkd3d_shader_uav_counter_binding> counters;
        std::map<uint32_t,std::pair<const vkd3d_shader_descriptor_info*,bool>> source;
        for(unsigned i=0;i<scan.descriptors.descriptor_count;++i){const auto& d=scan.descriptors.descriptors[i];const char type=resource_class(d.type);
            const auto limit=type=='b'?14u:type=='s'?16u:type=='u'?64u:128u;
            require(!d.register_space && d.register_index<limit && d.count && d.count<=limit-d.register_index,
                "Bytecode descriptor range exceeds original register contract");
            const auto binding=HlslCompiler::binding(stage,type,d.register_index);
            require(source.emplace(binding,std::make_pair(&d,false)).second,"Bytecode register aliases another declaration");
            const bool buffer=d.resource_type==VKD3D_SHADER_RESOURCE_BUFFER;
            mapping.push_back({d.type,0,d.register_index,VKD3D_SHADER_VISIBILITY_ALL,
                unsigned(buffer?VKD3D_SHADER_BINDING_FLAG_BUFFER:VKD3D_SHADER_BINDING_FLAG_IMAGE),{0,binding,d.count}});
            if(d.flags&VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_UAV_COUNTER){
                require(type=='u' && d.count==1,"Bytecode counter array requires another adapter");
                const auto counter=HlslCompiler::binding(stage,type,d.register_index,true);
                counters.push_back({0,d.register_index,VKD3D_SHADER_VISIBILITY_ALL,{0,counter,1},0});
                source.emplace(counter,std::make_pair(&d,true));}
        }
        vkd3d_shader_spirv_target_info target{VKD3D_SHADER_STRUCTURE_TYPE_SPIRV_TARGET_INFO};
        target.environment=VKD3D_SHADER_SPIRV_ENVIRONMENT_VULKAN_1_1;
        const vkd3d_shader_spirv_extension float_controls=VKD3D_SHADER_SPIRV_EXTENSION_KHR_FLOAT_CONTROLS;
        if(mode==DxbcFloat32Mode::Hardware){target.extensions=&float_controls;target.extension_count=1;}
        vkd3d_shader_interface_info interface_info{VKD3D_SHADER_STRUCTURE_TYPE_INTERFACE_INFO};interface_info.next=&target;
        interface_info.bindings=mapping.data();interface_info.binding_count=unsigned(mapping.size());
        interface_info.uav_counters=counters.data();interface_info.uav_counter_count=unsigned(counters.size());info.next=&interface_info;
        result=impl_->compile(&info,&scan.code,&scan.messages);scan.compiled=result>=0;messages();
        if(result<0)throw std::runtime_error("Bytecode translation failed: "+next.diagnostics);
        require(scan.code.code && scan.code.size>=20 && scan.code.size<=(32u<<20) && !(scan.code.size%4),"Translated bytecode module exceeds budget");
        next.spirv.resize(scan.code.size/4);memcpy(next.spirv.data(),scan.code.code,scan.code.size);
        lower_dxbc_float32(next.spirv,mode==DxbcFloat32Mode::Software);
        Reflection reflection;
        require(spvReflectCreateShaderModule(next.spirv.size()*4,next.spirv.data(),&reflection.module)==SPV_REFLECT_RESULT_SUCCESS,"Bytecode SPIR-V reflection failed");reflection.valid=true;
        auto& m=reflection.module;require(m.entry_point_count==1 && m.shader_stage==unsigned(next.stage),"Bytecode stage differs from its requested execution stage");
        for(unsigned i=0;i<m.capability_count;++i)next.capabilities.push_back(unsigned(m.capabilities[i].value));
        next.threads[0]=m.entry_points[0].local_size.x;next.threads[1]=m.entry_points[0].local_size.y;next.threads[2]=m.entry_points[0].local_size.z;
        for(unsigned i=0;i<m.input_variable_count;++i)interfaces(m.input_variables[i],next.input_signature,next.inputs,false);
        for(unsigned i=0;i<m.output_variable_count;++i)interfaces(m.output_variables[i],next.output_signature,next.outputs,stage=='p');
        std::set<uint32_t> occupied;
        for(unsigned i=0;i<m.descriptor_binding_count;++i){const auto& b=m.descriptor_bindings[i];const auto found=source.find(b.binding);
            require(!b.set && found!=source.end() && occupied.insert(b.binding).second,"Bytecode descriptor lost its original register mapping");
            const auto& d=*found->second.first;ShaderBinding entry;
            entry.name=b.name?b.name:"";entry.source_type=resource_class(d.type);entry.slot=d.register_index;
            entry.counter=found->second.second;entry.count=b.count;entry.binding=b.binding;entry.type=VkDescriptorType(b.descriptor_type);
            if(d.resource_type==VKD3D_SHADER_RESOURCE_BUFFER && (entry.source_type=='t' || entry.source_type=='u')){
                const auto native=buffers.find({entry.source_type,entry.slot});
                require(native!=buffers.end() && d.count==1,"Scanned buffer lacks an exact declaration contract");
                entry.source_buffer=native->second.kind;entry.source_stride=native->second.stride;
                require(bool(d.flags&VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_RAW_BUFFER)==(entry.source_buffer==ShaderBufferKind::Raw),
                    "Native scanner and buffer declaration disagree");}
            require(b.count==d.count,"Bytecode descriptor count differs from source");
            entry.image_dimension=unsigned(b.image.dim);entry.image_format=unsigned(b.image.image_format);entry.image_depth=b.image.depth;
            entry.image_array=bool(b.image.arrayed);entry.image_multisample=bool(b.image.ms);entry.block_size=b.block.padded_size;
            if(b.type_description){const auto& t=*b.type_description;entry.scalar_width=t.traits.numeric.scalar.width;
                if(t.type_flags&SPV_REFLECT_TYPE_FLAG_FLOAT)entry.scalar_type=1;
                else if(t.type_flags&SPV_REFLECT_TYPE_FLAG_INT)entry.scalar_type=t.traits.numeric.scalar.signedness?3:2;}
            if(b.block.member_count==1){const auto& member=b.block.members[0];entry.stride=member.array.stride;
                if(!entry.stride && member.type_description)entry.stride=member.type_description->traits.array.stride;}
            next.bindings.push_back(std::move(entry));
        }
        output=std::move(next);return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
}
