// Bridge frontend-owned file declarations into the existing real GPU cache.
#include "../frontend/assets/file_plan.h"
static void file_plan_tests(Gpu& g)
{
    const auto folder=std::filesystem::current_path()/L"file-plan-fixtures";
    std::filesystem::create_directories(folder);
    const auto vb=folder/L"mesh.buf",ib=folder/L"indices.buf",dds=folder/L"color.dds",bad=folder/L"partial.buf";
    write_fixture(vb,std::vector<uint8_t>(64,1));write_fixture(ib,{0,0,1,0,2,0});
    write_fixture(dds,fixture(99));write_fixture(bad,std::vector<uint8_t>(63,2));
    auto vertex=std::make_shared<FileResource>();vertex->path=vb;vertex->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;vertex->stride=16;
    auto index=std::make_shared<FileResource>();index->path=ib;index->usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;index->stride=2;index->index_type=VK_INDEX_TYPE_UINT16;
    auto image=std::make_shared<FileResource>();image->path=dds;
    FilePlan plan;plan.skip_original=true;
    auto bind=[&](FileOperation op,unsigned slot,std::shared_ptr<const FileResource> r){FilePlanItem i;i.operation=op;i.slot=slot;i.resource=std::move(r);plan.items.push_back(i);};
    bind(FileOperation::BindVertex,0,vertex);bind(FileOperation::BindIndex,0,index);bind(FileOperation::BindPixelResource,15,image);
    bind(FileOperation::BindPixelResource,18,{});bind(FileOperation::BindVertex,1,vertex);
    FilePlanItem native;native.operation=FileOperation::External;native.external=L"native/materials";native.phase_mask=1;plan.items.push_back(native);
    FilePlanItem draw;draw.operation=FileOperation::DrawIndexed;draw.index_count=3;plan.items.push_back(draw);
    ResourceBudgets budgets;ResourceCache cache(cache_device(g),budgets);PreparedFilePlan prepared;std::string reason;
    check(prepare_file_plan(plan,cache,prepared,reason) && !cache.stats().file_reads && prepared.requests().size()==7,
          "file plan setup declares resources without file IO or GPU demand");
    check(prepared.requests()[0]==prepared.requests()[4] && !prepared.requests()[3] && !prepared.requests()[5] &&
          prepared.plan().items[5].external==L"native/materials",
          "shared files reuse upload handles while explicit null and native boundary stay distinct");
    vertex->stride=32;vertex->path=bad;
    check(prepared.plan().items[0].resource->stride==16 && prepared.plan().items[0].resource->path==vb,
          "prepared plan freezes resource metadata independently of caller mutation");
    std::vector<ResourceSnapshot> ready(1);ready[0].reason="sentinel";
    check(request_file_plan(prepared,cache,ready,reason)==FileReadiness::Pending && ready.size()==1 && ready[0].reason=="sentinel",
          "partial readiness leaves complete caller output unchanged");
    for(const auto& request:prepared.requests())if(request)await_texture(cache,request,g,true);
    check(request_file_plan(prepared,cache,ready,reason)==FileReadiness::Ready && ready.size()==7 && ready[0].buffer && ready[1].buffer && ready[2].image &&
          ready[0].buffer==ready[4].buffer && !ready[3].image && !ready[3].buffer && prepared.plan().items[5].phase_mask==1,
          "real GPU readiness preserves typed owners and unresolved native operation");
    check(cache.stats().file_reads==3 && cache.stats().submissions==3,"repeated bindings trigger one upload per distinct file and usage");
    auto invalid=prepared.plan();invalid.items[1].resource=invalid.items[0].resource;
    check(!prepare_file_plan(invalid,cache,prepared,reason) && prepared.plan().items[1].resource->index_type==VK_INDEX_TYPE_UINT16,
          "later invalid binding preserves the previously prepared complete plan");
    ResourceCache other(cache_device(g),budgets);
    check(request_file_plan(prepared,other,ready,reason)==FileReadiness::Failed && ready.size()==7 && ready[0].buffer,
          "foreign generation rejects the complete owner collection");
    auto partial=std::make_shared<FileResource>(*prepared.plan().items[0].resource);partial->path=bad;
    invalid=prepared.plan();invalid.items[4].resource=partial;PreparedFilePlan refused;
    check(prepare_file_plan(invalid,cache,refused,reason),"partial vertex file declares without eager reading");
    request_file_plan(refused,cache,ready,reason);
    for(const auto& request:refused.requests())if(request)await_texture(cache,request,g,true);
    check(request_file_plan(refused,cache,ready,reason)==FileReadiness::Failed && ready.size()==7 && ready[0].buffer &&
          reason=="Ready resource does not satisfy its binding declaration",
          "nonintegral vertex stream refuses atomically after upload metadata is known");
    cache.stop();other.stop();
    check(ready[0].buffer->buffer() && ready[2].image->view() && budgets.resident->used(),
          "collected file-plan owners survive cache retirement for later command recording");
    ready.clear();check(!budgets.resident->used(),"last collected owner releases all file-plan residency");
}
