#include "resource_plan.h"
#include <cstdio>

using namespace efmi::frontend;
namespace assets=efmi::assets;
int HostResourcePlanTests()
{
    int passed=0,failed=0;
    auto check=[&](bool ok,const char* text){if(ok)++passed;else{++failed;printf("FAIL: %s\n",text);}};
    CustomResources resources;
    auto define=[&](const wchar_t* name,const wchar_t* file,CustomResourceType type)->CustomResource*{
        auto& r=resources[name];r.name=name;r.filename=(std::filesystem::current_path()/file).wstring();
        r.override_type=type;r.override_color_space=CustomColorSpace::DEFAULT;return &r;
    };
    auto* vertex=define(L"vertex",L"nonexistent-owned.buf",CustomResourceType::BUFFER);vertex->override_stride=20;
    auto* index=define(L"index",L"nonexistent-index.buf",CustomResourceType::BUFFER);index->override_format=DXGI_FORMAT_R16_UINT;
    auto* image=define(L"image",L"nonexistent-image.dds",CustomResourceType::INVALID);
    auto* unused=define(L"unused",L"missing.png",CustomResourceType::TEXTURE3D);
    auto* changed=define(L"transformed",L"changed.buf",CustomResourceType::BUFFER);changed->override_byte_width=128;
    auto* pool=define(L"pool",L"pool.buf",CustomResourceType::BUFFER);pool->pool_index=0;
    auto* relative=define(L"relative",L"relative.buf",CustomResourceType::BUFFER);relative->filename=L"relative.buf";
    auto* srgb=define(L"srgb",L"color.dds",CustomResourceType::INVALID);srgb->override_color_space=CustomColorSpace::SRGB;
    FilePlanCompiler compiler(resources);
    DrawPlan plan;plan.skip_original=true;
    auto bind=[&](PlanOperation op,unsigned slot,CustomResource* r){PlanItem item;item.operation=op;item.slot=slot;item.resource=r;plan.items.push_back(item);};
    PlanItem external;external.operation=PlanOperation::External;external.external=L"native/materials";external.phase_mask=3;plan.items.push_back(external);
    bind(PlanOperation::BindVertex,5,vertex);bind(PlanOperation::BindIndex,0,index);
    bind(PlanOperation::BindPixelResource,17,image);bind(PlanOperation::BindPixelResource,18,nullptr);
    PlanItem draw;draw.operation=PlanOperation::DrawIndexed;draw.index_count=6;draw.first_index=4;draw.vertex_offset=-2;
    draw.instance_count=2;draw.first_instance=7;
    InputLayoutElementOverride edit;edit.match.semantic_name="BLENDINDICES";edit.match.semantic_index=0;edit.match.input_slot=5;
    edit.replace.format=DXGI_FORMAT_R16G16B16A16_UINT;draw.layout.push_back(edit);plan.items.push_back(draw);
    assets::FilePlan owned;std::wstring reason;
    check(compiler.compile(plan,owned,reason),"file declarations compile without opening nonexistent files");
    check(owned.skip_original && owned.items.size()==6 && owned.items[0].external==L"native/materials" && owned.items[0].phase_mask==3,
          "native boundary and original skip survive resource ownership conversion");
    check(owned.items[1].resource && owned.items[1].resource->stride==20 && owned.items[1].resource->usage==VK_BUFFER_USAGE_VERTEX_BUFFER_BIT &&
          owned.items[2].resource && owned.items[2].resource->index_type==VK_INDEX_TYPE_UINT16 && owned.items[2].resource->stride==2,
          "vertex byte stride and index element format are independent owned contracts");
    check(owned.items[3].resource && !owned.items[3].resource->usage && !owned.items[4].resource && image->is_null,
          "unloaded file declarations are distinct from explicit null bindings");
    check(owned.items[5].index_count==6 && owned.items[5].first_index==4 && owned.items[5].vertex_offset==-2 &&
          owned.items[5].instance_count==2 && owned.items[5].first_instance==7 && owned.items[5].layout[0].replace.format==DXGI_FORMAT_R16G16B16A16_UINT,
          "all indexed arguments and layout edits retain execution order and values");
    const auto previous=owned.items[1].resource;
    vertex->filename=L"Z:/edited.buf";vertex->override_stride=4;
    check(compiler.compile(plan,owned,reason) && owned.items[1].resource==previous && owned.items[1].resource->stride==20,
          "catalog snapshot is immutable across later frontend declaration edits");
    auto bad=plan;
    for(auto* resource:{unused,changed,pool,relative,srgb}){
        bad.items.back()={};bad.items.back().operation=PlanOperation::BindPixelResource;bad.items.back().resource=resource;
        check(!compiler.compile(bad,owned,reason) && !reason.empty() && owned.items.size()==6 && owned.items[1].resource==previous,
              "late unsupported used resource refuses atomically while dormant declarations are harmless");
    }
    CustomResource foreign;bad=plan;bad.items[1].resource=&foreign;
    check(!compiler.compile(bad,owned,reason),"foreign frontend resource is refused without dereferencing it");
    bad=plan;bad.items[1].slot=32;check(!compiler.compile(bad,owned,reason),"out of range owned binding refuses");
    bad=plan;bad.items[0].phase_mask=0;check(!compiler.compile(bad,owned,reason),"invalid native phase cannot vanish into a no-op");
    bad=plan;bad.items[2].operation=PlanOperation::BindVertex;check(!compiler.compile(bad,owned,reason),"index format cannot be guessed as a vertex stride");
    bad=plan;bad.items[2].slot=1;check(!compiler.compile(bad,owned,reason),"index slot is bounded independently of vertex slots");
    bad=plan;bad.items.back().instance_count=0;check(!compiler.compile(bad,owned,reason),"malformed zero draw refuses before resource preparation");
    resources.clear();plan.items.clear();
    check(owned.items[1].resource==previous && previous->stride==20 && previous->path.filename()==L"nonexistent-owned.buf" &&
          owned.items.back().layout[0].match.semantic_name=="BLENDINDICES",
          "owned file plan and layout survive destruction of the entire frontend declaration map");
    check(compiler.compile({},owned,reason) && owned.items.empty(),"empty plan remains a valid empty resource transaction");
    printf("XXMI resource planner: %d checks passed, %d failed\n",passed,failed);return failed;
}
