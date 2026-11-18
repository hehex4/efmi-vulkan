#pragma once
#include "state.h"
#include "../../frontend/assets/file_plan.h"
#include "../../frontend/xxmi/shader_input_index.h"
#include "variant_cache.h"

namespace efvk {
struct NativeTargetPlan;
// One configuration's immutable semantic index and bounded lookup caches.
struct NativeGeometryContext {
    std::shared_ptr<VariantCache> variants;
    std::shared_ptr<const efmi::frontend::ShaderInputIndex> inputs;
    std::shared_ptr<const efmi::frontend::ShaderModelIndex> models;
    std::shared_ptr<const efmi::frontend::ShaderSourceIndex> sources;
    std::map<std::shared_ptr<const ShaderInfo>,std::shared_ptr<const efmi::frontend::SourceShaderCode>,std::owner_less<std::shared_ptr<const ShaderInfo>>> source_codes;
    size_t source_bytes=0;
    std::map<std::shared_ptr<const ShaderInfo>,std::string,std::owner_less<std::shared_ptr<const ShaderInfo>>> source_models;
    std::mutex mutex;
    struct Profile {std::shared_ptr<const efmi::frontend::VertexProfile> value;std::string reason;};
    std::map<std::shared_ptr<const ShaderInfo>,Profile,std::owner_less<std::shared_ptr<const ShaderInfo>>> profiles;
    std::map<std::shared_ptr<const GraphicsRecipe>,uint64_t,std::owner_less<std::shared_ptr<const GraphicsRecipe>>> identities;
};
struct NativeGeometryPlan {
    std::vector<std::shared_ptr<const NativeGeometryState>> bindings;
    std::vector<VkDeviceSize> binding_sizes;
    struct Draw {std::shared_ptr<const PipelineVariant> variant;VkPipeline pipeline=VK_NULL_HANDLE;};
    std::vector<Draw> draws; // Last cell is the implicit application call.
};
bool prepare_native_geometry(Device*, CmdBuf*, const DrawRec&, const efmi::assets::FilePlan&,
                             NativeGeometryPlan&, std::string&, NativeGeometryContext* = nullptr,
                             const NativeTargetPlan* = nullptr, bool* pending = nullptr);
void record_native_geometry(Device*, CmdBuf*, const efmi::assets::FilePlan&, const NativeGeometryPlan&, size_t);
void bind_native_geometry_pipeline(Device*,CmdBuf*,const NativeGeometryPlan&,size_t);
}
