// Execute the installed original Core identification state machine with bounded
// CPU instance windows. Only SpatialHash is replaced by a buffer-read adapter.
#include "buffer_queries.h"
#include "spatial_palettes.h"
#include "globals.h"
#include "HackerContext.h"
#include <filesystem>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
void HostParseIniFile(const std::filesystem::path&);
void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostParseConstants();
void HostEnumerateLists();
void HostParseResources();
void HostCompileGraph(const std::wstring&,const std::vector<std::wstring>&);
void HostResourceRoot(const std::wstring&);
size_t HostDiagnosticCount();
using namespace efmi::frontend;
int HostCoreSpatial(const wchar_t* folder)
{
    int passed=0,failed=0;
    auto check=[&](bool ok,const char* text){if(ok)++passed;else{++failed;printf("FAIL: %s\n",text);}};
    try{
        const auto core=std::filesystem::absolute(folder);HostResourceRoot(core.wstring());
        for(auto* file:{L"API.ini",L"MergedSkeleton.ini",L"SpatialIdentification.ini",L"Helpers.ini"})HostParseIniFile(core/file);
        HostParseExcerpt(LR"(
namespace = spatial_fixture
[PoolObjects]
pool_size = 4
pool_index_type = spatial
pool_spatial_radius = $\EFMIv1\cfg_spatial_base_radius
pool_expiration_timeout_frames = $\EFMIv1\cfg_spatial_expiration_frames
pool_expiration_reset_elements = 1
pool_expiration_refresh_on_read = 1
[CommandListIdentify]
Pool\EFMIv1\Input_ObjectSpatialIdentity = ref PoolObjects
run = CommandList\EFMIv1\SpatialIdentity_IdentifyComponentInstances
)",L"spatial-fixture.ini");
        HostEnumerateLists();HostParseConstants();HostParseResources();
        const std::wstring root=L"commandlist\\spatial_fixture\\identify";
        HostCompileGraph(root,{});optimise_command_lists(nullptr);
        BufferQueries queries;DrawPlanAdapter adapter;std::vector<uint32_t> hashes;
        adapter.resource_queries([&](ResourceCopyTarget& target,CommandListState& state,float& value,std::wstring& reason){
            if(!queries.read(target,state,value,reason))return false;
            uint32_t hash;memcpy(&hash,&value,4);hashes.push_back(hash);return true;
        });
        adapter.adapt(explicitCommandListSections.at(root).command_list);
        if(HostDiagnosticCount())throw std::runtime_error("Original Core parser reported diagnostics");
        std::array<uint8_t,512> bytes{};std::wstring reason;DrawPlan plan;
        auto position=[&](unsigned instance,float x,float y,float z){
            const float xyz[]={x,y,z};memcpy(bytes.data()+instance*256+48,xyz,sizeof(xyz));
        };
        position(0,10,1,10);position(1,40,1,10);
        const uint32_t offsets[2]={2000,4000};for(unsigned i=0;i<2;++i)memcpy(bytes.data()+i*256+80,offsets,8);
        auto entry=[&](unsigned i){efvk::InstanceEntry e;memcpy(e.data(),bytes.data()+i*256,256);return e;};
        auto* original=&customResources.at(L"resource\\efmiv1\\original_vs_cb");
        if(!queries.bind(original,bytes.data(),bytes.size(),reason))throw std::runtime_error("Cannot bind original fixture window");
        SpatialPalettes palettes;efmi::core::PaletteDefinition definition;std::string error;
        definition.bones=4;definition.instances=4;definition.components.resize(4);
        for(unsigned c=0;c<4;++c){definition.components[c].range={c,1};for(auto& lod:definition.components[c].lods)lod.available=true;}
        auto& pool=customResourcePools.at(L"pool\\spatial_fixture\\objects");
        if(!palettes.configure(pool,definition,error))throw std::runtime_error(error);
        std::vector<uint64_t> tokens;
        HackerContext context;DrawCallInfo call;call.InstanceCount=2;
        auto value=[&](const wchar_t* name,unsigned instance){
            return customResourcePools.at(std::wstring(L"pool\\efmiv1\\spatialidentity_")+name).GetVariable(float(instance),false,true,false)->fval;
        };
        auto run=[&](unsigned frame,unsigned component,bool new_draw=true){
            G->frame_no=frame;G->gFrameTime=1.0f/120;
            command_list_globals.at(L"$\\efmiv1\\component_id").fval=float(component);
            if(new_draw)++context.draw_number;
            CommandListState state;state.mHackerContext=&context;state.call_info=&call;
            hashes.clear();
            if(!adapter.execute(explicitCommandListSections.at(root).command_list,state,plan,reason)){
                palettes.invalidate();fwprintf(stderr,L"Spatial planning refused: %ls\n",reason.c_str());return false;
            }
            if(hashes.size()!=call.InstanceCount)throw std::runtime_error("Unexpected original spatial query count");
            std::vector<SpatialObservation> observations;
            for(unsigned i=0;i<call.InstanceCount;++i){
                const float slot=value(L"spatialids",i);
                if(!std::isfinite(slot) || slot<0 || slot>=pool.GetPoolSize() || std::floor(slot)!=slot)
                    throw std::runtime_error("Invalid original slot result");
                observations.push_back({uint32_t(slot),hashes[i]});
            }
            if(!palettes.observe(frame,float(G->gFrameTime),observations,tokens,error))throw std::runtime_error(error);
            for(unsigned i=0;i<call.InstanceCount;++i){
                std::array<float,24> bones;for(unsigned n=0;n<bones.size();++n)bones[n]=float(i*100+component*10+n);
                const efvk::BoneWindow view{reinterpret_cast<const uint8_t*>(bones.data()),sizeof(bones)};
                if(palettes.import_component(tokens[i],component,0,entry(i),view,view,1,error)==efvk::BoneImportResult::Rejected)
                    throw std::runtime_error(error);
            }
            return true;
        };
        bool warm=true;for(unsigned c=0;c<4;++c)warm=run(100,c) && warm;
        check(warm && plan.items.empty() && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==1,
              "original Core delays two newly observed objects for their first logical frame");
        const float left=value(L"spatialids",0),right=value(L"spatialids",1);
        check(left!=right && left>=0 && right>=0,"original spatial pool assigns separate ids to distant instances");
        const auto original_tokens=tokens;std::shared_ptr<const efmi::core::PaletteSnapshot> a,b;
        check(tokens[0]!=tokens[1] && palettes.snapshot(tokens[0],entry(0),a,error) && palettes.snapshot(tokens[1],entry(1),b,error) &&
              a->bones!=b->bones,"original spatial results select complete, separate character palettes");
        const auto retained=a;
        check(run(101,0) && value(L"identifiedinstances",0)==3 && value(L"identifiedinstances",1)==3 &&
              value(L"spatialids",0)==left && value(L"spatialids",1)==right,
              "four observed components become custom on the next frame without merging instances");
        position(0,10.125f,1,10.125f);
        check(run(102,0) && value(L"identifiedinstances",0)==3 && value(L"spatialids",0)==left,
              "original nearby-cell matching preserves identity while the object moves");
        check(tokens==original_tokens && palettes.snapshot(tokens[0],entry(0),a,error),
              "nearby movement retains palette epoch and prior-update component coverage");
        position(0,200,1,10);
        check(run(103,0) && value(L"identifiedinstances",0)==1 && value(L"spatialids",0)!=left &&
              value(L"identifiedinstances",1)==3 && value(L"spatialids",1)==right,
              "a distant new object cannot inherit the old object's custom status");
        check(tokens[0]!=original_tokens[0] && tokens[1]==original_tokens[1] &&
              !palettes.snapshot(tokens[0],entry(0),a,error),"distant original identity needs fresh whole-character bones");
        check(run(104,0) && value(L"identifiedinstances",0)==2 && value(L"identifiedinstances",1)==3,
              "insufficient component evidence uses the original instance after initial delay");
        check(run(104,0,false) && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==3,
              "duplicate caller skips an unclaimed original instance without losing the custom one");
        check(run(108,0) && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==1,
              "expired objects restart identification instead of retaining custom status");
        check(tokens[1]!=original_tokens[1] && !palettes.snapshot(tokens[1],entry(1),b,error),
              "expiration retires the previous palette even when the original slot number returns");
        queries.clear();check(!run(109,0),"missing original buffer refuses the full identification transaction");
        queries.bind(original,bytes.data(),52,reason);check(!run(109,0),"partial instance position is rejected before original pool use");
        queries.bind(original,bytes.data(),bytes.size(),reason);position(0,std::numeric_limits<float>::quiet_NaN(),1,1);
        check(!run(109,0),"nonfinite mapped coordinates refuse before quantization");
        position(0,0,0,0);check(!run(109,0),"reserved zero spatial hash is not fabricated into an instance id");
        check(!palettes.snapshot(tokens[0],entry(0),a,error) && retained->bones.size()==(512+8)*48,
              "failed original execution invalidates active masters while retained snapshots survive");
        // Fill and force the real original pool's FIFO replacement in one frame.
        pool.ResetPool();palettes.invalidate();call.InstanceCount=1;position(0,50,1,10);
        for(unsigned c=0;c<4;++c)run(110,c);
        const auto old_token=tokens[0];const float old_slot=value(L"spatialids",0);bool recycled=false;
        for(unsigned i=0;i<5;++i){
            position(0,float(100+20*i),1,10);run(110,0);
            if(value(L"spatialids",0)==old_slot){
                recycled=true;check(tokens[0]!=old_token && !palettes.snapshot(tokens[0],entry(0),a,error),
                                   "full original pool reuse gets a new epoch and cannot inherit a complete palette");
            }
        }
        check(recycled,"fixture exercised actual original spatial FIFO slot replacement");
        auto unchanged=tokens;
        check(!palettes.observe(110,1.f/120,{{0,hashes[0]},{0,hashes[0]}},tokens,error) && tokens==unchanged,
              "ambiguous instanced results refuse atomically and invalidate continuity");
        pool.read_refreshes_expiration=false;
        check(!palettes.observe(111,1.f/120,{{0,hashes[0]}},tokens,error),"changing original pool lifetime flags requires reconfiguration");
    }catch(const std::exception& e){++failed;printf("FAIL: %s\n",e.what());}
    printf("XXMI original spatial Core: %d checks passed, %d failed\n",passed,failed);return failed?1:0;
}
