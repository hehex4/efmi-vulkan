// Expose the original parser's CPU-only entry points for the standalone prototype.
#include "IniHandler.cpp"

void HostParseExcerpt(const wchar_t* text,const wchar_t* ns)
{
    std::wistringstream stream(text);
    std::wstring name=ns?ns:L"";
    ParseIniStream(&stream,ns?&name:nullptr);
}
void HostCompileList(const wchar_t* name,CommandList* pre,CommandList* post)
{
    ParseCommandList(name,pre,post,nullptr,true);
}
void HostParseConstants() { ParseConstantsSection(); }
void HostEnumerateLists() { EnumerateExplicitCommandListSections(); }
void HostParseResources() { ParseResourceSections(); }
void HostParseTextureOverrides() { ParseTextureOverrideSections(); }
void HostCompileNamedList(const wchar_t* name)
{
    auto& list=explicitCommandListSections.at(name);
    ParseCommandList(name,&list.command_list,&list.post_command_list,nullptr,true);
}
// Compile only the original command graph reachable from a selected root.
// Native boundaries are enumerated first and never parsed as graphics commands.
static void compile_graph(std::vector<ExplicitCommandListSection*> pending,const std::vector<CommandList*>& roots,
                          const std::vector<std::wstring>& natives)
{
    std::unordered_map<ExplicitCommandListSection*,std::wstring> names;
    for(auto& pair:explicitCommandListSections)names.emplace(&pair.second,pair.first);
    std::unordered_set<ExplicitCommandListSection*> compiled;
    std::function<void(CommandList&,size_t)> visit=[&](CommandList& list,size_t depth){
        if(depth>256)throw std::runtime_error("Original command graph nesting budget exceeded");
        for(auto& command:list.commands){
            if(auto* call=dynamic_cast<RunExplicitCommandList*>(command.get())){
                if(call->command_list_section)pending.push_back(call->command_list_section);
            }else if(auto* copy=dynamic_cast<CopyCommandListCommand*>(command.get())){
                if(copy->src)pending.push_back(copy->src);if(copy->dst)pending.push_back(copy->dst);
            }else if(auto* branch=dynamic_cast<IfCommand*>(command.get())){
                for(auto* body:{branch->true_commands_pre.get(),branch->true_commands_post.get(),
                               branch->false_commands_pre.get(),branch->false_commands_post.get()})if(body)visit(*body,depth+1);
            }
        }
    };
    for(auto* root:roots)if(root)visit(*root,0);
    while(!pending.empty()){
        auto* section=pending.back();pending.pop_back();
        if(!compiled.insert(section).second)continue;
        if(compiled.size()>4096)throw std::runtime_error("Original command graph section budget exceeded");
        const auto& name=names.at(section);
        if(std::find(natives.begin(),natives.end(),name)!=natives.end())continue;
        HostCompileNamedList(name.c_str());visit(section->command_list,0);visit(section->post_command_list,0);
    }
}
void HostCompileGraph(const std::wstring& root,const std::vector<std::wstring>& natives)
{
    compile_graph({&explicitCommandListSections.at(root)},{},natives);
}
void HostCompileRoots(const std::vector<CommandList*>& roots,const std::vector<std::wstring>& natives)
{
    compile_graph({},roots,natives);
}
// Callback-only offline inspection. Core pools/dynamic resources are outside
// this fixture; their existence is not implied by these file declarations.
void HostParseFileResources(size_t root_length)
{
    auto lower=ini_sections.lower_bound(L"Resource");
    auto upper=prefix_upper_bound(ini_sections,std::wstring(L"Resource"));
    for(auto it=lower;it!=upper;++it){
        wchar_t name[MAX_PATH];
        if(!GetIniString(it->first.c_str(),L"filename",nullptr,name,MAX_PATH))continue;
        std::wstring path;get_namespaced_section_path(it->first.c_str(),&path);
        if(root_length+path.size()+wcslen(name)+2>=MAX_PATH)
            throw std::runtime_error("Resource filename exceeds original parser path capacity");
        ParseResourceSection(it->first.c_str(),nullptr);
    }
}
