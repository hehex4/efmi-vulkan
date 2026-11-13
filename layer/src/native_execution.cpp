#include "native_execution.h"
#include "native_descriptors.h"
#include "../../frontend/gpu/source_registers.h"

namespace efvk {
namespace {
bool compute_call(const DrawRec& r) { return r.kind == DK_DISPATCH || r.kind == DK_DISPATCH_INDIRECT; }

void caller(Device* device, CmdBuf* cb, const DrawRec& r)
{
    auto& d = device->d; const auto cmd = cb->handle;
    switch (r.kind) {
    case DK_DRAW: d.CmdDraw(cmd,r.count,r.instances,r.first,r.first_instance); break;
    case DK_INDEXED: d.CmdDrawIndexed(cmd,r.count,r.instances,r.first,r.vertex_offset,r.first_instance); break;
    case DK_DISPATCH: d.CmdDispatch(cmd,r.count,r.instances,r.first); break;
    case DK_DISPATCH_INDIRECT: d.CmdDispatchIndirect(cmd,r.indirect,r.indirect_off); break;
    case DK_INDIRECT:
        if (r.indirect_count) {
            const auto fn = d.CmdDrawIndirectCount ? d.CmdDrawIndirectCount : d.CmdDrawIndirectCountKHR;
            fn(cmd,r.indirect,r.indirect_off,r.indirect_count,r.indirect_count_off,r.count,r.indirect_stride);
        } else d.CmdDrawIndirect(cmd,r.indirect,r.indirect_off,r.count,r.indirect_stride);
        break;
    case DK_INDEXED_INDIRECT:
        if (r.indirect_count) {
            const auto fn = d.CmdDrawIndexedIndirectCount ? d.CmdDrawIndexedIndirectCount : d.CmdDrawIndexedIndirectCountKHR;
            fn(cmd,r.indirect,r.indirect_off,r.indirect_count,r.indirect_count_off,r.count,r.indirect_stride);
        } else d.CmdDrawIndexedIndirect(cmd,r.indirect,r.indirect_off,r.count,r.indirect_stride);
        break;
    }
}

void compute_order(PFN_vkCmdPipelineBarrier command, CmdBuf* cb)
{
    // DX11's immediate context orders resource hazards. New Vulkan dispatches need an explicit
    // dependency, including indirect arguments and work following the last injected dispatch.
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    command(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                0,1,&barrier,0,nullptr,0,nullptr);
}
}

bool native_calls(NativeCallCache& cache, Device* device, CmdBuf* cb, const DrawRec& r, const efmi::assets::FilePlan& plan,
                  bool hunting_skip, std::string& why)
{
    using O = efmi::assets::FileOperation;
    auto refuse = [&](const char* text) { why = text; return false; };
    if (!device || !cb || plan.items.empty()) return false;
    const bool compute = compute_call(r);
    if(compute){
        std::call_once(cache.loaded,[&]{if(device->owned_gdpa)cache.barrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->owned_gdpa(device->handle,"vkCmdPipelineBarrier"));});
        if(!cache.barrier)return refuse("Native compute barrier entry is unavailable");
    }
    if (compute == (cb->in_rp || cb->inherited_rp)) return refuse("Native calls require the application's matching compute/render-pass scope");
    if (r.kind > DK_DISPATCH_INDIRECT) return refuse("Native caller kind is unavailable");
    if (r.indirect_count && ((r.kind == DK_INDIRECT && !device->d.CmdDrawIndirectCount && !device->d.CmdDrawIndirectCountKHR) ||
                            (r.kind == DK_INDEXED_INDIRECT && !device->d.CmdDrawIndexedIndirectCount && !device->d.CmdDrawIndexedIndirectCountKHR)))
        return refuse("Native indirect-count entry is unavailable");
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto pipe = device->pipes.find(r.pipeline);
    if (pipe == device->pipes.end()) return refuse("Native application pipeline is unavailable");
    if (!compute) {
        // No transfer/compute barrier can be invented inside the application's render pass. Draws
        // with storage side effects need a compatible self-dependency before they can be repeated.
        // Only resource categories are used here; inferred source-register numbers are irrelevant.
        std::lock_guard<std::mutex> access_lock(cache.mutex);
        auto known = cache.graphics_access.find(pipe->second.id);
        if (known == cache.graphics_access.end()) {
            if (cache.graphics_access.size() >= 4096) cache.graphics_access.clear();
            std::string result;
            for (const auto& stage : pipe->second.shaders) {
                if (!stage.shader) { result="Native graphics shader metadata is unavailable"; break; }
                const auto& code=stage.shader->code;
                for(size_t at=5;at<code.size();){
                    const auto count=code[at]>>16,op=code[at]&65535;
                    if(!count || count>code.size()-at){result="Malformed native graphics shader";break;}
                    // Physical pointers and transform feedback can write outside ordinary descriptors.
                    if((op==14 && count>=3 && code[at+1]!=0) || (op==17 && count==2 && code[at+1]==53)){
                        result="Native graphics physical pointers or transform feedback need explicit hazard tracking";break;
                    }
                    at+=count;
                }
                if(!result.empty())break;
                std::vector<efmi::gpu::SourceRegister> resources; std::string reason;
                if (!efmi::gpu::source_registers(stage.shader->code,resources,reason)) {
                    result="Native graphics resource access is unknown: "+reason;break;
                }
                for (const auto& resource : resources)
                    if (resource.kind == efmi::gpu::SourceRegister::ImageUav || resource.kind == efmi::gpu::SourceRegister::BufferUav ||
                        resource.kind == efmi::gpu::SourceRegister::TexelUav)
                        result="Native graphics storage writes need an explicit render-pass dependency";
                if(!result.empty())break;
            }
            known=cache.graphics_access.emplace(pipe->second.id,std::move(result)).first;
        }
        if(!known->second.empty()){why=known->second;return false;}
    }
    for (const auto& item : plan.items) {
        if (!item.layout.empty() || item.automatic || item.resource || item.owned)
            return refuse("Native call plan contains an unsupported layout or resource operation");
        if (item.operation == O::Caller || item.operation == O::NativeBind) continue;
        if (item.operation == O::Dispatch) {
            if (!compute) return refuse("Native dispatch cannot run in the application's graphics pass");
            const auto* limit = device->physical_properties.limits.maxComputeWorkGroupCount;
            if (item.index_count > limit[0] || item.instance_count > limit[1] || item.first_index > limit[2])
                return refuse("Native dispatch dimensions exceed the device limits");
        } else if (item.operation == O::DrawIndexed) {
            if (compute) return refuse("Native graphics call has no graphics render-pass scope");
            if (item.indexed) {
                const auto buffer = device->bufs.find(r.ib);
                if (buffer == device->bufs.end() || !r.index_size || r.ib_off > buffer->second.size ||
                    uint64_t(item.first_index) + item.index_count > (buffer->second.size - r.ib_off) / r.index_size)
                    return refuse("Native indexed call exceeds the bound index buffer");
            }
        } else return refuse("Native resource bindings require the descriptor executor");
    }
    lock.unlock();
    NativeDescriptorPlan descriptors;
    if(!hunting_skip && !prepare_native_descriptors(device,cb,r,plan,descriptors,why))return false;
    const size_t split = std::min(plan.pre_items,plan.items.size());
    auto record = [&](size_t index) {
        if (hunting_skip) return;
        const auto& item=plan.items[index];
        if(item.operation==O::NativeBind){record_native_descriptor(device,cb,r,descriptors,index);return;}
        if (compute) compute_order(cache.barrier,cb);
        if (item.operation == O::Caller) caller(device,cb,r);
        else if (item.operation == O::Dispatch) device->d.CmdDispatch(cb->handle,item.index_count,item.instance_count,item.first_index);
        else if (item.indexed) device->d.CmdDrawIndexed(cb->handle,item.index_count,item.instance_count,item.first_index,item.vertex_offset,item.first_instance);
        else device->d.CmdDraw(cb->handle,item.index_count,item.instance_count,item.first_index,item.first_instance);
    };
    for (size_t i=0;i<split;++i) record(i);
    if (!plan.skip_original && !hunting_skip) { if (compute) compute_order(cache.barrier,cb); caller(device,cb,r); }
    for (size_t i=split;i<plan.items.size();++i) record(i);
    if (compute && !hunting_skip) compute_order(cache.barrier,cb);
    return true;
}
}
