// The parser is included unchanged, as in the standalone feasibility host.
#include "ini_module.cpp"
#include "runtime_host.h"

std::wstring HostResourceDirectory();

void RuntimeResetParser()
{
    ini_sections.clear(); recursive_includes.clear(); ini_warned = false;
}
void RuntimeParseKeys()
{
    RegisterPresetKeyBindings(); ParsePresetOverrideSections();
}
void RuntimeEnumerateCustomShaders() { EnumerateCustomShaderSections(); EnumeratePresetOverrideSections(); }
void RuntimeCompilePresent()
{
    ParseCommandList(L"Present", &G->present_command_list, &G->post_present_command_list, nullptr);
}
void RuntimeCompileConstants(CommandList& pre,CommandList& post)
{
    ParseCommandList(L"Constants", &pre, &post, nullptr);
}
void RuntimeParseParameterRegister()
{
    G->IniParamsReg=GetIniInt(L"Rendering",L"ini_params",120,nullptr);
    // The source disables out-of-range registers. Negative registers also
    // disable automatic binding; the resource itself is still allocated.
    if(G->IniParamsReg>=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)G->IniParamsReg=-1;
}
void RuntimeValidateSections(const std::vector<std::wstring>& native_shaders,bool custom_shaders)
{
    // These require renderer adapters not supplied by the character runtime.
    // Reject the whole candidate instead of silently dropping active features.
    for (const auto& section : ini_sections) {
        if (!_wcsnicmp(section.first.c_str(), L"Include", 7))
            throw std::runtime_error("Recursive INI includes need an explicit runtime input adapter");
        for (const auto* prefix : {L"CustomShader", L"ShaderOverride", L"ShaderRegex"})
            if (!_wcsnicmp(section.first.c_str(), prefix, wcslen(prefix)) &&
                !(custom_shaders && !wcscmp(prefix,L"CustomShader")) &&
                std::none_of(native_shaders.begin(), native_shaders.end(), [&](const auto& name) { return !_wcsicmp(name.c_str(), section.first.c_str()); }))
                throw std::runtime_error("INI declares an unsupported shader/overlay section");
    }
}
void efmi::frontend::RuntimeParseCustomShaders(const std::function<void(const RuntimeShaderDeclaration&)>& compile)
{
    const auto recursive=GetIniBoolOrInt(L"Rendering",L"recursive_include",false,nullptr);
    for(auto& pair:customShaders){auto& shader=pair.second;const auto* name=pair.first.c_str();wchar_t text[MAX_PATH];
        if(GetIniStringAndLog(name,L"flags",nullptr,text,MAX_PATH))
            shader.compile_flags=parse_enum_option_string<const wchar_t*,D3DCompileFlags,wchar_t*>(D3DCompileFlagNames,text,nullptr);
        RuntimeShaderDeclaration declaration;declaration.shader=&shader;declaration.name=pair.first;
        declaration.root=HostResourceDirectory();declaration.recursive_include=recursive;
        get_namespaced_section_path(name,&declaration.namespace_path);
        const wchar_t* keys[]={L"vs",L"hs",L"ds",L"gs",L"ps",L"cs"};
        for(unsigned i=0;i<6;++i)if(GetIniString(name,keys[i],nullptr,text,MAX_PATH))declaration.sources[i]=text;
        compile(declaration);
        ParseBlendState(&shader,name);ParseDepthStencilState(&shader,name);ParseRSState(&shader,name);ParseTopology(&shader,name);ParseSamplerState(&shader,name);
        shader.max_executions_per_frame=GetIniInt(name,L"max_executions_per_frame",0,nullptr);
        ParseCommandList(name,&shader.command_list,&shader.post_command_list,CustomShaderIniKeys);
    }
}
