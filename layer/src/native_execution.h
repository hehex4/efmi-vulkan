#pragma once
#include "state.h"
#include "../../frontend/assets/file_plan.h"

namespace efvk {
class TextureResidency;
struct NativeCallCache {
    std::mutex mutex;
    std::once_flag loaded;
    PFN_vkCmdPipelineBarrier barrier = nullptr;
    std::unordered_map<uint32_t,std::string> graphics_access; // Pipeline id, empty reason means read-only resources.
    struct File {std::weak_ptr<const efmi::assets::FileResource> declaration;uint32_t id=UINT32_MAX;};
    std::unordered_map<const efmi::assets::FileResource*,File> files;
};
void native_file_frame(NativeCallCache&,TextureResidency*);
// Validate the entire native call plan before recording any work. True consumes the original call.
bool native_calls(NativeCallCache&, TextureResidency*, Device*, CmdBuf*, const DrawRec&, const efmi::assets::FilePlan&, bool hunting_skip, std::string&);
}
