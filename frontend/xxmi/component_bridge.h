#pragma once
#include "palette_files.h"

namespace efmi::frontend {
// Evaluated declarations selected by a retained frontend generation.
struct SkeletonSetup {
    ExplicitCommandListSection* initialize=nullptr;
    CommandListVariable* bones=nullptr;
    CommandListVariable* instances=nullptr;
    CustomResourcePool* offsets=nullptr;
    CustomResourcePool* counts=nullptr;
    CustomResourcePool* remaps=nullptr;
    CustomResourcePool* spatial=nullptr;
};
// One verified rolling-buffer instance, with the original 256-byte entry at
// instance zero. Profile resolution supplies the original DX interface slots;
// this bridge neither guesses Vulkan bindings nor reads live application memory.
struct ComponentInput {
    uint32_t frame=0;
    float frame_seconds=0;
    int vs_cb_slot=-1,ps_cb_slot=-1;
    efvk::InstanceEntry entry{};
    efvk::BoneWindow current,previous;
};
class CoreComponentBridge {
public:
    CoreComponentBridge(DrawPlanAdapter&,const FilePlanCompiler&);
    // Install before graph compilation/optimization/adaptation. Missing Core
    // declarations throw; all original globals/lists must outlive this bridge.
    void install();
    static std::vector<std::wstring> boundaries();
    // Generation preparation, outside draw execution: execute original init,
    // copy evaluated metadata and load LOD0/LOD1 remaps through read-only handles.
    bool prepare(const SkeletonSetup&,CommandListState&,std::wstring& reason);
    // Serial CPU planning. Only single-instance rolling-buffer draws are accepted
    // here. Failed GPU/material readiness after this step must retain the imported
    // CPU bones, while a failed original identification invalidates continuity.
    bool plan(CommandList& pre,CommandList& post,CommandListState&,const ComponentInput&,DrawPlan&,std::wstring& reason);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
