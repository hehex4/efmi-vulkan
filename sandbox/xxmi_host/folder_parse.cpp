// Full original parser inspection. No command execution, graphics or file writes.
#include "globals.h"
#include <filesystem>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <typeinfo>
#include <functional>
#include <array>

void HostParseExcerpt(const wchar_t*, const wchar_t*);
void HostParseIncludes();
void HostInsertBuiltins();
void HostResourceRoot(const std::wstring&);
void HostInspectAllSections();
void HostVisitShaderSources(const std::function<void(const std::wstring&,const std::wstring&,UINT,int,const std::array<std::wstring,6>&)>&);
size_t HostDiagnosticCount();
size_t HostNoticeCount();
size_t HostInputCount();

static void json(const std::wstring& value)
{
    putchar('"');
    for (wchar_t c : value) {
        if (c == L'"' || c == L'\\') { putchar('\\'); putchar(char(c)); }
        else if (c < 32 || c > 126) printf("\\u%04x", unsigned(c));
        else putchar(char(c));
    }
    putchar('"');
}

int HostParseFolder(const wchar_t* root_name, const wchar_t* core_name, const wchar_t* mods_name)
{
    try {
        const auto root = std::filesystem::absolute(root_name);
        const auto core = std::filesystem::absolute(core_name) / L"main.ini";
        const auto mods = std::filesystem::absolute(mods_name);
        if (!std::filesystem::is_regular_file(core) || !std::filesystem::is_directory(mods))
            throw std::runtime_error("Core main.ini or Mods directory missing");
        HostResourceRoot(root.wstring());
        // The pinned engine owns ordering, implicit namespaces, include
        // conditions, nested Include sections and recursive exclusion rules.
        const auto text = L"[Include]\ninclude = " + core.lexically_relative(root).wstring() +
            L"\ninclude_recursive = " + mods.lexically_relative(root).wstring() +
            L"\nexclude_recursive = DISABLED*\nexclude_recursive = desktop.ini\n";
        HostInsertBuiltins();
        HostParseExcerpt(text.c_str(), nullptr);
        HostParseIncludes();
        printf("{");
        HostInspectAllSections();
        printf(",\"keys\":%zu,\"resource_declarations\":[", HostInputCount());
        bool comma = false;
        // Stable report order and original resolved filenames, not a second
        // resource-section parser guessing paths or dropping unused branches.
        std::map<std::wstring, const CustomResource*> ordered;
        for (const auto& pair : customResources) ordered.emplace(pair.first, &pair.second);
        for (const auto& pair : ordered) {
            const auto& r = *pair.second;
            if (comma) putchar(','); comma = true;
            printf("{\"name\":"); json(pair.first);
            printf(",\"filename\":"); json(r.filename);
            printf(",\"type\":%d,\"format\":%d,\"stride\":%d,\"inline_bytes\":%zu,\"width\":%d,\"height\":%d}",
                int(r.override_type), int(r.override_format), r.override_stride, r.initial_data_size,
                r.override_width, r.override_height);
        }
        printf("],\"shader_declarations\":["); comma=false;
        HostVisitShaderSources([&](const auto& name,const auto& path,UINT flags,int recursive,const auto& sources){
            if(comma)putchar(',');comma=true;printf("{\"name\":");json(name);printf(",\"namespace_path\":");json(path);
            printf(",\"flags\":%u,\"recursive_include\":%d,\"sources\":[",flags,recursive);
            for(unsigned i=0;i<6;++i){if(i)putchar(',');json(sources[i]);}printf("]}");
        });
        printf("],\"command_types\":{"); comma = false;
        std::map<std::string, size_t> commands;
        for (auto* list : registered_command_lists) if (list)
            for (const auto& command : list->commands) ++commands[typeid(*command).name()];
        for (const auto& pair : commands) {
            if (comma) putchar(','); comma = true;
            json(std::wstring(pair.first.begin(), pair.first.end())); printf(":%zu", pair.second);
        }
        printf("},\"diagnostics\":%zu,\"notices\":%zu}\n", HostDiagnosticCount(), HostNoticeCount());
        return HostDiagnosticCount() ? 1 : 0;
    } catch (const std::exception& e) { fprintf(stderr, "Full INI inspection failed: %s\n", e.what()); return 2; }
}
