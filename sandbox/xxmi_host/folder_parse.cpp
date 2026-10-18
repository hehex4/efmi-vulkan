// Full original parser inspection. No command execution, graphics or file writes.
#include "globals.h"
#include "CommandList.h"
#include <filesystem>
#include <set>
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
size_t HostDireCount();
const std::vector<std::pair<bool,std::string>>& HostDiagnosticTexts();
size_t HostInputCount();

static void json(const std::wstring& value);

// What one parsed command does, as a key for counting: its class and the parts of it that
// decide which adapter can execute it (target kinds, stages, copy options, draw form).
static std::wstring target_kind(const ResourceCopyTarget& t)
{
    std::wstring name;
    for (const auto& entry : ResourceCopyTargetTypeNames) if (entry.val == t.type) { name = entry.name; break; }
    if (name.empty()) { wchar_t hex[16]; swprintf(hex, 16, L"0x%x", unsigned(t.type)); name = hex; }
    if (t.shader_type) { name += L":"; name += t.shader_type; }
    if (t.slot_expression) name += L"[expr]";
    if (t.range_start || t.range_end) name += L"[range]";
    if (t.evaluation_mode != ResourceCopyTargetEvaluationMode::RESOURCE) name += L"(member)";
    return name;
}
static std::wstring operation_key(const CommandListCommand& command)
{
    std::wstring key = std::wstring(L"");
    const char* raw = typeid(command).name();
    if (!strncmp(raw, "class ", 6)) raw += 6;
    for (; *raw; ++raw) key += wchar_t(*raw);
    wchar_t number[32];
    if (auto* copy = dynamic_cast<const ResourceCopyOperation*>(&command)) {
        swprintf(number, 32, L" options=0x%x", unsigned(copy->options));
        key += L" dst=" + target_kind(copy->dst) + L" src=" + target_kind(copy->src) + number;
    } else if (auto* range = dynamic_cast<const SlotRangeCopyOperation*>(&command)) {
        swprintf(number, 32, L" options=0x%x", unsigned(range->options));
        key += L" dst=" + target_kind(range->dst) + L" src=" + target_kind(range->src) + number;
    } else if (auto* pool = dynamic_cast<const PoolCopyOperation*>(&command)) {
        key += L" dst=" + target_kind(pool->dst) + L" src=" + target_kind(pool->src);
    } else if (auto* draw = dynamic_cast<const DrawCommand*>(&command)) {
        swprintf(number, 32, L" type=%d", int(draw->type)); key += number;
    } else if (auto* clear = dynamic_cast<const ClearViewCommand*>(&command)) {
        key += L" target=" + target_kind(clear->target);
    } else if (auto* check = dynamic_cast<const CheckTextureOverrideCommand*>(&command)) {
        key += L" target=" + target_kind(check->target);
    } else if (auto* store = dynamic_cast<const StoreCommand*>(&command)) {
        key += L" src=" + target_kind(store->src);
    }
    return key;
}
// Every reachable command once: registered lists, override and shader lists, and the branch
// lists of if blocks. Counted per section, so that the report can attribute it to a Mod.
static void census()
{
    std::map<std::pair<std::wstring, std::wstring>, size_t> counts;
    std::set<const CommandList*> seen;
    std::function<void(const CommandList*, const std::wstring&)> walk = [&](const CommandList* list, const std::wstring& section) {
        if (!list || !seen.insert(list).second) return;
        const std::wstring& name = list->ini_section.empty() ? section : list->ini_section;
        for (const auto& command : list->commands) {
            if (!command) continue;
            ++counts[{name, operation_key(*command)}];
            if (auto* branch = dynamic_cast<const IfCommand*>(command.get()))
                for (const auto* child : {branch->true_commands_pre.get(), branch->true_commands_post.get(),
                                          branch->false_commands_pre.get(), branch->false_commands_post.get()})
                    walk(child, name);
        }
    };
    for (auto* list : registered_command_lists) walk(list, L"");
    for (const auto& hashed : G->mTextureOverrideMap)
        for (const auto& entry : hashed.second) { walk(&entry.command_list, entry.ini_section); walk(&entry.post_command_list, entry.ini_section); }
    for (const auto& entry : G->mShaderOverrideMap) { walk(&entry.second.command_list, L""); walk(&entry.second.post_command_list, L""); }
    for (const auto& entry : customShaders) { walk(&entry.second.command_list, entry.first); walk(&entry.second.post_command_list, entry.first); }
    walk(&G->present_command_list, L"Present"); walk(&G->post_present_command_list, L"Present");
    printf(",\"operation_census\":[");
    bool comma = false;
    for (const auto& entry : counts) {
        if (comma) putchar(','); comma = true;
        printf("{\"section\":"); json(entry.first.first);
        printf(",\"key\":"); json(entry.first.second);
        printf(",\"count\":%zu}", entry.second);
    }
    putchar(']');
}

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
        putchar('}');
        census();
        printf(",\"diagnostics\":%zu,\"dire\":%zu,\"notices\":%zu,\"diagnostic_messages\":[",
               HostDiagnosticCount(), HostDireCount(), HostNoticeCount());
        comma = false;
        for (const auto& entry : HostDiagnosticTexts()) {
            if (comma) putchar(','); comma = true;
            printf("{\"level\":\"%s\",\"text\":", entry.first ? "dire" : "warning");
            // The texts are UTF-8 (mod paths need not be ASCII); json() escapes the decoded text.
            std::wstring wide(entry.second.size(), L'\0');
            const int chars = MultiByteToWideChar(CP_UTF8, 0, entry.second.data(), int(entry.second.size()), wide.data(), int(wide.size()));
            wide.resize(chars > 0 ? size_t(chars) : 0);
            json(wide); putchar('}');
        }
        printf("]}\n");
        // The original loader continues after warnings; the inspection completes like it. Dire
        // messages (the original's internal BUG reports and failed operations) still fail it.
        return HostDireCount() ? 1 : 0;
    } catch (const std::exception& e) { fprintf(stderr, "Full INI inspection failed: %s\n", e.what()); return 2; }
}
