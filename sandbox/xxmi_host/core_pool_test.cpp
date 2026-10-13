// Exercise original pool/expression/callback semantics through command mediation.
#include "draw_plan.h"
#include "HackerContext.h"
#include "globals.h"
#include <cstdio>
void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostParseConstants();
void HostEnumerateLists();
void HostParseResources();
void HostCompileNamedList(const wchar_t*);
using namespace efmi::frontend;
int HostCorePoolTests()
{
    int passed=0,failed=0;
    auto check=[&](bool ok,const char* message){if(ok)++passed;else{++failed;printf("FAIL: %s\n",message);}};
    registered_command_lists.clear();
    HostParseExcerpt(LR"(
namespace = core_pools
[Constants]
global $index = 1
global $result = 0
[PoolValues]
pool_size = 3
[PoolAlias]
[PoolResources]
pool_size = 3
[PoolExpiring]
pool_size = 2
pool_index_type = fifo
pool_expiration_timeout_frames = 1
[ResourceOne]
type = Buffer
stride = 16
[ResourceTwo]
type = Buffer
stride = 8
[ResourceAlias]
[ResourceSnapshot]
[CommandListNative]
[CommandListCallback]
[CommandListBody]
drawindexed = $PoolAlias[$index], 0, 0
[CommandListInitialize]
run = CommandListNative
PoolAlias = ref PoolValues
$PoolValues[$index] = 9
$PoolValues[-1] = 7
$result = $PoolAlias[$index] + $PoolAlias[-1]
ResourceAlias = ref ResourceOne
ResourceSnapshot = ref ResourceAlias
ResourceAlias = ref ResourceTwo
PoolResources[$index] = ref ResourceSnapshot
vb0 = ref PoolResources[$index]
vb1 = ref ResourceAlias
PoolResources[2] = null
vb2 = ref PoolResources[2]
CommandListCallback = ref CommandListBody
run = CommandListCallback
CommandListCallback = null
[CommandListAgain]
run = CommandListCallback
drawindexed = $PoolValues[-1], 0, 0
[CommandListBadIndex]
$index = 0 / 0
$PoolValues[$index] = 2
[CommandListBadRead]
$index = 1 / 0
drawindexed = $PoolValues[$index], 0, 0
[CommandListBadNested]
$result = $PoolValues[@vs-t0]
[CommandListBadCopy]
ResourceAlias = copy ResourceOne
[CommandListBadPoolCycle]
PoolAlias = ref PoolValues
PoolValues = ref PoolAlias
[CommandListBadCallbackCycle]
CommandListCallback = ref CommandListBody
CommandListBody = ref CommandListCallback
[CommandListBadExpiring]
PoolExpiring[$index] = ref ResourceOne
)",L"core-pools-test.ini");
    HostEnumerateLists();HostParseConstants();HostParseResources();
    const std::wstring prefix=L"commandlist\\core_pools\\";
    DrawPlanAdapter adapter;adapter.external(explicitCommandListSections.at(prefix+L"native"),L"core/init");
    for(auto& pair:explicitCommandListSections)if(pair.first.compare(0,prefix.size(),prefix)==0)HostCompileNamedList(pair.first.c_str());
    optimise_command_lists(nullptr);
    for(auto& pair:explicitCommandListSections)if(pair.first.compare(0,prefix.size(),prefix)==0)adapter.adapt(pair.second.command_list);
    auto run=[&](const wchar_t* name,DrawPlan& plan,std::wstring& reason){
        HackerContext context;CommandListState state;state.mHackerContext=&context;
        return adapter.execute(explicitCommandListSections.at(prefix+name).command_list,state,plan,reason);
    };
    DrawPlan plan;std::wstring reason;
    bool ok=run(L"initialize",plan,reason);
    if(!ok)fwprintf(stderr,L"Core pool test refused: %ls\n",reason.c_str());
    check(ok && command_list_globals.at(L"$\\core_pools\\result").fval==16,"original pool aliases, writes and wrapped reads execute");
    check(ok && plan.items.size()==5 && plan.items[0].operation==PlanOperation::External && plan.items.back().index_count==9,
          "runtime callback executes the adapted source while native initialization remains explicit");
    if(ok && plan.items.size()==5){
        check(plan.items[1].resource==&customResources.at(L"resource\\core_pools\\one") &&
              plan.items[2].resource==&customResources.at(L"resource\\core_pools\\two") && !plan.items[3].resource,
              "resource and pool references snapshot the selected declaration, including null");
    }else check(false,"resource reference snapshots available");
    check(run(L"again",plan,reason) && plan.items.size()==1 && plan.items[0].index_count==7,
          "cleared callback returns to its empty declaration and pools persist across transactions");
    for(auto* name:{L"badindex",L"badread",L"badnested",L"badcopy",L"badpoolcycle",L"badcallbackcycle",L"badexpiring"}){
        command_list_globals.at(L"$\\core_pools\\index").fval=1;
        check(!run(name,plan,reason) && !reason.empty() && plan.items.size()==1 && plan.items[0].index_count==7,
              "unsupported pool/resource/callback operations preserve the previous output");
    }
    printf("XXMI Core pools: %d checks passed, %d failed\n",passed,failed);return failed;
}
