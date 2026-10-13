// Execute the installed original Core identification state machine with bounded
// CPU instance windows. Only SpatialHash is replaced by a buffer-read adapter.
#include "buffer_queries.h"
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
        BufferQueries queries;DrawPlanAdapter adapter;
        adapter.resource_queries([&](ResourceCopyTarget& target,CommandListState& state,float& value,std::wstring& reason){
            return queries.read(target,state,value,reason);
        });
        adapter.adapt(explicitCommandListSections.at(root).command_list);
        if(HostDiagnosticCount())throw std::runtime_error("Original Core parser reported diagnostics");
        std::array<uint8_t,512> bytes{};std::wstring reason;DrawPlan plan;
        auto position=[&](unsigned instance,float x,float y,float z){
            const float xyz[]={x,y,z};memcpy(bytes.data()+instance*256+48,xyz,sizeof(xyz));
        };
        position(0,10,1,10);position(1,40,1,10);
        auto* original=&customResources.at(L"resource\\efmiv1\\original_vs_cb");
        if(!queries.bind(original,bytes.data(),bytes.size(),reason))throw std::runtime_error("Cannot bind original fixture window");
        HackerContext context;DrawCallInfo call;call.InstanceCount=2;
        auto run=[&](unsigned frame,unsigned component,bool new_draw=true){
            G->frame_no=frame;G->gFrameTime=1.0f/120;
            command_list_globals.at(L"$\\efmiv1\\component_id").fval=float(component);
            if(new_draw)++context.draw_number;
            CommandListState state;state.mHackerContext=&context;state.call_info=&call;
            const bool ok=adapter.execute(explicitCommandListSections.at(root).command_list,state,plan,reason);
            if(!ok)fwprintf(stderr,L"Spatial planning refused: %ls\n",reason.c_str());return ok;
        };
        auto value=[&](const wchar_t* name,unsigned instance){
            return customResourcePools.at(std::wstring(L"pool\\efmiv1\\spatialidentity_")+name).GetVariable(float(instance),false,true,false)->fval;
        };
        bool warm=true;for(unsigned c=0;c<4;++c)warm=run(100,c) && warm;
        check(warm && plan.items.empty() && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==1,
              "original Core delays two newly observed objects for their first logical frame");
        const float left=value(L"spatialids",0),right=value(L"spatialids",1);
        check(left!=right && left>=0 && right>=0,"original spatial pool assigns separate ids to distant instances");
        check(run(101,0) && value(L"identifiedinstances",0)==3 && value(L"identifiedinstances",1)==3 &&
              value(L"spatialids",0)==left && value(L"spatialids",1)==right,
              "four observed components become custom on the next frame without merging instances");
        position(0,10.125f,1,10.125f);
        check(run(102,0) && value(L"identifiedinstances",0)==3 && value(L"spatialids",0)==left,
              "original nearby-cell matching preserves identity while the object moves");
        position(0,200,1,10);
        check(run(103,0) && value(L"identifiedinstances",0)==1 && value(L"spatialids",0)!=left &&
              value(L"identifiedinstances",1)==3 && value(L"spatialids",1)==right,
              "a distant new object cannot inherit the old object's custom status");
        check(run(104,0) && value(L"identifiedinstances",0)==2 && value(L"identifiedinstances",1)==3,
              "insufficient component evidence uses the original instance after initial delay");
        check(run(104,0,false) && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==3,
              "duplicate caller skips an unclaimed original instance without losing the custom one");
        check(run(108,0) && value(L"identifiedinstances",0)==1 && value(L"identifiedinstances",1)==1,
              "expired objects restart identification instead of retaining custom status");
        queries.clear();check(!run(109,0),"missing original buffer refuses the full identification transaction");
        queries.bind(original,bytes.data(),52,reason);check(!run(109,0),"partial instance position is rejected before original pool use");
        queries.bind(original,bytes.data(),bytes.size(),reason);position(0,std::numeric_limits<float>::quiet_NaN(),1,1);
        check(!run(109,0),"nonfinite mapped coordinates refuse before quantization");
        position(0,0,0,0);check(!run(109,0),"reserved zero spatial hash is not fabricated into an instance id");
    }catch(const std::exception& e){++failed;printf("FAIL: %s\n",e.what());}
    printf("XXMI original spatial Core: %d checks passed, %d failed\n",passed,failed);return failed?1:0;
}
