// Execute the selected original initialization list, then export evaluated pools.
// GPU initialization is an explicit native event, not executed by this fixture.
#include "skeleton_plan.h"
#include "globals.h"
#include "HackerContext.h"
#include <fstream>
#include <cmath>
#include <cstdio>
#include <stdexcept>
void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostParseConstants();
void HostEnumerateLists();
void HostParseResources();
void HostCompileNamedList(const wchar_t*);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
using namespace efmi::frontend;
void HostParseIniFile(const std::filesystem::path& file)
{
    std::ifstream in(file,std::ios::binary|std::ios::ate);
    if(!in || in.tellg()<0 || in.tellg()>32*1024*1024)throw std::runtime_error("Invalid Core/INI file size");
    std::string bytes(static_cast<size_t>(in.tellg()),'\0');in.seekg(0);in.read(bytes.data(),bytes.size());
    if(!in)throw std::runtime_error("Cannot read Core/INI file");
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);
    if(!length)throw std::runtime_error("Core/INI file is not UTF-8");
    std::wstring text(length,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),text.data(),length);
    if(!text.empty() && text[0]==0xfeff)text.erase(text.begin());
    for(size_t at=0;(at=text.find(L"\r\n",at))!=std::wstring::npos;)text.erase(at,1);
    HostParseExcerpt(text.c_str(),file.filename().c_str());
}
static void json(const std::wstring& value)
{
    putchar('"');
    for(wchar_t c:value){
        if(c==L'"' || c==L'\\'){putchar('\\');putchar(char(c));}
        else if(c<32 || c>126)printf("\\u%04x",unsigned(c));else putchar(char(c));
    }
    putchar('"');
}
int HostModSkeleton(const wchar_t* filename,const wchar_t* core_folder,const wchar_t* name)
{
    try{
        const auto source=std::filesystem::absolute(filename),core=std::filesystem::absolute(core_folder);
        HostResourceRoot(source.parent_path().wstring());
        for(auto* file:{L"API.ini",L"MergedSkeleton.ini",L"SpatialIdentification.ini"})HostParseIniFile(core/file);
        HostParseIniFile(source);HostEnumerateLists();HostParseConstants();HostParseResources();
        const std::wstring ns=std::wstring(L"\\")+name+L"\\";
        const std::wstring init=L"commandlist"+ns+L"initializemergedskeleton";
        const std::wstring native=L"commandlist\\efmiv1\\initializemergedskeleton";
        DrawPlanAdapter adapter;adapter.external(explicitCommandListSections.at(native),native);
        HostCompileNamedList(init.c_str());optimise_command_lists(nullptr);
        adapter.adapt(explicitCommandListSections.at(init).command_list);
        HackerContext context;CommandListState state;state.mHackerContext=&context;DrawPlan events;std::wstring reason;
        if(!adapter.execute(explicitCommandListSections.at(init).command_list,state,events,reason)){
            fwprintf(stderr,L"Original initialization refused: %ls\n",reason.c_str());return 1;
        }
        if(HostDiagnosticCount())throw std::runtime_error("Original parser reported diagnostics");
        if(events.items.size()!=1 || events.items[0].operation!=PlanOperation::External || events.items[0].external!=native)
            throw std::runtime_error("Unexpected unresolved operations in initialization");
        auto number=[&](const std::wstring& variable){
            const float value=command_list_globals.at(variable).fval;
            if(!std::isfinite(value) || value<0 || value>65536 || std::floor(value)!=value)throw std::runtime_error("Invalid dimension");
            return uint32_t(value);
        };
        if(number(L"$\\efmiv1\\cfg_ms_implicit_bones_count")!=512 || number(L"$\\efmiv1\\cfg_ms_skeletons_count")!=2 ||
           number(L"$\\efmiv1\\cfg_ms_bone_entry_size")!=3)throw std::runtime_error("Unsupported Core bone layout");
        FilePlanCompiler files(customResources);SkeletonFilePlan plan;
        const std::wstring pools=L"pool"+ns+L"_mergedskeleton_component_";
        if(!read_skeleton_plan(number(L"$"+ns+L"bones_count"),number(L"$"+ns+L"max_instance_count"),
            number(L"$\\efmiv1\\cfg_ms_max_lod_level_count"),customResourcePools.at(pools+L"vertexgroupoffsets"),
            customResourcePools.at(pools+L"vertexgroupcounts"),customResourcePools.at(pools+L"lodremaps"),
            adapter,files,plan,reason)){
            fwprintf(stderr,L"Skeleton metadata refused: %ls\n",reason.c_str());return 1;
        }
        printf("{\"scope\":\"evaluated original initialization; native GPU initialization unresolved\",\"bones\":%u,\"instances\":%u,\"lods\":%u,\"components\":[",
               plan.bones,plan.instances,plan.lod_count);
        for(size_t c=0;c<plan.components.size();++c){
            const auto& item=plan.components[c];if(c)putchar(',');
            printf("{\"id\":%zu,\"first\":%u,\"count\":%u,\"remaps\":[",c,item.first,item.count);
            for(uint32_t lod=0;lod<plan.lod_count;++lod){if(lod)putchar(',');const auto& remap=item.lods[lod];
                printf("{\"state\":%d,\"path\":",int(remap.source));json(remap.path.wstring());putchar('}');
            }
            printf("]}");
        }
        printf("]}\n");return 0;
    }catch(const std::exception& e){fprintf(stderr,"Skeleton initialization failed: %s\n",e.what());return 1;}
}
