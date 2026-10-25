#include "hlsl_compiler.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxcapi.h>
#include <spirv_reflect.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace efmi::gpu {
namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void checked(HRESULT result,const char* why){if(FAILED(result))throw std::runtime_error(std::string(why)+": "+std::to_string(uint32_t(result)));}
void reflected(SpvReflectResult result){require(result==SPV_REFLECT_RESULT_SUCCESS,"SPIR-V reflection failed");}
struct Stage {char key;const char* macro;VkShaderStageFlagBits flag;};
constexpr Stage stages[]={{'v',"VERTEX_SHADER",VK_SHADER_STAGE_VERTEX_BIT},{'h',"HULL_SHADER",VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {'d',"DOMAIN_SHADER",VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},{'g',"GEOMETRY_SHADER",VK_SHADER_STAGE_GEOMETRY_BIT},
    {'p',"PIXEL_SHADER",VK_SHADER_STAGE_FRAGMENT_BIT},{'c',"COMPUTE_SHADER",VK_SHADER_STAGE_COMPUTE_BIT}};
unsigned stage_index(char key){for(unsigned i=0;i<std::size(stages);++i)if(stages[i].key==key)return i;throw std::runtime_error("Unknown original shader stage");}
std::string blob_text(ID3DBlob* blob){
    if(!blob)return {};auto size=blob->GetBufferSize();const auto* bytes=static_cast<const char*>(blob->GetBufferPointer());
    while(size && !bytes[size-1])--size;return {bytes,size};
}
// DXC faults on a diagnostic whose file name holds a character outside ASCII (a Mod folder named in
// another script ended the game, BUG-046). It sees the preprocessed text with its #line files named
// by ASCII aliases; its diagnostics name the files again.
std::string line_alias(size_t index){return "source"+std::to_string(index)+".hlsl";}
std::string aliased_lines(const char* data,size_t size,std::vector<std::string>& names){
    std::string out;out.reserve(size+64);size_t at=0;
    while(at<size){size_t end=at;while(end<size && data[end]!='\n')++end;
        const std::string_view line(data+at,end-at);const auto start=line.find_first_not_of(" \t");
        const auto open=start!=std::string_view::npos && line.substr(start,5)=="#line"?line.find('"',start+5):std::string_view::npos;
        size_t close=open==std::string_view::npos?line.size():open+1;
        while(close<line.size() && line[close]!='"')close+=line[close]=='\\'?2:1;
        if(close<line.size()){const std::string name(line.substr(open+1,close-open-1));
            auto found=std::find(names.begin(),names.end(),name);if(found==names.end())found=names.insert(names.end(),name);
            out.append(line.substr(0,open+1));out+=line_alias(size_t(found-names.begin()));out.append(line.substr(close));}
        else out.append(line);
        if(end<size)out+='\n';at=end+1;}
    return out;
}
std::string named_lines(std::string text,const std::vector<std::string>& names,const std::string& main){
    auto replace=[&](const std::string& alias,const std::string& name){
        for(size_t at=text.find(alias);at!=std::string::npos;at=text.find(alias,at+name.size()))text.replace(at,alias.size(),name);};
    replace("source.hlsl",main);
    for(size_t i=0;i<names.size();++i){std::string name;
        for(size_t c=0;c<names[i].size();++c){if(names[i][c]=='\\' && c+1<names[i].size())++c;name+=names[i][c];}
        replace(line_alias(i),name);}
    return text;
}
// A fault inside DXC must not end the host process (the layer runs inside the game): the compile
// refuses, and no later compile of this process enters DXC, its state being unknown after the fault.
std::atomic<uint32_t> dxc_fault{0};
HRESULT guarded_compile(IDxcCompiler3* compiler,const DxcBuffer* text,LPCWSTR* args,UINT count,IDxcResult** result,DWORD* fault){
    __try{return compiler->Compile(text,args,count,nullptr,__uuidof(IDxcResult),reinterpret_cast<void**>(result));}
    __except(EXCEPTION_EXECUTE_HANDLER){*fault=GetExceptionCode();return E_FAIL;}
}
class Includes final : public ID3DInclude {
    const HlslRequest& request_;
    std::vector<ShaderDependency>& dependencies_;
    std::vector<std::filesystem::path> directories_;
    std::map<const void*,std::unique_ptr<std::vector<uint8_t>>> opened_;
    size_t bytes_=0;
public:
    std::string error;
    Includes(const HlslRequest& r,std::vector<ShaderDependency>& files):request_(r),dependencies_(files){directories_.push_back(r.source.parent_path());}
    std::vector<uint8_t> read(const std::filesystem::path& path){
        std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file) && file.tellg()>=0 && uint64_t(file.tellg())<=(32u<<20),"HLSL source missing or exceeds file budget");
        const auto size=size_t(file.tellg());require(dependencies_.size()<1024 && size<=(64u<<20)-bytes_,"HLSL include graph exceeds source budget");
        std::vector<uint8_t> bytes(size);file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);require(bool(file),"HLSL source read failed");bytes_+=size;
        dependencies_.push_back({std::filesystem::absolute(path).lexically_normal(),bytes});return bytes;
    }
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE,LPCSTR filename,LPCVOID,LPCVOID* data,UINT* size) override {
        try{
            require(filename && data && size && directories_.size()<256,"HLSL include nesting exceeds budget");
            // Original -1 selects the standard legacy include behavior; zero
            // uses its bounded equivalent. Other nonzero values are recursive.
            const bool recursive=request_.recursive_include!=0 && request_.recursive_include!=-1;
            const auto& directory=recursive?directories_.back():directories_.front();
            auto path=std::filesystem::u8path(directory.u8string()+"/"+filename);
            if(!std::filesystem::is_regular_file(path) && !recursive)path=request_.working_directory/std::filesystem::u8path(filename);
            auto bytes=std::make_unique<std::vector<uint8_t>>(read(path));
            // D3DCompile accepts empty includes, but standalone D3DPreprocess
            // returns E_INVALIDARG for them. One whitespace byte preserves the
            // source semantics; the dependency snapshot remains exactly empty.
            if(bytes->empty())bytes->push_back('\n');*size=UINT(bytes->size());
            *data=bytes->data();opened_.emplace(*data,std::move(bytes));directories_.push_back(path.parent_path());return S_OK;
        }catch(const std::exception& e){error=e.what();return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override {
        if(!opened_.erase(data) || directories_.size()<2)return E_FAIL;directories_.pop_back();return S_OK;
    }
};
char source_type(D3D_SHADER_INPUT_TYPE type){
    switch(type){
    case D3D_SIT_CBUFFER:return 'b';
    case D3D_SIT_SAMPLER:return 's';
    case D3D_SIT_TBUFFER:case D3D_SIT_TEXTURE:case D3D_SIT_STRUCTURED:case D3D_SIT_BYTEADDRESS:return 't';
    case D3D_SIT_UAV_RWTYPED:case D3D_SIT_UAV_RWSTRUCTURED:case D3D_SIT_UAV_RWBYTEADDRESS:
    case D3D_SIT_UAV_APPEND_STRUCTURED:case D3D_SIT_UAV_CONSUME_STRUCTURED:case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:return 'u';
    default:throw std::runtime_error("Original shader resource type has no Vulkan binding contract");
    }
}
void append_interface(std::vector<ShaderInterface>& out,const SpvReflectInterfaceVariable* value){
    if(value->member_count){for(uint32_t i=0;i<value->member_count;++i)append_interface(out,&value->members[i]);return;}
    out.push_back({value->name?value->name:"",value->semantic?value->semantic:"",value->location,uint32_t(value->built_in),VkFormat(value->format),value->spirv_id});
}
D3D11_SHADER_INPUT_BIND_DESC source_binding(const std::map<std::string,D3D11_SHADER_INPUT_BIND_DESC>& resources,
                                          const SpvReflectDescriptorBinding& descriptor){
    const std::string name=descriptor.name?descriptor.name:"";auto found=resources.find(name);
    if(found!=resources.end())return found->second;
    // Shader model 5 reflection flattens resource arrays into used Name[index]
    // entries. Infer the common base only from verified source register facts;
    // a leading unused element can be absent after original optimization.
    D3D11_SHADER_INPUT_BIND_DESC result{};bool have=false;const auto prefix=name+"[";
    for(const auto& pair:resources){if(pair.first.compare(0,prefix.size(),prefix) || pair.first.back()!=']')continue;
        const auto number=pair.first.substr(prefix.size(),pair.first.size()-prefix.size()-1);
        if(number.empty() || number.find_first_not_of("0123456789")!=std::string::npos)continue;
        const auto index=std::stoull(number);const auto& item=pair.second;
        require(index<descriptor.count && item.BindPoint>=index && item.BindCount==1,"Original resource array index/range differs");
        const auto base=item.BindPoint-UINT(index);
        if(have)require(result.BindPoint==base && result.Type==item.Type,"Original resource array does not have one contiguous register range");
        else{result=item;result.BindPoint=base;result.BindCount=descriptor.count;have=true;}
    }
    if(!have)throw std::runtime_error("SPIR-V resource has no original reflected slot: "+name);
    return result;
}
void reflect_program(HlslProgram& out,char stage,ID3DBlob* original){
    ComPtr<ID3D11ShaderReflection> native;checked(D3DReflect(original->GetBufferPointer(),original->GetBufferSize(),IID_PPV_ARGS(&native)),"Reflect original shader");
    D3D11_SHADER_DESC desc{};checked(native->GetDesc(&desc),"Read original shader description");
    for(unsigned kind=0;kind<3;++kind){const auto count=kind==0?desc.InputParameters:kind==1?desc.OutputParameters:desc.PatchConstantParameters;
        require(count<=4096,"Original shader signature exceeds the bounded compiler");
        auto& signature=kind==0?out.input_signature:kind==1?out.output_signature:out.patch_signature;
        for(UINT i=0;i<count;++i){D3D11_SIGNATURE_PARAMETER_DESC field{};
            checked(kind==0?native->GetInputParameterDesc(i,&field):kind==1?native->GetOutputParameterDesc(i,&field):native->GetPatchConstantParameterDesc(i,&field),"Read original shader signature");
            signature.push_back({field.SemanticName?field.SemanticName:"",field.SemanticIndex,field.Register,uint32_t(field.ComponentType),uint32_t(field.SystemValueType),field.Stream,field.Mask,field.ReadWriteMask});
        }
    }
    std::map<std::string,D3D11_SHADER_INPUT_BIND_DESC> source;
    for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC entry{};checked(native->GetResourceBindingDesc(i,&entry),"Read original resource slot");source.emplace(entry.Name,entry);}
    struct Reflection {SpvReflectShaderModule module{};bool valid=false;~Reflection(){if(valid)spvReflectDestroyShaderModule(&module);}} reflection;
    reflected(spvReflectCreateShaderModule(out.spirv.size()*4,out.spirv.data(),&reflection.module));reflection.valid=true;auto& module=reflection.module;
    require(module.entry_point_count==1 && module.shader_stage==uint32_t(out.stage),"Translated shader entry point/stage differs");
    for(uint32_t i=0;i<module.capability_count;++i)out.capabilities.push_back(uint32_t(module.capabilities[i].value));
    out.threads[0]=module.entry_points[0].local_size.x;out.threads[1]=module.entry_points[0].local_size.y;out.threads[2]=module.entry_points[0].local_size.z;
    for(uint32_t i=0;i<module.input_variable_count;++i)append_interface(out.inputs,module.input_variables[i]);
    for(uint32_t i=0;i<module.output_variable_count;++i)append_interface(out.outputs,module.output_variables[i]);
    std::map<uint32_t,const SpvReflectDescriptorBinding*> counters;
    for(uint32_t i=0;i<module.descriptor_binding_count;++i){const auto& b=module.descriptor_bindings[i];if(b.uav_counter_binding)counters[b.uav_counter_binding->spirv_id]=&b;}
    std::set<uint32_t> occupied;
    for(uint32_t i=0;i<module.descriptor_binding_count;++i){const auto& b=module.descriptor_bindings[i];
        const auto counter=counters.find(b.spirv_id);const auto* owner=counter==counters.end()?&b:counter->second;
        const auto original=source_binding(source,*owner);const auto type=source_type(original.Type);
        require(b.set==0 && b.count && original.BindCount && (counter!=counters.end() || b.count==original.BindCount),"Shader resource array/register-space translation differs");
        ShaderBinding entry;entry.name=b.name?b.name:"";entry.source_type=type;entry.slot=original.BindPoint;entry.count=b.count;
        entry.counter=counter!=counters.end();entry.binding=HlslCompiler::binding(stage,type,entry.slot,entry.counter);entry.type=VkDescriptorType(b.descriptor_type);
        const uint32_t limit=type=='b'?14:type=='s'?16:type=='u'?64:128;
        require(entry.slot<limit && entry.count<=limit-entry.slot && occupied.insert(entry.binding).second,"Original shader resource slots exceed contract or alias");
        entry.image_dimension=uint32_t(b.image.dim);entry.image_format=uint32_t(b.image.image_format);
        entry.image_array=bool(b.image.arrayed);entry.image_multisample=bool(b.image.ms);entry.image_depth=b.image.depth;
        if(b.type_description){const auto& t=*b.type_description;entry.scalar_width=t.traits.numeric.scalar.width;
            if(t.type_flags&SPV_REFLECT_TYPE_FLAG_FLOAT)entry.scalar_type=1;
            else if(t.type_flags&SPV_REFLECT_TYPE_FLAG_INT)entry.scalar_type=t.traits.numeric.scalar.signedness?3:2;}
        entry.block_size=b.block.padded_size;
        if(b.block.member_count==1){const auto& member=b.block.members[0];entry.stride=member.array.stride;
            if(!entry.stride && member.type_description)entry.stride=member.type_description->traits.array.stride;}
        require(b.word_offset.binding<out.spirv.size() && b.word_offset.set<out.spirv.size(),"Reflected descriptor word outside module");
        out.spirv[b.word_offset.binding]=entry.binding;out.spirv[b.word_offset.set]=0;out.bindings.push_back(std::move(entry));
    }
    // Reflection-only Google annotations describe original semantics/counters.
    // Keep that metadata above; strip their instructions/extensions from the
    // executable so device extension enablement is not implicitly required.
    std::vector<uint32_t> executable(out.spirv.begin(),out.spirv.begin()+5);
    for(size_t at=5;at<out.spirv.size();){const auto word=out.spirv[at],count=word>>16,op=word&65535;
        require(count && count<=out.spirv.size()-at,"Invalid SPIR-V instruction boundary");bool strip=false;
        if(op==SpvOpExtension){const char* name=reinterpret_cast<const char*>(&out.spirv[at+1]);
            require(memchr(name,0,(count-1)*4)!=nullptr,"Unterminated SPIR-V extension");
            strip=!strcmp(name,"SPV_GOOGLE_hlsl_functionality1") || !strcmp(name,"SPV_GOOGLE_user_type");}
        else if(op==SpvOpDecorateId && count>=3)strip=out.spirv[at+2]==SpvDecorationHlslCounterBufferGOOGLE;
        else if(op==SpvOpDecorateStringGOOGLE || op==SpvOpMemberDecorateStringGOOGLE)strip=true;
        if(!strip)executable.insert(executable.end(),out.spirv.begin()+at,out.spirv.begin()+at+count);at+=count;
    }
    out.spirv=std::move(executable);
}
}
struct HlslCompiler::Impl {
    HMODULE library=nullptr;
    ComPtr<IDxcCompiler3> compiler;
    ~Impl(){compiler.Reset();if(library)FreeLibrary(library);}
};
HlslCompiler::HlslCompiler(const std::filesystem::path& path):impl_(std::make_unique<Impl>()){
    require(path.is_absolute(),"DXC DLL location must be absolute");
    impl_->library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(impl_->library!=nullptr,"Cannot load configured DXC library");
    const auto create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(impl_->library,"DxcCreateInstance"));require(create!=nullptr,"DXC factory unavailable");
    checked(create(CLSID_DxcCompiler,IID_PPV_ARGS(&impl_->compiler)),"Create SPIR-V compiler");
}
HlslCompiler::~HlslCompiler()=default;
uint32_t HlslCompiler::binding(char stage,char type,uint32_t slot,bool counter){
    const uint32_t index=stage_index(stage),base=counter?512:type=='b'?0:type=='t'?128:type=='s'?256:type=='u'?384:UINT32_MAX;
    require(base!=UINT32_MAX && slot<128 && (!counter || type=='u'),"Invalid original shader register class");return index*1024+base+slot;
}
bool HlslCompiler::compile(const HlslRequest& request,HlslProgram& output,std::string& reason,std::vector<uint8_t>* reference){
    reason.clear();if(reference)reference->clear();
    try{
        require(request.source.is_absolute() && request.working_directory.is_absolute(),"HLSL source and working directory must be absolute");
        require(request.vulkan_version==VK_API_VERSION_1_1 || request.vulkan_version==VK_API_VERSION_1_2,"Unsupported shader target Vulkan version");
        const auto& stage=stages[stage_index(request.stage)];HlslProgram next;next.stage=stage.flag;next.vulkan_version=request.vulkan_version;
        Includes includes(request,next.dependencies);const auto source=includes.read(request.source);const auto name=request.source.u8string();
        const D3D_SHADER_MACRO macros[]={{stage.macro,""},{nullptr,nullptr}};
        ComPtr<ID3DBlob> expanded,errors,dxbc;
        auto hr=D3DPreprocess(source.data(),source.size(),name.c_str(),macros,&includes,&expanded,&errors);
        next.diagnostics+=blob_text(errors.Get());if(FAILED(hr))throw std::runtime_error("Original HLSL preprocessing failed ("+std::to_string(uint32_t(hr))+"): "+includes.error+next.diagnostics);
        errors.Reset();const auto model=std::string(1,request.stage)+"s_5_0";
        hr=D3DCompile(expanded->GetBufferPointer(),expanded->GetBufferSize(),name.c_str(),nullptr,nullptr,"main",model.c_str(),request.flags,0,&dxbc,&errors);
        next.diagnostics+=blob_text(errors.Get());if(FAILED(hr))throw std::runtime_error("Original shader-model-5 compilation failed: "+next.diagnostics);
        const auto* native=static_cast<const uint8_t*>(dxbc->GetBufferPointer());next.dxbc.assign(native,native+dxbc->GetBufferSize());
        auto refuse=[&](const std::string& why){if(reference)*reference=next.dxbc;throw std::runtime_error(why);};
        if(const auto fault=dxc_fault.load()){char code[16];snprintf(code,sizeof(code),"0x%08x",fault);
            refuse(std::string("DXC faulted earlier in this process (")+code+") and takes no further work");}
        std::vector<std::string> files;
        const auto text_aliased=aliased_lines(static_cast<const char*>(expanded->GetBufferPointer()),expanded->GetBufferSize(),files);
        std::vector<std::wstring> args={L"source.hlsl",L"-spirv",request.vulkan_version==VK_API_VERSION_1_2?L"-fspv-target-env=vulkan1.2":L"-fspv-target-env=vulkan1.1",L"-fspv-reflect",L"-fvk-use-dx-layout",
            L"-fvk-stage-io-order=alpha",L"-fvk-use-dx-position-w",L"-fvk-support-nonzero-base-instance",L"-fvk-support-nonzero-base-vertex",
            L"-flegacy-resource-reservation",L"-HV",L"2016",L"-E",L"main",L"-T",std::wstring(1,wchar_t(request.stage))+L"s_6_0"};
        // Native SM5 pixel w and vertex/instance IDs differ from Vulkan's raw
        // built-ins. The offset options may require actual shaderDrawParameters
        // enablement; the executor must gate capabilities before pipeline creation.
        const auto flags=request.flags;
        if(flags&D3DCOMPILE_DEBUG)args.push_back(L"-Zi");
        if(flags&D3DCOMPILE_SKIP_OPTIMIZATION)args.push_back(L"-Od");
        else{const auto level=flags&(D3DCOMPILE_OPTIMIZATION_LEVEL0|D3DCOMPILE_OPTIMIZATION_LEVEL3);
            args.push_back(level==D3DCOMPILE_OPTIMIZATION_LEVEL0?L"-O0":level==D3DCOMPILE_OPTIMIZATION_LEVEL2?L"-O2":level==D3DCOMPILE_OPTIMIZATION_LEVEL3?L"-O3":L"-O1");}
        for(const auto& option:std::array<std::pair<uint32_t,const wchar_t*>,10>{{
            {D3DCOMPILE_PACK_MATRIX_ROW_MAJOR,L"-Zpr"},{D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR,L"-Zpc"},
            {D3DCOMPILE_AVOID_FLOW_CONTROL,L"-Gfa"},{D3DCOMPILE_PREFER_FLOW_CONTROL,L"-Gfp"},
            {D3DCOMPILE_ENABLE_STRICTNESS,L"-Ges"},
            {D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY,L"-Gec"},{D3DCOMPILE_IEEE_STRICTNESS,L"-Gis"},
            {D3DCOMPILE_WARNINGS_ARE_ERRORS,L"-WX"},{D3DCOMPILE_RESOURCES_MAY_ALIAS,L"-res-may-alias"},
            {D3DCOMPILE_ALL_RESOURCES_BOUND,L"-all-resources-bound"}}})if(flags&option.first)args.push_back(option.second);
        // Remaining source flags concern DXBC validation/legacy optimization
        // hints. The original compiler above receives every bit. SPIR-V always
        // validates; no unsupported device feature is enabled by a source flag.
        std::vector<LPCWSTR> pointers;for(const auto& arg:args)pointers.push_back(arg.c_str());
        DxcBuffer text{text_aliased.data(),text_aliased.size(),DXC_CP_UTF8};ComPtr<IDxcResult> result;DWORD fault=0;
        hr=guarded_compile(impl_->compiler.Get(),&text,pointers.data(),UINT(pointers.size()),result.ReleaseAndGetAddressOf(),&fault);
        if(fault){dxc_fault.store(fault);result.Detach();char code[16];snprintf(code,sizeof(code),"0x%08x",fault);
            refuse(std::string("DXC faulted (")+code+") compiling "+name+"; it takes no further work in this process");}
        checked(hr,"Compile SPIR-V");
        ComPtr<IDxcBlobUtf8> messages;checked(result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&messages),nullptr),"Read SPIR-V diagnostics");
        if(messages && messages->GetStringLength())next.diagnostics+=named_lines(std::string(messages->GetStringPointer(),messages->GetStringLength()),files,name);
        checked(result->GetStatus(&hr),"Read SPIR-V compile status");if(FAILED(hr))refuse("SPIR-V compilation failed: "+next.diagnostics);
        ComPtr<IDxcBlob> object;checked(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr),"Read compiled SPIR-V");
        require(object && object->GetBufferSize()>=20 && object->GetBufferSize()<=(32u<<20) && object->GetBufferSize()%4==0,"Compiled SPIR-V size invalid");
        next.spirv.resize(object->GetBufferSize()/4);memcpy(next.spirv.data(),object->GetBufferPointer(),object->GetBufferSize());
        reflect_program(next,request.stage,dxbc.Get());output=std::move(next);return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
}
