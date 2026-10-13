// Combined frontend-to-backend GPU handoff. The synthetic shader deliberately
// reads original buffers in FS and private palette buffers in VS.
#include "../frontend/gpu/draw_transaction.h"
#include "variant_fixture.h"

namespace transaction_fixture {
using namespace efmi::gpu;
using namespace efvk;
struct Objects {
    VkDevice device;
    VkRenderPass pass{};VkFramebuffer framebuffer{};VkPipeline pipeline{};VkPipelineLayout layout{};
    VkDescriptorPool pool{};std::vector<VkDescriptorSetLayout> sets;std::vector<VkImageView> views;
    ~Objects(){
        if(framebuffer)pDestroyFramebuffer(device,framebuffer,nullptr);
        if(pipeline)pDestroyPipeline(device,pipeline,nullptr);
        if(layout)pDestroyPipelineLayout(device,layout,nullptr);
        if(pool)pDestroyDescriptorPool(device,pool,nullptr);
        if(pass)pDestroyRenderPass(device,pass,nullptr);
        for(auto s:sets)pDestroyDescriptorSetLayout(device,s,nullptr);
        for(auto v:views)pDestroyImageView(device,v,nullptr);
    }
    VkImage target(Gpu& g,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect){
        g.images.emplace_back();auto& image=g.images.back();
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;
        ci.extent={64,64,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=usage;
        CHECK_VK(pCreateImage(device,&ci,nullptr,&image.image));VkMemoryRequirements requirements{};
        pGetImageMemoryRequirements(device,image.image,&requirements);image.memory=g.allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK_VK(pBindImageMemory(device,image.image,image.memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=format;vi.subresourceRange={aspect,0,1,0,1};VkImageView view{};
        CHECK_VK(pCreateImageView(device,&vi,nullptr,&view));views.push_back(view);return image.image;
    }
};
template<class T,size_t N> std::vector<uint8_t> bytes(const T(&array)[N]){
    const auto p=reinterpret_cast<const uint8_t*>(array);return {p,p+sizeof(array)};
}
static void run(Gpu& g)
{
    VariantDispatch dispatch;SnapshotDispatch snapshot_dispatch;
#define X(n) dispatch.n=p##n;
    EFVK_VARIANT_FUNCS(X)
#undef X
#define X(n) snapshot_dispatch.n=p##n;
    EFVK_SNAPSHOT_FUNCS(X)
#undef X
    VkPhysicalDeviceProperties properties{};pGetPhysicalDeviceProperties(g.physical,&properties);
    Objects objects{g.device};variant_fixture::RecipeFixture fixture_recipe(true);fixture_recipe.ba.blendEnable=VK_FALSE;
    auto recipe=std::make_shared<GraphicsRecipe>(fixture_recipe.capture());
    auto layout=std::make_shared<PipelineLayoutInfo>(fixture_recipe.layout);
    CHECK_VK(recipe->render_pass->create(g.device,pCreateRenderPass,pCreateRenderPass2,&objects.pass));
    for(const auto& set:layout->sets){
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(const auto& pair:set->bindings)bindings.push_back({pair.first,static_cast<VkDescriptorType>(pair.second.type),pair.second.count,pair.second.stages,nullptr});
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=static_cast<uint32_t>(bindings.size());ci.pBindings=bindings.data();
        VkDescriptorSetLayout handle{};CHECK_VK(pCreateDescriptorSetLayout(g.device,&ci,nullptr,&handle));objects.sets.push_back(handle);
    }
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=3;pl.pSetLayouts=objects.sets.data();
    CHECK_VK(pCreatePipelineLayout(g.device,&pl,nullptr,&objects.layout));std::vector<VkShaderModule> modules;
    for(const auto& stage:recipe->stages){VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize=stage.shader->code.size()*4;ci.pCode=stage.shader->code.data();VkShaderModule module{};
        CHECK_VK(pCreateShaderModule(g.device,&ci,nullptr,&module));modules.push_back(module);}
    const auto created=recipe->create(g.device,pCreateGraphicsPipelines,objects.layout,objects.pass,modules,&objects.pipeline);
    for(auto module:modules)pDestroyShaderModule(g.device,module,nullptr);CHECK_VK(created);
    auto color=objects.target(g,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    objects.target(g,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=objects.pass;
    fb.attachmentCount=2;fb.pAttachments=objects.views.data();fb.width=fb.height=64;fb.layers=1;
    CHECK_VK(pCreateFramebuffer(g.device,&fb,nullptr,&objects.framebuffer));

    struct OriginalVertex{float xy[2];uint8_t blend[4];};struct ModVertex{float xy[2];uint16_t blend[4];};
    const OriginalVertex ov[]={{{-.22f,-.2f},{1,0,0,0}},{{.22f,-.2f},{1,0,0,0}},{{0,.22f},{1,0,0,0}}};
    const ModVertex mv[]={{{-.22f,-.2f},{257,0,0,0}},{{.22f,-.2f},{257,0,0,0}},{{0,.22f},{257,0,0,0}},
                          {{-.22f,-.2f},{257,0,0,0}},{{.22f,-.2f},{257,0,0,0}},{{0,.22f},{257,0,0,0}}};
    const uint16_t oi[]={0,1,2};const uint32_t mi[]={0,1,2,0,1,2};
    auto original_vb=g.buffer(sizeof(ov),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(original_vb.mapped,ov,sizeof(ov));
    auto original_ib=g.buffer(sizeof(oi)+16,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);memcpy(static_cast<uint8_t*>(original_ib.mapped)+16,oi,sizeof(oi));
    DrawGeometry original{0,{original_vb.buffer},{0},original_ib.buffer,16,VK_INDEX_TYPE_UINT16};
    auto bones=g.buffer(259*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);const float left=-.5f;const float white[]={1,1,1,1};
    memcpy(static_cast<uint8_t*>(bones.mapped)+16,&left,4);memcpy(static_cast<uint8_t*>(bones.mapped)+258*16,white,16);
    const auto dynamic_offset=std::max(32u,static_cast<uint32_t>(properties.limits.minUniformBufferOffsetAlignment));
    auto entry=g.buffer(dynamic_offset+32,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const float app_entry[]={0,-.35f,0,0,1,1,1,1};memcpy(static_cast<uint8_t*>(entry.mapped)+dynamic_offset,app_entry,32);
    static_cast<float*>(entry.mapped)[0]=10; // Wrong restoration of dynamic offset is visible.

    ResourceBudgets budgets;FileDraws files;std::shared_ptr<const ImageUpload> blue;
    {
        const auto folder=std::filesystem::current_path()/L"transaction-fixtures";std::filesystem::create_directories(folder);
        write_fixture(folder/L"mesh.buf",bytes(mv));write_fixture(folder/L"index.buf",bytes(mi));
        FilePlan plan;plan.skip_original=true;
        auto bind=[&](FileOperation op,uint32_t slot,std::shared_ptr<const FileResource> resource){
            FilePlanItem item;item.operation=op;item.slot=slot;item.resource=std::move(resource);plan.items.push_back(std::move(item));};
        for(unsigned i=0;i<2;++i){auto r=std::make_shared<FileResource>();r->path=folder/(i?L"index.buf":L"mesh.buf");
            r->stride=i?4:16;r->usage=i?VK_BUFFER_USAGE_INDEX_BUFFER_BIT:VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if(i)r->index_type=VK_INDEX_TYPE_UINT32;bind(i?FileOperation::BindIndex:FileOperation::BindVertex,0,r);}
        auto make_image=[&](unsigned channel){auto data=fixture(28);data.resize(152);put(data,12,1);put(data,16,1);put(data,28,1);
            data[148]=data[149]=data[150]=0;data[148+channel]=255;data[151]=255;
            auto r=std::make_shared<FileResource>();r->path=folder/(std::to_wstring(channel)+L".dds");write_fixture(r->path,data);return r;};
        auto blue_file=make_image(2);bind(FileOperation::BindPixelResource,22,blue_file);
        for(unsigned i=0;i<3;++i){
            bind(FileOperation::BindPixelResource,15,i==2?nullptr:make_image(i));
            auto palette=std::make_shared<efmi::core::PaletteSnapshot>();palette->identity=17;palette->bones.resize(259*16);
            const float move=.5f;memcpy(palette->bones.data()+257*16,&move,4);
            const float private_tint[]={1,0,1,1};memcpy(palette->bones.data()+258*16,private_tint,16);
            const float own_entry[]={i==0?0.f:i==1?-1.f:-.5f,i==2?-.7f:.35f,0,0,1,0,1,1};memcpy(palette->entry.data(),own_entry,32);
            FilePlanItem draw;draw.operation=FileOperation::DrawIndexed;draw.index_count=3;draw.palette=palette;
            if(i==1){draw.first_index=3;draw.vertex_offset=3;}plan.items.push_back(std::move(draw));
        }
        ResourceCache cache(cache_device(g),budgets);PreparedFilePlan prepared;std::string why;
        if(!prepare_file_plan(plan,cache,prepared,why))throw std::runtime_error(why);
        request_file_draws(prepared,cache,files,why);
        for(const auto& request:prepared.requests())if(request)await_texture(cache,request,g,true);
        if(request_file_draws(prepared,cache,files,why)!=FileReadiness::Ready)throw std::runtime_error(why);
        blue=files.draws[0].pixel[22].image;
        // This extra binding supplies only the application fixture; it is not a mod edit.
        for(auto& draw:files.draws)draw.pixel[22]={};cache.stop();
    }
    std::string reason;MaterialProfile material_profile;
    const auto& fragment=recipe->stages[1].shader->code;
    check(material_profile.prepare(fragment,{{15,MaterialSlotKind::Image,{1,6}}},reason),"transaction exact fragment profile prepared");
    MaterialPlan materials;check(resolve_material_plan(files,fragment,&material_profile,materials,reason),"transaction resolves distinct images and explicit null");
    const std::vector<VertexInputRecipe> vertices(3,variant_fixture::RecipeFixture::mod_vertex());DrawTransaction plan;
    if(!prepare_draw_transaction(files,vertices,materials,original,plan,reason))throw std::runtime_error(reason);
    check(plan.skip_original && plan.draws.size()==3 && plan.draws[1].draw.first==3 && plan.draws[1].draw.vertex_offset==3,
          "transaction owns ordered indexed ranges and private palettes");
    auto refuse=[&](const FileDraws& f,const std::vector<VertexInputRecipe>& v,const MaterialPlan& m,const DrawGeometry& o,const char* label){
        check(!prepare_draw_transaction(f,v,m,o,plan,reason) && plan.draws.size()==3 && plan.draws[1].draw.first==3 && !reason.empty(),label);};
    auto bad=files;bad.draws.back().palette.reset();refuse(bad,vertices,materials,original,"late missing palette preserves entire transaction");
    bad=files;bad.draws.back().first_index=6;refuse(bad,vertices,materials,original,"late index range overflow rejects transaction");
    bad=files;bad.draws.back().vertex_offset=-1;refuse(bad,vertices,materials,original,"negative referenced vertex rejects transaction");
    bad=files;bad.draws.back().instance_count=2;refuse(bad,vertices,materials,original,"compact palette refuses unresolved instancing");
    auto wrong_vertices=vertices;wrong_vertices.back().bindings[0].stride=12;
    refuse(files,wrong_vertices,materials,original,"mismatched resolved stride cannot reach the driver");
    bad=files;bad.draws.back().vertex[0]={};refuse(bad,vertices,materials,original,"advancing inherited stream cannot use unproved replacement indices");
    auto wrong_material=materials;wrong_material.draws[1].images[0].owner.reset();
    refuse(files,vertices,wrong_material,original,"missing late material owner refuses transaction");
    auto wrong_original=original;wrong_original.first_binding=1;
    refuse(files,vertices,materials,wrong_original,"unrestorable replacement stream refuses transaction");

    VariantCache variants(g.device,dispatch,properties.limits.maxBoundDescriptorSets,128,properties.limits);
    VariantTransaction ready;ready.plan.skip_original=false;
    auto state=request_draw_variants(plan,variants,41,recipe,layout,ready,reason);
    check(state==FileReadiness::Pending && ready.variants.empty() && !ready.plan.skip_original,"variant demand does not publish a partial transaction");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(state==FileReadiness::Pending && std::chrono::steady_clock::now()<deadline){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));state=request_draw_variants(plan,variants,41,recipe,layout,ready,reason);}
    if(state!=FileReadiness::Ready)throw std::runtime_error("Transaction variant creation: "+reason);
    check(ready.variants.size()==3 && ready.variants[0]==ready.variants[1] && ready.variants[0]!=ready.variants[2],
          "equal contracts share variants while explicit null uses a separate pipeline");
    check(request_draw_variants(plan,variants,0,recipe,layout,ready,reason)==FileReadiness::Failed && ready.variants.size()==3,
          "invalid pipeline generation preserves the ready transaction");
    std::vector<std::shared_ptr<DrawSnapshots>> arenas;
    for(unsigned i=0;i<3;++i){auto a=std::make_shared<DrawSnapshots>(g.device,snapshot_dispatch);
        if(!a->create(g.memory,properties.limits,1,259*16,65536,i==2?0:1))throw std::runtime_error(a->reason);arenas.push_back(a);}
    RecordedTransaction recorded;
    auto missing_arenas=arenas;missing_arenas[2].reset();
    check(!append_draw_snapshots(ready,missing_arenas,recorded,reason) && !arenas[0]->used(),"missing late arena refuses before any snapshot mutation");
    if(!append_draw_snapshots(ready,arenas,recorded,reason))throw std::runtime_error(reason);
    check(recorded.skip_original() && arenas[0]->used()==1 && arenas[2]->used()==1,"one retained immutable snapshot for every frontend draw");
    check(!append_draw_snapshots(ready,arenas,recorded,reason) && recorded.skip_original(),"exhausted snapshot append preserves prior recording");
    for(auto& a:arenas)a->seal();
    check(!append_draw_snapshots(ready,arenas,recorded,reason),"sealed command snapshots cannot be appended");

    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=3;dp.poolSizeCount=3;dp.pPoolSizes=sizes;
    CHECK_VK(pCreateDescriptorPool(g.device,&dp,nullptr,&objects.pool));VkDescriptorSet sets[3]{};
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=objects.pool;da.descriptorSetCount=3;da.pSetLayouts=objects.sets.data();
    CHECK_VK(pAllocateDescriptorSets(g.device,&da,sets));VkDescriptorBufferInfo info[]={{bones.buffer,0,259*16},{entry.buffer,0,32}};
    VkDescriptorImageInfo image{VK_NULL_HANDLE,blue->view(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet writes[3]{};
    for(unsigned i=0;i<3;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[i];w.descriptorCount=1;
        w.dstBinding=i==0?7:i==1?6:0;w.descriptorType=i==0?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        if(i==1)w.pImageInfo=&image;else w.pBufferInfo=&info[i==0?0:1];}
    pUpdateDescriptorSets(g.device,3,writes,0,nullptr);
    std::weak_ptr<const ImageUpload> retained_image=files.draws[0].pixel[15].image;
    std::weak_ptr<const BufferUpload> retained_mesh=files.draws[0].vertex[0].buffer;
    std::weak_ptr<const PipelineVariant> retained_variant=ready.variants[0];
    std::weak_ptr<const efmi::core::PaletteSnapshot> retained_palette=files.draws[0].palette;
    variants.retire(41);variants.stop();ready={};plan={};files={};bad={};materials={};wrong_material={};arenas.clear();missing_arenas.clear();
    check(!retained_mesh.expired() && !retained_image.expired() && !retained_variant.expired() && !retained_palette.expired(),
          "recorded transaction alone retains replacement buffers, textures, pipelines and palettes");
    auto readback=g.readback(64*64*4);auto command=g.begin();VkClearValue clears[2]{};clears[0].color.float32[3]=1;clears[1].depthStencil.depth=1;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=objects.pass;begin.framebuffer=objects.framebuffer;
    begin.renderArea.extent={64,64};begin.clearValueCount=2;begin.pClearValues=clears;pCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,64,64,0,1};VkRect2D scissor{{0,0},{64,64}};pCmdSetViewport(command,0,1,&viewport);pCmdSetScissor(command,0,1,&scissor);
    pCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.pipeline);VkDeviceSize zero=0;
    pCmdBindVertexBuffers(command,0,1,&original_vb.buffer,&zero);pCmdBindIndexBuffer(command,original_ib.buffer,16,VK_INDEX_TYPE_UINT16);
    pCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,objects.layout,0,3,sets,1,&dynamic_offset);
    check(!recorded.emit(dispatch,command,VK_NULL_HANDLE),"missing original pipeline records no replacement commands");
    check(recorded.emit(dispatch,command,objects.pipeline),"retired frontend transaction emits through the shared backend");
    pCmdDrawIndexed(command,3,1,0,0,0);pCmdEndRenderPass(command);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={64,64,1};
    pCmdCopyImageToBuffer(command,color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);g.finish(command,readback);
    auto pixels=[&](){auto sample=[&](unsigned x,unsigned y,std::array<uint8_t,4> expected,const char* label){
        const auto p=static_cast<const uint8_t*>(readback.mapped)+4*(y*64+x);check(!memcmp(p,expected.data(),4),label);};
        sample(48,42,{255,0,0,255},"transaction red image follows private 16-bit bone index and palette");
        sample(16,42,{0,255,0,255},"transaction second range uses its material, base vertex and entry with original FS buffers");
        sample(32,8,{0,0,0,0},"transaction explicit null produces zero without a null descriptor");
        sample(16,19,{0,0,255,255},"transaction restores original VB, IB offset/type, pipeline, descriptors and dynamic offset");
        sample(32,32,{0,0,0,255},"transaction keeps pixels outside geometry unchanged");};
    pixels();VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    CHECK_VK(pQueueSubmit(g.queue,1,&submit,VK_NULL_HANDLE));CHECK_VK(pQueueWaitIdle(g.queue));pixels();
    recorded={};check(retained_mesh.expired() && retained_image.expired() && retained_variant.expired() && retained_palette.expired(),
                     "completed command owner releases retired replacement resources");
}
} // namespace transaction_fixture
