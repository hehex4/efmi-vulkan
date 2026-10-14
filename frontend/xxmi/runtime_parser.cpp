// The parser is included unchanged, as in the standalone feasibility host.
#include "ini_module.cpp"

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
void RuntimeCompileConstants(CommandList& list)
{
    ParseCommandList(L"Constants", &list, nullptr, nullptr);
}
void RuntimeValidateSections(const std::vector<std::wstring>& native_shaders)
{
    // These require renderer adapters not supplied by the character runtime.
    // Reject the whole candidate instead of silently dropping active features.
    for (const auto& section : ini_sections) {
        if (!_wcsnicmp(section.first.c_str(), L"Include", 7))
            throw std::runtime_error("Recursive INI includes need an explicit runtime input adapter");
        for (const auto* prefix : {L"CustomShader", L"ShaderOverride", L"ShaderRegex"})
            if (!_wcsnicmp(section.first.c_str(), prefix, wcslen(prefix)) &&
                std::none_of(native_shaders.begin(), native_shaders.end(), [&](const auto& name) { return !_wcsicmp(name.c_str(), section.first.c_str()); }))
                throw std::runtime_error("INI declares an unsupported shader/overlay section");
    }
}
