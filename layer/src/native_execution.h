#pragma once
#include "state.h"
#include "../../frontend/assets/file_plan.h"

namespace efvk {
struct NativeCallCache {
    std::mutex mutex;
    std::once_flag loaded;
    PFN_vkCmdPipelineBarrier barrier = nullptr;
    std::unordered_map<uint32_t,std::string> graphics_access; // Pipeline id, empty reason means read-only resources.
};
// Validate the entire native call plan before recording any work. True consumes the original call.
bool native_calls(NativeCallCache&, Device*, CmdBuf*, const DrawRec&, const efmi::assets::FilePlan&, bool hunting_skip, std::string&);
}
