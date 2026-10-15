#include "../frontend/xxmi/shader_regex.h"

static std::vector<uint8_t> native_regex_bytecode(const char* model){
    const char* source="float4 main(uint id:SV_VertexID):SV_Position{return float4(id,0,0,1);}";
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    const auto result=D3DCompile(source,strlen(source),"regex-model.hlsl",nullptr,nullptr,"main",model,0,0,&code,&errors);
    if(errors)errors->Release();check(SUCCEEDED(result) && code,"Native model metadata fixture compiles");
    const auto* data=static_cast<const uint8_t*>(code->GetBufferPointer());std::vector<uint8_t> bytes(data,data+code->GetBufferSize());code->Release();return bytes;
}
struct RegexReferenceExtension final : RuntimeExtension {
    std::function<void()> prepared;unsigned failures=0;
    std::vector<std::wstring> install(DrawPlanAdapter&) override {return {};}
    bool prepare(DrawPlanAdapter&,CommandListState&,std::wstring&) override {if(prepared)prepared();return true;}
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
$called = 1
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
        call.shaders.stages[0]={true,true,true,10,source_shader_declarations(old_bytes.data(),old_bytes.size()),old_model};
        check(runtime->intercept(call,why) && originals==1 && runtime->status().variables.at("$\\regex_pattern\\called")==0,
            "Unmatched native models do not acquire a regex link");
        call.shaders.stages[0].model.clear();
        check(!runtime->intercept(call,why) && originals==1,"Unknown relevant source model is not silently treated as unmatched");
    }
    retained.reset();retained_extension.reset();
}
