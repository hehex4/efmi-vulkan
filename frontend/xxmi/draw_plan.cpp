#include "draw_plan.h"
#include "resource_match.h"
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
struct QueryService {DrawPlanAdapter::ResourceQuery read,texture,plan_read;DrawPlanAdapter::ShaderQuery shader;size_t count=0;bool general_matches=false;
    DrawPlanAdapter::HostValue host;DrawPlanAdapter::RenderTargetSize rt;
    // inside: in a draw's scope, within a CustomShader (D-087); drawing: a draw's scope is open.
    std::function<bool()> gate,inside,drawing;bool open() const {return !gate || gate() || (inside && inside());}};
// What the owned work of a replaced draw may read or bind (D-087): the Mod's resources, IniParams,
// null and the compute stage's slots. The application's bindings are not reachable from it.
bool owned_side(const ResourceCopyTarget& t)
{
    switch(t.type){
    case ResourceCopyTargetType::EMPTY: case ResourceCopyTargetType::CUSTOM_RESOURCE: case ResourceCopyTargetType::INI_PARAMS: return true;
    case ResourceCopyTargetType::SHADER_RESOURCE: case ResourceCopyTargetType::CONSTANT_BUFFER: case ResourceCopyTargetType::UNORDERED_ACCESS_VIEW:
        return t.shader_type==L'c';
    default: return false;
    }
}
class NativeShaderQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListOperand> source;
    std::shared_ptr<QueryService> service;
    NativeShaderQuery(std::shared_ptr<CommandListOperand> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* =nullptr) override {
        float value=0;std::wstring why;
        if(!state || !service->shader || !service->open() || ++service->count>max_steps)refuse(L"Shader query adapter is unavailable or over budget");
        if(!service->shader(*source,*state,value,why))refuse(why.empty()?L"Original shader query refused":why);return value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
class ShaderSlotGuard final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListEvaluatable> source;unsigned limit;
    ShaderSlotGuard(std::shared_ptr<CommandListEvaluatable> value,unsigned maximum):source(std::move(value)),limit(maximum){}
    float evaluate(CommandListState* state,HackerDevice* device=nullptr) override {
        const float value=source->evaluate(state,device);
        if(!std::isfinite(value) || value<0 || double(value)>=limit)refuse(L"Shader declaration slot is outside source bounds");return value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
class NativeResourceQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListOperand> source;
    std::shared_ptr<QueryService> service;
    NativeResourceQuery(std::shared_ptr<CommandListOperand> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* =nullptr) override {
        float result=0;std::wstring reason;
        if(!state || ++service->count>max_steps)refuse(L"Resource query adapter is unavailable or over budget");
        auto& target=source->texture_filter_target;
        const bool drawing=service->drawing && service->drawing();
        if(!service->open() || (drawing && !owned_side(target))){
            // Outside an owned scope (a replaced draw's plan), or the application's bindings in a draw's
            // scope: what the host answers on the CPU. A Mod resource in a draw's scope: what the
            // owned work holds (D-087).
            if(service->plan_read && service->plan_read(target,*state,result,reason))return result;
            if(!drawing || !owned_side(target) || !service->read)
                refuse(reason.empty()?L"Resource query adapter is unavailable or over budget":reason);
            reason.clear();
        }
        const bool pixel=target.type==ResourceCopyTargetType::THIS_RESOURCE ||
            (target.type==ResourceCopyTargetType::SHADER_RESOURCE && target.shader_type==L'p');
        const auto& read=(pixel || service->general_matches) && target.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE && service->texture?service->texture:service->read;
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
// The original's ProcessParamRTSize caches the size of o0 in the state once a 2D view is bound
// and asks again while the cache stays -1. The owned domain answers for o0; -2 marks "asked,
// nothing bound" for the window heuristic, so the reference context is never queried.
void fill_render_target(CommandListState& state,QueryService& service)
{
    if(state.rt_width!=-1 && state.rt_width!=-2)return;
    if(!service.rt || !service.open())refuse(L"Render target size needs the owned GPU domain");
    bool bound=false;float width=-1,height=-1;std::wstring why;
    if(!service.rt(state,bound,width,height,why))refuse(why.empty()?L"Render target size query refused":why);
    if(bound){state.rt_width=width;state.rt_height=height;}
}
class RenderTargetQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListOperand> source;
    std::shared_ptr<QueryService> service;
    RenderTargetQuery(std::shared_ptr<CommandListOperand> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* =nullptr) override {
        if(!state)refuse(L"Render target size needs a command list state");
        fill_render_target(*state,*service);
        const float value=source->type==ParamOverrideType::RT_WIDTH?state->rt_width:state->rt_height;
        return value==-2?-1.0f:value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
// window_width, window_height and the relative cursor fall back to the bound render target
// when neither the window nor the resolution reaches 640. Outside the owned domain a hosted
// generation has no render target, so the heuristic continues as the original does with none
// bound.
class HostWindowQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListEvaluatable> source;
    std::shared_ptr<QueryService> service;
    HostWindowQuery(std::shared_ptr<CommandListEvaluatable> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* device=nullptr) override {
        if(state && (state->rt_width==-1 || state->rt_width==-2)){
            if(service->rt && service->open())fill_render_target(*state,*service);
            if(state->rt_width==-1)state->rt_width=state->rt_height=-2;
        }
        return source->evaluate(state,device);
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
// effective_dpi and cursor_hotspot_x/y: the original asks Win32 directly (GetDpiForMonitor on
// the window's monitor, GetIconInfo on the cursor); the host answers from its sample.
class HostValueQuery final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListOperand> source;
    std::shared_ptr<QueryService> service;
    HostValueQuery(std::shared_ptr<CommandListOperand> value,std::shared_ptr<QueryService> owner):source(std::move(value)),service(std::move(owner)){}
    float evaluate(CommandListState* state,HackerDevice* =nullptr) override {
        if(!state && source->type!=ParamOverrideType::EFFECTIVE_DPI)refuse(L"Cursor operand needs a command list state");
        float value=0;
        if(!service->host || !service->host(source->type,value) || !std::isfinite(value))refuse(L"Host value is unavailable");
        return value;
    }
    bool static_evaluate(float*,HackerDevice* =nullptr,bool=false) override {return false;}
    bool optimise(HackerDevice*,std::shared_ptr<CommandListEvaluatable>*) override {return false;}
};
bool host_value(ParamOverrideType type){
    return type==ParamOverrideType::EFFECTIVE_DPI || type==ParamOverrideType::CURSOR_HOTSPOT_X || type==ParamOverrideType::CURSOR_HOTSPOT_Y;
}
bool window_heuristic(ParamOverrideType type){
    return type==ParamOverrideType::WINDOW_WIDTH || type==ParamOverrideType::WINDOW_HEIGHT ||
           type==ParamOverrideType::CURSOR_X || type==ParamOverrideType::CURSOR_Y;
}
class SlotIndexGuard final : public CommandListEvaluatable {
public:
    std::shared_ptr<CommandListEvaluatable> source;
    explicit SlotIndexGuard(std::shared_ptr<CommandListEvaluatable> value):source(std::move(value)){}
    float evaluate(CommandListState* state,HackerDevice* device=nullptr) override {
        const auto value=source->evaluate(state,device);
        if(!std::isfinite(value))refuse(L"Resource slot index is not finite");return value;
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
    if(const auto* query=dynamic_cast<NativeShaderQuery*>(node.get())){
        for(const auto& arg:query->source->shader_target.member_args)
            if(arg.expression && !cpu_expression(arg.expression->evaluatable,depth+1))return false;return true;
    }
    if(const auto* guard=dynamic_cast<ShaderSlotGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if(const auto* query=dynamic_cast<NativeResourceQuery*>(node.get()))return cpu_target(query->source->texture_filter_target,depth+1);
    if(const auto* guard=dynamic_cast<PoolIndexGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if(const auto* guard=dynamic_cast<RangeIndexGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if(const auto* guard=dynamic_cast<SlotIndexGuard*>(node.get()))return cpu_expression(guard->source,depth+1);
    if(const auto* query=dynamic_cast<HostWindowQuery*>(node.get()))return dynamic_cast<CommandListOperand*>(query->source.get())!=nullptr;
    if(dynamic_cast<HostValueQuery*>(node.get()) || dynamic_cast<RenderTargetQuery*>(node.get()))return true;
    if (const auto* operand = dynamic_cast<CommandListOperand*>(node.get())) {
        switch (operand->type) {
        case ParamOverrideType::VALUE: return true;
        case ParamOverrideType::INI_PARAM:
            return G && operand->param_idx >= 0 && size_t(operand->param_idx) < G->iniParams.size();
        case ParamOverrideType::VARIABLE: return operand->var_ftarget != nullptr;
        case ParamOverrideType::TEXTURE: {
            const auto& target=operand->texture_filter_target;
            // process_texture_filter has no case for ->region: it returns 0 without the resource.
            if(target.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE_REGION)return true;
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
        case ParamOverrideType::TIME: case ParamOverrideType::FPS:
        case ParamOverrideType::THREAD_GROUP_COUNT_X: case ParamOverrideType::THREAD_GROUP_COUNT_Y:
        case ParamOverrideType::THREAD_GROUP_COUNT_Z: case ParamOverrideType::INDIRECT_OFFSET:
        // The host's window, cursor and swap chain sample answers these (RuntimeInput).
        case ParamOverrideType::RES_WIDTH: case ParamOverrideType::RES_HEIGHT:
        case ParamOverrideType::CURSOR_VISIBLE: case ParamOverrideType::CURSOR_SCREEN_X: case ParamOverrideType::CURSOR_SCREEN_Y:
        case ParamOverrideType::CURSOR_WINDOW_X: case ParamOverrideType::CURSOR_WINDOW_Y:
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
    if(target.slot_expression){auto& expression=target.slot_expression->evaluatable;guard_expression(expression,queries,depth+1);
        if(!dynamic_cast<SlotIndexGuard*>(expression.get()))expression=std::make_shared<SlotIndexGuard>(expression);}
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
        if(operand->type==ParamOverrideType::SHADER && queries->shader){
            auto& target=operand->shader_target;
            const auto mode=target.evaluation_mode;
            const unsigned limit=mode==ShaderTargetEvaluationMode::DCL_CB_TYPE || mode==ShaderTargetEvaluationMode::DCL_CB_SIZE?14u:128u;
            for(auto& arg:target.member_args)if(arg.expression){guard_expression(arg.expression->evaluatable,queries,depth+1);
                if(!dynamic_cast<ShaderSlotGuard*>(arg.expression->evaluatable.get()))
                    arg.expression->evaluatable=std::make_shared<ShaderSlotGuard>(arg.expression->evaluatable,limit);}
            node=std::make_shared<NativeShaderQuery>(std::move(operand),queries);return;
        }
        if(window_heuristic(operand->type)){node=std::make_shared<HostWindowQuery>(std::move(operand),queries);return;}
        if(host_value(operand->type) && queries->host){node=std::make_shared<HostValueQuery>(std::move(operand),queries);return;}
        if((operand->type==ParamOverrideType::RT_WIDTH || operand->type==ParamOverrideType::RT_HEIGHT) && queries->rt){
            node=std::make_shared<RenderTargetQuery>(std::move(operand),queries);return;
        }
        guard_target(operand->texture_filter_target,queries,depth+1);
        if(operand->type==ParamOverrideType::TEXTURE && !cpu_expression(operand,depth+1) && (queries->read || queries->texture || queries->plan_read))
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
    DrawPlanAdapter::DrawPrepare draw_prepare;
    DrawPlanAdapter::ShaderCommand shader_command;
    bool nested_failed=false;
    bool keep_original=false; // a native kept the application's draw (DrawPlanAdapter::keep_original)
    size_t active_depth=0;
    DrawPlanAdapter::TextureQuery texture_query;
    DrawPlanAdapter::ResourceMatchQuery resource_match;
    std::array<DrawPlanAdapter::PixelBinding,128> pixel_bindings;
    // `ResourceX = ref ps-tN` in a matched draw's plan: X refers to the application's texture at
    // pixel slot N of that draw. One stand-in per slot marks it; it never reaches a file plan.
    std::array<CustomResource,128> application_textures;
    int application_slot(const CustomResource* resource) const {
        if(resource<application_textures.data() || resource>=application_textures.data()+application_textures.size())return -1;
        return int(resource-application_textures.data());
    }
    std::unordered_set<CustomResource*> files;
    std::function<bool()> owned_gate;
    // The owned GPU callbacks apply unless a gate says the host has no owned scope open.
    bool owned() const {return !owned_gate || owned_gate();}
    // A replaced draw's scope for owned GPU work (D-087), the CustomShader nesting within it, the
    // host's owned vertex buffers and the Mod resources whose content owned work wrote.
    std::function<bool()> draw_gate;
    DrawPlanAdapter::OwnedBinding owned_binding;
    size_t shader_depth=0;
    std::unordered_set<CustomResource*> owned_contents;
    bool drawing() const {return draw_gate && draw_gate();}
    // The owned callbacks apply: the host's owned scope, or within a CustomShader in a draw's scope.
    bool gpu() const {return owned() || (shader_depth && drawing());}
    // A copy executed by owned work leaves the Mod resource it wrote with owned content.
    void mark_owned(CommandListCommand& command,CommandListState& state)
    {
        auto* copy=dynamic_cast<ResourceCopyOperation*>(&command);
        if(!copy || typeid(*copy)!=typeid(ResourceCopyOperation))return;
        auto& dst=effective_target(copy->dst,state);
        if(dst.type!=ResourceCopyTargetType::CUSTOM_RESOURCE || dst.custom_resource_pool || dst.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE)return;
        auto* destination=dst.GetCustomResource(&state,true);if(!destination)return;
        if(copy->options!=ResourceCopyOptions::REFERENCE){
            if(references.size()>=max_steps && !references.count(destination))refuse(L"Declaration reference budget exceeded");
            references[destination]=destination;owned_contents.insert(destination);
        }
    }
    // Owned work in a draw's scope stays on the owned side (D-087).
    void owned_only(CommandListCommand& command,CommandListState&)
    {
        auto check=[&](const ResourceCopyTarget& t){
            if(!owned_side(t))refuse(L"Owned work in a replaced draw reaches the application's resources: "+command.ini_line);
        };
        if(auto* copy=dynamic_cast<ResourceCopyOperation*>(&command)){check(copy->src);check(copy->dst);}
        else if(auto* range=dynamic_cast<SlotRangeCopyOperation*>(&command)){check(range->src);check(range->dst);}
        else if(auto* clear=dynamic_cast<ClearViewCommand*>(&command))check(clear->target);
        else if(dynamic_cast<StoreCommand*>(&command))refuse(L"Store needs GPU feedback, which a replaced draw's scope cannot give: "+command.ini_line);
        else if(auto* batch=dynamic_cast<ShaderResourceBatch*>(&command)){
            for(const auto& operation:batch->operations)if(operation){check(operation->src);check(operation->dst);}
        }
    }

    void flush(CommandListState& state,bool force=false){
        if(!parameter_flush || !gpu() || (!force && !state.update_params))return;
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
        if(resolved.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE || resolved.IsRange())
            refuse(L"Resource matching requires an adapted single resource target");
        matches.clear();bound=false;
        if(resource_match && gpu()){
            SourceResourceMatch source;std::wstring why;
            if(!resource_match(resolved,state,source,why))refuse(why.empty()?L"Original resource identity is unknown":why);
            // Native THIS delegates resource resolution but keeps its own type
            // when deciding whether to apply IA region hashing.
            bound=source.bound;source_resource_matches(source,target.type,state,matches);
        }else{
            if(!texture_query || resolved.type!=ResourceCopyTargetType::SHADER_RESOURCE || resolved.shader_type!=L'p')
                refuse(L"Texture query requires an adapted single pixel slot");
            const auto slot=resolved.ResolveSlot(&state);
            if(slot>=pixel_bindings.size())refuse(L"Texture query slot outside supported range");
            DrawPlanAdapter::TextureIdentity identity;
            const auto& binding=pixel_bindings[slot];
            if(binding.application || binding.resource){
            std::wstring why;
            if(!texture_query(slot,binding,state,identity,why))refuse(why.empty()?L"Texture source identity is unknown":why);
            }
            bound=identity.bound;if(!bound)return;
            if(!G->mFuzzyTextureOverrides.empty())refuse(L"Texture descriptor fuzzy matching has no adapter");
            find_texture_override_for_hash(identity.hash,&matches,state.call_info);
        }
        if(matches.size()>max_items)refuse(L"Texture match count exceeds planner budget");
        for(auto* match:matches)if(!match || !adapted.count(match->command_list.ResolveCommandList()) ||
                                   !adapted.count(match->post_command_list.ResolveCommandList()))
            refuse(L"Matched texture command lists were not adapted");
    }
    // A declaration reference executed by the owned backend is also what later replaced draws
    // resolve through the plan, exactly as the plan path itself would record it.
    void mirror_reference(CommandListCommand& command,CommandListState& state)
    {
        auto* copy=dynamic_cast<ResourceCopyOperation*>(&command);
        if(!copy || typeid(*copy)!=typeid(ResourceCopyOperation) || copy->options!=ResourceCopyOptions::REFERENCE)return;
        auto& dst=effective_target(copy->dst,state);
        if(dst.type!=ResourceCopyTargetType::CUSTOM_RESOURCE || dst.custom_resource_pool || dst.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE ||
           copy->src.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE || copy->src.custom_resource_pool)return;
        CustomResource* source=nullptr;
        if(copy->src.type==ResourceCopyTargetType::CUSTOM_RESOURCE)source=referenced(copy->src.GetCustomResource(&state));
        else if(copy->src.type!=ResourceCopyTargetType::EMPTY)return;
        auto* destination=dst.GetCustomResource(&state,true);if(!destination)return;
        if(references.size()>=max_steps && !references.count(destination))refuse(L"Declaration reference budget exceeded");
        references[destination]=source;
    }
    void check_texture(CheckTextureOverrideCommand& check,CommandListState& state)
    {
        auto& target=effective_target(check.target,state);
        if(!(resource_match && gpu()) && (target.type!=ResourceCopyTargetType::SHADER_RESOURCE || target.shader_type!=L'p' ||
           (target.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE &&
            target.evaluation_mode!=ResourceCopyTargetEvaluationMode::SLOT_RANGE)))
            refuse(L"Texture override only supports pixel resource slots");
        int first=0;unsigned count=1;
        if(target.IsRange()){
            if(target.custom_resource_pool)validate_pool_runtime(*target.custom_resource_pool);
            if(!target.ResolveRange(&state,&first,&count))return; // Native finite invalid range is a no-op.
            if(count>max_items)refuse(L"Checked resource range exceeds transaction budget");
        }
        auto* saved_this=state.this_target;const auto saved_post=state.post;
        struct Restore {CommandListState& state;ResourceCopyTarget* target;bool post;~Restore(){state.this_target=target;state.post=post;}} restore{state,saved_this,saved_post};
        for(unsigned i=0;i<count;++i){
            if(++steps>max_steps)refuse(L"Checked resource range exceeds execution budget");
            ResourceCopyTarget local;ResourceCopyTarget& checked=target.IsRange()?local:target;
            if(target.evaluation_mode==ResourceCopyTargetEvaluationMode::POOL_RANGE){
                const int64_t index=int64_t(first)+i;
                if(!target.custom_resource_pool || index<INT_MIN || index>INT_MAX)refuse(L"Checked pool index exceeds signed bounds");
                local.type=ResourceCopyTargetType::CUSTOM_RESOURCE;
                local.SetCustomResource(target.custom_resource_pool->GetResource(float(index),false,true,false));
            }else if(target.IsRange()){
                local.type=target.type;local.shader_type=target.shader_type;local.slot=unsigned(first)+i;local.max_slot=target.max_slot;
            }
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
            if(shader_command && !owned() && !drawing())refuse(L"CustomShader needs the host's owned GPU scope, which replaced draws do not open");
            // In a draw's scope everything the shader's lists run is owned work (D-087).
            const bool nested=!owned() && drawing();
            struct Nesting {size_t& depth;bool on;~Nesting(){if(on)--depth;}} nesting{shader_depth,nested};
            if(nested)++shader_depth;
            if(!shader_command || !shader_command(*shader,*state,why) || nested_failed)
                refuse(why.empty()?L"CustomShader requires a shader execution adapter":why);
            return;
        }
        if(dynamic_cast<DrawCommand*>(original.get())){
            if(state->call_info && state->call_info->hunting_skip)return;
            if(draw_prepare && gpu()){std::wstring why;if(!draw_prepare(*state,why))refuse(why.empty()?L"Source draw preparation refused":why);}
            // Original DrawCommand flushes even for a zero-count draw, before
            // evaluating arguments or entering the draw/dispatch implementation.
            flush(*state);
        }
        // A replaced draw's scope, outside CustomShaders: a copy into a Mod resource, or a reference to
        // one with owned content, is owned work; the draw's own bindings and draws keep the plan (D-087).
        if(resource_command && drawing() && !shader_depth && !owned()){
            if(auto* copy=dynamic_cast<ResourceCopyOperation*>(original.get()); copy && typeid(*copy)==typeid(ResourceCopyOperation)){
                auto& dst=effective_target(copy->dst,*state);
                bool owned_source=false;
                if(copy->src.type==ResourceCopyTargetType::CUSTOM_RESOURCE && !copy->src.custom_resource_pool &&
                   copy->src.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE)
                    owned_source=owned_contents.count(referenced(copy->src.GetCustomResource(state)))!=0;
                if(dst.type==ResourceCopyTargetType::CUSTOM_RESOURCE && !dst.custom_resource_pool &&
                   dst.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE &&
                   (copy->options!=ResourceCopyOptions::REFERENCE || owned_source)){
                    owned_only(*original,*state);
                    std::wstring why;
                    if(!resource_command(*original,*state,why))refuse(why.empty()?L"GPU resource command refused":why);
                    mirror_reference(*original,*state);mark_owned(*original,*state);
                    return;
                }
            }
        }
        if(resource_command && gpu() && (dynamic_cast<ResourceCopyOperation*>(original.get()) ||
           dynamic_cast<ClearViewCommand*>(original.get()) || dynamic_cast<StoreCommand*>(original.get()) ||
           dynamic_cast<DrawCommand*>(original.get()) || dynamic_cast<SlotRangeCopyOperation*>(original.get()))) {
            std::wstring why;
            if(!owned())owned_only(*original,*state);
            if(owned_gate || draw_gate){mirror_reference(*original,*state);mark_owned(*original,*state);}
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
                }else if(src.type==ResourceCopyTargetType::SHADER_RESOURCE && src.shader_type==L'p' &&
                         binding->options==ResourceCopyOptions::REFERENCE){
                    // The application's texture at this pixel slot of the matched draw, kept by reference.
                    const unsigned slot=src.ResolveSlot(state);
                    if(slot>=128)refuse(L"Pixel slot out of range: "+original->ini_line);
                    source=&application_textures[slot];
                }else if(src.type!=ResourceCopyTargetType::EMPTY ||
                         (binding->options!=ResourceCopyOptions::REFERENCE && binding->options!=ResourceCopyOptions::COPY))
                    refuse(L"Unsupported declaration reference: "+original->ini_line);
                if(references.size()>=max_steps && !references.count(destination))refuse(L"Declaration reference budget exceeded");
                references[destination]=source;
                if(!source || !owned_contents.count(source))owned_contents.erase(destination);
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
                const int application=application_slot(item.resource);
                if(application>=0){
                    if(item.operation!=PlanOperation::BindPixelResource)refuse(L"An application texture bound outside a pixel slot: "+original->ini_line);
                    item.application_slot=application;item.resource=nullptr;
                }
                if(item.resource && owned_contents.count(item.resource)){
                    // What owned work wrote: its buffer, as the owned side holds it now (D-087).
                    if(item.operation!=PlanOperation::BindVertex)refuse(L"Owned GPU content bound outside a vertex slot: "+original->ini_line);
                    if(!owned_binding || !drawing())refuse(L"Owned GPU content bound outside a replaced draw's owned scope: "+original->ini_line);
                    PlanItem::OwnedBuffer buffer;std::wstring why;
                    if(!owned_binding(src,*state,buffer,why))refuse(why.empty()?L"Owned vertex buffer unavailable: "+original->ini_line:why);
                    item.owned=std::make_shared<const PlanItem::OwnedBuffer>(std::move(buffer));item.resource=nullptr;
                }
            } else if (src.type != ResourceCopyTargetType::EMPTY ||
                       (binding->options != ResourceCopyOptions::REFERENCE &&
                        binding->options != ResourceCopyOptions::COPY)) {
                refuse(L"Unsupported resource source or copy policy: " + original->ini_line);
            }
            if(item.operation==PlanOperation::BindPixelResource)
                pixel_bindings[item.slot]=item.application_slot==int(item.slot)?PixelBinding{}:PixelBinding{false,item.resource};
            append(std::move(item));
            return;
        }
        if (auto* batch = dynamic_cast<ShaderResourceBatch*>(original.get())) {
            // A fetch of pixel slots into resources by reference keeps the application's textures
            // (application_textures); other fetches need the owned GPU adapter.
            bool application_fetch=dynamic_cast<ShaderResourceFetchBatch*>(batch) && batch->shader_type==L'p';
            for(const auto& operation:batch->operations)
                application_fetch=application_fetch && operation && operation->options==ResourceCopyOptions::REFERENCE &&
                    operation->dst.type==ResourceCopyTargetType::CUSTOM_RESOURCE && operation->src.type==ResourceCopyTargetType::SHADER_RESOURCE;
            if(!(resource_command && gpu()) && !dynamic_cast<ShaderResourceBindBatch*>(batch) && !application_fetch)
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
        if(auto* call=dynamic_cast<RunLinkedCommandList*>(original.get())){
            if(!call->link || !adapted.count(call->link->ResolveCommandList()))refuse(L"Linked command target was not adapted");
            original->run(state);return;
        }
        if (dynamic_cast<IfCommand*>(original.get()) || typeid(*original) == typeid(VariableAssignment) ||
            dynamic_cast<PoolVariableOperation*>(original.get()) || dynamic_cast<AbortCommand*>(original.get()) ||
            dynamic_cast<PresetCommand*>(original.get()) || dynamic_cast<ResetPerFrameLimitsCommand*>(original.get()) ||
            dynamic_cast<UpscalingFlipBBCommand*>(original.get())) {
            // The upscaling flip only clears this generation's bb_is_upscaling_bb flag.
            original->run(state);
            return;
        }
        // The original draws the 3DMigoto overlay only when the swap chain has one, and a
        // hosted generation has none; it must not reach the reference swap chain scaffolding.
        if (dynamic_cast<Draw3DMigotoOverlayCommand*>(original.get())) return;
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
class BridgeLinkedCommand final : public RunLinkedCommandList {
public:
    std::shared_ptr<RunLinkedCommandList> original;
    std::shared_ptr<DrawPlanAdapter::Runtime> runtime;
    BridgeLinkedCommand(std::shared_ptr<RunLinkedCommandList> command,std::shared_ptr<DrawPlanAdapter::Runtime> owner)
        :RunLinkedCommandList(command->link),original(std::move(command)),runtime(std::move(owner)){ini_line=original->ini_line;}
    void run(CommandListState* state) override {runtime->run(original,state);}
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
std::shared_ptr<RunLinkedCommandList> DrawPlanAdapter::linked(CommandList& target,const std::wstring& line){
    if(runtime_->active)refuse(L"Create linked command boundaries outside execution");
    adapt(target);CommandList holder;auto source=LinkCommandLists(&holder,&target,&line);
    return std::make_shared<BridgeLinkedCommand>(std::move(source),runtime_);
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
void DrawPlanAdapter::plan_resource_queries(ResourceQuery query)
{
    if(runtime_->active || !runtime_->adapted.empty() || !query)refuse(L"Install plan resource queries before adapting command lists");
    runtime_->queries->plan_read=std::move(query);
}
void DrawPlanAdapter::keep_original()
{
    if(!runtime_->active)refuse(L"Keeping the original draw needs an active plan");
    runtime_->keep_original=true;
}
void DrawPlanAdapter::host_values(HostValue values){
    if(runtime_->active || !runtime_->adapted.empty() || !values)refuse(L"Install host values before adapting command lists");
    runtime_->queries->host=std::move(values);
}
void DrawPlanAdapter::render_target_size(RenderTargetSize query){
    if(runtime_->active || !runtime_->adapted.empty() || !query)refuse(L"Install the render target size before adapting command lists");
    runtime_->queries->rt=std::move(query);
}
void DrawPlanAdapter::shader_queries(ShaderQuery query){
    if(runtime_->active || !runtime_->adapted.empty() || !query)refuse(L"Install shader queries before adapting command lists");
    runtime_->queries->shader=std::move(query);
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
void DrawPlanAdapter::draw_prepare(DrawPrepare callback){
    if(runtime_->active || !runtime_->adapted.empty())refuse(L"Draw preparation must be installed before adaptation");
    runtime_->draw_prepare=std::move(callback);
}
void DrawPlanAdapter::owned_scope(std::function<bool()> gate){
    if(runtime_->active || !runtime_->adapted.empty() || !gate || runtime_->owned_gate)refuse(L"Install one owned scope gate before adaptation");
    runtime_->owned_gate=gate;runtime_->queries->gate=std::move(gate);
}
void DrawPlanAdapter::draw_scope(std::function<bool()> gate){
    if(runtime_->active || !runtime_->adapted.empty() || !gate || runtime_->draw_gate)refuse(L"Install one draw scope gate before adaptation");
    auto* runtime=runtime_.get();
    runtime_->queries->drawing=gate;runtime_->queries->inside=[runtime]{return runtime->shader_depth && runtime->drawing();};
    runtime_->draw_gate=std::move(gate);
}
void DrawPlanAdapter::owned_bindings(OwnedBinding binding){
    if(runtime_->active || !runtime_->adapted.empty() || !binding || runtime_->owned_binding)refuse(L"Install one owned binding provider before adaptation");
    runtime_->owned_binding=std::move(binding);
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
    if(runtime_->active || !runtime_->adapted.empty() || !query || runtime_->texture_query)refuse(L"Install one texture matcher before adapting command lists");
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
bool DrawPlanAdapter::has_texture_queries() const {return bool(runtime_->texture_query) || bool(runtime_->resource_match);}
const CommandListCommand* DrawPlanAdapter::original(const CommandListCommand& command)
{
    const auto* bridge=dynamic_cast<const BridgeCommand*>(&command);return bridge?bridge->original.get():&command;
}
void DrawPlanAdapter::resource_matches(ResourceMatchQuery query){
    if(runtime_->active || !runtime_->adapted.empty() || !query || runtime_->resource_match)
        refuse(L"Install one source resource matcher before adapting command lists");
    runtime_->resource_match=std::move(query);runtime_->queries->general_matches=true;std::weak_ptr<Runtime> weak=runtime_;
    runtime_->queries->texture=[weak](ResourceCopyTarget& target,CommandListState& state,float& result,std::wstring& reason){
        auto owner=weak.lock();if(!owner || !owner->active){reason=L"Resource matching outside an active transaction";return false;}
        bool bound=false;TextureOverrideMatches matches;owner->texture_matches(target,state,bound,matches);
        if(!bound){result=-0.f;return true;}result=matches.empty()?0.f:1.f;
        for(auto it=matches.rbegin();it!=matches.rend();++it)if((*it)->filter_index!=FLT_MAX){result=(*it)->filter_index;break;}
        return true;
    };
}
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
        if(auto* linked=dynamic_cast<BridgeLinkedCommand*>(command.get())){
            if(linked->runtime!=runtime_ || !linked->link || !runtime_->adapted.count(linked->link->ResolveCommandList()))
                refuse(L"Linked command belongs to another planner");continue;
        }
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
        } else if(auto* call=dynamic_cast<RunLinkedCommandList*>(command.get())){
            if(call->link)adapt_tree(*call->link,depth+1);
        }
        command = std::make_shared<BridgeCommand>(command, runtime_);
    }
}

bool DrawPlanAdapter::execute(CommandList& root, CommandListState& state,
                              DrawPlan& output, std::wstring& reason)
{
    return execute_impl({&root},nullptr,state,output,reason);
}
bool DrawPlanAdapter::execute_pair(CommandList& pre,CommandList& post,CommandListState& state,DrawPlan& output,std::wstring& reason,OriginalCall between)
{
    const std::vector<CommandList*> after={&post};return execute_impl({&pre},&after,state,output,reason,std::move(between));
}
bool DrawPlanAdapter::execute_matches(const std::vector<CommandList*>& pre,const std::vector<CommandList*>& post,CommandListState& state,DrawPlan& output,std::wstring& reason)
{
    return execute_impl(pre,&post,state,output,reason);
}
bool DrawPlanAdapter::execute_impl(const std::vector<CommandList*>& pre,const std::vector<CommandList*>* post,CommandListState& state,DrawPlan& output,std::wstring& reason,OriginalCall between)
{
    reason.clear();
    auto valid=[&](const auto& lists){if(lists.size()>max_items)return false;for(auto* list:lists)
        if(!list || !runtime_->adapted.count(list->ResolveCommandList()))return false;return true;};
    if (runtime_->active || !valid(pre) || (post && !valid(*post)) || !state.mHackerContext ||
        state.aborted || state.recursion) {
        reason = L"Planner requires an adapted root and a fresh serial execution state";
        return false;
    }
    DrawPlan candidate;
    auto saved_layout = state.input_layout_overrides;const auto saved_post=state.post;
    runtime_->active = &candidate; runtime_->steps = runtime_->draws = 0;runtime_->queries->count=0;runtime_->nested_failed=false;runtime_->active_depth=0;
    runtime_->keep_original=false;runtime_->shader_depth=0;
    runtime_->pixel_bindings={};
    try {
        auto run=[&](const auto& lists,CommandListState& phase){for(auto* root:lists){
            if(phase.aborted)break;
            auto* resolved=root->ResolveCommandList();
            // An earlier source command can change a later list's alias.
            if(!runtime_->adapted.count(resolved))refuse(L"Matched root callback target was not adapted");
            for(auto& command:resolved->commands){if(phase.aborted)break;command->run(&phase);}
            if(runtime_->nested_failed)refuse(L"Nested native callback failed");
        }};
        if(post)state.post=false;
        run(pre,state);
        if(runtime_->nested_failed)refuse(L"Nested native callback failed");
        runtime_->flush(state);
        candidate.pre_items=candidate.items.size();
        if(between){std::wstring why;if(!between(state,why))refuse(why.empty()?L"Root phase boundary refused":why);}
        if(post){
            // RunCommandListComplete creates a fresh state for the independent
            // post root. Applied resource/layout state persists in the backend;
            // pending edits, abort and cached queries do not cross this boundary.
            CommandListState after;after.mHackerDevice=state.mHackerDevice;after.mHackerContext=state.mHackerContext;
            after.mOrigDevice1=state.mOrigDevice1;after.mOrigContext1=state.mOrigContext1;after.call_info=state.call_info;
            after.this_target=state.this_target;after.resource=state.resource;after.view=state.view;after.post=true;
            run(*post,after);
            if(runtime_->nested_failed)refuse(L"Nested native callback failed");
            runtime_->flush(after);
        }
        if(runtime_->nested_failed)refuse(L"Nested native callback failed");
        if(runtime_->keep_original)candidate.skip_original=false;
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

bool DrawPlanAdapter::execute_intercepted(const std::vector<CommandList*>& pre,const std::vector<CommandList*>& post,
                                         CommandListState& state,OriginalCall original,std::wstring& reason){
    reason.clear();
    auto valid=[&](const auto& lists){if(lists.size()>6)return false;for(auto* list:lists)
        if(!list || !runtime_->adapted.count(list->ResolveCommandList()))return false;return true;};
    if(runtime_->active || !state.mHackerContext || !state.call_info || state.aborted || state.recursion || !original || !valid(pre) || !valid(post)){
        reason=L"Intercepted call requires adapted stage roots and fresh source state";return false;
    }
    DrawPlan candidate;candidate.skip_original=state.call_info->skip;
    runtime_->active=&candidate;runtime_->steps=runtime_->draws=runtime_->queries->count=0;
    runtime_->nested_failed=false;runtime_->active_depth=0;runtime_->pixel_bindings={};runtime_->keep_original=false;runtime_->shader_depth=0;
    try{
        auto fresh=[&](CommandListState& root,bool after){root.mHackerDevice=state.mHackerDevice;root.mHackerContext=state.mHackerContext;
            root.mOrigDevice1=state.mOrigDevice1;root.mOrigContext1=state.mOrigContext1;root.call_info=state.call_info;root.post=after;};
        auto run=[&](const auto& lists,bool after){for(auto* list:lists){CommandListState root;fresh(root,after);
            auto* resolved=list->ResolveCommandList();
            if(!runtime_->adapted.count(resolved))refuse(L"Shader root callback alias was not adapted");
            for(auto& command:resolved->commands){if(root.aborted)break;command->run(&root);}
            if(runtime_->nested_failed)refuse(L"Nested shader root callback failed");runtime_->flush(root);
            if(!candidate.items.empty())refuse(L"Intercepted shader calls require immediate resource/draw execution");
        }};
        run(pre,false);
        if(!candidate.skip_original && !state.call_info->hunting_skip){CommandListState caller;fresh(caller,false);
            std::wstring why;if(!original(caller,why))refuse(why.empty()?L"Original call callback refused":why);
            if(runtime_->nested_failed || !candidate.items.empty())refuse(L"Original call callback left failed or deferred work");}
        run(post,true);
    }catch(const Refused& e){reason=e.reason;}
     catch(const std::exception& e){const std::string text=e.what();reason.assign(text.begin(),text.end());}
    runtime_->active=nullptr;return reason.empty();
}

} // namespace efmi::frontend
