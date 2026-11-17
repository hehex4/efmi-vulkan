#include "../frontend/xxmi/owned_runtime.h"
static void prepared_host_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"increment.hlsl",R"hlsl(RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[0] += 5; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $calls = 0
ResourceOutput = copy ResourceSource
[ResourceSource]
type = StructuredBuffer
stride = 4
data = R32_UINT 7
bind_flags = shader_resource
[ResourceOutput]
bind_flags = shader_resource unordered_access
[CustomShaderIncrement]
cs = increment.hlsl
max_executions_per_frame = 1
cs-u0 = ref ResourceOutput
dispatch = 1,1,1
cs-u0 = null
[ShaderOverrideOrdered]
hash = 1122aabb
$calls = $calls + 1
ResourceOutput = copy ResourceSource
run = CustomShaderIncrement
ps-t0 = ref ResourceOutput
draw = 3,0
post ResourceOutput = copy ResourceSource
post ps-t0 = null
[ShaderOverrideBindingOnly]
hash = 1122aabd
handling = skip
post ps-t1 = ref ResourceSource
[ShaderOverrideRefused]
hash = 1122aabc
run = CustomShaderInherited
[CustomShaderInherited]
dispatch = 1,1,1
)ini");
    frontend::OwnedRuntime::Tools tools;
    tools.compiler=L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll";
    tools.bytecode=L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll";
    auto owned=std::make_unique<frontend::OwnedRuntime>(d.resource,shader_host(d),tools,root);
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-host"};config.installation=root;
    owned->begin_deferred();std::string why;
    auto ini=frontend::IniRuntime::load(config,owned->combine(nullptr),why);
    if(!ini)throw std::runtime_error("Load prepared host configuration: "+why);++checks;
    owned->suspend();
    frontend::ApplicationResourceState application;application.omitted_slots_unbound=true;
    frontend::RuntimeDraw draw;draw.native_calls=true;draw.indexed=false;draw.count=3;
    draw.stage_hash_mask=1;draw.stage_hashes[0]=0x1122aabb;draw.frame=1;
    check(!owned->prepare_draw(ini,draw,application,true,why) && owned->pending(),
        "Host preparation refuses unpublished Constants without discarding them");
    owned->begin(d.queue);{auto ticket=owned->finish({},d.queue);if(ticket)ticket->wait();}
    application.omitted_slots_unbound=false;
    check(!owned->prepare_draw(ini,draw,application,true,why) && !owned->scope_active(),
        "Host preparation refuses an incomplete application snapshot without a leaked scope");
    application.omitted_slots_unbound=true;
    {
        frontend::OwnedRuntime other(d.resource,shader_host(d),tools,root);
        check(!other.prepare_draw(ini,draw,application,true,why) && why.find("matching")!=std::string::npos,
            "Another GPU registry cannot prepare this original configuration");
    }
    assets::FilePlan reused;
    {
        auto only=draw;only.stage_hashes[0]=0x1122aabd;
        auto scope=owned->prepare_draw(ini,only,application,true,why);
        if(!scope)throw std::runtime_error("Prepare binding-only host transaction: "+why);
        const auto final=scope->plan().prepared_final_state;
        check(scope->plan().skip_original && scope->plan().items.empty() && final &&
            bool(final->bindings.at({L'p',ResourceCopyTargetType::SHADER_RESOURCE,1})),
            "Skipped draw with only post bindings retains its final state without fabricated work");
        check(!final->admitted(),"Binding-only final state starts unadmitted");scope.reset();
        check(!final->admitted(),"Cancelling a binding-only draw does not admit its final state");
        scope=owned->prepare_draw(ini,only,application,true,why);
        check(scope && scope->admit(why) && scope->plan().prepared_final_state->admitted(),
            "A binding-only retry admits its final state without a GPU packet");
        reused=scope->plan();
    }
    auto unmatched=draw;unmatched.stage_hashes[0]=0x1122aaff;
    check(ini->shader_draw(unmatched,reused,why) && !reused.prepared_final_state,
        "Ordinary unmatched draw replaces a reused prepared output");
    check(ini->shader_draw(unmatched,reused,why) && !reused.prepared_final_state,
        "Recycled ordinary plan storage cannot revive a previous final snapshot");
    auto pending=owned->prepare_draw(ini,draw,application,true,why);
    if(!pending)throw std::runtime_error("Prepare host transaction: "+why);++checks;
    assets::FilePlan cancelled=pending->plan();
    check(cancelled.skip_original && cancelled.items.size()==4 && cancelled.items[0].prepared_work && cancelled.items[3].prepared_work,
        "Host transaction contains positioned pre work, explicit draw, caller and post work");
    check(cancelled.prepared_final_state && !cancelled.prepared_final_state->admitted() &&
        !cancelled.prepared_final_state->bindings.at({L'p',ResourceCopyTargetType::SHADER_RESOURCE,0}) &&
        bool(cancelled.items[1].prepared_state->bindings.at({L'p',ResourceCopyTargetType::SHADER_RESOURCE,0})),
        "Final post-null state differs from the retained draw bindings");
    check(!cancelled.items[1].prepared_state->admitted() && awaiting_admission(cancelled.items[0].prepared_work),
        "Host preparation never admits or records speculative work");
    check(!owned->prepare_draw(ini,draw,application,true,why),"Overlapping host preparation is rejected");
    check(!cancelled.items[1].prepared_state->admitted() && ini->status().variables.at("$calls")==1,
        "Rejected overlap does not execute or publish the original transaction");
    pending.reset();
    check(!cancelled.prepared_final_state->admitted() && !cancelled.items[1].prepared_state->admitted() && awaiting_admission(cancelled.items[0].prepared_work),
        "Abandoned host transaction leaves retained calls and packets unrecordable");
    cancelled={};
    draw.stage_hashes[0]=0x1122aabc;
    check(!owned->prepare_draw(ini,draw,application,true,why) && !owned->scope_active(),
        "Contained original command failure cancels host preparation");
    draw.stage_hashes[0]=0x1122aabb;
    pending=owned->prepare_draw(ini,draw,application,true,why);
    if(!pending)throw std::runtime_error("Retry prepared host transaction: "+why);++checks;
    check(ini->status().variables.at("$calls")==2,"Cancelled GPU work preserves original scalar assignments");
    check(!owned->prepare_draw(ini,draw,application,true,why),"Second overlap refuses without cancelling the transaction awaiting admission");
    std::weak_ptr<frontend::IniRuntime> registry=ini;ini.reset();owned.reset();
    check(!registry.expired(),"Prepared host transaction retains its registry after host generation retirement");
    check(pending->admit(why) && pending->admit(why),"Host acceptance admits the complete plan exactly once");
    assets::FilePlan accepted=pending->plan();pending.reset();
    check(registry.expired(),"Accepted plan packets do not keep the original configuration alive");
    check(accepted.prepared_final_state->admitted() && !accepted.prepared_final_state->bindings.at({L'p',ResourceCopyTargetType::SHADER_RESOURCE,0}),
        "Accepted final bindings remain valid after the original host and registry retire");
    check(accepted.items[1].prepared_state->admitted() && accepted.items[2].prepared_state->admitted(),
        "All original call snapshots share the host admission");
    const auto output=accepted.items[1].prepared_state->bindings.at({L'p',ResourceCopyTargetType::SHADER_RESOURCE,0}).buffer;
    gpu::ResourceCommands app(d.resource);auto before=app.create(4,0),captured=app.create(4,0),after=app.create(4,0);
    std::vector<std::shared_ptr<const gpu::PreparedResourceCommands>> packets;
    for(const auto& item:accepted.items)if(item.prepared_work)packets.push_back(item.prepared_work);
    auto capture=[&](VkCommandBuffer cb,const gpu::Buffer& destination){
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        d.resource.dispatch.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        VkBufferCopy copy{0,0,4};d.resource.dispatch.CmdCopyBuffer(cb,output->buffer(),destination->buffer(),1,&copy);
        d.resource.dispatch.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    };
    record_prepared_packets(d,packets,[&](auto cb){capture(cb,before);},[&](auto cb){capture(cb,after);},
        [&](auto cb,size_t){capture(cb,captured);});
    auto word=[&](const gpu::Buffer& buffer){const auto bytes=app.readback(d.queue,buffer,0,4);uint32_t value=0;memcpy(&value,bytes.data(),4);return value;};
    check(word(before)==7,"Host preparation and admission leave GPU contents unchanged until their recorded position");
    check(word(captured)==12,"Same-frame host retry restores the shader quota and consumes the pre segment at the call");
    check(word(after)==7,"Post work retains its original position after host/configuration destruction");
}
