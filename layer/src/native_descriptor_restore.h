// Exact set/layout/dynamic-offset replay shared by both pipeline bind points.
#pragma once
#include "common.h"
#include <memory>
#include <vector>

namespace efvk {
struct Device;
struct SetBind;
struct DescriptorSetInfo;
struct NativePushWrites {
    std::vector<VkWriteDescriptorSet> writes;
};
std::shared_ptr<const NativePushWrites> prepare_native_push_locked(Device*,const DescriptorSetInfo&);
struct NativeDescriptorRestore {
    struct Set {
        uint32_t slot=0;
        VkPipelineLayout layout=VK_NULL_HANDLE;
        VkDescriptorSet handle=VK_NULL_HANDLE;
        std::vector<uint32_t> offsets;
        std::shared_ptr<const NativePushWrites> pushed;
    };
    std::vector<Set> sets;
    std::vector<std::shared_ptr<const void>> layouts;
    PFN_vkCmdBindDescriptorSets bind_sets=nullptr;
    PFN_vkCmdPushDescriptorSetKHR push_sets=nullptr;
    void record_sets(VkCommandBuffer command,VkPipelineBindPoint point) const {
        for(const auto& set:sets){
            if(set.pushed)push_sets(command,point,set.layout,set.slot,uint32_t(set.pushed->writes.size()),set.pushed->writes.data());
            else bind_sets(command,point,set.layout,set.slot,1,&set.handle,uint32_t(set.offsets.size()),set.offsets.data());
        }
    }
};
bool prepare_native_descriptor_restore(Device*,const SetBind*,const std::vector<uint32_t>&,
    NativeDescriptorRestore&,std::string&,bool resource_lock_held=false);
}
