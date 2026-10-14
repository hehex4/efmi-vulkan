// Read-only callback planning fixture. It does not load Core or render a mod.
#include "draw_plan.h"
#include "resource_plan.h"
#include "globals.h"
#include "HackerContext.h"
#include "../../frontend/xxmi/rabbit_textures.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostParseConstants();
void HostEnumerateLists();
void HostParseFileResources(size_t);
void HostCompileNamedList(const wchar_t*);
void HostCompileGraph(const std::wstring&, const std::vector<std::wstring>&);
void HostParseResourceFamily(const std::wstring&);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
using namespace efmi::frontend;

static void json(const std::wstring& value)
{
    putchar('"');
    for(wchar_t c:value){
        if(c==L'"' || c==L'\\'){putchar('\\');putchar(static_cast<char>(c));}
        else if(c<32 || c>126)printf("\\u%04x",static_cast<unsigned>(c));
        else putchar(static_cast<char>(c));
    }
    putchar('"');
}

int HostModPlan(const wchar_t* filename,const wchar_t* prefix,const wchar_t* native)
{
    try {
        const auto source=std::filesystem::absolute(filename);
        const auto root=source.parent_path().wstring();
        HostResourceRoot(root);
        std::ifstream file(source,std::ios::binary|std::ios::ate);
        if(!file || file.tellg()<0 || file.tellg()>32*1024*1024)throw std::runtime_error("Invalid INI input size");
        std::string bytes(static_cast<size_t>(file.tellg()),'\0');file.seekg(0);file.read(bytes.data(),bytes.size());
        if(!file)throw std::runtime_error("Cannot read INI input");
        int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
        if(!size)throw std::runtime_error("INI input must be UTF-8");
        std::wstring input(size,L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),input.data(),size);
        if(!input.empty() && input.front()==0xfeff)input.erase(input.begin());
        // The original file reader uses Windows text mode. Match its CRLF
        // translation before exposing a binary UTF-8 input as a string stream.
        std::wstring text;text.reserve(input.size());
        for(size_t i=0;i<input.size();++i)
            if(input[i]!=L'\r' || i+1==input.size() || input[i+1]!=L'\n')text+=input[i];
        HostParseExcerpt(text.c_str(),source.filename().wstring().c_str());
        auto lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        const bool rabbit = lower.find(L"commandlist\\rabbitfx\\settextures") != std::wstring::npos;
        if (rabbit) HostParseExcerpt(rabbit_texture_declarations().c_str(), L"efvk-rabbit-textures.ini");
        HostEnumerateLists();HostParseConstants();HostParseFileResources(root.size());
        if (rabbit) HostParseResourceFamily(L"Resource\\rabbitfx\\");
        DrawPlanAdapter adapter;
        adapter.external(explicitCommandListSections[native],native);
        std::vector<std::wstring> names;
        const std::wstring family=prefix;
        for(const auto& pair:explicitCommandListSections)
            if(pair.first.compare(0,family.size(),family)==0)names.push_back(pair.first);
        std::sort(names.begin(),names.end());
        if(names.empty())throw std::runtime_error("No command lists match the requested prefix");
        for(const auto& name:names)HostCompileGraph(name, {native});
        optimise_command_lists(nullptr);
        for(const auto& name:names)adapter.adapt(explicitCommandListSections.at(name).command_list);
        if(HostDiagnosticCount())throw std::runtime_error("Original parser reported diagnostics; refusing incomplete plans");
        FilePlanCompiler resource_compiler(customResources);

        // Sample coverage is data-driven: vary every persistent scalar, all
        // scalars together, and each of the two requested LOD values.
        std::vector<std::pair<std::wstring,float>> variables;
        for(auto* variable:persistent_variables)variables.emplace_back(variable->name,variable->fval);
        std::sort(variables.begin(),variables.end());
        if(variables.size()>64)throw std::runtime_error("Fixture persistent-variable budget exceeded");
        const auto namespace_end=family.find_last_of(L'\\');
        const std::wstring variable_namespace=L"$"+family.substr(11,namespace_end-11)+L"\\";
        auto lod=command_list_globals.find(variable_namespace+L"lod_level");
        if(lod==command_list_globals.end())throw std::runtime_error("LOD test variable is absent");

        printf("{\"scope\":\"CPU callback plans; native Core boundary remains unresolved\",\"rabbit_texture_roles\":%s,\"resources\":[", rabbit ? "true" : "false");
        bool comma=false;
        for(const auto& pair:customResources){
            if(comma)putchar(',');comma=true;
            const auto& r=pair.second;
            printf("{\"name\":");json(r.name);printf(",\"filename\":");json(r.filename);
            printf(",\"stride\":%d,\"format\":%d}",r.override_stride,static_cast<int>(r.override_format));
        }
        printf("],\"runs\":[");comma=false;
        for(size_t scenario=0;scenario<variables.size()+3;++scenario){
            for(auto& entry:variables)command_list_globals.at(entry.first).fval=entry.second;
            if(scenario==1 || scenario==2)
                for(auto& entry:variables)command_list_globals.at(entry.first).fval=static_cast<float>(scenario);
            else if(scenario>=3)command_list_globals.at(variables[scenario-3].first).fval=1;
            for(unsigned level=0;level<2;++level){
                lod->second.fval=static_cast<float>(level);
                for(const auto& name:names){
                    HackerContext context;CommandListState state;state.mHackerContext=&context;
                    DrawPlan plan;std::wstring reason;
                    if(!adapter.execute(explicitCommandListSections.at(name).command_list,state,plan,reason))
                        throw std::runtime_error("Callback planning refused");
                    efmi::assets::FilePlan owned;
                    if(!resource_compiler.compile(plan,owned,reason)){
                        fwprintf(stderr,L"Resource plan refused for %ls: %ls\n",name.c_str(),reason.c_str());
                        throw std::runtime_error("Callback resource declarations refused");
                    }
                    if(comma)putchar(',');comma=true;
                    printf("{\"section\":");json(name);
                    printf(",\"scenario\":%zu,\"lod\":%u,\"items\":[",scenario,level);
                    bool item_comma=false;size_t owned_at=0;
                    for(const auto& item:plan.items){
                        const auto& resolved=owned.items.at(owned_at++);
                        if(item_comma)putchar(',');item_comma=true;
                        printf("{\"op\":%d,\"slot\":%u",static_cast<int>(item.operation),item.slot);
                        if(item.operation==PlanOperation::External){
                            printf(",\"external\":");json(item.external);printf(",\"phase_mask\":%u",item.phase_mask);
                        } else if(item.operation==PlanOperation::DrawIndexed){
                            printf(",\"count\":%u,\"first\":%u,\"base\":%d,\"instances\":%u,\"first_instance\":%u",
                                   item.index_count,item.first_index,item.vertex_offset,item.instance_count,item.first_instance);
                        } else {
                            printf(",\"resource\":");if(item.resource)json(item.resource->name);else printf("null");
                            printf(",\"file_binding\":");
                            if(resolved.resource){
                                printf("{\"path\":");json(resolved.resource->path.wstring());
                                printf(",\"usage\":%u,\"stride\":%u,\"index_type\":%d}",resolved.resource->usage,resolved.resource->stride,
                                       static_cast<int>(resolved.resource->index_type));
                            }else printf("null");
                        }
                        putchar('}');
                    }
                    printf("]}");
                }
            }
        }
        printf("]}\n");
        return HostDiagnosticCount()?1:0;
    } catch(const std::exception& e){fprintf(stderr,"Mod callback planning failed: %s\n",e.what());return 1;}
}
