// Execute actual sample TextureOverride entries and original Core logic against
// saved CPU windows. The second logical update repeats bytes; it is not animation.
#include "component_bridge.h"
#include "vertex_plan.h"
#include "globals.h"
#include "ResourceHash.h"
#include "HackerContext.h"
#include <fstream>
#include <cstring>
#include <cstdio>
#include <stdexcept>
void HostParseIniFile(const std::filesystem::path&);
void HostParseConstants();
void HostEnumerateLists();
void HostParseResources();
void HostParseTextureOverrides();
void HostCompileRoots(const std::vector<CommandList*>&,const std::vector<std::wstring>&);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
using namespace efmi::frontend;
class ComponentReader {
    std::vector<uint8_t> data;size_t at=0;
public:
    explicit ComponentReader(const std::filesystem::path& path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);const auto size=in.tellg();
        if(!in || size<0 || size>(64<<20))throw std::runtime_error("Component replay input budget");
        data.resize(size_t(size));in.seekg(0);in.read(reinterpret_cast<char*>(data.data()),size);
        if(!in)throw std::runtime_error("Component replay read failed");
    }
    const uint8_t* take(size_t n){
        if(n>data.size()-at)throw std::runtime_error("Truncated component replay");
        auto* result=data.data()+at;at+=n;return result;
    }
    template<class T>T get(){T v;std::memcpy(&v,take(sizeof(T)),sizeof(T));return v;}
    bool done()const{return at==data.size();}
};
template<class T>static void write(std::ofstream& out,T value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
struct ComponentRecord {
    uint32_t group,hash,count,first;int32_t base;ComponentInput input;
    std::vector<uint32_t> vertex_code;std::vector<VertexSemantic> semantics;efvk::VertexInputRecipe vertex;
};
int HostCoreComponents(const wchar_t* filename,const wchar_t* core_folder,const wchar_t* name,
                       const wchar_t* replay_file,const wchar_t* output_file)
{
    try{
        const auto source=std::filesystem::absolute(filename),core=std::filesystem::absolute(core_folder);
        HostResourceRoot(source.parent_path().wstring());
        for(auto* file:{L"API.ini",L"MergedSkeleton.ini",L"SpatialIdentification.ini",L"Helpers.ini"})HostParseIniFile(core/file);
        HostParseIniFile(source);HostEnumerateLists();HostParseConstants();HostParseResources();
        FilePlanCompiler files(customResources);DrawPlanAdapter adapter;CoreComponentBridge bridge(adapter,files);bridge.install();
        HostParseTextureOverrides();
        std::vector<CommandList*> roots;
        const std::wstring ns=std::wstring(L"\\")+name+L"\\";
        const auto prefix=L"textureoverride"+ns+L"_entrypoint_";
        for(auto& hashed:G->mTextureOverrideMap)for(auto& entry:hashed.second)
            if(!_wcsnicmp(entry.ini_section.c_str(),prefix.c_str(),prefix.size())){
                roots.push_back(&entry.command_list);roots.push_back(&entry.post_command_list);
            }
        if(roots.empty())throw std::runtime_error("Original sample entries were not parsed");
        HostCompileRoots(roots,CoreComponentBridge::boundaries());optimise_command_lists(nullptr);
        for(auto* root:roots)adapter.adapt(*root);
        if(HostDiagnosticCount())throw std::runtime_error("Original entry/Core graph has parser diagnostics");
        SkeletonSetup setup;setup.initialize=&explicitCommandListSections.at(L"commandlist"+ns+L"initializemergedskeleton");
        setup.bones=&command_list_globals.at(L"$"+ns+L"bones_count");setup.instances=&command_list_globals.at(L"$"+ns+L"max_instance_count");
        const auto pools=L"pool"+ns+L"_mergedskeleton_component_";
        setup.offsets=&customResourcePools.at(pools+L"vertexgroupoffsets");setup.counts=&customResourcePools.at(pools+L"vertexgroupcounts");
        setup.remaps=&customResourcePools.at(pools+L"lodremaps");setup.spatial=&customResourcePools.at(L"pool"+ns+L"_objectspatialidentity");
        HackerContext context;CommandListState preparation;preparation.mHackerContext=&context;std::wstring reason;
        if(!bridge.prepare(setup,preparation,reason)){fwprintf(stderr,L"Core preparation refused: %ls\n",reason.c_str());return 1;}
        // Registration/UI is outside this CPU fixture; explicitly select enabled.
        command_list_globals.at(L"$"+ns+L"mod_enabled").fval=1;
        ComponentReader reader(std::filesystem::absolute(replay_file));
        if(std::memcmp(reader.take(8),"EFMICP02",8))throw std::runtime_error("Component replay magic");
        const auto count=reader.get<uint32_t>();if(!count || count>4096)throw std::runtime_error("Component record budget");
        std::vector<ComponentRecord> records(count);
        for(auto& r:records){
            r.group=reader.get<uint32_t>();r.hash=reader.get<uint32_t>();r.count=reader.get<uint32_t>();
            r.first=reader.get<uint32_t>();r.base=reader.get<int32_t>();
            if(!r.group || r.group>4096)throw std::runtime_error("Component group budget");
            std::memcpy(r.input.entry.data(),reader.take(256),256);
            for(auto* view:{&r.input.current,&r.input.previous}){
                view->size=reader.get<uint32_t>();if(view->size>(1<<20))throw std::runtime_error("Component source window budget");
                view->data=reader.take(view->size);
            }
            auto size=reader.get<uint32_t>();if(size>(1<<22))throw std::runtime_error("Vertex code budget");
            r.vertex_code.resize(size);std::memcpy(r.vertex_code.data(),reader.take(size*4),size*4);
            size=reader.get<uint32_t>();if(size>256)throw std::runtime_error("Vertex semantic budget");
            for(uint32_t i=0;i<size;++i){VertexSemantic s;s.location=reader.get<uint32_t>();s.index=reader.get<uint32_t>();
                const auto length=reader.get<uint32_t>();if(length>128)throw std::runtime_error("Vertex semantic name budget");
                s.name.assign(reinterpret_cast<const char*>(reader.take(length)),length);r.semantics.push_back(std::move(s));}
            size=reader.get<uint32_t>();if(size>32)throw std::runtime_error("Vertex binding budget");
            for(uint32_t i=0;i<size;++i){VkVertexInputBindingDescription b;b.binding=reader.get<uint32_t>();b.stride=reader.get<uint32_t>();
                b.inputRate=VkVertexInputRate(reader.get<uint32_t>());r.vertex.bindings.push_back(b);}
            size=reader.get<uint32_t>();if(size>256)throw std::runtime_error("Vertex attribute budget");
            for(uint32_t i=0;i<size;++i){VkVertexInputAttributeDescription a;a.location=reader.get<uint32_t>();a.binding=reader.get<uint32_t>();
                a.format=VkFormat(reader.get<uint32_t>());a.offset=reader.get<uint32_t>();r.vertex.attributes.push_back(a);}
            r.input.frame_seconds=1.f/120;r.input.vs_cb_slot=2;r.input.ps_cb_slot=2;
        }
        if(!reader.done())throw std::runtime_error("Component replay trailing bytes");
        std::ofstream out(std::filesystem::absolute(output_file),std::ios::binary);out.write("EFMICO02",8);write(out,count*2);
        uint32_t plans=0,draws=0,externals=0,empty=0;size_t bytes=12;
        for(size_t begin=0;begin<records.size();){
            size_t end=begin+1;while(end<records.size() && records[end].group==records[begin].group)++end;
            for(unsigned phase=0;phase<2;++phase)for(size_t at=begin;at<end;++at){
                auto& r=records[at];r.input.frame=100+r.group*3+phase;
                DrawCallInfo call(DrawCall::DrawIndexedInstanced,0,r.count,1,uint32_t(r.base),r.first,0,nullptr,0);
                TextureOverrideMatches matches;find_texture_override_for_hash(r.hash,&matches,&call);
                if(matches.size()!=1 || _wcsnicmp(matches[0]->ini_section.c_str(),prefix.c_str(),prefix.size()))
                    throw std::runtime_error("Original matcher did not select exactly one sample entry");
                ++context.draw_number;CommandListState state;state.mHackerContext=&context;state.call_info=&call;
                DrawPlan plan;
                if(!bridge.plan(matches[0]->command_list,matches[0]->post_command_list,state,r.input,plan,reason)){
                    fwprintf(stderr,L"Component record %zu phase %u refused: %ls\n",at,phase,reason.c_str());return 1;
                }
                if(!plan.skip_original || call.skip)throw std::runtime_error("Replacement plan did not preserve deferred original handling");
                efmi::assets::FilePlan owned;
                if(!files.compile(plan,owned,reason)){fwprintf(stderr,L"Component file plan refused: %ls\n",reason.c_str());return 1;}
                uint32_t n=0,native=0;efmi::assets::FileDraws declarations;efmi::assets::FileDraw declaration;
                for(const auto& item:owned.items){
                    if(item.operation==efmi::assets::FileOperation::External){
                        if(item.external!=L"commandlist\\efmiv1\\overridetextures")throw std::runtime_error("Unexpected unresolved native command");
                        ++native;
                    }else if(item.operation==efmi::assets::FileOperation::BindVertex){
                        declaration.vertex[item.slot].resource=item.resource;
                        declaration.vertex[item.slot].source=item.resource?efmi::assets::BindingSource::File:efmi::assets::BindingSource::Null;
                    }else if(item.operation==efmi::assets::FileOperation::DrawIndexed){
                        ++n;declaration.layout=item.layout;declarations.draws.push_back(declaration);
                    }
                }
                VertexPlan vertices;
                if(n){
                    VertexProfile profile;std::string vertex_reason;
                    if(!profile.prepare(r.vertex_code,r.semantics,vertex_reason) ||
                       !profile.resolve(declarations,r.vertex_code,r.vertex,vertices,vertex_reason)){
                        fprintf(stderr,"Component record %zu phase %u vertex refused: %s\n",at,phase,vertex_reason.c_str());return 1;
                    }
                }
                ++plans;draws+=n;externals+=native;if(!n)++empty;
                write(out,uint32_t(at));write(out,phase);write(out,n);write(out,native);
                write(out,uint32_t(command_list_globals.at(L"$\\efmiv1\\component_id").fval));
                write(out,uint32_t(command_list_globals.at(L"$\\efmiv1\\lod_level").fval));bytes+=24;
                uint32_t vertex_index=0;
                for(const auto& item:owned.items)if(item.operation==efmi::assets::FileOperation::DrawIndexed){
                    if(!item.palette || item.instance_count!=1 || item.first_instance!=0 || item.layout.empty())
                        throw std::runtime_error("Custom draw lacks native palette, instance or declaration contract");
                    write(out,item.index_count);write(out,item.first_index);write(out,item.vertex_offset);
                    write(out,item.palette->identity);
                    out.write(reinterpret_cast<const char*>(item.palette->entry.data()),256);
                    write(out,uint32_t(item.palette->bones.size()));
                    out.write(reinterpret_cast<const char*>(item.palette->bones.data()),item.palette->bones.size());
                    bytes+=280+item.palette->bones.size();
                    const auto& vertex=vertices.draws.at(vertex_index++);
                    write(out,uint32_t(vertex.bindings.size()));
                    for(const auto& b:vertex.bindings){write(out,b.binding);write(out,b.stride);write(out,uint32_t(b.inputRate));}
                    write(out,uint32_t(vertex.attributes.size()));
                    for(const auto& a:vertex.attributes){write(out,a.location);write(out,a.binding);write(out,uint32_t(a.format));write(out,a.offset);}
                    bytes+=8+12*vertex.bindings.size()+16*vertex.attributes.size();
                    if(bytes>(128<<20))throw std::runtime_error("Component output budget");
                }
            }
            begin=end;
        }
        if(!out)throw std::runtime_error("Component output write failed");
        auto& last=records.back();
        DrawCallInfo call(DrawCall::DrawIndexedInstanced,0,last.count,1,uint32_t(last.base),last.first,0,nullptr,0);
        TextureOverrideMatches matches;find_texture_override_for_hash(last.hash,&matches,&call);
        if(matches.size()!=1)throw std::runtime_error("Final original match changed");
        auto run=[&](const ComponentInput& input,DrawPlan& plan){
            ++context.draw_number;CommandListState state;state.mHackerContext=&context;state.call_info=&call;
            return bridge.plan(matches[0]->command_list,matches[0]->post_command_list,state,input,plan,reason);
        };
        unsigned checks=0;DrawPlan saved;saved.items.emplace_back();saved.items[0].index_count=999;
        auto invalid=last.input;invalid.vs_cb_slot=-1;
        if(run(invalid,saved) || saved.items.size()!=1 || saved.items[0].index_count!=999)throw std::runtime_error("Invalid profile was not rejected atomically");
        ++checks;
        auto* offset=setup.offsets->GetVariable(0,false,true,true);const float old=offset->fval;offset->fval=old+1;
        if(run(last.input,saved) || saved.items.size()!=1 || saved.items[0].index_count!=999)throw std::runtime_error("Changed live metadata was not rejected atomically");
        offset->fval=old;++checks;
        if(!run(last.input,saved))throw std::runtime_error("Valid callback did not recover after metadata restoration");
        ++checks;
        command_list_globals.at(L"$"+ns+L"mod_enabled").fval=0;
        if(!run(last.input,saved) || saved.skip_original || !saved.items.empty())throw std::runtime_error("Original disabled-mod branch did not retain the application draw");
        ++checks;
        printf("Original component/Core replay: %u plans, %u draws, %u unresolved material calls, %u empty plans, %u refusal/control checks\n",
               plans,draws,externals,empty,checks);
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"Component/Core fixture failed: %s\n",e.what());return 1;}
}
