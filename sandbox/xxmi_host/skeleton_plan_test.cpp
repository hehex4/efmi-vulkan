#include "skeleton_plan.h"
#include <cstdio>
#include <limits>
using namespace efmi::frontend;
int HostSkeletonPlanTests()
{
    int passed=0,failed=0;
    auto check=[&](bool ok,const char* text){if(ok)++passed;else{++failed;printf("FAIL: %s\n",text);}};
    CustomResourcePool offsets,counts,remaps;CustomResource resource;
    offsets.name=L"pool_skeleton_test_offsets";counts.name=L"pool_skeleton_test_counts";remaps.name=L"pool_skeleton_test_remaps";
    for(auto* pool:{&offsets,&counts,&remaps}){
        pool->resource_template=&resource;
        pool->variable_template=std::make_unique<CommandListVariable>(pool->name,0,VariableFlags::NONE);
        pool->Initialize(3);
    }
    auto set=[&](CustomResourcePool& p,unsigned i,float value){p.GetVariable(float(i),false,true,true)->fval=value;};
    set(offsets,0,1);set(counts,0,1);set(counts,1,1);
    CustomResources files;FilePlanCompiler compiler(files);DrawPlanAdapter adapter;SkeletonFilePlan result;std::wstring reason;
    auto read=[&](unsigned bones=2,unsigned instances=3,unsigned lods=1){
        return read_skeleton_plan(bones,instances,lods,offsets,counts,remaps,adapter,compiler,result,reason);
    };
    check(read() && result.components.size()==3 && result.components[0].first==1 && result.components[2].count==0 &&
          result.components[0].lods[0].source==RemapSource::Unassigned,"unordered component ids and empty slots retain identity; absent LODs remain unassigned");
    const auto unchanged=[&](){return result.bones==2 && result.instances==3 && result.components.size()==3 && result.components[0].first==1;};
    set(offsets,0,0);check(!read() && !reason.empty() && unchanged(),"overlapping partitions reject atomically");
    set(offsets,0,1);set(counts,1,0);check(!read() && unchanged(),"uncovered bones reject atomically");
    set(counts,1,1);set(offsets,0,.5f);check(!read() && unchanged(),"fractional component offsets are not truncated");
    set(offsets,0,std::numeric_limits<float>::quiet_NaN());check(!read() && unchanged(),"nonfinite component offsets refuse");
    set(offsets,0,2);check(!read() && unchanged(),"component bounds cannot exceed the skeleton");
    set(offsets,0,1);remaps.expiration_timeout_frames=1;check(!read() && unchanged(),"expiring metadata pools require a lifetime adapter");
    remaps.expiration_timeout_frames=UINT32_MAX;
    check(!read(2,3,2) && unchanged(),"remap pool dimensions must cover every component and LOD");
    check(!read(2,UINT32_MAX,1) && unchanged(),"skeleton byte budget checks wide instance arithmetic");
    check(read() && unchanged(),"a refused metadata read does not damage a later valid read");
    printf("XXMI skeleton metadata: %d checks passed, %d failed\n",passed,failed);return failed;
}
