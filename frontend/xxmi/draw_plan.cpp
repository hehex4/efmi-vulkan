#include "draw_plan.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <typeinfo>

namespace efmi::frontend {
namespace {
struct Refused : std::exception {
    std::wstring reason;
    explicit Refused(std::wstring text) : reason(std::move(text)) {}
};
[[noreturn]] void refuse(const std::wstring& text) { throw Refused{text}; }
constexpr size_t max_steps = 65536, max_items = 4096, max_draws = 256;

// Inspect the original expression tree without reimplementing its evaluator.
// Unadapted resource/window queries must not reach the reference D3D context.
bool cpu_expression(const std::shared_ptr<CommandListEvaluatable>& node, size_t depth = 0)
{
    if (!node || depth > 256) return false;
    if (const auto* operand = dynamic_cast<CommandListOperand*>(node.get())) {
        switch (operand->type) {
        case ParamOverrideType::VALUE: return true;
        case ParamOverrideType::VARIABLE: return operand->var_ftarget != nullptr;
        case ParamOverrideType::VERTEX_COUNT: case ParamOverrideType::INDEX_COUNT:
        case ParamOverrideType::INSTANCE_COUNT: case ParamOverrideType::FIRST_VERTEX:
        case ParamOverrideType::FIRST_INDEX: case ParamOverrideType::FIRST_INSTANCE:
        case ParamOverrideType::DRAW_TYPE: case ParamOverrideType::FRAME_NUMBER:
        case ParamOverrideType::DRAW_NUMBER: case ParamOverrideType::DISPATCH_NUMBER:
            return true;
        default: return false;
        }
    }
    if (const auto* operation = dynamic_cast<CommandListOperator*>(node.get()))
        return (!operation->lhs || cpu_expression(operation->lhs, depth + 1)) &&
               (!operation->rhs || cpu_expression(operation->rhs, depth + 1));
    return false;
}

bool cpu_arguments(CommandListCommand& command)
{
    if (auto* draw = dynamic_cast<DrawCommand*>(&command)) {
        const int count = draw->type == DrawCommandType::DRAW_INDEXED ? 3 :
                          draw->type == DrawCommandType::DRAW_INDEXED_INSTANCED ? 5 : 0;
        for (int i = 0; i < count; ++i) if (!cpu_expression(draw->args[i].evaluatable)) return false;
    } else if (auto* branch = dynamic_cast<IfCommand*>(&command)) {
        return cpu_expression(branch->expression.evaluatable);
    } else if (auto* assignment = dynamic_cast<AssignmentCommand*>(&command)) {
        return cpu_expression(assignment->expression.evaluatable);
    } else if (auto* binding = dynamic_cast<ResourceCopyOperation*>(&command)) {
        return !binding->dst.slot_expression || cpu_expression(binding->dst.slot_expression->evaluatable);
    } else if (auto* layout = dynamic_cast<LayoutElementOperation*>(&command)) {
        return layout->dst.member_args[1].expression &&
               cpu_expression(layout->dst.member_args[1].expression->evaluatable);
    }
    return true;
}

int integer(CommandListExpression& expression, CommandListState* state)
{
    const double value = expression.evaluate(state);
    if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max())
        refuse(L"Draw argument is outside the signed integer range");
    return static_cast<int>(value); // XXMI truncates towards zero.
}
unsigned positive(CommandListExpression& expression, CommandListState* state)
{
    const int value = integer(expression, state);
    if (value < 0) refuse(L"Negative unsigned draw argument");
    return static_cast<unsigned>(value);
}
}

struct DrawPlanAdapter::Runtime {
    DrawPlan* active = nullptr;
    size_t steps = 0, draws = 0;
    std::unordered_set<CommandList*> adapted;
    std::unordered_map<ExplicitCommandListSection*, std::wstring> externals;

    void append(PlanItem item)
    {
        if (!active) refuse(L"Planner command invoked outside a transaction");
        if (active->items.size() >= max_items) refuse(L"Plan operation budget exceeded");
        active->items.push_back(std::move(item));
    }

