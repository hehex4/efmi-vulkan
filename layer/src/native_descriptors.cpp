#include "native_descriptors.h"
#include "../../frontend/gpu/source_registers.h"

namespace efvk {
namespace {
using Resource=efmi::frontend::NativeResource;
bool dynamic_type(uint32_t t){return t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;}
bool buffer_type(uint32_t t){return dynamic_type(t) || t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;}
bool image_type(uint32_t t){return t==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || t==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || t==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || t==VK_DESCRIPTOR_TYPE_SAMPLER;}
struct Writes {
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkBufferView> views;
    Writes(const DescriptorSetInfo& contents,VkDescriptorSet set){
        const auto n=contents.values.size();writes.reserve(n);buffers.reserve(n);images.reserve(n);views.reserve(n);
        for(const auto& entry:contents.values){
            const auto& value=entry.second;if(!value.known)continue;
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;
            w.dstBinding=entry.first.first;w.dstArrayElement=entry.first.second;w.descriptorCount=1;w.descriptorType=VkDescriptorType(value.type);
            if(buffer_type(value.type)){buffers.push_back({value.buffer,value.offset,value.range});w.pBufferInfo=&buffers.back();}
            else if(image_type(value.type)){images.push_back({value.sampler,value.image_view,VkImageLayout(value.image_layout)});w.pImageInfo=&images.back();}
            else {views.push_back(value.buffer_view);w.pTexelBufferView=&views.back();}
            writes.push_back(w);
        }
    }
};
bool hold_locked(Device* d,const DescriptorSetInfo& contents,std::vector<std::shared_ptr<const void>>& owners)
{
    for(const auto& pair:contents.values){
        const auto& v=pair.second;if(!v.known)continue;
        if(v.native)owners.push_back(v.native);
        else {
        if(v.buffer){
            const auto b=d->bufs.find(v.buffer);
            if(b==d->bufs.end() || b->second.id!=v.buffer_id || !b->second.lifetime || b->second.lifetime->dependencies.empty())return false;
            owners.push_back(b->second.lifetime);
        }
        if(v.image_view){
            const auto view=d->image_views.find(v.image_view);
            if(view==d->image_views.end() || view->second.id!=v.image_view_id || !view->second.lifetime || view->second.lifetime->dependencies.empty())return false;
            const auto image=d->images.find(view->second.image);
            if(image==d->images.end() || !image->second.lifetime || image->second.lifetime->dependencies.empty())return false;
            owners.push_back(view->second.lifetime);
        }
        if(v.buffer_view){
            const auto view=d->buffer_views.find(v.buffer_view);
            if(view==d->buffer_views.end() || view->second.id!=v.buffer_view_id || !view->second.lifetime || view->second.lifetime->dependencies.empty())return false;
            const auto buffer=d->bufs.find(view->second.buffer);
            if(buffer==d->bufs.end() || !buffer->second.lifetime || buffer->second.lifetime->dependencies.empty())return false;
            owners.push_back(view->second.lifetime);
        }
        }
        if(v.sampler){
            const auto binding=contents.layout->bindings.find(pair.first.first);
            if(binding!=contents.layout->bindings.end() && !binding->second.immutable_samplers.empty())continue;
            const auto sampler=d->samplers.find(v.sampler);
            if(sampler==d->samplers.end() || sampler->second.id!=v.sampler_id || !sampler->second.lifetime)return false;
            owners.push_back(sampler->second.lifetime);
        }
    }
    return true;
}
}
struct NativeDescriptorSet {
    Device* device=nullptr;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout owned_layout=VK_NULL_HANDLE;
    SetBind bind;
    uint32_t slot=0;
    DescriptorSetInfo contents;
    std::vector<uint32_t> zero_offsets;
    std::vector<std::shared_ptr<const void>> owners;
    ~NativeDescriptorSet(){
        if(owned_layout)device->d.DestroyPipelineLayout(device->handle,owned_layout,nullptr);
        if(pool){
            {std::unique_lock<std::shared_mutex> lock(device->res_mx);
             auto found=device->descriptor_sets.find(bind.set);if(found!=device->descriptor_sets.end() && found->second.id==bind.id)device->descriptor_sets.erase(found);}
            device->d.DestroyDescriptorPool(device->handle,pool,nullptr);
        }
    }

};
namespace {
struct NativeDescriptorOwner:CommandOwner {
    std::vector<std::shared_ptr<NativeDescriptorSet>> sets;
    std::mutex submit_mutex;
    std::map<VkSemaphore,uint64_t> publications;
    std::map<VkQueue,std::map<VkSemaphore,uint64_t>> waited;
    bool sealed=false;
    void reset_after_completion()override{sets.clear();publications.clear();waited.clear();used=false;sealed=false;}
    bool seal()override{if(!used || sealed)return false;sealed=true;return true;}
    VkResult before_submit(VkQueue queue)override{
        std::lock_guard<std::mutex> lock(submit_mutex);
        if(sets.empty() || publications.empty())return VK_SUCCESS;
        auto& prior=waited[queue];std::vector<VkSemaphore> semaphores;std::vector<uint64_t> values;
        for(const auto& p:publications)if(prior[p.first]<p.second){semaphores.push_back(p.first);values.push_back(p.second);}
        if(semaphores.empty())return VK_SUCCESS;
        std::vector<VkPipelineStageFlags> stages(values.size(),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        timeline.waitSemaphoreValueCount=uint32_t(values.size());timeline.pWaitSemaphoreValues=values.data();
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.pNext=&timeline;
        submit.waitSemaphoreCount=uint32_t(values.size());submit.pWaitSemaphores=semaphores.data();submit.pWaitDstStageMask=stages.data();
        const auto result=sets.front()->device->d.QueueSubmit(queue,1,&submit,VK_NULL_HANDLE);
        if(result==VK_SUCCESS)for(size_t i=0;i<values.size();++i)prior[semaphores[i]]=values[i];
        return result;
    }
};
const char native_descriptor_owner=0;
}
bool prepare_native_descriptors(Device* device,CmdBuf* cb,const DrawRec& draw,const efmi::assets::FilePlan& plan,
                                NativeDescriptorPlan& output,std::string& why)
{
    auto refuse=[&](const char* reason){why=reason;return false;};
    NativeDescriptorPlan next;next.items.resize(plan.items.size());
    bool needed=false;for(const auto& item:plan.items)needed|=item.operation==efmi::assets::FileOperation::NativeBind;
    if(!needed){output=std::move(next);return true;}
    // DX11's single context orders references implicitly. Queue-local barriers cannot extend
    // the application's semaphore graph to accesses introduced on another application queue.
    if(device->application_queue_count!=1){
        for(const auto& item:plan.items)if(item.operation==efmi::assets::FileOperation::NativeBind &&
            item.native_resource && item.native_resource->bound){
            const bool published=item.native_resource->uploaded_readonly && device->application_queue_count &&
                                 device->material_queues.size()==device->application_queue_count;
            if(!published)return refuse("Native application references require one application queue until cross-queue hazards are tracked");
        }
    }
    auto owner=std::static_pointer_cast<NativeDescriptorOwner>(owner_for(cb->owners,&native_descriptor_owner,[]{
        auto result=std::make_shared<NativeDescriptorOwner>();result->backend=&native_descriptor_owner;return result;
    }));
    if(owner->sealed || owner->sets.size()+plan.items.size()>4096)return refuse("Native descriptor recording budget or sealed state");
    std::array<SetBind,kMaxSets> state;std::copy(std::begin(draw.sets),std::end(draw.sets),state.begin());
    std::shared_ptr<const PipelineLayoutInfo> expected;
    {std::shared_lock<std::shared_mutex> lock(device->res_mx);const auto p=device->pipes.find(draw.pipeline);if(p!=device->pipes.end())expected=p->second.layout;}
    if(!expected)return refuse("Native descriptor pipeline layout is unavailable");
    for(size_t i=0;i<plan.items.size();++i){
        const auto& item=plan.items[i];if(item.operation!=efmi::assets::FileOperation::NativeBind)continue;
        const auto& target=item.native_target;
        if(target.set>=kMaxSets || target.set>=expected->prefix.size())return refuse("Native descriptor target set is outside the pipeline");
        const auto& bound=state[target.set];
        if(bound.unsupported || !bound.layout || target.set>=bound.layout->prefix.size() || !expected->prefix[target.set] ||
           bound.layout->prefix[target.set]!=expected->prefix[target.set])return refuse("Native descriptor target layout is incompatible");
        auto set=std::make_shared<NativeDescriptorSet>();set->device=device;set->slot=target.set;set->bind=bound;
        {
            std::shared_lock<std::shared_mutex> lock(device->res_mx);
            const DescriptorSetInfo* original=bound.pushed.get();
            const auto found=device->descriptor_sets.find(bound.set);
            if(!original && found!=device->descriptor_sets.end() && found->second.id==bound.id)original=&found->second;
            if(!original || original->unsupported || !original->layout || !original->layout->complete || !original->layout->lifetime)
                return refuse("Native descriptor target has no retained complete layout");
            if(original->layout->flags & ~VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR)return refuse("Native descriptors do not clone update-after-bind or descriptor-buffer layouts");
            if(original->values.size()>1024)return refuse("Native descriptor set element budget exceeded");
            set->contents=*original;
            size_t dynamic=0,total=0;
            for(const auto& binding:original->layout->bindings){
                const auto type=binding.second.type;
                if(binding.second.flags || type>VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)return refuse("Native descriptor binding flags or type require a separate executor");
                if(dynamic_type(type))dynamic+=binding.second.count;
                total+=binding.second.count;
            }
            if(total>4096)return refuse("Native descriptor layout allocation budget exceeded");
            if(bound.dyn_count!=dynamic || bound.dyn_first>cb->dyn.size() || dynamic>cb->dyn.size()-bound.dyn_first)
                return refuse("Native descriptor dynamic offsets are incomplete");
            set->zero_offsets.resize(dynamic);
            {
                const std::vector<uint32_t> offsets(cb->dyn.begin()+bound.dyn_first,cb->dyn.begin()+bound.dyn_first+dynamic);
                size_t at=0;
                for(const auto& binding:original->layout->bindings)if(dynamic_type(binding.second.type)){
                    for(uint32_t element=0;element<binding.second.count;++element){
                        auto value=set->contents.values.find({binding.first,element});
                        if(value!=set->contents.values.end())value->second.offset+=offsets[at+element];
                    }
                    at+=binding.second.count;
                }
            }
            const auto layout=original->layout->bindings.find(target.binding);
            if(layout==original->layout->bindings.end() || target.element>=layout->second.count || target.stage>=6 ||
               !(layout->second.stages & (1u<<target.stage)))return refuse("Native descriptor alias is absent or invisible to its stage");
            const uint32_t type=layout->second.type;
            using Kind=efmi::frontend::NativeBinding;
            const bool constant=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            const bool storage=type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            const bool sampled=type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if((target.kind==Kind::Constant && !constant) || (target.kind==Kind::Resource && !(storage || sampled || type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)) ||
               (target.kind==Kind::Access && !(storage || type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)))
                return refuse("Native descriptor alias kind differs from its layout type");
            auto replacement=item.native_resource;
            if(!replacement)replacement=std::make_shared<const Resource>();
            const auto& resource=*replacement;
            auto value=set->contents.values[{target.binding,target.element}];value.type=type;value.known=true;value.native=replacement;
            if(!resource.bound){
                if(!device->null_descriptors)return refuse("Native null descriptors require the enabled nullDescriptor feature");
                value.buffer=VK_NULL_HANDLE;value.buffer_view=VK_NULL_HANDLE;value.image_view=VK_NULL_HANDLE;
                value.buffer_id=value.buffer_view_id=value.image_view_id=0;value.offset=0;value.range=VK_WHOLE_SIZE;
                value.image_layout=VK_IMAGE_LAYOUT_GENERAL;
            }else{
            if(!resource.keep)return refuse("Native resource has no complete retained ownership");
            if(resource.uploaded_readonly){
                const auto pipeline=device->pipes.find(draw.pipeline);
                if(pipeline==device->pipes.end())return refuse("Native file consumer pipeline is unavailable");
                for(const auto& stage:pipeline->second.shaders){
                    std::vector<efmi::gpu::SourceRegister> registers;std::string reason;
                    if(!stage.shader || !efmi::gpu::source_registers(stage.shader->code,registers,reason))
                        return refuse("Native file consumer resource access is unknown");
                    for(const auto& r:registers)if(r.set==target.set && r.binding==target.binding){
                        using R=efmi::gpu::SourceRegister;
                        if(r.kind==R::BufferUav || r.kind==R::ImageUav || r.kind==R::TexelUav)
                            return refuse("Native file uploads are read-only; writable views need a mutable resource adapter");
                        if(storage && r.stride && resource.buffer_stride!=r.stride)
                            return refuse("Native file stride differs from the shader's structured-buffer stride");
                    }
                }
            }
            if(!resource.uploaded_readonly && draw.kind!=DK_DISPATCH && draw.kind!=DK_DISPATCH_INDIRECT &&
               (resource.usage&(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)))
                return refuse("Native graphics references to writable buffers need in-pass hazard tracking");
            if(constant || storage){
                const auto usage=constant?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                const auto& limits=device->physical_properties.limits;
                const auto alignment=constant?limits.minUniformBufferOffsetAlignment:limits.minStorageBufferOffsetAlignment;
                const auto maximum=constant?limits.maxUniformBufferRange:limits.maxStorageBufferRange;
                if(!resource.buffer || !(resource.usage&usage) || !resource.range || resource.range>maximum || (alignment && resource.offset%alignment))
                    return refuse("Native buffer reference violates descriptor usage, range or alignment");
                value.buffer=VkBuffer(resource.buffer);value.buffer_id=uint32_t(resource.identity>>2);value.offset=resource.offset;value.range=resource.range;
            }else if(sampled || type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE){
                // Retained image references need subresource layout tracking before they can move
                // between calls. File uploads have a fixed published layout and are added separately.
                return refuse("Native application image bindings require tracked subresource layouts");
            }else{
                const auto usage=type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER?VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
                if(!resource.buffer_view || !(resource.usage&usage))return refuse("Native texel reference has incompatible view usage");
                value.buffer_view=VkBufferView(resource.buffer_view);
            }
            }
            set->contents.values[{target.binding,target.element}]=value;
            if(!hold_locked(device,set->contents,set->owners))return refuse("Native descriptor set contains an unretainable application resource");
        }
        // The application may destroy its pipeline-layout handle after recording its bind. Retained
        // set layouts and push ranges reproduce the full compatibility contract for our new bind.
        std::vector<VkDescriptorSetLayout> layouts;
        for(const auto& layout:bound.layout->sets){
            if(!layout || !layout->complete || !layout->lifetime)return refuse("Native pipeline layout cannot be reproduced with retained set layouts");
            layouts.push_back(layout->lifetime->handle);
        }
        VkPipelineLayoutCreateInfo pipeline_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipeline_layout.flags=bound.layout->flags;
        pipeline_layout.setLayoutCount=uint32_t(layouts.size());pipeline_layout.pSetLayouts=layouts.data();
        pipeline_layout.pushConstantRangeCount=uint32_t(bound.layout->pushes.size());pipeline_layout.pPushConstantRanges=bound.layout->pushes.data();
        if(device->d.CreatePipelineLayout(device->handle,&pipeline_layout,nullptr,&set->owned_layout)!=VK_SUCCESS)return refuse("Native compatible pipeline layout allocation failed");
        set->bind.layout_handle=set->owned_layout;
        if(bound.pushed){
            if(!device->d.CmdPushDescriptorSetKHR && !device->d.CmdPushDescriptorSet)return refuse("Native push-descriptor command is unavailable");
            set->bind.pushed=std::make_shared<const DescriptorSetInfo>(set->contents);
        }else{
            std::map<uint32_t,uint32_t> counts;
            for(const auto& binding:set->contents.layout->bindings)if(binding.second.count)counts[binding.second.type]+=binding.second.count;
            std::vector<VkDescriptorPoolSize> sizes;for(const auto& count:counts)sizes.push_back({VkDescriptorType(count.first),count.second});
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=uint32_t(sizes.size());pool.pPoolSizes=sizes.data();
            if(device->d.CreateDescriptorPool(device->handle,&pool,nullptr,&set->pool)!=VK_SUCCESS)return refuse("Native descriptor pool allocation failed");
            const auto layout=set->contents.layout->lifetime->handle;
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=set->pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&layout;
            set->bind.set=VK_NULL_HANDLE;set->bind.id=0;
            if(device->d.AllocateDescriptorSets(device->handle,&allocation,&set->bind.set)!=VK_SUCCESS)return refuse("Native descriptor allocation failed");
            {std::unique_lock<std::shared_mutex> lock(device->res_mx);set->contents.id=device->next_resource_id++;set->contents.pool=set->pool;set->bind.id=set->contents.id;
             device->descriptor_sets[set->bind.set]=set->contents;}
            Writes writes(set->contents,set->bind.set);device->d.UpdateDescriptorSets(device->handle,uint32_t(writes.writes.size()),writes.writes.data(),0,nullptr);
        }
        // Later assignment events are planned against the earlier event's actual descriptor state.
        set->bind.dyn_first=uint32_t(cb->dyn.size());set->bind.dyn_count=uint32_t(set->zero_offsets.size());
        cb->dyn.insert(cb->dyn.end(),set->zero_offsets.begin(),set->zero_offsets.end());
        state[target.set]=set->bind;next.items[i]=std::move(set);
    }
    owner->sets.reserve(owner->sets.size()+plan.items.size());
    next.owner=owner;output=std::move(next);return true;
}
void record_native_descriptor(Device* device,CmdBuf* cb,const DrawRec& draw,const NativeDescriptorPlan& plan,size_t i)
{
    const auto& set=plan.items[i];if(!set)return;
    auto owner=std::static_pointer_cast<NativeDescriptorOwner>(plan.owner);
    owner->used=true;owner->sets.push_back(set);
    for(const auto& value:set->contents.values)if(value.second.native){
        const auto& resource=*std::static_pointer_cast<const Resource>(value.second.native);
        if(resource.publication_semaphore && resource.publication_value){
            auto& serial=owner->publications[VkSemaphore(resource.publication_semaphore)];serial=std::max(serial,resource.publication_value);
        }
    }
    const bool compute=draw.kind==DK_DISPATCH || draw.kind==DK_DISPATCH_INDIRECT;
    const auto point=compute?VK_PIPELINE_BIND_POINT_COMPUTE:VK_PIPELINE_BIND_POINT_GRAPHICS;
    if(set->bind.pushed){
        Writes writes(set->contents,VK_NULL_HANDLE);
        const auto push=device->d.CmdPushDescriptorSetKHR?device->d.CmdPushDescriptorSetKHR:device->d.CmdPushDescriptorSet;
        push(cb->handle,point,set->bind.layout_handle,set->slot,uint32_t(writes.writes.size()),writes.writes.data());
    }else device->d.CmdBindDescriptorSets(cb->handle,point,set->bind.layout_handle,set->slot,1,&set->bind.set,uint32_t(set->zero_offsets.size()),set->zero_offsets.data());
    (compute?cb->comp_sets:cb->sets)[set->slot]=set->bind;
}
}
