#pragma once
#include "state.h"
#include "command_owner.h"
#include "../../frontend/assets/file_plan.h"

namespace efvk {
struct NativeDescriptorSet;
struct NativeDescriptorPlan {
    std::vector<std::shared_ptr<NativeDescriptorSet>> items;
    std::shared_ptr<CommandOwner> owner;
};
bool prepare_native_descriptors(Device*,CmdBuf*,const DrawRec&,const efmi::assets::FilePlan&,NativeDescriptorPlan&,std::string&);
void record_native_descriptor(Device*,CmdBuf*,const DrawRec&,const NativeDescriptorPlan&,size_t);
}
