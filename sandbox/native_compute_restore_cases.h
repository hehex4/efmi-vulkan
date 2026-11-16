#include "../layer/src/native_compute_restore.h"

namespace compute_restore_test {
static std::vector<uint32_t> calls,offsets;
static VkPipeline selected=VK_NULL_HANDLE;
static unsigned created=0,destroyed=0;
static bool allocation_fails=false;
static VKAPI_ATTR void VKAPI_CALL pipeline(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline value){
    check(point==VK_PIPELINE_BIND_POINT_COMPUTE,"Restore touches only compute pipeline");selected=value;calls.push_back(100);
}
static VKAPI_ATTR void VKAPI_CALL sets(VkCommandBuffer,VkPipelineBindPoint point,VkPipelineLayout,uint32_t first,
    uint32_t count,const VkDescriptorSet*,uint32_t n,const uint32_t* data){
    check(point==VK_PIPELINE_BIND_POINT_COMPUTE && count==1,"Restore preserves each compute set position");
    calls.push_back(first);if(n)offsets.insert(offsets.end(),data,data+n);
}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks*,VkPipelineLayout* out){
    if(allocation_fails)return VK_ERROR_OUT_OF_HOST_MEMORY;
    check(info->setLayoutCount==3 && info->pushConstantRangeCount==1,"Recreated layout preserves unused slots and push ranges");
    *out=h<VkPipelineLayout>(10000+(++created));return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){++destroyed;}
static VKAPI_ATTR void VKAPI_CALL destroy_set(VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*){}
}
static void native_compute_restore_cases(){
    namespace test=compute_restore_test;
    Device d;CmdBuf cb;d.d.CmdBindPipeline=test::pipeline;d.d.CmdBindDescriptorSets=test::sets;
    d.d.CreatePipelineLayout=test::create;d.d.DestroyPipelineLayout=test::destroy;d.d.DestroyDescriptorSetLayout=test::destroy_set;
    const auto sl=h<VkDescriptorSetLayout>(1200),plain=h<VkDescriptorSetLayout>(1201);
    const auto pl=h<VkPipelineLayout>(1202);const auto pool=h<VkDescriptorPool>(1203);
    const auto a=h<VkDescriptorSet>(1204),b=h<VkDescriptorSet>(1205);
    layout(d,sl,{{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,2,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    layout(d,plain,{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    pipeline_layout(d,pl,{sl,plain,sl},true);allocate(d,pool,sl,a);allocate(d,pool,sl,b);
    const uint32_t first[]={256,512},last[]={768,1024};
    on_bind_sets(&d,&cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&a,2,first);
    on_bind_sets(&d,&cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,2,1,&b,2,last);
    cb.comp_pipeline=h<VkPipeline>(1206);cb.comp_bound=h<VkPipeline>(1207);
    cb.gfx_bound=h<VkPipeline>(1208);cb.sets[0].set=h<VkDescriptorSet>(1209);
    NativeComputeRestore restore;std::string why;
    check(prepare_native_compute_restore(&d,&cb,restore,why) && why.empty() && restore.sets.size()==2,"Capture sparse complete compute state");
    cb.dyn[0]=999;test::calls.clear();test::offsets.clear();restore.record(h<VkCommandBuffer>(1210));
    check(test::selected==cb.comp_bound && test::selected!=cb.comp_pipeline,"Restore the actually selected native pipeline");
    check(test::calls==std::vector<uint32_t>({100,0,2}) && test::offsets==std::vector<uint32_t>({256,512,768,1024}),"Restore snapshots exact dynamic arrays and ascending set positions");
    check(cb.gfx_bound==h<VkPipeline>(1208) && cb.sets[0].set==h<VkDescriptorSet>(1209) && cb.dyn[0]==999,"Restoration does not mutate tracked graphics or compute state");cb.dyn[0]=256;
    const auto original=cb.comp_sets[2];
    auto rejected=[&](const char* label){const auto prior=restore.pipeline;const auto calls=test::calls.size();
        check(!prepare_native_compute_restore(&d,&cb,restore,why) && !why.empty() && restore.pipeline==prior && test::calls.size()==calls,label);};
    cb.comp_sets[2].dyn_count=1;rejected("Refuse incomplete dynamic arrays without output or command mutation");
    cb.comp_sets[2]=original;cb.comp_sets[2].dyn_first=UINT32_MAX;rejected("Refuse dynamic-offset range overflow");
    cb.comp_sets[2]=original;cb.comp_sets[2].id++;rejected("Refuse a recycled descriptor handle");
    cb.comp_sets[2]=original;cb.comp_sets[2].pushed=std::make_shared<DescriptorSetInfo>();rejected("Refuse unsupported push-descriptor restoration");
    cb.comp_sets[2]=original;cb.comp_sets[2].unsupported="descriptor_buffer";rejected("Refuse unknown descriptor-buffer state");
    cb.comp_sets[2]=original;cb.comp_bound=VK_NULL_HANDLE;rejected("Refuse missing actual pipeline selection");cb.comp_bound=h<VkPipeline>(1207);
    d.passive=true;rejected("Refuse a passive device with no complete tracking");d.passive=false;
    g_cfg.track_resources=false;rejected("Refuse resource tracking disabled");g_cfg.track_resources=true;
    auto incompatible=std::make_shared<PipelineLayoutInfo>(*original.layout);++incompatible->prefix[0];
    cb.comp_sets[2].layout=incompatible;rejected("Refuse mutually disturbing saved descriptor layouts");cb.comp_sets[2]=original;
    on_destroy_pipeline_layout(&d,pl);test::created=0;test::destroyed=0;
    check(prepare_native_compute_restore(&d,&cb,restore,why) && restore.layouts.size()==1 && test::created==1 &&
        restore.sets[0].layout==restore.sets[1].layout && restore.sets[0].layout!=pl,"Recreate a destroyed application layout once for all its sets");
    restore={};check(test::destroyed==1,"Retire private restoration layouts with the plan");
    test::allocation_fails=true;rejected("Refuse failed layout recreation before recording");test::allocation_fails=false;
    cb.comp_sets[2].id++;
    rejected("Later refusal discards an earlier recreated layout");check(test::created==test::destroyed,"Failed whole-state admission releases recreated layouts");
    cb.reset_recording();pipeline_layout(d,pl,{sl,plain,sl,plain,sl},false);
    on_bind_sets(&d,&cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,4,1,&a,2,first);
    check(cb.compute_sets_complete,"Graphics overflow does not poison compute state");
    on_bind_sets(&d,&cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,4,1,&a,2,first);
    rejected("Refuse compute state outside the tracked set bound");
    on_bind_sets(&d,&cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&a,2,first);
    check(!cb.compute_sets_complete,"Later low-set binds cannot erase unknown higher compute state");
    cb.reset_recording();check(cb.compute_sets_complete && prepare_native_compute_restore(&d,&cb,restore,why) && restore.sets.empty() && !restore.pipeline,"Legal recording reset restores known empty state");
    on_unsupported_descriptor_bind(&cb,VK_PIPELINE_BIND_POINT_COMPUTE,"secondary_execution");
    rejected("Secondary execution invalidates complete compute-state proof");
    cb.reset_recording();const auto push=h<VkDescriptorSetLayout>(1211);
    layout(d,push,{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}},nullptr,VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
    pipeline_layout(d,pl,{plain,plain,plain,plain,push},false);
    on_push_descriptors(&d,&cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,4,0,nullptr);
    check(!cb.compute_sets_complete,"Push descriptors beyond the tracked bound also invalidate complete state");
}
