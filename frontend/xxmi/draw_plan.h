#pragma once
#include "CommandList.h"

namespace efmi::frontend {

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
    void adapt(CommandList& root);
    // Serial execution only. Failure preserves output; frontend variable side
    // effects are not rolled back. Callers use a fresh CommandListState.
    bool execute(CommandList& root, CommandListState& state, DrawPlan& output,
                 std::wstring& reason);
private:
    void adapt_tree(CommandList& root, size_t depth);
    std::shared_ptr<Runtime> runtime_;
};

} // namespace efmi::frontend