    void run(const std::shared_ptr<CommandListCommand>& original, CommandListState* state)
    {
        if (!active) refuse(L"Planner command invoked outside a transaction");
        if (++steps > max_steps) refuse(L"Plan execution budget exceeded");
        if (state->recursion >= MAX_COMMAND_LIST_RECURSION)
            refuse(L"Plan recursion budget exceeded");
        if (auto* draw = dynamic_cast<DrawCommand*>(original.get())) {
            if (state->call_info && state->call_info->hunting_skip) return;
            PlanItem item; item.operation = PlanOperation::DrawIndexed;
            if (draw->type == DrawCommandType::DRAW_INDEXED) {
                item.index_count = positive(draw->args[0], state);
                item.first_index = positive(draw->args[1], state);
                item.vertex_offset = integer(draw->args[2], state);
            } else if (draw->type == DrawCommandType::DRAW_INDEXED_INSTANCED) {
                item.index_count = positive(draw->args[0], state);
                item.instance_count = positive(draw->args[1], state);
                item.first_index = positive(draw->args[2], state);
                item.vertex_offset = integer(draw->args[3], state);
                item.first_instance = positive(draw->args[4], state);
            } else {
                refuse(L"Planner supports direct indexed draws only: " + original->ini_line);
            }
            if (!item.index_count || !item.instance_count) return;
            if (++draws > max_draws) refuse(L"Plan draw budget exceeded");
            for (const auto* layout : state->input_layout_overrides) {
                if (!layout || item.layout.size() >= 256) refuse(L"Invalid layout operation list");
                item.layout.push_back(*layout);
            }
            append(std::move(item));
            return;
        }
        if (auto* binding = dynamic_cast<ResourceCopyOperation*>(original.get())) {
            auto& src = binding->src; auto& dst = binding->dst;
            if (typeid(*binding) != typeid(ResourceCopyOperation) ||
                src.evaluation_mode != ResourceCopyTargetEvaluationMode::RESOURCE ||
                dst.evaluation_mode != ResourceCopyTargetEvaluationMode::RESOURCE)
                refuse(L"Unsupported resource operation: " + original->ini_line);
            PlanItem item;
            if (dst.type == ResourceCopyTargetType::VERTEX_BUFFER)
                item.operation = PlanOperation::BindVertex;
            else if (dst.type == ResourceCopyTargetType::INDEX_BUFFER)
                item.operation = PlanOperation::BindIndex;
            else if (dst.type == ResourceCopyTargetType::SHADER_RESOURCE && dst.shader_type == L'p')
                item.operation = PlanOperation::BindPixelResource;
            else refuse(L"Unsupported binding destination: " + original->ini_line);
            item.slot = dst.ResolveSlot(state);
            const unsigned limit = item.operation == PlanOperation::BindVertex ? 32 :
                                   item.operation == PlanOperation::BindIndex ? 1 : 128;
            if (item.slot >= limit) refuse(L"Binding slot out of range: " + original->ini_line);
            if (src.type == ResourceCopyTargetType::CUSTOM_RESOURCE &&
                binding->options == ResourceCopyOptions::REFERENCE) {
                item.resource = src.GetCustomResource(state);
                if (!item.resource) refuse(L"Missing resource declaration: " + original->ini_line);
            } else if (src.type != ResourceCopyTargetType::EMPTY ||
                       (binding->options != ResourceCopyOptions::REFERENCE &&
                        binding->options != ResourceCopyOptions::COPY)) {
                refuse(L"Unsupported resource source or copy policy: " + original->ini_line);
            }
            append(std::move(item));
            return;
        }
        if (auto* batch = dynamic_cast<ShaderResourceBindBatch*>(original.get())) {
            // Upstream batches adjacent reference operations after parsing.
            for (auto& operation : batch->operations) {
                if (!operation || !cpu_arguments(*operation)) refuse(L"Unadapted batched binding query");
                run(operation, state);
            }
            return;
        }
        if (auto* call = dynamic_cast<RunExplicitCommandList*>(original.get())) {
            auto found = externals.find(call->command_list_section);
            if (found != externals.end()) {
                PlanItem item; item.operation = PlanOperation::External; item.external = found->second;
                item.phase_mask = call->run_pre_and_post_together ? 3 : state->post ? 2 : 1;
                append(std::move(item));
                return;
            }
            if (!call->command_list_section ||
                !adapted.count(call->command_list_section->command_list.ResolveCommandList()) ||
                !adapted.count(call->command_list_section->post_command_list.ResolveCommandList()))
                refuse(L"Call target was not adapted: " + original->ini_line);
            original->run(state);
            return;
        }
        if (dynamic_cast<SkipCommand*>(original.get())) {
            active->skip_original = true; // Do not mutate the live caller before GPU readiness.
            return;
        }
        if (dynamic_cast<IfCommand*>(original.get()) ||
            typeid(*original) == typeid(VariableAssignment)) {
            original->run(state);
            return;
        }
        if (auto* layout = dynamic_cast<LayoutElementOperation*>(original.get())) {
            if (state->input_layout_overrides.size() >= 256) refuse(L"Layout operation budget exceeded");
            layout->run(state);
            return;
        }
        // Never forward arbitrary commands to the reference's D3D scaffolding.
        refuse(L"Unsupported frontend operation: " + original->ini_line);
    }
};

