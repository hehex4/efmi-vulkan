#include "native_execution.h"
#include "native_descriptors.h"
#include "native_targets.h"
#include "texture_residency.h"
#include "texture_identity.h"
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

}

void native_file_frame(NativeCallCache& cache,TextureResidency* residency)
{
    if(!residency)return;
    std::lock_guard<std::mutex> lock(cache.mutex);
    for(auto it=cache.files.begin();it!=cache.files.end();){
        if(!it->second.declaration.expired()){++it;continue;}
        residency->forget({it->second.id});it=cache.files.erase(it);
    }
}
namespace {
bool native_files(NativeCallCache& cache,TextureResidency* residency,const efmi::assets::FilePlan& input,
                  efmi::assets::FilePlan& output,std::string& why,bool wait,bool* pending)
{
    output=input;
    std::vector<uint32_t> ids;std::vector<size_t> indices;
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        for(size_t i=0;i<input.items.size();++i){
            if(input.items[i].operation!=efmi::assets::FileOperation::NativeBind)continue;
            const auto& file=input.items[i].resource;if(!file)continue;
            if(!residency){why="Native file residency is unavailable";return false;}
            auto found=cache.files.find(file.get());
            if(found!=cache.files.end() && found->second.declaration.expired()){
                residency->forget({found->second.id});cache.files.erase(found);found=cache.files.end();
            }
            if(found==cache.files.end()){
                if(cache.files.size()>=4096){why="Native file declaration budget exceeded";return false;}
                const auto id=residency->declare(file->path.u8string(),why,file->loader_flags,file->usage,true,file->writable);
                if(id==TextureResidency::kNone)return false;
                found=cache.files.emplace(file.get(),NativeCallCache::File{file,id}).first;
            }
            ids.push_back(found->second.id);indices.push_back(i);
        }
    }
    if(ids.empty())return true;
    std::vector<TextureResidency::Binding> bindings;residency->bind(ids,bindings,wait);
    for(size_t i=0;i<indices.size();++i){
        auto& item=output.items[indices[i]];const auto& file=*item.resource;const auto& binding=bindings[i];
        if(binding.failed){why="Native file upload failed: "+binding.reason;return false;}
        if(!binding.buffer && !binding.owner){if(pending)*pending=true;why="Native file upload is pending publication";return false;}
        auto resource=std::make_shared<efmi::frontend::NativeResource>();
        resource->bound=true;resource->format_known=true;
        if(binding.buffer){
            const auto& buffer=binding.buffer;
            if(!file.stride || buffer->size()%file.stride){why="Native file byte count is not a multiple of its stride";return false;}
            resource->dimension=1;resource->identity=reinterpret_cast<uint64_t>(buffer.get())|3;
            resource->buffer=uint64_t(buffer->buffer());resource->size=resource->range=buffer->size();
            resource->usage=buffer->usage();resource->buffer_stride=resource->stride=file.stride;
            resource->bind_flags=file.native_bind_flags;
        }else{
            const auto& image=*binding.owner;const auto& metadata=image.metadata();
            const bool storage=item.native_target.kind==efmi::frontend::NativeBinding::Access;
            if(storage?!file.writable:!file.sampled){why="Native file image bind flags do not support this view";return false;}
            if(file.view_format && efmi::assets::image_vk_format(file.view_format)!=metadata.format){why="Native file view format differs from the uploaded typed image";return false;}
            const auto skipped=image.skipped_levels();
            if(skipped>=metadata.levels.size()){why="Native file image has no published mip levels";return false;}
            resource->dimension=3;resource->identity=reinterpret_cast<uint64_t>(binding.owner.get())|3;
            resource->width=metadata.levels[skipped].width;resource->height=metadata.levels[skipped].height;resource->depth=1;
            resource->levels=uint32_t(metadata.levels.size())-skipped;resource->layers=image.layers();
            resource->view_type=storage?image.storage_view_type():image.view_type();resource->view_format=metadata.format;
            resource->format=resource->resource_format=dxgi_format(metadata.format);resource->format_known=resource->format!=0;
            resource->image=uint64_t(image.image());resource->image_safe=true;
            resource->image_view=uint64_t(storage?image.storage_view():binding.view);
            resource->usage=(file.sampled?VK_IMAGE_USAGE_SAMPLED_BIT:0)|(file.writable?VK_IMAGE_USAGE_STORAGE_BIT:0);
            resource->bind_flags=file.native_bind_flags;
            resource->aspect=VK_IMAGE_ASPECT_COLOR_BIT;resource->mip_count=storage?1:resource->levels;resource->layer_count=resource->layers;
            resource->image_layout=image.layout();
        }
        struct Owners {std::shared_ptr<const efmi::assets::BufferUpload> buffer;std::shared_ptr<const efmi::assets::ImageUpload> image;std::shared_ptr<const void> publication;};
        resource->uploaded_readonly=!file.writable;resource->uploaded_mutable=file.writable;
        resource->keep=std::make_shared<const Owners>(Owners{binding.buffer,binding.owner,binding.publication});
        resource->publication_semaphore=uint64_t(binding.semaphore);resource->publication_value=binding.serial;
        item.native_resource=std::move(resource);item.resource.reset();
    }
    return true;
}
}
bool prepare_native_transaction(NativeCallCache& cache,TextureResidency* residency,Device* device,CmdBuf* cb,const DrawRec& draw,
                                const efmi::assets::FilePlan& plan,NativeDrawTransaction& output,std::string& why,
                                NativeTargetCache* target_cache,bool wait_files,bool* pending)
{
    if(pending)*pending=false;
    NativeDrawTransaction next;
    if(!target_cache){
        std::lock_guard<std::mutex> lock(cache.mutex);
        if(!cache.targets)cache.targets=std::make_shared<NativeTargetCache>();
        target_cache=cache.targets.get();
    }
    if(!native_files(cache,residency,plan,next.plan,why,wait_files,pending) ||
       !prepare_native_targets(device,cb,draw,next.plan,next.targets,why,target_cache) ||
       !prepare_native_descriptors(device,cb,draw,next.plan,next.descriptors,why))return false;
    output=std::move(next);return true;
}
void begin_native_transaction(Device* device,CmdBuf* cb,const NativeDrawTransaction& transaction)
{
    record_native_images(device,cb,transaction.descriptors.images);
}
void record_native_binding(Device* device,CmdBuf* cb,const DrawRec& draw,const NativeDrawTransaction& transaction,size_t i)
{
    const auto& item=transaction.plan.items[i];
    if(item.operation!=efmi::assets::FileOperation::NativeBind)return;
    if(native_output_binding(item.native_target))record_native_target(device,cb,transaction.targets,i);
    else record_native_descriptor(device,cb,draw,transaction.descriptors,i);
}
DrawRec native_transaction_draw(const DrawRec& original,const NativeDrawTransaction& transaction,size_t before)
{
    return native_descriptor_draw(original,transaction.descriptors,before);
}
bool native_calls(NativeCallCache& cache, TextureResidency* residency, Device* device, CmdBuf* cb, const DrawRec& r, const efmi::assets::FilePlan& plan,
                  bool hunting_skip, std::string& why,bool wait_files,bool* pending)
{
    using O = efmi::assets::FileOperation;
    auto refuse = [&](const char* text) { why = text; return false; };
    if (!device || !cb || plan.items.empty()) return false;
    const bool compute = compute_call(r);
    if(compute && device->synchronization2_enabled && r.kind == DK_DISPATCH_INDIRECT)
        return refuse("Native indirect replay under synchronization2 needs argument-stage dependencies");
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
        // DrawCommand returns before evaluating arguments when hunting hides this draw.
        if(hunting_skip && (item.operation==O::Caller || item.operation==O::DrawIndexed || item.operation==O::Dispatch))continue;
        if (!item.layout.empty() || item.automatic || (item.resource && item.operation!=O::NativeBind) || item.owned)
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
    NativeDrawTransaction transaction;
    if(!hunting_skip || !compute){
        if(!prepare_native_transaction(cache,residency,device,cb,r,plan,transaction,why,nullptr,wait_files,pending))return false;
    }
    begin_native_transaction(device,cb,transaction);
    const size_t split = std::min(plan.pre_items,plan.items.size());
    auto record = [&](size_t index) {
        if (hunting_skip && compute) return;
        const auto& item=plan.items[index];
        if(item.operation==O::NativeBind){
            record_native_binding(device,cb,r,transaction,index);return;
        }
        if (hunting_skip) return;
        if (compute) native_compute_order(device,cb,cache.barrier);
        if (item.operation == O::Caller) caller(device,cb,r);
        else if (item.operation == O::Dispatch) device->d.CmdDispatch(cb->handle,item.index_count,item.instance_count,item.first_index);
        else if (item.indexed) device->d.CmdDrawIndexed(cb->handle,item.index_count,item.instance_count,item.first_index,item.vertex_offset,item.first_instance);
        else device->d.CmdDraw(cb->handle,item.index_count,item.instance_count,item.first_index,item.first_instance);
    };
    for (size_t i=0;i<split;++i) record(i);
    if (!plan.skip_original && !hunting_skip) { if (compute) native_compute_order(device,cb,cache.barrier); caller(device,cb,r); }
    for (size_t i=split;i<plan.items.size();++i) record(i);
    if (compute && !hunting_skip) native_compute_order(device,cb,cache.barrier);
    return true;
}
}
