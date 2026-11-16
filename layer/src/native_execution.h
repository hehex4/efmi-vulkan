#pragma once
#include "state.h"
#include "native_descriptors.h"
#include "native_targets.h"
#include "../../frontend/assets/file_plan.h"

namespace efvk {
class TextureResidency;
struct NativeGeometryContext;
struct NativeTargetCache;
struct NativeCallCache {
    std::shared_ptr<NativeTargetCache> targets;
    std::mutex mutex;
    std::once_flag loaded;
    PFN_vkCmdPipelineBarrier barrier = nullptr;
    std::unordered_map<uint32_t,std::string> graphics_access; // Pipeline id, empty reason means read-only resources.
    struct File {std::weak_ptr<const efmi::assets::FileResource> declaration;uint32_t id=UINT32_MAX;};
    std::unordered_map<const efmi::assets::FileResource*,File> files;
};
void native_file_frame(NativeCallCache&,TextureResidency*);
// One retained, ordered resource transaction for application, merged, own-mesh and texture draws.
struct NativeDrawTransaction {
    efmi::assets::FilePlan plan;
    NativeTargetPlan targets;
    NativeDescriptorPlan descriptors;
};
bool prepare_native_transaction(NativeCallCache&, TextureResidency*, Device*, CmdBuf*, const DrawRec&,
                                const efmi::assets::FilePlan&, NativeDrawTransaction&, std::string&,
                                NativeTargetCache* = nullptr, bool wait_files = true, bool* pending = nullptr);
void begin_native_transaction(Device*, CmdBuf*, const NativeDrawTransaction&);
void record_native_binding(Device*, CmdBuf*, const DrawRec&, const NativeDrawTransaction&, size_t);
DrawRec native_transaction_draw(const DrawRec&, const NativeDrawTransaction&, size_t before);
// Validate the entire native call plan before recording any work. True consumes the original call.
bool native_calls(NativeCallCache&, TextureResidency*, Device*, CmdBuf*, const DrawRec&, const efmi::assets::FilePlan&, bool hunting_skip, std::string&,
                  bool wait_files = true, bool* pending = nullptr, NativeGeometryContext* = nullptr);
}