namespace {
class BridgeCommand final : public CommandListCommand {
public:
    std::shared_ptr<CommandListCommand> original;
    std::shared_ptr<DrawPlanAdapter::Runtime> runtime;
    bool arguments_supported;
    BridgeCommand(std::shared_ptr<CommandListCommand> command,
                  std::shared_ptr<DrawPlanAdapter::Runtime> owner)
        : original(std::move(command)), runtime(std::move(owner)),
          arguments_supported(cpu_arguments(*original)) { ini_line = original->ini_line; }
    void run(CommandListState* state) override {
        if (!arguments_supported) refuse(L"Unadapted expression query: " + ini_line);
        runtime->run(original, state);
    }
};
}

DrawPlanAdapter::DrawPlanAdapter() : runtime_(std::make_shared<Runtime>()) {}
DrawPlanAdapter::~DrawPlanAdapter() = default;

void DrawPlanAdapter::external(ExplicitCommandListSection& section, const std::wstring& name)
{
    runtime_->externals.emplace(&section, name);
    section.command_list.runtime_populated = section.post_command_list.runtime_populated = true;
}

void DrawPlanAdapter::adapt(CommandList& root)
{
    adapt_tree(root, 0);
}

void DrawPlanAdapter::adapt_tree(CommandList& root, size_t depth)
{
    if (depth > MAX_COMMAND_LIST_RECURSION || runtime_->adapted.size() >= max_steps)
        refuse(L"Command graph adaptation budget exceeded");
    if (!runtime_->adapted.insert(&root).second) return;
    for (auto& command : root.commands) {
        if (dynamic_cast<BridgeCommand*>(command.get()))
            refuse(L"A command list already belongs to another planner");
        if (auto* branch = dynamic_cast<IfCommand*>(command.get())) {
            for (auto* list : {branch->true_commands_pre.get(), branch->true_commands_post.get(),
                               branch->false_commands_pre.get(), branch->false_commands_post.get()})
                if (list) adapt_tree(*list, depth + 1);
        } else if (auto* call = dynamic_cast<RunExplicitCommandList*>(command.get())) {
            if (call->command_list_section && !runtime_->externals.count(call->command_list_section)) {
                adapt_tree(call->command_list_section->command_list, depth + 1);
                adapt_tree(call->command_list_section->post_command_list, depth + 1);
            }
        }
        command = std::make_shared<BridgeCommand>(command, runtime_);
    }
}

bool DrawPlanAdapter::execute(CommandList& root, CommandListState& state,
                              DrawPlan& output, std::wstring& reason)
{
    reason.clear();
    auto* resolved = root.ResolveCommandList();
    if (runtime_->active || !runtime_->adapted.count(resolved) || !state.mHackerContext ||
        state.aborted || state.recursion) {
        reason = L"Planner requires an adapted root and a fresh serial execution state";
        return false;
    }
    DrawPlan candidate;
    auto saved_layout = state.input_layout_overrides;
    runtime_->active = &candidate; runtime_->steps = runtime_->draws = 0;
    try {
        for (auto& command : resolved->commands) command->run(&state);
        output = std::move(candidate);
    } catch (const Refused& error) {
        reason = error.reason;
    } catch (const std::exception&) {
        reason = L"Frontend raised an exception while planning";
    }
    runtime_->active = nullptr;
    state.input_layout_overrides = std::move(saved_layout);
    // The state is throwaway, but leave nesting counters usable for diagnostics.
    state.recursion = state.extra_indent = 0;
    return reason.empty();
}

} // namespace efmi::frontend
