#pragma once
#include "CommandList.h"
#include <functional>
namespace efmi::core {struct PaletteSnapshot;}

namespace efmi::frontend {

// Guard original pool indexing before its numeric casts or lookup. Call with
// the owning generation's globals installed; the original engine owns mapping.
void validate_pool_runtime(CustomResourcePool&);

enum class PlanOperation { BindVertex, BindIndex, BindPixelResource, DrawIndexed, External };

// Resource pointers borrow declarations from a retained frontend generation.
// This CPU plan has no GPU readiness, resource lifetime or pipeline guarantee.
struct PlanItem {
    PlanOperation operation = PlanOperation::External;
    unsigned slot = 0;
    CustomResource* resource = nullptr; // An explicit null binding when absent.
    unsigned index_count = 0, first_index = 0, instance_count = 1, first_instance = 0;
    int vertex_offset = 0;
    std::vector<InputLayoutElementOverride> layout;
    std::wstring external;
    unsigned phase_mask = 0; // External callback: bit 0 pre, bit 1 post.
    std::shared_ptr<const core::PaletteSnapshot> palette; // Optional owned native Core draw data.
};

struct DrawPlan {
    bool skip_original = false;
    std::vector<PlanItem> items;
};

// Mediate backend commands; retain the original XXMI expression, branch and
// named-call implementation. Adapt after upstream flow-control optimisation.
// An external entry stays an explicit unresolved operation, never a no-op.
class DrawPlanAdapter {
public:
    struct Runtime;
    DrawPlanAdapter();
    ~DrawPlanAdapter();
    DrawPlanAdapter(const DrawPlanAdapter&) = delete;
    DrawPlanAdapter& operator=(const DrawPlanAdapter&) = delete;

    // Register native boundaries before upstream optimisation; otherwise an
    // empty native declaration would be optimised out as a no-op.
    void external(ExplicitCommandListSection& section, const std::wstring& name);
    using NativeCommand=std::function<bool(CommandListState&,unsigned,std::wstring&)>;
    void native(ExplicitCommandListSection&,const std::wstring& name,NativeCommand);
    // Keep original list execution, then observe its evaluated CPU state.
    void after(ExplicitCommandListSection&,NativeCommand);
    // Invoke an adapted callback from a native command inside the same bounded
    // transaction. A refused nested call makes that transaction fail even when
    // its caller accidentally ignores the returned false.
    bool invoke(ExplicitCommandListSection&,CommandListState&,unsigned phases,std::wstring& reason);
    using PaletteSource=std::function<bool(CommandListState&,std::shared_ptr<const core::PaletteSnapshot>&,std::wstring&)>;
    void palette_source(PaletteSource);
    void adapt(CommandList& root);
    // Serial execution only. Failure preserves output; frontend variable side
    // effects are not rolled back. Callers use a fresh CommandListState.
    bool execute(CommandList& root, CommandListState& state, DrawPlan& output,
                 std::wstring& reason);
    bool execute_pair(CommandList& pre,CommandList& post,CommandListState&,DrawPlan&,std::wstring& reason);
    // CPU declaration references are snapshots, not D3D resources. The frontend
    // generation must outlive these pointers. Pool resources are supported only
    // for nonexpiring direct-index pools until runtime identity is connected.
    CustomResource* referenced_resource(CustomResource* declaration) const;
    bool has_reference(const CustomResource* declaration) const;
    using ResourceQuery = std::function<bool(ResourceCopyTarget&,CommandListState&,float&,std::wstring&)>;
    // Install before adaptation. Every mediated query must explicitly succeed;
    // refusal never falls through to the reference D3D graphics scaffolding.
    void resource_queries(ResourceQuery);
    using ResourceCommand=std::function<bool(CommandListCommand&,CommandListState&,std::wstring&)>;
    // An opt-in execution backend owns resource state, GPU synchronization and
    // feedback. Without one, the existing immutable file-plan path is retained.
    // Install before adaptation; backend refusal never falls through to D3D.
    void resource_commands(ResourceCommand);
    using ParameterFlush=std::function<bool(CommandListState&,bool force,std::wstring&)>;
    // Root pre/post completion and every injected draw/dispatch publish dirty
    // parameters. Transitions can force publication outside a command root.
    void parameter_flush(ParameterFlush);
    using DrawPrepare=std::function<bool(CommandListState&,std::wstring&)>;
    // Original layout edits occur before parameter publication and argument
    // evaluation, including zero draws and compute dispatch. Hunting skips it.
    void draw_prepare(DrawPrepare);
    bool flush_parameters(CommandListState&,bool force,std::wstring&);
    using ShaderCommand=std::function<bool(RunCustomShaderCommand&,CommandListState&,std::wstring&)>;
    void shader_commands(ShaderCommand);
    // Both nested phases share the caller's abort/parameter state; no root flush.
    bool invoke_shader(CustomShader&,CommandListState&,std::wstring&);
    struct PixelBinding {
        bool application=true;
        CustomResource* resource=nullptr; // Null with application=false means an explicit unbind.
    };
    struct TextureIdentity {bool bound=false;uint32_t hash=0;};
    using TextureQuery=std::function<bool(unsigned,const PixelBinding&,CommandListState&,TextureIdentity&,std::wstring&)>;
    // The provider proves the source-API resource hash (including the original
    // hash policy), not merely a Vulkan object identity or arbitrary byte hash.
    // Unknown data refuses. Hash matching uses original XXMI rules; descriptor
    // fuzzy matching is unsupported and refuses. Install before adaptation.
    void texture_queries(TextureQuery);
    bool has_texture_queries() const;
    // File declarations referenced anywhere in the adapted command graph,
    // including both branches. Unused INI declarations need not exist on disk.
    std::vector<CustomResource*> referenced_files() const;
private:
    bool execute_impl(CommandList& pre,CommandList* post,CommandListState&,DrawPlan&,std::wstring&);
    void adapt_tree(CommandList& root, size_t depth);
    std::shared_ptr<Runtime> runtime_;
};

} // namespace efmi::frontend
