#include "native_compute_restore.h"
#include "state.h"

namespace efvk {
namespace {
struct RestoreLayout {
    VkDevice device=VK_NULL_HANDLE;
    VkPipelineLayout handle=VK_NULL_HANDLE;
    PFN_vkDestroyPipelineLayout destroy=nullptr;
    ~RestoreLayout(){if(handle)destroy(device,handle,nullptr);}
};
}
bool prepare_native_descriptor_restore(Device* device,const SetBind* bindings,const std::vector<uint32_t>& dynamic,
    NativeDescriptorRestore& output,std::string& why,bool resource_lock_held)
{
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(!device || !bindings || device->passive || !g_cfg.track_resources)return refuse("Descriptor bindings are not tracked");
    NativeDescriptorRestore next;next.bind_sets=device->d.CmdBindDescriptorSets;
    next.push_sets=device->d.CmdPushDescriptorSetKHR?device->d.CmdPushDescriptorSetKHR:device->d.CmdPushDescriptorSet;
    if(!next.bind_sets)return refuse("Descriptor restoration command is unavailable");
    std::shared_lock<std::shared_mutex> lock(device->res_mx,std::defer_lock);
    if(!resource_lock_held)lock.lock();
    std::map<const PipelineLayoutInfo*,VkPipelineLayout> recreated;
    std::vector<std::pair<uint32_t,std::shared_ptr<const PipelineLayoutInfo>>> earlier;
    for(uint32_t slot=0;slot<kMaxSets;++slot){
        const auto& bind=bindings[slot];
        if(bind.pushed && (bind.set || bind.id || bind.dyn_count || !next.push_sets))return refuse("Push descriptor binding state or command is unavailable");
        if(bind.unsupported && strcmp(bind.unsupported,"disturbed_pipeline_layout"))return refuse("Descriptor binding has unknown state");
        if(!bind.set && !bind.pushed){if(bind.id || bind.dyn_count)return refuse("Empty descriptor set has nonempty binding metadata");continue;}
        if(bind.unsupported)return refuse("Disturbed descriptor set still has a handle");
        const auto& layout=bind.layout;
        if(!layout || !layout->complete || slot>=layout->sets.size() || !layout->sets[slot] ||
           slot>=layout->prefix.size() || !layout->prefix[slot])return refuse("Descriptor pipeline layout is incomplete");
        const auto set=device->descriptor_sets.find(bind.set);
        if(!bind.pushed && (!bind.id || set==device->descriptor_sets.end() || set->second.id!=bind.id))
            return refuse("Descriptor set was destroyed or recycled");
        const auto& description=bind.pushed?*bind.pushed:set->second;
        if(!description.layout || !description.layout->complete || !description.layout->canonical ||
           description.layout->canonical!=layout->sets[slot]->canonical)
            return refuse("Descriptor set layout differs from its binding");
        uint64_t offsets=0;
        for(const auto& item:description.layout->bindings){
            if(item.second.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC && item.second.type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)continue;
            const auto count=description.counts.find(item.first);
            if(count==description.counts.end())return refuse("Descriptor dynamic descriptor count is unknown");
            offsets+=count->second;
        }
        if(offsets!=bind.dyn_count || bind.dyn_first>dynamic.size() || bind.dyn_count>dynamic.size()-bind.dyn_first)
            return refuse("Descriptor dynamic offsets are incomplete");
        for(const auto& item:earlier)
            if(item.first>=layout->prefix.size() || layout->prefix[item.first]!=item.second->prefix[item.first])
                return refuse("Descriptor sets cannot be restored without disturbing an earlier binding");
        earlier.emplace_back(slot,layout);
        VkPipelineLayout handle=bind.layout_handle;
        const auto live=device->pipeline_layouts.find(handle);
        if(!handle || live==device->pipeline_layouts.end() || live->second!=layout){
            const auto previous=recreated.find(layout.get());
            if(previous!=recreated.end())handle=previous->second;
            else{
                if(!device->d.CreatePipelineLayout || !device->d.DestroyPipelineLayout)return refuse("Descriptor layout recreation is unavailable");
                std::vector<VkDescriptorSetLayout> sets;
                for(const auto& entry:layout->sets){
                    if(!entry || !entry->complete || !entry->lifetime || !entry->lifetime->handle)
                        return refuse("Descriptor layout has no retained descriptor layouts");
                    sets.push_back(entry->lifetime->handle);
                }
                auto owner=std::make_shared<RestoreLayout>();owner->device=device->handle;owner->destroy=device->d.DestroyPipelineLayout;
                VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};info.flags=layout->flags;
                info.setLayoutCount=uint32_t(sets.size());info.pSetLayouts=sets.data();
                info.pushConstantRangeCount=uint32_t(layout->pushes.size());info.pPushConstantRanges=layout->pushes.data();
                if(device->d.CreatePipelineLayout(device->handle,&info,nullptr,&owner->handle)!=VK_SUCCESS)
                    return refuse("Descriptor restoration pipeline layout allocation failed");
                handle=owner->handle;next.layouts.push_back(owner);recreated.emplace(layout.get(),handle);
            }
        }
        NativeDescriptorRestore::Set restored;restored.slot=slot;restored.handle=bind.set;restored.layout=handle;
        if(bind.pushed){
            if(!(description.layout->flags&VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR))return refuse("Pushed set lacks a push descriptor layout");
            restored.pushed=prepare_native_push_locked(device,description);
            if(!restored.pushed)return refuse("Pushed descriptor contents or lifetime could not be restored");
        }
        restored.offsets.assign(dynamic.begin()+bind.dyn_first,dynamic.begin()+bind.dyn_first+bind.dyn_count);
        next.sets.push_back(std::move(restored));
    }
    output=std::move(next);why.clear();return true;
}
bool prepare_native_compute_restore(Device* device,const CmdBuf* cb,NativeComputeRestore& output,std::string& why,bool resource_lock_held){
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(!device || !cb || !cb->compute_sets_complete)return refuse("Compute bindings are not completely tracked");
    if(cb->comp_pipeline && !cb->comp_bound)return refuse("Actual compute pipeline is unknown");
    NativeComputeRestore next;next.pipeline=cb->comp_bound;next.bind_pipeline=device->d.CmdBindPipeline;
    if(!next.bind_pipeline)return refuse("Compute restoration command is unavailable");
    if(!prepare_native_descriptor_restore(device,cb->comp_sets,cb->dyn,next,why,resource_lock_held))return false;
    output=std::move(next);why.clear();return true;
}
}
