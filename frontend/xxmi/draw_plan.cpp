#include "draw_plan.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <typeinfo>
#include <climits>
#include <array>
#include "ResourceHash.h"
#include "globals.h"

void FrontendRunCommandList(CommandList&,CommandListState&);

namespace efmi::frontend {
namespace {
struct Refused : std::exception {
    std::wstring reason;
    explicit Refused(std::wstring text) : reason(std::move(text)) {}
};
[[noreturn]] void refuse(const std::wstring& text) { throw Refused{text}; }
constexpr size_t max_steps = 65536, max_items = 4096, max_draws = 256;
constexpr size_t max_active_depth = 64;
struct QueryService {DrawPlanAdapter::ResourceQuery read,texture;size_t count=0;};
class NativeResourceQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListOperand> source;
    std::shared_ptr<QueryService> service;
    NativeResourceQuery(std::shared_ptr<CommandListOperand> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* =nullptr) override {
        float result=0;std::wstring reason;
        if(!state || ++service->count>max_steps)refuse(L"Resource query adapter is unavailable or over budget");
        auto& target=source->texture_filter_target;
        const bool pixel=target.type==ResourceCopyTargetType::THIS_RESOURCE ||
            (target.type==ResourceCopyTargetType::SHADER_RESOURCE && target.shader_type==L'p');
        const auto& read=pixel && target.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE && service->texture?service->texture:service->read;
        if(!read)refuse(L"Resource query adapter is unavailable");
        if(!read(target,*state,result,reason))
            refuse(reason.empty()?L"Native resource query refused":reason);
        return result;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
class PoolIndexGuard final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListEvaluatable> source;
    CustomResourcePool* pool;
    PoolIndexGuard(std::shared_ptr<CommandListEvaluatable> value,CustomResourcePool* owner):source(std::move(value)),pool(owner){}
    float evaluate(CommandListState* state,HackerDevice* device=nullptr) override {
        auto* resolved=pool->ResolvePool();
        try{validate_pool_runtime(*resolved);}
        catch(const std::exception& e){const std::string text=e.what();refuse(std::wstring(text.begin(),text.end()));}
        const float value=source->evaluate(state,device);
        if((resolved->index_type==PoolIndexType::RING || resolved->index_type==PoolIndexType::STATIC) &&
           (!std::isfinite(value) || double(value)<INT_MIN || double(value)>INT_MAX))refuse(L"Direct pool index outside signed integer range");
        return value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
class RangeIndexGuard final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListEvaluatable> source;
    explicit RangeIndexGuard(std::shared_ptr<CommandListEvaluatable> value):source(std::move(value)){}
    float evaluate(CommandListState* state,HackerDevice* device=nullptr) override {
        const auto value=source->evaluate(state,device);
        if(!std::isfinite(value) || double(value)<INT_MIN || double(value)>INT_MAX)throw std::runtime_error("Resource range bound outside signed integer range");
        return value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
bool cpu_expression(const std::shared_ptr<CommandListEvaluatable>&,size_t);
bool cpu_target(const ResourceCopyTarget& target,size_t depth=0)
{
    if(!(depth<=256 && (!target.custom_resource_pool || target.custom_resource_pool->GetPoolSize()<=max_steps) &&
        (!target.pool_dynamic_index_expression || cpu_expression(target.pool_dynamic_index_expression->evaluatable,depth+1)) &&
        (!target.slot_expression || cpu_expression(target.slot_expression->evaluatable,depth+1)) &&
        (!target.range_start || cpu_expression(target.range_start->evaluatable,depth+1)) &&
        (!target.range_end || cpu_expression(target.range_end->evaluatable,depth+1))))return false;
    for(const auto& arg:target.member_args)if(arg.expression && !cpu_expression(arg.expression->evaluatable,depth+1))return false;
    return true;
}

// Inspect the original expression tree without reimplementing its evaluator.
// Unadapted resource/window queries must not reach the reference D3D context.
bool cpu_expression(const std::shared_ptr<CommandListEvaluatable>& node, size_t depth = 0)
{
    if (!node || depth > 256) return false;
    if(const auto* query=dynamic_cast<NativeResourceQuery*>(node.get()))return cpu_target(query->source->texture_filter_target,depth+1);
    if(const auto* guard=dynamic_cast<PoolIndexGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if(const auto* guard=dynamic_cast<RangeIndexGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if (const auto* operand = dynamic_cast<CommandListOperand*>(node.get())) {
        switch (operand->type) {
        case ParamOverrideType::VALUE: return true;
        case ParamOverrideType::INI_PARAM:
            return G && operand->param_idx >= 0 && size_t(operand->param_idx) < G->iniParams.size();
        case ParamOverrideType::VARIABLE: return operand->var_ftarget != nullptr;
        case ParamOverrideType::TEXTURE: {
            const auto& target=operand->texture_filter_target;
            if(!target.custom_resource_pool || !cpu_target(target,depth+1))return false;
            switch(target.evaluation_mode){
            case ResourceCopyTargetEvaluationMode::VARIABLE:
            case ResourceCopyTargetEvaluationMode::POOL_SIZE:
            case ResourceCopyTargetEvaluationMode::POOL_INDEX:
            case ResourceCopyTargetEvaluationMode::POOL_IDENTITY:return true;
            default:return false;
            }
        }
        case ParamOverrideType::VERTEX_COUNT: case ParamOverrideType::INDEX_COUNT:
        case ParamOverrideType::INSTANCE_COUNT: case ParamOverrideType::FIRST_VERTEX:
        case ParamOverrideType::FIRST_INDEX: case ParamOverrideType::FIRST_INSTANCE:
        case ParamOverrideType::DRAW_TYPE: case ParamOverrideType::FRAME_NUMBER: case ParamOverrideType::FRAME_TIME:
        case ParamOverrideType::DRAW_NUMBER: case ParamOverrideType::DISPATCH_NUMBER:
        case ParamOverrideType::TIME:
        case ParamOverrideType::THREAD_GROUP_COUNT_X: case ParamOverrideType::THREAD_GROUP_COUNT_Y:
        case ParamOverrideType::THREAD_GROUP_COUNT_Z: case ParamOverrideType::INDIRECT_OFFSET:
            return true;
        default: return false;
        }
    }
    if (const auto* operation = dynamic_cast<CommandListOperator*>(node.get()))
        return (!operation->lhs || cpu_expression(operation->lhs, depth + 1)) &&
               (!operation->rhs || cpu_expression(operation->rhs, depth + 1));
    return false;
}

void guard_expression(std::shared_ptr<CommandListEvaluatable>&,const std::shared_ptr<QueryService>&,size_t=0);
void guard_target(ResourceCopyTarget& target,const std::shared_ptr<QueryService>& queries,size_t depth=0)
{
    if(depth>256)refuse(L"Pool expression nesting budget exceeded");
    if(target.pool_dynamic_index_expression){
        auto& expression=target.pool_dynamic_index_expression->evaluatable;guard_expression(expression,queries,depth+1);
        if(target.custom_resource_pool && !dynamic_cast<PoolIndexGuard*>(expression.get()))
            expression=std::make_shared<PoolIndexGuard>(expression,target.custom_resource_pool);
    }
    if(target.slot_expression)guard_expression(target.slot_expression->evaluatable,queries,depth+1);
    for(auto* bound:{target.range_start.get(),target.range_end.get()})if(bound){
        guard_expression(bound->evaluatable,queries,depth+1);
        if(!dynamic_cast<RangeIndexGuard*>(bound->evaluatable.get()))bound->evaluatable=std::make_shared<RangeIndexGuard>(bound->evaluatable);
    }
    for(auto& arg:target.member_args)if(arg.expression)guard_expression(arg.expression->evaluatable,queries,depth+1);
}
void guard_expression(std::shared_ptr<CommandListEvaluatable>& node,const std::shared_ptr<QueryService>& queries,size_t depth)
{
    if(depth>256)refuse(L"Pool expression nesting budget exceeded");
    if(auto operand=std::dynamic_pointer_cast<CommandListOperand>(node)){
        guard_target(operand->texture_filter_target,queries,depth+1);
        if(operand->type==ParamOverrideType::TEXTURE && !cpu_expression(operand,depth+1) && (queries->read || queries->texture))
            node=std::make_shared<NativeResourceQuery>(std::move(operand),queries);
    }
    else if(auto* op=dynamic_cast<CommandListOperator*>(node.get())){
        if(op->lhs)guard_expression(op->lhs,queries,depth+1);if(op->rhs)guard_expression(op->rhs,queries,depth+1);
    }
}
int draw_argument_count(DrawCommandType type){
    switch(type){
    case DrawCommandType::DRAW:return 2;
    case DrawCommandType::DRAW_INDEXED:case DrawCommandType::DISPATCH:return 3;
    case DrawCommandType::DRAW_INSTANCED:return 4;
    case DrawCommandType::DRAW_INDEXED_INSTANCED:return 5;
    case DrawCommandType::DRAW_INDEXED_INSTANCED_INDIRECT:case DrawCommandType::DRAW_INSTANCED_INDIRECT:
    case DrawCommandType::DISPATCH_INDIRECT:return 1;
    default:return 0;
    }
}
bool indirect_command(DrawCommandType type){return type==DrawCommandType::DRAW_INDEXED_INSTANCED_INDIRECT ||
    type==DrawCommandType::DRAW_INSTANCED_INDIRECT || type==DrawCommandType::DISPATCH_INDIRECT;}
void guard_arguments(CommandListCommand& command,const std::shared_ptr<QueryService>& queries)
{
    if(auto* draw=dynamic_cast<DrawCommand*>(&command)){
        const int count=draw_argument_count(draw->type);
        for(int i=0;i<count;++i)guard_expression(draw->args[i].evaluatable,queries);
        if(indirect_command(draw->type))guard_target(draw->indirect_buffer,queries);
    }else if(auto* branch=dynamic_cast<IfCommand*>(&command))guard_expression(branch->expression.evaluatable,queries);
    else if(auto* assignment=dynamic_cast<AssignmentCommand*>(&command)){
        guard_expression(assignment->expression.evaluatable,queries);
        if(auto* pool=dynamic_cast<PoolVariableOperation*>(assignment))guard_target(pool->dst,queries);
    }else if(auto* binding=dynamic_cast<ResourceCopyOperation*>(&command)){guard_target(binding->src,queries);guard_target(binding->dst,queries);}
    else if(auto* range=dynamic_cast<SlotRangeCopyOperation*>(&command)){guard_target(range->src,queries);guard_target(range->dst,queries);}
    else if(auto* layout=dynamic_cast<LayoutElementOperation*>(&command)){
        if(layout->dst.member_args[1].expression)guard_expression(layout->dst.member_args[1].expression->evaluatable,queries);
    }else if(auto* clear=dynamic_cast<ClearViewCommand*>(&command))guard_target(clear->target,queries);
    else if(auto* store=dynamic_cast<StoreCommand*>(&command)){
        guard_target(store->src,queries);guard_target(store->pool_var,queries);
        if(store->offset_expression)guard_expression(store->offset_expression->evaluatable,queries);
    }else if(auto* batch=dynamic_cast<ShaderResourceBatch*>(&command)){
        for(auto& binding:batch->operations)if(binding)guard_arguments(*binding,queries);
    }
    else if(auto* check=dynamic_cast<CheckTextureOverrideCommand*>(&command))guard_target(check->target,queries);
}

bool cpu_arguments(CommandListCommand& command)
{
    if (auto* draw = dynamic_cast<DrawCommand*>(&command)) {
        const int count=draw_argument_count(draw->type);
        for (int i = 0; i < count; ++i) if (!cpu_expression(draw->args[i].evaluatable)) return false;
        if(indirect_command(draw->type) && !cpu_target(draw->indirect_buffer))return false;
    } else if (auto* branch = dynamic_cast<IfCommand*>(&command)) {
        return cpu_expression(branch->expression.evaluatable);
    } else if (auto* assignment = dynamic_cast<AssignmentCommand*>(&command)) {
        const auto* pool=dynamic_cast<PoolVariableOperation*>(assignment);
        return cpu_expression(assignment->expression.evaluatable) && (!pool || cpu_target(pool->dst));
    } else if (auto* binding = dynamic_cast<ResourceCopyOperation*>(&command)) {
        return cpu_target(binding->src) && cpu_target(binding->dst);
    } else if(auto* range=dynamic_cast<SlotRangeCopyOperation*>(&command)) {
        return cpu_target(range->src) && cpu_target(range->dst);
    } else if(auto* clear=dynamic_cast<ClearViewCommand*>(&command)) {
        return cpu_target(clear->target);
    } else if(auto* store=dynamic_cast<StoreCommand*>(&command)) {
        return cpu_target(store->src) && cpu_target(store->pool_var) && store->offset_expression &&
               cpu_expression(store->offset_expression->evaluatable);
    } else if (auto* layout = dynamic_cast<LayoutElementOperation*>(&command)) {
        return layout->dst.member_args[1].expression &&
               cpu_expression(layout->dst.member_args[1].expression->evaluatable);
    } else if(auto* check=dynamic_cast<CheckTextureOverrideCommand*>(&command)) {
        return cpu_target(check->target);
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

void validate_pool_runtime(CustomResourcePool& input)
{
    auto* pool=input.ResolvePool();
    if(!pool->GetPoolSize() || pool->GetPoolSize()>max_steps)throw std::runtime_error("Pool size outside planner budget");
    if(pool->index_type!=PoolIndexType::RING && pool->index_type!=PoolIndexType::STATIC &&
       pool->index_type!=PoolIndexType::FIFO && pool->index_type!=PoolIndexType::SPATIAL)
        throw std::runtime_error("Original pool index type is invalid");
    if(pool->index_type==PoolIndexType::SPATIAL){
        if(!G || !std::isfinite(G->gFrameTime) || G->gFrameTime<0)throw std::runtime_error("Spatial pool frame duration is invalid");
        const float scaled=std::ceil(pool->spatial_radius*(G->gFrameTime*120.0f));
        if(!std::isfinite(scaled) || scaled<0 || double(scaled)>UINT32_MAX)
            throw std::runtime_error("Spatial pool radius outside unsigned integer range");
    }
}

struct DrawPlanAdapter::Runtime {
    DrawPlan* active = nullptr;
    size_t steps = 0, draws = 0;
    std::unordered_set<CommandList*> adapted;
    struct External {std::wstring name;DrawPlanAdapter::NativeCommand command;};
    std::unordered_map<ExplicitCommandListSection*,External> externals;
    std::unordered_map<ExplicitCommandListSection*,DrawPlanAdapter::NativeCommand> observers;
    std::unordered_map<CustomResource*,CustomResource*> references;
    std::shared_ptr<QueryService> queries=std::make_shared<QueryService>();
    DrawPlanAdapter::PaletteSource palette_source;
    DrawPlanAdapter::ResourceCommand resource_command;
    DrawPlanAdapter::ParameterFlush parameter_flush;
    DrawPlanAdapter::ShaderCommand shader_command;
    bool nested_failed=false;
    size_t active_depth=0;
    DrawPlanAdapter::TextureQuery texture_query;
    std::array<DrawPlanAdapter::PixelBinding,128> pixel_bindings;
    std::unordered_set<CustomResource*> files;

    void flush(CommandListState& state,bool force=false){
        if(!parameter_flush || (!force && !state.update_params))return;
        std::wstring why;
        if(!parameter_flush(state,force,why))refuse(why.empty()?L"GPU parameter publication refused":why);
    }

    void collect_files(CommandListCommand& command)
    {
        if (auto* copy = dynamic_cast<ResourceCopyOperation*>(&command)) {
            if (copy->src.type == ResourceCopyTargetType::CUSTOM_RESOURCE && !copy->src.custom_resource_pool) {
                auto* resource = copy->src.GetCustomResource(nullptr);
                if (resource && !resource->filename.empty()) files.insert(resource);
            }
        } else if (auto* batch = dynamic_cast<ShaderResourceBindBatch*>(&command)) {
            for (const auto& copy : batch->operations) if (copy) collect_files(*copy);
        }
    }

    ResourceCopyTarget& effective_target(ResourceCopyTarget& target,CommandListState& state)
    {
        if(target.type!=ResourceCopyTargetType::THIS_RESOURCE)return target;
        if(!state.this_target || state.this_target->type==ResourceCopyTargetType::THIS_RESOURCE)
            refuse(L"Texture this target is missing or cyclic");
        return *state.this_target;
    }
    void texture_matches(ResourceCopyTarget& target,CommandListState& state,bool& bound,TextureOverrideMatches& matches)
    {
        auto& resolved=effective_target(target,state);
        if(!texture_query || resolved.type!=ResourceCopyTargetType::SHADER_RESOURCE || resolved.shader_type!=L'p' ||
           resolved.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE || resolved.IsRange())
            refuse(L"Texture query requires an adapted single pixel slot");
        const auto slot=resolved.ResolveSlot(&state);
        if(slot>=pixel_bindings.size())refuse(L"Texture query slot outside supported range");
        DrawPlanAdapter::TextureIdentity identity;
        const auto& binding=pixel_bindings[slot];
        if(binding.application || binding.resource){
            std::wstring why;
            if(!texture_query(slot,binding,state,identity,why))refuse(why.empty()?L"Texture source identity is unknown":why);
        }
        bound=identity.bound;matches.clear();
        if(!bound)return;
        if(!G->mFuzzyTextureOverrides.empty())refuse(L"Texture descriptor fuzzy matching has no adapter");
        find_texture_override_for_hash(identity.hash,&matches,state.call_info);
        if(matches.size()>max_items)refuse(L"Texture match count exceeds planner budget");
        for(auto* match:matches)if(!match || !adapted.count(match->command_list.ResolveCommandList()) ||
                                   !adapted.count(match->post_command_list.ResolveCommandList()))
            refuse(L"Matched texture command lists were not adapted");
    }
    void check_texture(CheckTextureOverrideCommand& check,CommandListState& state)
    {
        auto& target=effective_target(check.target,state);
        if(target.type!=ResourceCopyTargetType::SHADER_RESOURCE || target.shader_type!=L'p' ||
           (target.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE &&
            target.evaluation_mode!=ResourceCopyTargetEvaluationMode::SLOT_RANGE))
            refuse(L"Texture override only supports pixel resource slots");
        int first=0;unsigned count=1;
        if(target.IsRange()){
            if(!target.ResolveRange(&state,&first,&count))refuse(L"Texture slot range cannot resolve");
        }else first=static_cast<int>(target.ResolveSlot(&state));
        if(first<0 || first>=128 || count>128-unsigned(first))refuse(L"Texture slot range outside supported bounds");
        auto* saved_this=state.this_target;const auto saved_post=state.post;
        struct Restore {CommandListState& state;ResourceCopyTarget* target;bool post;~Restore(){state.this_target=target;state.post=post;}} restore{state,saved_this,saved_post};
        for(unsigned i=0;i<count;++i){
            ResourceCopyTarget checked;checked.type=ResourceCopyTargetType::SHADER_RESOURCE;checked.shader_type=L'p';checked.slot=unsigned(first)+i;
            state.this_target=&checked;bool bound=false;TextureOverrideMatches matches;texture_matches(checked,state,bound,matches);
            auto run=[&](bool post){state.post=post;for(auto* match:matches){
                auto& list=post?match->post_command_list:match->command_list;
                if(!adapted.count(list.ResolveCommandList()))refuse(L"Texture callback alias target was not adapted");
                FrontendRunCommandList(list,state);
            }};
            if(check.run_pre_and_post_together){run(false);run(true);}else run(saved_post);
        }
    }

    CustomResource* referenced(CustomResource* resource) const
    {
        const auto found=references.find(resource);
        // The original pool engine marks only the affected resource null when
        // a slot changes type or a resource reset runs. Preserve references in
        // other slots and the independent resource half of a mixed element.
        if (found != references.end()) return resource->is_null ? nullptr : found->second;
        // An empty original Resource declaration has no object until assigned.
        // Preserve typed/generated resources for their own adapters.
        if (resource && resource->filename.empty() && resource->override_type == CustomResourceType::INVALID &&
            resource->override_format == DXGI_FORMAT(-1) && resource->override_stride == -1 &&
            !resource->initial_data && !resource->resource && !resource->pool) return nullptr;
        return resource;
    }
    void direct_resource(const CustomResource* resource)
    {
        if(!resource)refuse(L"Missing resource declaration");
        if(resource->pool){
            const auto* pool=resource->pool->ResolvePool();
            if((pool->index_type!=PoolIndexType::RING && pool->index_type!=PoolIndexType::STATIC) ||
               pool->expiration_timeout_frames!=UINT32_MAX)
                refuse(L"Resource references require a nonexpiring direct-index pool");
        }
    }

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
        if(active_depth>=max_active_depth)refuse(L"Native command stack budget exceeded");
        ++active_depth;struct Depth {size_t& value;~Depth(){--value;}} depth{active_depth};
        if (state->recursion >= MAX_COMMAND_LIST_RECURSION)
            refuse(L"Plan recursion budget exceeded");
        if(auto* check=dynamic_cast<CheckTextureOverrideCommand*>(original.get())){
            check_texture(*check,*state);return;
        }
        if(auto* shader=dynamic_cast<RunCustomShaderCommand*>(original.get())){
            std::wstring why;
            if(!shader_command || !shader_command(*shader,*state,why) || nested_failed)
                refuse(why.empty()?L"CustomShader requires a shader execution adapter":why);
            return;
        }
        if(dynamic_cast<DrawCommand*>(original.get())){
            if(state->call_info && state->call_info->hunting_skip)return;
            // Original DrawCommand flushes even for a zero-count draw, before
            // evaluating arguments or entering the draw/dispatch implementation.
            flush(*state);
        }
        if(resource_command && (dynamic_cast<ResourceCopyOperation*>(original.get()) ||
           dynamic_cast<ClearViewCommand*>(original.get()) || dynamic_cast<StoreCommand*>(original.get()) ||
           dynamic_cast<DrawCommand*>(original.get()) || dynamic_cast<SlotRangeCopyOperation*>(original.get()))) {
            std::wstring why;
            if(!resource_command(*original,*state,why))refuse(why.empty()?L"GPU resource command refused":why);
            return;
        }
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
            if(palette_source){
                std::wstring reason;
                if(!palette_source(*state,item.palette,reason))refuse(reason.empty()?L"Native draw palette unavailable":reason);
            }
            append(std::move(item));
            return;
        }
        if (auto* binding = dynamic_cast<ResourceCopyOperation*>(original.get())) {
            auto& src = binding->src; auto& dst = effective_target(binding->dst,*state);
            if (typeid(*binding) != typeid(ResourceCopyOperation) ||
                src.evaluation_mode != ResourceCopyTargetEvaluationMode::RESOURCE ||
                dst.evaluation_mode != ResourceCopyTargetEvaluationMode::RESOURCE)
                refuse(L"Unsupported resource operation: " + original->ini_line);
            PlanItem item;
            if(dst.type==ResourceCopyTargetType::CUSTOM_RESOURCE){
                auto* destination=dst.GetCustomResource(state,true);direct_resource(destination);
                CustomResource* source=nullptr;
                if(src.type==ResourceCopyTargetType::CUSTOM_RESOURCE && binding->options==ResourceCopyOptions::REFERENCE){
                    source=src.GetCustomResource(state);direct_resource(source);source=referenced(source);
                }else if(src.type!=ResourceCopyTargetType::EMPTY ||
                         (binding->options!=ResourceCopyOptions::REFERENCE && binding->options!=ResourceCopyOptions::COPY))
                    refuse(L"Unsupported declaration reference: "+original->ini_line);
                if(references.size()>=max_steps && !references.count(destination))refuse(L"Declaration reference budget exceeded");
                references[destination]=source;
                destination->is_null=source==nullptr;
                return;
            }else if (dst.type == ResourceCopyTargetType::VERTEX_BUFFER)
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
                direct_resource(item.resource);item.resource=referenced(item.resource);
            } else if (src.type != ResourceCopyTargetType::EMPTY ||
                       (binding->options != ResourceCopyOptions::REFERENCE &&
                        binding->options != ResourceCopyOptions::COPY)) {
                refuse(L"Unsupported resource source or copy policy: " + original->ini_line);
            }
            if(item.operation==PlanOperation::BindPixelResource)pixel_bindings[item.slot]={false,item.resource};
            append(std::move(item));
            return;
        }
        if (auto* batch = dynamic_cast<ShaderResourceBatch*>(original.get())) {
            if(!resource_command && !dynamic_cast<ShaderResourceBindBatch*>(batch))
                refuse(L"Shader resource fetch requires a GPU resource adapter");
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
                PlanItem item; item.operation = PlanOperation::External; item.external = found->second.name;
                item.phase_mask = call->run_pre_and_post_together ? 3 : state->post ? 2 : 1;
                if(found->second.command){
                    std::wstring reason;
                    if(!found->second.command(*state,item.phase_mask,reason) || nested_failed)
                        refuse(reason.empty()?L"Native command refused: "+item.external:reason);
                    return;
                }
                append(std::move(item));
                return;
            }
            if (!call->command_list_section ||
                !adapted.count(call->command_list_section->command_list.ResolveCommandList()) ||
                !adapted.count(call->command_list_section->post_command_list.ResolveCommandList()))
                refuse(L"Call target was not adapted: " + original->ini_line);
            original->run(state);
            auto observer=observers.find(call->command_list_section);
            if(observer!=observers.end()){
                std::wstring reason;const unsigned phases=call->run_pre_and_post_together?3:state->post?2:1;
                if(!observer->second(*state,phases,reason) || nested_failed)
                    refuse(reason.empty()?L"Original command observer refused":reason);
            }
            return;
        }
        if (dynamic_cast<SkipCommand*>(original.get())) {
            active->skip_original = true; // Do not mutate the live caller before GPU readiness.
            return;
        }
        if (auto* param = dynamic_cast<ParamOverride*>(original.get())) {
            if (!G || param->param_idx < 0 || size_t(param->param_idx) >= G->iniParams.size())
                refuse(L"INI parameter assignment exceeds generation storage");
            original->run(state); return;
        }
        if (dynamic_cast<IfCommand*>(original.get()) || typeid(*original) == typeid(VariableAssignment) ||
            dynamic_cast<PoolVariableOperation*>(original.get()) || dynamic_cast<AbortCommand*>(original.get()) ||
            dynamic_cast<PresetCommand*>(original.get()) || dynamic_cast<ResetPerFrameLimitsCommand*>(original.get())) {
            original->run(state);
            return;
        }
        if(auto* pool=dynamic_cast<PoolCopyOperation*>(original.get())){
            if(pool->dst.type!=ResourceCopyTargetType::POOL || !pool->dst.custom_resource_pool ||
               (pool->src.type!=ResourceCopyTargetType::POOL && pool->src.type!=ResourceCopyTargetType::EMPTY) ||
               (pool->src.type==ResourceCopyTargetType::POOL && !pool->src.custom_resource_pool) ||
               !cpu_target(pool->dst) || !cpu_target(pool->src))refuse(L"Unsupported pool operation: "+original->ini_line);
            auto* destination=pool->dst.custom_resource_pool;auto* source=pool->src.custom_resource_pool;
            const bool reference=source && !(pool->options & ResourceCopyOptions::COPY_MASK);
            if(reference && destination==source->ResolvePool())refuse(L"Cyclic pool reference");
            // Original operations own metadata copies, proxy detach and the
            // independent resource/variable/full resets. They do no GPU work.
            original->run(state);
            if(reference && destination->ResolvePool()!=source->ResolvePool())refuse(L"Pool reference did not resolve");
            return;
        }
        if(auto* copy=dynamic_cast<CopyCommandListCommand*>(original.get())){
            if(!copy->dst || (copy->src &&
               (!adapted.count(copy->src->command_list.ResolveCommandList()) || !adapted.count(copy->src->post_command_list.ResolveCommandList()))))
                refuse(L"Callback source was not adapted");
            if(copy->src && (&copy->dst->command_list==copy->src->command_list.ResolveCommandList() ||
                            &copy->dst->post_command_list==copy->src->post_command_list.ResolveCommandList()))refuse(L"Cyclic callback reference");
            original->run(state);return;
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
        : original(std::move(command)), runtime(std::move(owner)) {
        guard_arguments(*original,runtime->queries);arguments_supported=cpu_arguments(*original);ini_line=original->ini_line;
    }
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
    if(runtime_->active || !runtime_->adapted.empty())refuse(L"Register native boundaries before adaptation");
    runtime_->externals.emplace(&section, Runtime::External{name,{}});
    section.command_list.runtime_populated = section.post_command_list.runtime_populated = true;
}
void DrawPlanAdapter::native(ExplicitCommandListSection& section,const std::wstring& name,NativeCommand command)
{
    if(!command)refuse(L"Native command handler is empty");
    external(section,name);runtime_->externals.at(&section).command=std::move(command);
}
void DrawPlanAdapter::after(ExplicitCommandListSection& section,NativeCommand command)
{
    if(!command || runtime_->active || !runtime_->adapted.empty() || runtime_->externals.count(&section))
        refuse(L"Register an original command observer before adaptation");
    runtime_->observers[&section]=std::move(command);
    section.command_list.runtime_populated=section.post_command_list.runtime_populated=true;
}
void DrawPlanAdapter::palette_source(PaletteSource source)
{
    if(runtime_->active || !runtime_->adapted.empty())refuse(L"Install native palette source before adaptation");
    runtime_->palette_source=std::move(source);
}
bool DrawPlanAdapter::invoke(ExplicitCommandListSection& section,CommandListState& state,unsigned phases,std::wstring& reason)
{
    reason.clear();const auto post=state.post;
    if(!runtime_->active || !phases || phases>3 || state.aborted ||
       state.recursion>=MAX_COMMAND_LIST_RECURSION-1 || ++runtime_->steps>max_steps){
        runtime_->nested_failed=true;reason=L"Native callback requires an adapted, bounded active transaction";return false;
    }
    ++state.recursion;
    try{
        auto run=[&](CommandList& list,bool post){
            if(state.aborted)return;
            auto* resolved=list.ResolveCommandList();
            if(!runtime_->adapted.count(resolved))refuse(L"Native callback target was not adapted");
            state.post=post;
            for(auto& command:resolved->commands){
                if(state.aborted)break;
                command->run(&state);
            }
        };
        if(phases&1)run(section.command_list,false);
        // Original callback aliases may change during their pre phase.
        if(phases&2)run(section.post_command_list,true);
    }catch(const Refused& e){reason=e.reason;}
     catch(const std::exception&){reason=L"Native callback raised a frontend exception";}
    --state.recursion;state.post=post;
    if(!reason.empty())runtime_->nested_failed=true;
    return reason.empty();
}

void DrawPlanAdapter::adapt(CommandList& root)
{
    adapt_tree(root, 0);
}
CustomResource* DrawPlanAdapter::referenced_resource(CustomResource* resource) const
{
    return runtime_->referenced(resource);
}
bool DrawPlanAdapter::has_reference(const CustomResource* resource) const
{
    return runtime_->references.count(const_cast<CustomResource*>(resource))!=0;
}
void DrawPlanAdapter::resource_queries(ResourceQuery query)
{
    if(runtime_->active || !runtime_->adapted.empty())refuse(L"Install resource queries before adapting command lists");
    runtime_->queries->read=std::move(query);
}
void DrawPlanAdapter::resource_commands(ResourceCommand command)
{
    if(runtime_->active || !runtime_->adapted.empty() || !command)refuse(L"Install resource commands before adaptation");
    runtime_->resource_command=std::move(command);
}
void DrawPlanAdapter::parameter_flush(ParameterFlush callback)
{
    if(runtime_->active || !runtime_->adapted.empty() || !callback)refuse(L"Install parameter publication before adaptation");
    runtime_->parameter_flush=std::move(callback);
}
void DrawPlanAdapter::shader_commands(ShaderCommand callback){
    if(runtime_->active || !runtime_->adapted.empty() || !callback)refuse(L"Install shader execution before adaptation");
    runtime_->shader_command=std::move(callback);
}
bool DrawPlanAdapter::invoke_shader(CustomShader& shader,CommandListState& state,std::wstring& reason){
    reason.clear();const auto post=state.post;
    if(!runtime_->active || state.recursion>=MAX_COMMAND_LIST_RECURSION || state.aborted){
        runtime_->nested_failed=true;reason=L"CustomShader callback requires a bounded active transaction";return false;
    }
    ++state.recursion;
    try{for(unsigned phase=0;phase<2 && !state.aborted;++phase){
        auto* list=(phase?shader.post_command_list:shader.command_list).ResolveCommandList();
        if(!runtime_->adapted.count(list))refuse(L"CustomShader command list was not adapted");state.post=phase!=0;
        for(auto& command:list->commands){if(state.aborted)break;command->run(&state);}
    }}catch(const Refused& e){reason=e.reason;}
      catch(const std::exception&){reason=L"CustomShader callback raised a frontend exception";}
    --state.recursion;state.post=post;if(!reason.empty())runtime_->nested_failed=true;return reason.empty();
}
bool DrawPlanAdapter::flush_parameters(CommandListState& state,bool force,std::wstring& reason)
{
    reason.clear();
    try{runtime_->flush(state,force);return true;}
    catch(const Refused& e){reason=e.reason;}
    catch(const std::exception&){reason=L"Frontend raised an exception publishing parameters";}
    return false;
}

void DrawPlanAdapter::texture_queries(TextureQuery query)
{
    if(runtime_->active || !runtime_->adapted.empty() || !query)refuse(L"Install texture queries before adapting command lists");
    runtime_->texture_query=std::move(query);std::weak_ptr<Runtime> weak=runtime_;
    runtime_->queries->texture=[weak](ResourceCopyTarget& target,CommandListState& state,float& result,std::wstring& reason){
        auto owner=weak.lock();if(!owner || !owner->active){reason=L"Texture query outside an active plan";return false;}
        bool bound=false;TextureOverrideMatches matches;owner->texture_matches(target,state,bound,matches);
        if(!bound){result=-0.f;return true;}
        if(matches.empty()){result=0.f;return true;}
        result=1.f;for(auto it=matches.rbegin();it!=matches.rend();++it)if((*it)->filter_index!=FLT_MAX){result=(*it)->filter_index;break;}
        return true;
    };
}
bool DrawPlanAdapter::has_texture_queries() const {return bool(runtime_->texture_query);}
std::vector<CustomResource*> DrawPlanAdapter::referenced_files() const
{
    return {runtime_->files.begin(), runtime_->files.end()};
}

void DrawPlanAdapter::adapt_tree(CommandList& root, size_t depth)
{
    if (depth > MAX_COMMAND_LIST_RECURSION || runtime_->adapted.size() >= max_steps)
        refuse(L"Command graph adaptation budget exceeded");
    if (!runtime_->adapted.insert(&root).second) return;
    for (auto& command : root.commands) {
        if (dynamic_cast<BridgeCommand*>(command.get()))
            refuse(L"A command list already belongs to another planner");
        runtime_->collect_files(*command);
        if (auto* branch = dynamic_cast<IfCommand*>(command.get())) {
            for (auto* list : {branch->true_commands_pre.get(), branch->true_commands_post.get(),
                               branch->false_commands_pre.get(), branch->false_commands_post.get()})
                if (list) adapt_tree(*list, depth + 1);
        } else if (auto* call = dynamic_cast<RunExplicitCommandList*>(command.get())) {
            if (call->command_list_section && !runtime_->externals.count(call->command_list_section)) {
                adapt_tree(call->command_list_section->command_list, depth + 1);
                adapt_tree(call->command_list_section->post_command_list, depth + 1);
            }
        } else if(auto* copy=dynamic_cast<CopyCommandListCommand*>(command.get())) {
            for(auto* target:{copy->src,copy->dst})if(target && !runtime_->externals.count(target)){
                adapt_tree(target->command_list,depth+1);adapt_tree(target->post_command_list,depth+1);
            }
        } else if(auto* call=dynamic_cast<RunCustomShaderCommand*>(command.get())) {
            if(call->custom_shader){adapt_tree(call->custom_shader->command_list,depth+1);adapt_tree(call->custom_shader->post_command_list,depth+1);}
        }
        command = std::make_shared<BridgeCommand>(command, runtime_);
    }
}

bool DrawPlanAdapter::execute(CommandList& root, CommandListState& state,
                              DrawPlan& output, std::wstring& reason)
{
    return execute_impl(root,nullptr,state,output,reason);
}
bool DrawPlanAdapter::execute_pair(CommandList& pre,CommandList& post,CommandListState& state,DrawPlan& output,std::wstring& reason)
{
    return execute_impl(pre,&post,state,output,reason);
}
bool DrawPlanAdapter::execute_impl(CommandList& root,CommandList* post,CommandListState& state,DrawPlan& output,std::wstring& reason)
{
    reason.clear();
    auto* resolved = root.ResolveCommandList();
    if (runtime_->active || !runtime_->adapted.count(resolved) || !state.mHackerContext ||
        state.aborted || state.recursion) {
        reason = L"Planner requires an adapted root and a fresh serial execution state";
        return false;
    }
    DrawPlan candidate;
    auto saved_layout = state.input_layout_overrides;const auto saved_post=state.post;
    runtime_->active = &candidate; runtime_->steps = runtime_->draws = 0;runtime_->queries->count=0;runtime_->nested_failed=false;runtime_->active_depth=0;
    runtime_->pixel_bindings={};
    try {
        if(post)state.post=false;
        for (auto& command : resolved->commands) {
            if (state.aborted) break;
            command->run(&state);
        }
        if(runtime_->nested_failed)refuse(L"Nested native callback failed");
        runtime_->flush(state);
        if(post){
            resolved=post->ResolveCommandList();
            if(!runtime_->adapted.count(resolved))refuse(L"Root post callback target was not adapted");
            // Source root pre/post lists use separate RunCommandListComplete
            // states. Abort stops its phase, not the later post invocation.
            state.post=true;state.aborted=false;
            for (auto& command : resolved->commands) {
                if (state.aborted) break;
                command->run(&state);
            }
            if(runtime_->nested_failed)refuse(L"Nested native callback failed");
            runtime_->flush(state);
        }
        if(runtime_->nested_failed)refuse(L"Nested native callback failed");
        output = std::move(candidate);
    } catch (const Refused& error) {
        reason = error.reason;
    } catch (const std::exception&) {
        reason = L"Frontend raised an exception while planning";
    }
    runtime_->active = nullptr;
    state.input_layout_overrides = std::move(saved_layout);state.post=saved_post;
    // The state is throwaway, but leave nesting counters usable for diagnostics.
    state.recursion = state.extra_indent = 0;state.aborted=false;
    return reason.empty();
}

} // namespace efmi::frontend
