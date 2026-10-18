#include "../frontend/xxmi/shader_regex.h"
#include "../frontend/xxmi/shader_assembly.h"
#include <wrl/client.h>

static std::vector<uint8_t> native_regex_bytecode(const char* model,const char* source="float4 main(uint id:SV_VertexID):SV_Position{return float4(id,0,0,1);}"){
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    const auto result=D3DCompile(source,strlen(source),"regex-model.hlsl",nullptr,nullptr,"main",model,0,0,&code,&errors);
    if(errors)errors->Release();check(SUCCEEDED(result) && code,"Native model metadata fixture compiles");
    const auto* data=static_cast<const uint8_t*>(code->GetBufferPointer());std::vector<uint8_t> bytes(data,data+code->GetBufferSize());code->Release();return bytes;
}
struct RegexReferenceExtension final : RuntimeExtension {
    std::function<void()> prepared;unsigned failures=0;
    std::function<void(DrawPlanAdapter&)> inspect;
    std::vector<std::wstring> install(DrawPlanAdapter&) override {return {};}
    bool prepare(DrawPlanAdapter& adapter,CommandListState&,std::wstring&) override {if(prepared)prepared();if(inspect)inspect(adapter);return true;}
    void execution_failed() noexcept override {++failures;}
};
static void runtime_regex_cases(const std::filesystem::path& folder){
    const auto root=std::filesystem::absolute(folder/"shader-regex");std::filesystem::create_directories(root);
    const auto bytes=native_regex_bytecode("vs_5_0"),old_bytes=native_regex_bytecode("vs_4_0");
    const auto model=source_shader_model(bytes.data(),bytes.size()),old_model=source_shader_model(old_bytes.data(),old_bytes.size());
    check(model=="vs_5_0" && old_model=="vs_4_0","Original bytecode reader supplies exact native model metadata");
    for(unsigned mode=0;mode<3;++mode){auto bad=bytes;uint32_t invalid=UINT_MAX;
        if(mode==0)memcpy(bad.data()+28,&invalid,4);else{uint32_t count;memcpy(&count,bad.data()+28,4);
            if(mode==1)memcpy(bad.data()+32+(count-1)*4,&invalid,4);
            else{uint32_t offset;memcpy(&offset,bad.data()+32,4);memcpy(bad.data()+offset+4,&invalid,4);}}
        bool refused=false;try{source_shader_model(bad.data(),bad.size());}catch(const std::exception&){refused=true;}
        check(refused,"Malformed DXBC tables and payloads refuse before the original backward model scan");
    }
    auto facts=known_source_shaders();facts.stages[0]={true,true,true,9,source_shader_declarations(bytes.data(),bytes.size()),model};
    std::shared_ptr<IniRuntime> retained;std::shared_ptr<RegexReferenceExtension> retained_extension;
    for(unsigned mode=0;mode<2;++mode){
        const auto path=root/(std::to_string(mode)+".ini");const std::string text=std::string(R"ini(namespace = regex_test
[Constants]
global $trace = 0
global $peek = 0
global $initial = 0
$initial = vs
[ShaderOverrideExplicit]
hash = 9
)ini")+(mode==0?"filter_index = 42\n":"")+R"ini($trace = 0
$trace = $trace * 10 + 1
post $trace = $trace * 10 + 4
[ShaderRegexA]
shader_model = vs_5_0
filter_index = 7
$trace = $trace * 10 + 2
$peek = vs
post $trace = $trace * 10 + 5
[ShaderRegexB]
shader_model = vs_5_0
filter_index = 11
$trace = $trace * 10 + 3
post $trace = $trace * 10 + 6
)ini";
        write(path,text.c_str());RuntimeConfig config{root,{path},root/"settings","regex"+std::to_string(mode)};config.shader_hooks=true;
        config.initial_shaders=std::make_shared<const SourceShaderState>(facts);auto extension=std::make_shared<RegexReferenceExtension>();
        extension->prepared=[mode]{auto& entry=G->mShaderOverrideMap.at(9);const auto pre=entry.command_list.commands.size(),post=entry.post_command_list.commands.size();
            for(unsigned repeat=0;repeat<3;++repeat)for(auto& group:shader_regex_groups)group.second.link_command_lists_and_filter_index(9);
            check(entry.command_list.commands.size()==pre && entry.post_command_list.commands.size()==post,"Native duplicate-link checks recognize adapted shared command identity");
            check(unlink_shader_regex_command_lists_and_filter_index(9) && entry.command_list.commands.size()==pre-2 &&
                entry.post_command_list.commands.size()==post-2 && entry.filter_index==(mode==0?42:FLT_MAX),
                "Native unlink removes bounded links and restores the explicit filter backup");
            for(auto& group:shader_regex_groups)group.second.link_command_lists_and_filter_index(9);
            check(entry.command_list.commands.size()==pre && entry.post_command_list.commands.size()==post,
                "Native relinking reuses the same bounded pre/post commands");
        };
        std::string why;auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        check(runtime->status().variables.at("$\\regex_test\\initial")== (mode==0?42:7),"Constants see initial native regex filters with explicit override precedence");
        auto call=resource_match_call();call.shaders=facts;call.original=[&](CommandListState&,std::wstring&){
            auto& trace=command_list_globals.at(L"$\\regex_test\\trace").fval;check(trace==123,"Explicit commands precede sorted regex links before the original call");
            trace=trace*10+7;return true;};
        for(unsigned repeat=0;repeat<12;++repeat){check(runtime->intercept(call,why),"Repeated source shader bind executes linked commands");
            const auto values=runtime->status().variables;check(values.at("$\\regex_test\\trace")==1237456 && values.at("$\\regex_test\\peek")== (mode==0?42:7),
                "Regex links preserve pre/original/post ordering without accumulating duplicates");}
        auto changed=facts;changed.stages[0].model=old_model;
        check(!runtime->bind_source_shaders(changed,why) && runtime->intercept(call,why),"Conflicting model update refuses without disturbing previous linked shader facts");
        if(mode==0){retained=runtime;retained_extension=extension;}
        else{check(retained->intercept(call,why) && retained->status().variables.at("$\\regex_test\\peek")==42,
            "Independent generations isolate native regex groups, links, filters and model caches");}
    }
    {
        const auto path=root/"pattern.ini";write(path,R"ini(namespace = regex_pattern
[Constants]
global $called = 0
[ShaderRegexAssembly]
shader_model = vs_5_0
filter_index = 5
$called = $called + 1
[ShaderRegexAssembly.Pattern]
(?<instruction>mov)
)ini");
        RuntimeConfig config{root,{path},root/"settings","pattern"};config.shader_hooks=true;std::string why;
        auto extension=std::make_shared<RegexReferenceExtension>();extension->prepared=[](){
            auto& pattern=shader_regex_groups.begin()->second.patterns.begin()->second;std::string text="MOV r0, r1";
            check(pattern.named_capture_groups.count("instruction") && pattern.matches(&text),"Original PCRE2 parser retains captures and case-insensitive matching");};
        auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        auto call=resource_match_call();call.shaders=facts;unsigned originals=0;
        call.original=[&](CommandListState&,std::wstring&){++originals;return true;};
        check(!runtime->intercept(call,why) && originals==0 && why.find("original assembly")!=std::string::npos,
            "Pattern-dependent model refuses before commands or the original operation without proven assembly execution");
        call.shaders.stages[0].code=SourceShaderCode::create(bytes.data(),bytes.size());
        check(runtime->intercept(call,why) && originals==1 && runtime->status().variables.at("$\\regex_pattern\\called")==1,
            "Proven original disassembly enables a native pattern-only command link");
        call.shaders.stages[0].code.reset();
        check(runtime->intercept(call,why) && originals==2 && runtime->status().variables.at("$\\regex_pattern\\called")==2,
            "Matched source bytecode remains cached when later bindings omit the optional payload");
        const auto different=native_regex_bytecode("vs_5_0","float4 main(uint id:SV_VertexID):SV_Position{return float4(id,1,0,1);}");
        call.shaders.stages[0].code=SourceShaderCode::create(different.data(),different.size());
        check(!runtime->intercept(call,why) && originals==2 && why.find("bytecode")!=std::string::npos,
            "A cached source hash cannot silently acquire different original bytecode");
        call.shaders.stages[0]={true,true,true,10,source_shader_declarations(old_bytes.data(),old_bytes.size()),old_model};
        check(runtime->intercept(call,why) && originals==3 && runtime->status().variables.at("$\\regex_pattern\\called")==2,
            "Unmatched native models do not acquire a regex link");
        call.shaders.stages[0].model.clear();
        check(!runtime->intercept(call,why) && originals==3,"Unknown relevant source model is not silently treated as unmatched");
    }
    retained.reset();retained_extension.reset();
}

