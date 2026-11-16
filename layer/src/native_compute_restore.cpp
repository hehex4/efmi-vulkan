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
bool prepare_native_compute_restore(Device* device,const CmdBuf* cb,NativeComputeRestore& output,std::string& why)
{
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(!device || !cb || device->passive || !g_cfg.track_resources || !cb->compute_sets_complete)
        return refuse("Compute bindings are not completely tracked");
    if(cb->comp_pipeline && !cb->comp_bound)return refuse("Actual compute pipeline is unknown");
    NativeComputeRestore next;next.pipeline=cb->comp_bound;
    next.bind_pipeline=device->d.CmdBindPipeline;next.bind_sets=device->d.CmdBindDescriptorSets;
    if(!next.bind_pipeline || !next.bind_sets)return refuse("Compute restoration commands are unavailable");
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    std::map<const PipelineLayoutInfo*,VkPipelineLayout> recreated;
    std::vector<std::pair<uint32_t,std::shared_ptr<const PipelineLayoutInfo>>> earlier;
    for(uint32_t slot=0;slot<kMaxSets;++slot){
        const auto& bind=cb->comp_sets[slot];
        if(bind.pushed)return refuse("Compute push descriptor restoration is not implemented");
        if(bind.unsupported && strcmp(bind.unsupported,"disturbed_pipeline_layout"))return refuse("Compute binding has unknown state");
        if(!bind.set){if(bind.id || bind.dyn_count)return refuse("Empty compute set has nonempty binding metadata");continue;}
        if(bind.unsupported)return refuse("Disturbed compute set still has a handle");
        const auto& layout=bind.layout;
        if(!layout || !layout->complete || slot>=layout->sets.size() || !layout->sets[slot] ||
           slot>=layout->prefix.size() || !layout->prefix[slot])return refuse("Compute pipeline layout is incomplete");
        const auto set=device->descriptor_sets.find(bind.set);
        if(!bind.id || set==device->descriptor_sets.end() || set->second.id!=bind.id)
            return refuse("Compute descriptor set was destroyed or recycled");
        const auto& description=set->second;
        if(!description.layout || !description.layout->complete || !description.layout->canonical ||
           description.layout->canonical!=layout->sets[slot]->canonical)
            return refuse("Compute descriptor set layout differs from its binding");
        uint64_t offsets=0;
        for(const auto& item:description.layout->bindings){
            if(item.second.type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC && item.second.type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)continue;
            const auto count=description.counts.find(item.first);
            if(count==description.counts.end())return refuse("Compute dynamic descriptor count is unknown");
            offsets+=count->second;
        }
        if(offsets!=bind.dyn_count || bind.dyn_first>cb->dyn.size() || bind.dyn_count>cb->dyn.size()-bind.dyn_first)
            return refuse("Compute dynamic offsets are incomplete");
        for(const auto& item:earlier)
            if(item.first>=layout->prefix.size() || layout->prefix[item.first]!=item.second->prefix[item.first])
                return refuse("Compute sets cannot be restored without disturbing an earlier binding");
        earlier.emplace_back(slot,layout);
        VkPipelineLayout handle=bind.layout_handle;
        const auto live=device->pipeline_layouts.find(handle);
        if(!handle || live==device->pipeline_layouts.end() || live->second!=layout){
            const auto previous=recreated.find(layout.get());
            if(previous!=recreated.end())handle=previous->second;
            else{
                if(!device->d.CreatePipelineLayout || !device->d.DestroyPipelineLayout)return refuse("Compute layout recreation is unavailable");
                std::vector<VkDescriptorSetLayout> sets;
                for(const auto& entry:layout->sets){
                    if(!entry || !entry->complete || !entry->lifetime || !entry->lifetime->handle)
                        return refuse("Compute layout has no retained descriptor layouts");
                    sets.push_back(entry->lifetime->handle);
                }
                auto owner=std::make_shared<RestoreLayout>();owner->device=device->handle;owner->destroy=device->d.DestroyPipelineLayout;
                VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};info.flags=layout->flags;
                info.setLayoutCount=uint32_t(sets.size());info.pSetLayouts=sets.data();
                info.pushConstantRangeCount=uint32_t(layout->pushes.size());info.pPushConstantRanges=layout->pushes.data();
                if(device->d.CreatePipelineLayout(device->handle,&info,nullptr,&owner->handle)!=VK_SUCCESS)
                    return refuse("Compute restoration pipeline layout allocation failed");
                handle=owner->handle;next.layouts.push_back(owner);recreated.emplace(layout.get(),handle);
            }
        }
        NativeComputeRestore::Set restored;restored.slot=slot;restored.handle=bind.set;restored.layout=handle;
        restored.offsets.assign(cb->dyn.begin()+bind.dyn_first,cb->dyn.begin()+bind.dyn_first+bind.dyn_count);
        next.sets.push_back(std::move(restored));
    }
    output=std::move(next);why.clear();return true;
}
}
