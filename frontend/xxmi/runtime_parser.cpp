// The parser is included unchanged, as in the standalone feasibility host.
#include "ini_module.cpp"
#include "runtime_host.h"
#include <regex>

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
// D-133: ShaderRegex replacements that keep the shader's discard behind `if_z` on an IniParams element
// read from t120, the original's IniParams texture (RabbitFX: `ld_indexable(texture1d)(...) ${ini}.xyzw,
// l(172, 0), t120.xyzw`, `if_z ${ini}.w`, `${discard}`): each element once, from the replacement text as
// the original reads it (the raw lines). Then the main ShaderRegex sections' unconditional assignments
// to those elements (`w172 = $censor`), parsed as the original parses an IniParams assignment, into
// `assignments`. The regex sections themselves stay unparsed and inert (D-089).
void RuntimeShaderRegexGates(std::vector<std::pair<uint32_t, uint32_t>>& gates, CommandList& assignments)
{
    static const std::wregex wrapped(LR"(ld_indexable\(texture1d\)\([^)]*\)\s*\$\{(\w+)\}\.xyzw,\s*l\((\d+)(?:,\s*0)*\),\s*t120\.xyzw(?:\\n|\s)*if_z\s+\$\{\1\}\.([xyzw])(?:\\n|\s)*\$\{discard\})");
    auto leaf = [](const std::wstring& name) { return name.substr(name.rfind(L'\\') + 1); };
    const std::wstring suffix = L".pattern.replace";
    for (const auto& section : ini_sections) {
        if (_wcsnicmp(section.first.c_str(), L"ShaderRegex", 11)) continue;
        const auto name = leaf(section.first);
        if (name.size() <= suffix.size() || _wcsicmp(name.c_str() + name.size() - suffix.size(), suffix.c_str())) continue;
        std::wstring text;
        for (const auto& line : section.second.kv_vec) text += line.raw_line + L"\n";
        for (std::wsregex_iterator it(text.begin(), text.end(), wrapped), end; it != end; ++it) {
            const std::pair<uint32_t, uint32_t> gate{uint32_t(std::stoul((*it)[2].str())), uint32_t(std::wstring(L"xyzw").find((*it)[3].str()[0]))};
            if (std::find(gates.begin(), gates.end(), gate) == gates.end()) gates.push_back(gate);
        }
    }
    if (gates.empty()) return;
    for (const auto& section : ini_sections) {
        if (_wcsnicmp(section.first.c_str(), L"ShaderRegex", 11) || leaf(section.first).find(L'.') != std::wstring::npos) continue;
        int depth = 0;
        for (const auto& line : section.second.kv_vec) {
            const auto& raw = line.raw_line;
            if (!_wcsnicmp(raw.c_str(), L"if ", 3)) { ++depth; continue; }
            if (!_wcsnicmp(raw.c_str(), L"endif", 5)) { depth = std::max(0, depth - 1); continue; }
            if (depth || !_wcsnicmp(raw.c_str(), L"else", 4) || !_wcsnicmp(raw.c_str(), L"elif", 4)) continue;
            int index = 0; float DirectX::XMFLOAT4::* member = nullptr;
            if (!ParseIniParamName(line.first.c_str(), &index, &member) || index < 0) continue;
            const uint32_t component = member == &DirectX::XMFLOAT4::x ? 0 : member == &DirectX::XMFLOAT4::y ? 1 :
                                       member == &DirectX::XMFLOAT4::z ? 2 : member == &DirectX::XMFLOAT4::w ? 3 : 4;
            if (std::find(gates.begin(), gates.end(), std::make_pair(uint32_t(index), component)) == gates.end()) continue;
            std::wstring value = line.second;
            ParseCommandListIniParamOverride(section.first.c_str(), line.first.c_str(), &value, &assignments, &line.ini_namespace);
        }
    }
}
void RuntimeValidateSections(const std::vector<std::wstring>& native_shaders,bool custom_shaders,bool shader_hooks)
{
    // These require renderer adapters not supplied by the character runtime.
    // Reject the whole candidate instead of silently dropping active features.
    for (const auto& section : ini_sections) {
        if (!_wcsnicmp(section.first.c_str(), L"Include", 7))
            throw std::runtime_error("Recursive INI includes need an explicit runtime input adapter");
        // ShaderOverride and ShaderRegex sections act on the game's shaders by their DX11 hash, which a
        // Vulkan character generation cannot see (D-089). Without shader hooks they stay inert, as for
        // shaders that never run: the original's overlay warning names each one that would act, and
        // the rest of the Mod loads. Shader operands then read the game's shaders as named by none.
        if (!shader_hooks && (!_wcsnicmp(section.first.c_str(), L"ShaderOverride", 14) || !_wcsnicmp(section.first.c_str(), L"ShaderRegex", 11))) {
            const bool declaration = std::all_of(section.second.kv_vec.begin(), section.second.kv_vec.end(), [](const IniLine& line) {
                return !_wcsicmp(line.first.c_str(), L"hash") || !_wcsicmp(line.first.c_str(), L"filter_index") ||
                       !_wcsicmp(line.first.c_str(), L"allow_duplicate_hash");
            });
            if (!declaration)
                LogOverlayW(LOG_WARNING, L"[%ls] acts on game shaders by their DX11 hash, which the Vulkan layer cannot see: inert\n",
                            section.first.c_str());
            continue;
        }
        for (const auto* prefix : {L"CustomShader", L"ShaderOverride", L"ShaderRegex"})
            if (!_wcsnicmp(section.first.c_str(), prefix, wcslen(prefix)) &&
                !(custom_shaders && !wcscmp(prefix,L"CustomShader")) &&
                !(shader_hooks && (!wcscmp(prefix,L"ShaderOverride") || !wcscmp(prefix,L"ShaderRegex"))) &&
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
        // As the original: a CustomShader that does not compile is skipped with a warning, its
        // command list left empty, so its runs do nothing and the rest of the Mod loads.
        try{compile(declaration);}
        catch(const std::exception& e){LogOverlay(LOG_WARNING,const_cast<char*>("Error compiling custom shader [%ls]: %s\n"),name,e.what());continue;}
        ParseBlendState(&shader,name);ParseDepthStencilState(&shader,name);ParseRSState(&shader,name);ParseTopology(&shader,name);ParseSamplerState(&shader,name);
        shader.max_executions_per_frame=GetIniInt(name,L"max_executions_per_frame",0,nullptr);
        ParseCommandList(name,&shader.command_list,&shader.post_command_list,CustomShaderIniKeys);
    }
}
