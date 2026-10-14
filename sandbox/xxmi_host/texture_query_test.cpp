// Actual original hash matching and callback bodies with explicit source
// identities. No D3D context query or assumptions about Vulkan hashes.
#include "draw_plan.h"
#include "globals.h"
#include "ResourceHash.h"
#include "HackerContext.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
void HostParseExcerpt(const wchar_t*,const wchar_t*);
void HostParseConstants();void HostEnumerateLists();void HostParseTextureOverrides();
void HostCompileNamedList(const wchar_t*);
void HostParseIniFile(const std::filesystem::path&);
size_t HostDiagnosticCount();
using namespace efmi::frontend;
int HostTextureQueryTests(const wchar_t* api)
{
    int passed=0,failed=0;auto check=[&](bool ok,const char* why){if(ok)++passed;else{++failed;printf("FAIL: %s\n",why);}};
    const auto diagnostics=HostDiagnosticCount();
    HostParseExcerpt(LR"(
namespace = texture_bridge
[Constants]
global $order = 0
global $filter = 0
global $slot = 0
[CommandListBoth]
$order = 0
$filter = ps-t0
pre CheckTextureOverride = ps-t0
drawindexed = 3, $order, 0
[CommandListSingle]
$order = 0
CheckTextureOverride = ps-t0
drawindexed = 3, $order, 0
[CommandListUnknown]
drawindexed = 3, 100, 0
CheckTextureOverride = ps-t3
[CommandListNull]
ps-t0 = null
$filter = ps-t0
CheckTextureOverride = ps-t0
[CommandListReplacement]
ps-t0 = reference ResourceOwn
CheckTextureOverride = ps-t0
[CommandListDynamic]
$order = 0
pre CheckTextureOverride = ps-t[$slot]
[CommandListRange]
CheckTextureOverride = ps-t[6:8]
[CommandListNested]
CheckTextureOverride = ps-t1
[CommandListCycle]
CheckTextureOverride = ps-t5
[CommandListUnadapted]
CheckTextureOverride = ps-t9
[CommandListBadThis]
this = reference ResourceOwn
[TextureOverrideA]
hash = 11223344
match_priority = 1
match_index_count = 7
filter_index = 5
$order = $order * 10 + 1
this = reference ResourceOwn
post $order = $order * 10 + 3
[TextureOverrideB]
hash = 11223344
match_priority = 2
match_index_count = 7
filter_index = 9
$order = $order * 10 + 2
post $order = $order * 10 + 4
post this = null
[TextureOverrideNested]
hash = aabbccdd
CheckTextureOverride = ps-t2
this = reference ResourceOwn
[TextureOverrideInner]
hash = aabbccde
this = null
[TextureOverrideCycle]
hash = 99990000
CheckTextureOverride = ps-t5
[TextureOverrideUnadapted]
hash = deadbeef
$order = 999
)",L"texture-query.ini");
    if(api)HostParseIniFile(std::filesystem::absolute(api));
    // A generation-owned declaration; parsing its filename is unrelated to
    // identity mediation and is already covered by resource-plan tests.
    auto& own=customResources[L"resource\\texture_bridge\\own"];own.name=L"resource\\texture_bridge\\own";
    own.filename=L"identity-fixture.dds";
    HostEnumerateLists();HostParseConstants();HostParseTextureOverrides();
    const std::wstring ns=L"commandlist\\texture_bridge\\";
    const wchar_t* names[]={L"both",L"single",L"unknown",L"null",L"replacement",L"dynamic",L"range",L"nested",L"cycle",L"unadapted",L"badthis"};
    for(auto name:names)HostCompileNamedList((ns+name).c_str());
    auto& single=explicitCommandListSections.at(ns+L"single").command_list;
    for(auto& command:single.commands)if(auto* c=dynamic_cast<CheckTextureOverrideCommand*>(command.get()))c->run_pre_and_post_together=false;
    DrawPlanAdapter adapter;unsigned queries=0,custom_queries=0;bool unknown=false,core_mode=false;
    adapter.texture_queries([&](unsigned slot,const DrawPlanAdapter::PixelBinding& binding,CommandListState&,DrawPlanAdapter::TextureIdentity& id,std::wstring& why){
        ++queries;if(!binding.application){++custom_queries;if(binding.resource!=&own){why=L"Unprepared custom texture";return false;}id={true,0x12345678};return true;}
        if((slot==3 && !core_mode) || unknown){why=L"Source texture identity unavailable";return false;}
        id.bound=true;
        if(core_mode && slot!=0){id.hash=0x24681357;return true;}
        switch(slot){case 0:id.hash=0x11223344;break;case 1:id.hash=0xaabbccdd;break;case 2:id.hash=0xaabbccde;break;
            case 5:id.hash=0x99990000;break;case 9:id.hash=0xdeadbeef;break;case 6:id.bound=false;break;default:id.hash=0x24681357;break;}
        return true;
    });
    const std::wstring core_name=L"commandlist\\efmiv1\\overridetextures";
    if(api)HostCompileNamedList(core_name.c_str());
    optimise_command_lists(nullptr);
    for(auto name:names){auto& s=explicitCommandListSections.at(ns+name);adapter.adapt(s.command_list);adapter.adapt(s.post_command_list);}
    for(auto& hash:G->mTextureOverrideMap)for(auto& item:hash.second)if(hash.first!=0xdeadbeef){adapter.adapt(item.command_list);adapter.adapt(item.post_command_list);}
    if(api){auto& lists=explicitCommandListSections.at(core_name);adapter.adapt(lists.command_list);adapter.adapt(lists.post_command_list);}
    check(HostDiagnosticCount()==diagnostics,"texture override fixture compiles without parser diagnostics");
    HackerContext context;DrawCallInfo call(DrawCall::DrawIndexed,0,7,1,0,0,0,nullptr,0);
    ResourceCopyTarget outer;outer.type=ResourceCopyTargetType::SHADER_RESOURCE;outer.shader_type=L'p';outer.slot=127;
    DrawPlan result;std::wstring why;bool restored=true;
    auto run=[&](const wchar_t* name,bool post=false){CommandListState state;state.mHackerContext=&context;state.call_info=&call;state.this_target=&outer;state.post=post;
        const auto ok=adapter.execute(explicitCommandListSections.at(ns+name).command_list,state,result,why);
        restored=state.this_target==&outer && state.post==post;return ok;};
    auto value=[&](const wchar_t* name)->float&{return command_list_globals.at(L"$\\texture_bridge\\"+std::wstring(name)).fval;};
    check(run(L"both") && result.items.size()==3 && result.items.back().first_index==1234 && value(L"filter")==9,
          "original priority/hash/draw matching runs all pre lists before all post lists");
    check(restored && result.items[0].operation==PlanOperation::BindPixelResource && result.items[0].slot==0 && result.items[0].resource==&own && !result.items[1].resource,
          "this binding targets the queried slot and restores outer target/phase");
    check(run(L"single") && result.items.back().first_index==12 && restored,"phase-local texture query executes pre only");
    check(run(L"single",true) && result.items.back().first_index==34 && restored,"phase-local texture query executes post only");
    call.IndexCount=8;check(run(L"both") && result.items.size()==1 && result.items[0].first_index==0 && value(L"filter")==0,
                          "original match_index_count excludes hash matches for another draw");call.IndexCount=7;
    const auto before_null=queries;
    check(run(L"null") && result.items.size()==1 && queries==before_null && value(L"filter")==0 && std::signbit(value(L"filter")),
          "explicit null bypasses identity lookup and preserves original negative-zero filter semantics");
    check(run(L"replacement") && custom_queries==1 && result.items.size()==1,"later query observes the prepared custom declaration instead of the original image");
    value(L"slot")=0;check(run(L"dynamic") && value(L"order")==1234,"original dynamic slot expression selects the current resource");
    value(L"slot")=6;check(run(L"dynamic") && result.items.empty(),"known unbound application slot has no overrides");
    value(L"slot")=7;check(run(L"dynamic") && result.items.empty(),"known bound resource with no original hash match is a successful empty result");
    const auto before_range=queries;check(run(L"range") && queries-before_range==3 && result.items.empty(),"original inclusive slot range evaluates each selected slot once");
    check(run(L"nested") && result.items.size()==2 && result.items[0].slot==2 && !result.items[0].resource && result.items[1].slot==1 && result.items[1].resource==&own && restored,
          "nested texture callback restores this for the enclosing original callback");
    auto saved=result.items[1].resource;
    check(!run(L"unknown") && result.items.size()==2 && result.items[1].resource==saved && restored && why==L"Source texture identity unavailable",
          "late unknown identity rejects the entire candidate without losing previous output");
    unknown=true;check(!run(L"both") && result.items.size()==2 && restored,"unknown filter query cannot fall through to D3D scaffolding");unknown=false;
    value(L"slot")=128;check(!run(L"dynamic") && result.items.size()==2 && restored,"out-of-range dynamic slot rejects atomically");
    check(!run(L"unadapted") && why==L"Matched texture command lists were not adapted" && restored,"unadapted matching callback cannot reach a graphics stub");
    check(!run(L"cycle") && restored && why==L"Native command stack budget exceeded","recursive texture callbacks stop at the bounded native stack");
    // Outside a query, a caller-provided valid this target has ordinary binding semantics.
    check(run(L"badthis") && result.items.size()==1 && result.items[0].slot==127,"valid caller this target resolves without a D3D resource");
    CommandListState no_this;no_this.mHackerContext=&context;no_this.call_info=&call;
    check(!adapter.execute(explicitCommandListSections.at(ns+L"badthis").command_list,no_this,result,why) && result.items.size()==1,
          "missing this binding refuses without publishing partial output");
    auto fuzzy=std::make_shared<FuzzyMatchResourceDesc>(L"fixture/fuzzy");G->mFuzzyTextureOverrides.insert(fuzzy);
    check(!run(L"both") && result.items.size()==1 && why==L"Texture descriptor fuzzy matching has no adapter",
          "descriptor-only fuzzy rules cannot be silently omitted by a hash-only adapter");
    G->mFuzzyTextureOverrides.erase(fuzzy);
    if(api){
        core_mode=true;value(L"order")=0;const auto before=queries,custom_before=custom_queries;
        CommandListState state;state.mHackerContext=&context;state.call_info=&call;auto& core=explicitCommandListSections.at(core_name);
        check(adapter.execute_pair(core.command_list,core.post_command_list,state,result,why) && queries-before==46 &&
              custom_queries-custom_before==1 && result.items.size()==1 && result.items[0].slot==0 && value(L"order")==12,
              "original Core checks 23 slots in each phase and post lookup sees the pre replacement");
    }
    printf("XXMI texture queries: %d checks passed, %d failed\n",passed,failed);return failed;
}
