// CPU execution against parsed command trees, with no D3D or Vulkan device.
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

int HostPlanTests()
{
    int failed=0,passed=0;
    auto check=[&](bool ok,const char* text){
        if(ok){++passed;printf("PASS: %s\n",text);}
        else{++failed;printf("FAIL: %s\n",text);}
    };
    registered_command_lists.clear();
    HostParseExcerpt(L"namespace = planner\n[Constants]\nglobal $choice = 0\n"
                     L"[ResourceIndex]\ntype = Buffer\nformat = R32_UINT\ndata = 0 1 2 2 3 0\n"
                     L"[ResourceVertices]\ntype = Buffer\nstride = 16\n"
                     L"[ResourceImage]\n[CommandListNative]\n"
                     L"[CommandListDraw]\nhandling = skip\nrun = CommandListNative\n"
                     L"ib = ResourceIndex\nvb5 = ResourceVertices\n"
                     L"ps-t15 = ResourceImage\nps-t16 = null\n"
                     L"vb5->ElementFormat(BLENDINDICES, $choice) = R16G16B16A16_UINT\n"
                     L"if $choice == 0\ndrawindexed = 3, 0, -2\n"
                     L"else\ndrawindexedinstanced = 6, 2, 3, -1, 4\nendif\n"
                     L"[CommandListFail]\ndrawindexed = 3, 0, 0\ndispatch = 1, 1, 1\n"
                     L"[CommandListEmpty]\nhandling = skip\n"
                     L"[CommandListNegative]\ndrawindexed = -3, 0, 0\n"
                     L"[CommandListNonFinite]\ndrawindexed = 1 / 0, 0, 0\n"
                     L"[CommandListGPUQuery]\ndrawindexed = rt_width, 0, 0\n"
                     L"[CommandListStateQuery]\n$choice = rt_width\n"
                     L"[CommandListSlotQuery]\nps-t[rt_width] = ResourceImage\n"
                     L"[CommandListRecursion]\nrun = CommandListRecursion\n",
                     L"planner.ini");
    std::wstring budget=L"namespace = planner\n[CommandListBudget]\n";
    for(int i=0;i<257;++i)budget+=L"drawindexed = 3, 0, 0\n";
    HostParseExcerpt(budget.c_str(),L"planner.ini");
    HostEnumerateLists();HostParseConstants();HostParseResources();
    DrawPlanAdapter adapter;
    adapter.external(explicitCommandListSections.at(L"commandlist\\planner\\native"),L"test/native");
    const wchar_t* names[]={L"draw",L"fail",L"empty",L"negative",L"nonfinite",L"recursion",L"gpuquery",L"statequery",L"slotquery",L"budget"};
    for(auto* name:names)HostCompileNamedList((L"commandlist\\planner\\"+std::wstring(name)).c_str());
    optimise_command_lists(nullptr);
    for(auto* name:names)adapter.adapt(explicitCommandListSections.at(L"commandlist\\planner\\"+std::wstring(name)).command_list);
    auto run=[&](const wchar_t* name,DrawPlan& plan,std::wstring& reason){
        HackerContext context;CommandListState state;DrawCallInfo caller;
        state.mHackerContext=&context;state.call_info=&caller;
        auto& list=explicitCommandListSections.at(L"commandlist\\planner\\"+std::wstring(name)).command_list;
        const bool ok=adapter.execute(list,state,plan,reason);
        check(!caller.skip && state.input_layout_overrides.empty(),"planning leaves caller skip/layout state unchanged");
        return ok;
    };
    DrawPlan plan;std::wstring reason;
    check(run(L"draw",plan,reason),"upstream conditional tree emits a CPU draw plan");
    check(plan.skip_original && plan.items.size()==6 && plan.items[0].operation==PlanOperation::External &&
          plan.items[0].external==L"test/native" && plan.items[0].phase_mask!=0,
          "native boundary is explicit and survives optimisation");
    check(plan.items.size()==6 && plan.items[1].operation==PlanOperation::BindIndex &&
          plan.items[2].operation==PlanOperation::BindVertex && plan.items[2].slot==5 &&
          plan.items[3].resource && !plan.items[4].resource && plan.items[4].slot==16,
          "resource references and explicit null keep original binding order");
    check(!plan.items.empty() && plan.items.back().index_count==3 && plan.items.back().vertex_offset==-2 &&
          plan.items.back().layout.size()==1 && plan.items.back().layout[0].match.semantic_index==0,
          "indexed arguments and evaluated layout are copied into the plan");
    command_list_globals.at(L"$\\planner\\choice").fval=1;
    DrawPlan second;
    check(run(L"draw",second,reason) && second.items.back().index_count==6 && second.items.back().first_index==3 &&
          second.items.back().vertex_offset==-1 && second.items.back().instance_count==2 &&
          second.items.back().first_instance==4 && second.items.back().layout[0].match.semantic_index==1,
          "same compiled list reevaluates branches and instanced arguments");
    check(plan.items.back().layout[0].match.semantic_index==0,"later layout execution cannot mutate the earlier plan");
    const size_t retained=plan.items.size();
    for(auto* name:{L"fail",L"negative",L"nonfinite",L"recursion",L"gpuquery",L"statequery",L"slotquery",L"budget"}) {
        check(!run(name,plan,reason) && !reason.empty() && plan.items.size()==retained &&
              plan.items.back().vertex_offset==-2 && plan.items.back().layout[0].match.semantic_index==0,
              "late unsupported/invalid/recursive command rejects the complete output");
    }
    check(run(L"empty",plan,reason) && plan.skip_original && plan.items.empty(),"empty hidden component is a deliberate skip");
    CommandList alias;
    alias.SetSourceCommandList(&explicitCommandListSections.at(L"commandlist\\planner\\empty").command_list);
    HackerContext context;CommandListState state;state.mHackerContext=&context;
    check(adapter.execute(alias,state,plan,reason) && plan.skip_original && plan.items.empty(),
          "root aliases resolve to an already adapted command list");
    CommandList unadapted;
    alias.SetSourceCommandList(&unadapted);
    check(!adapter.execute(alias,state,plan,reason) && !reason.empty(),"aliases cannot enter unadapted commands");
    printf("XXMI draw planner: %d checks passed, %d failed\n",passed,failed);
    return failed;
}
