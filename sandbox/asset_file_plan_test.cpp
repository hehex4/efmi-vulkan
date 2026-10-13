// Bridge frontend-owned file declarations into the existing real GPU cache.
#include "../frontend/assets/file_plan.h"
#include "../frontend/assets/file_draws.h"
#include "../frontend/core/palette_bank.h"
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
static void file_draw_tests(Gpu& g)
{
    const auto folder=std::filesystem::current_path()/L"file-plan-fixtures";
    auto vertex=std::make_shared<FileResource>();vertex->path=folder/L"mesh.buf";vertex->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;vertex->stride=16;
    auto alternate=std::make_shared<FileResource>(*vertex);alternate->stride=8;
    auto index=std::make_shared<FileResource>();index->path=folder/L"indices.buf";index->usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    index->stride=2;index->index_type=VK_INDEX_TYPE_UINT16;
    auto image=std::make_shared<FileResource>();image->path=folder/L"color.dds";
    FilePlan plan;plan.skip_original=true;
    auto bind=[&](FileOperation op,unsigned slot,std::shared_ptr<const FileResource> r){FilePlanItem i;i.operation=op;i.slot=slot;i.resource=std::move(r);plan.items.push_back(i);};
    bind(FileOperation::BindVertex,3,vertex);bind(FileOperation::BindIndex,0,index);bind(FileOperation::BindPixelResource,12,image);
    FilePlanItem draw;draw.operation=FileOperation::DrawIndexed;draw.index_count=3;draw.instance_count=2;draw.first_instance=4;
    auto first_palette=std::make_shared<efmi::core::PaletteSnapshot>();first_palette->identity=71;
    draw.palette=first_palette;
    LayoutEdit edit;edit.match.semantic_name="BLENDINDICES";edit.match.input_slot=3;edit.replace.format=12;draw.layout.push_back(edit);
    plan.items.push_back(draw);bind(FileOperation::BindPixelResource,12,{});bind(FileOperation::BindVertex,3,alternate);
    draw.index_count=2;draw.first_index=1;draw.vertex_offset=1;draw.layout[0].replace.format=13;draw.palette.reset();plan.items.push_back(draw);
    ResourceBudgets budgets;ResourceCache cache(cache_device(g),budgets);PreparedFilePlan prepared;std::string reason;
    FileDraws result;result.draws.emplace_back();result.draws[0].index_count=777;
    check(prepare_file_plan(plan,cache,prepared,reason) && request_file_draws(prepared,cache,result,reason)==FileReadiness::Pending &&
          result.draws.size()==1 && result.draws[0].index_count==777,"pending draw transaction preserves previous complete output");
    for(const auto& request:prepared.requests())if(request)await_texture(cache,request,g,true);
    check(request_file_draws(prepared,cache,result,reason)==FileReadiness::Ready && result.skip_original && result.draws.size()==2,
          "ready file bindings produce one snapshot per indexed draw");
    const auto& a=result.draws[0];const auto& b=result.draws[1];
    check(a.palette==first_palette && !b.palette,"per-draw native palette ownership survives file preparation without leaking to the next draw");
    check(a.vertex[3].buffer==b.vertex[3].buffer && a.vertex[3].resource->stride==16 && b.vertex[3].resource->stride==8 &&
          a.pixel[12].source==BindingSource::File && a.pixel[12].image && b.pixel[12].source==BindingSource::Null &&
          b.pixel[13].source==BindingSource::Application && a.vertex[4].source==BindingSource::Application,
          "later material and declaration changes leave prior draw state intact and preserve untouched application slots");
    check(a.index_count==3 && b.index_count==2 && b.first_index==1 && b.vertex_offset==1 && b.instance_count==2 && b.first_instance==4 &&
          a.layout[0].replace.format==12 && b.layout[0].replace.format==13,
          "binding snapshots retain signed base, nonzero first index, instancing and per-draw layout edits");
    const auto retained=a.vertex[3].buffer;
    for(unsigned failure=0;failure<4;++failure){
        auto invalid=plan;
        if(failure==0)invalid.items.back().first_index=2;
        if(failure==1)invalid.items.back().vertex_offset=9;
        if(failure==2)invalid.items.back().vertex_offset=-1;
        if(failure==3){FilePlanItem clear;clear.operation=FileOperation::BindIndex;invalid.items.insert(invalid.items.end()-1,clear);}
        PreparedFilePlan bad;
        check(prepare_file_plan(invalid,cache,bad,reason) && request_file_draws(bad,cache,result,reason)==FileReadiness::Failed &&
              !reason.empty() && result.draws.size()==2 && result.draws[0].vertex[3].buffer==retained && result.draws[1].vertex_offset==1,
              "late index or vertex bound failure rejects the complete draw transaction");
    }
    auto unresolved=plan;FilePlanItem native;native.operation=FileOperation::External;native.external=L"native/unresolved";native.phase_mask=1;
    unresolved.items.push_back(native);PreparedFilePlan native_plan;const auto reads=cache.stats().file_reads;
    check(prepare_file_plan(unresolved,cache,native_plan,reason) && request_file_draws(native_plan,cache,result,reason)==FileReadiness::Failed &&
          reason=="Native Core operation must resolve before file draws" && cache.stats().file_reads==reads && result.draws.size()==2,
          "ready assets never allow an unresolved native boundary to execute as a no-op");
    cache.stop();
    check(result.draws[0].pixel[12].image->view() && result.draws[1].index.buffer->buffer(),"per-draw owners survive resource cache retirement");
    ResourceCache fresh(cache_device(g),budgets);FilePlan hidden;hidden.skip_original=true;PreparedFilePlan hidden_plan;
    check(prepare_file_plan(hidden,fresh,hidden_plan,reason) && request_file_draws(hidden_plan,fresh,result,reason)==FileReadiness::Ready &&
          result.skip_original && result.draws.empty(),"intentional hidden geometry needs no resource or index binding");
}
