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
void HostCompileNamedList(const wchar_t* name)
{
    auto& list=explicitCommandListSections.at(name);
    ParseCommandList(name,&list.command_list,&list.post_command_list,nullptr,true);
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