static uint32_t native_regex_compute(const std::vector<uint8_t>& bytecode){
    using Microsoft::WRL::ComPtr;
    static const auto library=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(library!=nullptr,"System native D3D11 reference library is available");
    const auto create=reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(library,"D3D11CreateDevice"));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(create && SUCCEEDED(create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"Headless native regex reference device");
    ComPtr<ID3D11ComputeShader> shader;check(SUCCEEDED(device->CreateComputeShader(bytecode.data(),bytecode.size(),nullptr,&shader)),"Native device accepts original assembled regex bytecode");
    D3D11_BUFFER_DESC description{4,D3D11_USAGE_DEFAULT,D3D11_BIND_UNORDERED_ACCESS,0,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,4};
    uint32_t zero=0;D3D11_SUBRESOURCE_DATA initial{&zero,4,4};ComPtr<ID3D11Buffer> output,readback;
    check(SUCCEEDED(device->CreateBuffer(&description,&initial,&output)),"Native regex output buffer");
    description.Usage=D3D11_USAGE_STAGING;description.BindFlags=description.MiscFlags=description.StructureByteStride=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(device->CreateBuffer(&description,nullptr,&readback)),"Native regex readback buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC view_description{};view_description.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view_description.Buffer.NumElements=1;
    ComPtr<ID3D11UnorderedAccessView> view;check(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(),&view_description,&view)),"Native regex output UAV");
    auto* bound=view.Get();context->CSSetUnorderedAccessViews(0,1,&bound,nullptr);context->CSSetShader(shader.Get(),nullptr,0);context->Dispatch(1,1,1);
    bound=nullptr;context->CSSetUnorderedAccessViews(0,1,&bound,nullptr);context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Native regex output readback");uint32_t value;memcpy(&value,mapped.pData,4);
    context->Unmap(readback.Get(),0);return value;
}
static void runtime_regex_assembly_cases(const std::filesystem::path& folder){
    const auto root=std::filesystem::absolute(folder/"regex-assembly");std::filesystem::create_directories(root);
    const auto bytes=native_regex_bytecode("cs_5_0","RWStructuredBuffer<uint> Output:register(u0);[numthreads(1,1,1)]void main(){Output[0]=1;}");
    const auto code=SourceShaderCode::create(bytes.data(),bytes.size());
    const auto text=source_shader_disassembly(*code,false,true);write(root/"original.asm",text.c_str());std::vector<std::string> notices;
    const auto roundtrip=source_shader_assembly(text,notices);
    check(notices.empty() && native_regex_compute(bytes)==1 && native_regex_compute(roundtrip)==1,"Original disassembler and signature-aware assembler preserve native execution");
    auto facts=known_source_shaders();facts.stages[5]={true,true,true,0x22,code->declarations(),code->model(),code};
    for(unsigned mode=0;mode<3;++mode){
        const auto path=root/(std::to_string(mode)+".ini");
        std::string ini=R"ini(namespace = regex_assembly
[Constants]
global $a = 0
global $b = 0
global $explicit = 0
[ShaderOverrideOriginal]
hash = 22
filter_index = 42
local $local = 5
$explicit = $local
[ShaderRegexA]
shader_model = cs_5_0
filter_index = 7
temps = scratch
$a = $a + 1
[ShaderRegexA.Pattern]
(?<write>store_structured[^\n]*?)l\(1\)
[ShaderRegexA.Pattern.Replace]
mov ${scratch}.x, l(7)\n${write}${scratch}.x
[ShaderRegexA.InsertDeclarations]
dcl_resource_raw t15
)ini";
        if(mode){
            if(mode==2){const auto instruction=ini.find("store_structured");ini.replace(instruction,16,"UNMATCHED_OPCODE");}
            ini+=R"ini([ShaderRegexB]
shader_model = cs_5_0
filter_index = 9
$b = $b + 1
[ShaderRegexB.Pattern]
)ini";
            ini+=mode==1?"l\\(7\\)\n":"l\\(1\\)\n";
            if(mode==1)ini+="[ShaderRegexB.Pattern.Replace]\nl(9)\n";
        }
        if(mode==1)ini+="[Rendering]\nassemble_signature_comments = true\ndisassemble_undecipherable_custom_data = true\npatch_assembly_cb_offsets = true\n";
        write(path,ini.c_str());RuntimeConfig config{root,{path},root/"settings","assembly"+std::to_string(mode)};config.shader_hooks=true;
        std::vector<SourceShaderPatch> patches;std::wstring expected_tag;auto extension=std::make_shared<RegexReferenceExtension>();
        extension->inspect=[&](DrawPlanAdapter& adapter){SourceShaderRegex analysis;analysis.initialize(adapter);
            auto* previous_log=LogFile;const auto previous_level=gLogVerbosity;LogFile=_wfopen((root/(std::to_string(mode)+".log")).c_str(),L"wb");
            gLogVerbosity=LogVerbosity::DEBUG;
            struct RestoreLog {FILE* previous;LogVerbosity level;~RestoreLog(){if(LogFile)fclose(LogFile);LogFile=previous;gLogVerbosity=level;}} restore_log{previous_log,previous_level};
            check(LogFile!=nullptr,"Original regex analysis log is retained in the sandbox");
            check(G->assemble_signature_comments==bool(mode==1) && G->disassemble_undecipherable_custom_data==bool(mode==1) && G->patch_cb_offsets==bool(mode==1),
                "Original rendering getters initialize all regex assembly options, including their false defaults");
            fprintf(LogFile,"Preparing native regex model=%s bytes=%zu groups=%zu\n",code->model().c_str(),code->bytes().size(),shader_regex_groups.size());fflush(LogFile);
            auto& original=G->mShaderOverrideMap.at(0x22);auto* root_address=&original.command_list;
            auto* variable=original.command_list.static_vars.empty()?nullptr:&original.command_list.static_vars.front();
            check(variable!=nullptr,"Explicit native local variable is retained in its original root");
            const auto commands=original.command_list.commands;
            auto prepared=analysis.prepare(facts,32,true,true);patches=prepared->patches;
            auto selected=shader_regex_groups.begin();expected_tag=L"["+selected->second.ini_section+L"]";
            if(mode==1){++selected;expected_tag+=L"["+selected->second.ini_section+L"]";}
            check(&G->mShaderOverrideMap.at(0x22).command_list==root_address && &original.command_list.static_vars.front()==variable &&
                original.command_list.commands==commands && original.filter_index==42,"Offline native analysis preserves live map nodes, local storage, commands and filters");
            check(patches.size()==(mode==2?0:1),"Native groups preserve ordered patches and genuine nonmatches");
            if(mode!=2){bool refused=false;try{analysis.apply(std::move(prepared),adapter);}catch(const std::exception&){refused=true;}
                check(refused && original.command_list.commands==commands,"Patched source cannot publish linked commands without executable translation");}
        };
        std::string why;auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        if(mode!=2){check(patches[0].notices.empty(),"Original regex replacement assembles without native parse notices");
            write(root/(std::to_string(mode)+".asm"),patches[0].assembly.c_str());
            check(native_regex_compute(patches[0].bytecode)==(mode==0?7:9),"Native execution proves named captures, temporary registers and ordered replacement output");
            const auto declaration=source_shader_declarations(patches[0].bytecode.data(),patches[0].bytecode.size());
            check(declaration->resources[15].type==ShaderResourceType::RAW,"Matched native group inserts its resource declaration");
            check(patches[0].tagline==expected_tag,"Native patch tagline identifies the successfully patched group");}
        RuntimeShaderCall call;call.call.type=DrawCall::Dispatch;call.call.ThreadGroupCountX=call.call.ThreadGroupCountY=call.call.ThreadGroupCountZ=1;call.shaders=facts;
        unsigned originals=0;call.original=[&](CommandListState&,std::wstring&){++originals;return true;};
        if(mode==2){for(unsigned repeat=1;repeat<=3;++repeat){check(runtime->intercept(call,why),"Native match-only result after an unmatched replacement group executes");
                const auto values=runtime->status().variables;check(values.at("$\\regex_assembly\\a")==0 && values.at("$\\regex_assembly\\b")==float(repeat) &&
                    values.at("$\\regex_assembly\\explicit")==5 && originals==repeat,"Only successful groups link once; original local variable storage stays valid");}}
        else check(!runtime->intercept(call,why) && originals==0 && why.find("executable translation")!=std::string::npos &&
            runtime->status().variables.at("$\\regex_assembly\\a")==0,"Runtime refuses a required shader patch before any command or original operation");
        if(mode==0){const auto label=ini.find("[ShaderRegexA.Pattern]");ini.replace(label,strlen("[ShaderRegexA.Pattern]"),"[ShaderRegexA.Pattern1]");
            write(path,ini.c_str());unsigned preparations=0;auto invalid=std::make_shared<RegexReferenceExtension>();invalid->prepared=[&]{++preparations;};
            // The pinned source disables the whole group with a warning and loads the rest.
            auto disabled=IniRuntime::load(config,invalid,why);bool reported=false;
            if(disabled)for(const auto& line:disabled->status().diagnostic_messages)reported|=line.find("Disabling entire shader regex group")!=std::string::npos;
            check(disabled && reported,"Pinned source disables a group with a numbered Pattern section and reports it");}
    }
}
