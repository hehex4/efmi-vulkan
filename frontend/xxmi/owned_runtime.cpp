#include "owned_runtime.h"
#include "owned_generation.h"

namespace efmi::frontend {
struct OwnedRuntime::Impl {
    std::shared_ptr<GpuShaderRuntime> gpu;
    std::weak_ptr<RuntimeExtension> extension;
};
struct PreparedConfigurationDraw::Impl {
    std::shared_ptr<IniRuntime> registry;
    std::shared_ptr<GpuShaderRuntime> gpu;
    assets::FilePlan plan;
    std::map<std::tuple<wchar_t,ResourceCopyTargetType,unsigned>,GpuResourceBinding> initial;
    bool armed=false,admitted=false;
    ~Impl(){if(armed && !admitted)gpu->resources().discard();}
};
PreparedConfigurationDraw::PreparedConfigurationDraw():impl_(std::make_unique<Impl>()){}
PreparedConfigurationDraw::~PreparedConfigurationDraw()=default;
const assets::FilePlan& PreparedConfigurationDraw::plan() const {return impl_->plan;}
bool PreparedConfigurationDraw::admit(std::string& reason){
    reason.clear();
    if(impl_->admitted)return true;
    try{impl_->gpu->resources().accept_prepared();impl_->admitted=true;return true;}
    catch(const std::exception& error){reason=error.what();return false;}
}
OwnedRuntime::OwnedRuntime(gpu::ResourceDevice device,gpu::ShaderHost host,const Tools& tools,std::filesystem::path working):impl_(std::make_unique<Impl>()){
    impl_->gpu=std::make_shared<GpuShaderRuntime>(device,std::move(host),tools.compiler,gpu::ResourceBudgets{},std::move(working));
    impl_->gpu->enable_cached_bytecode(tools.bytecode,tools.mode);
    // Without its tools' identity the cache stays off; the generation compiles as before.
    if(!tools.cache.empty())try{impl_->gpu->enable_cache(std::make_shared<gpu::ShaderCache>(tools.cache,std::vector<std::filesystem::path>{tools.compiler,tools.bytecode}));}
    catch(const std::exception&){}
}
OwnedRuntime::~OwnedRuntime()=default;
std::shared_ptr<RuntimeExtension> OwnedRuntime::combine(std::shared_ptr<RuntimeExtension> native){
    if(!impl_->extension.expired())throw std::runtime_error("One GPU registry belongs to one combined configuration");
    auto extension=owned_generation(std::move(native),impl_->gpu);impl_->extension=extension;return extension;
}
std::unique_ptr<PreparedConfigurationDraw> OwnedRuntime::prepare_draw(std::shared_ptr<IniRuntime> registry,
    const RuntimeDraw& draw,const ApplicationResourceState& application,bool shader_only,std::string& reason){
    reason.clear();const auto extension=impl_->extension.lock();
    if(!registry || !registry->uses_extension(extension.get())){reason="Prepared draw needs its matching original configuration";return {};}
    auto pending=std::unique_ptr<PreparedConfigurationDraw>(new PreparedConfigurationDraw);
    auto& state=*pending->impl_;state.registry=std::move(registry);state.gpu=impl_->gpu;
    try{
        auto& resources=state.gpu->resources();resources.begin_prepared_plan(application);state.armed=true;
        state.gpu->application_compute(draw);
        for(const auto& input:application.buffers)if(input.type!=ResourceCopyTargetType::THIS_RESOURCE)state.initial[{input.stage,input.type,input.slot}]=resources.binding(input.stage,input.type,input.slot);
        for(const auto& input:application.images)state.initial[{input.stage,input.type,input.slot}]=resources.binding(input.stage,input.type,input.slot);
        const bool ran=shader_only?state.registry->shader_draw(draw,state.plan,reason):state.registry->draw(draw,state.plan,reason);
        if(!ran || resources.failed()){
            if(reason.empty())reason="Original configuration refused prepared GPU work";
            return {};
        }
        if(!state.plan.prepared_final_state)throw std::runtime_error("Original draw plan has no final binding snapshot");
        // The original plan must have cut its tail at AfterDraw. Never silently
        // append an unpositioned operation after a host caller.
        if(resources.finish_prepared())throw std::runtime_error("Original draw plan left an unpositioned GPU segment");
        return pending;
    }catch(const std::exception& error){reason=error.what();return {};}
}
#include "native_configuration_bridge.h"
void OwnedRuntime::begin(VkQueue queue){impl_->gpu->resources().begin(queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);}
void OwnedRuntime::begin_deferred(){impl_->gpu->resources().begin(VK_NULL_HANDLE,GpuResourceRuntime::ExecutionSite::Deferred);}
bool OwnedRuntime::pending() const {return impl_->gpu->resources().pending();}
VkQueue OwnedRuntime::queue() const {return impl_->gpu->resources().queue();}
void OwnedRuntime::rebind(VkQueue queue){impl_->gpu->resources().rebind(queue);}
void OwnedRuntime::suspend(){impl_->gpu->resources().suspend();}
void OwnedRuntime::discard(){impl_->gpu->resources().discard();}
bool OwnedRuntime::scope_active() const {return impl_->gpu->resources().scope_active();}
bool OwnedRuntime::failed() const {return impl_->gpu->resources().failed();}
std::shared_ptr<gpu::ResourceSubmission> OwnedRuntime::finish(const gpu::ResourceCommands::SubmitSync& sync,VkQueue queue){
    return impl_->gpu->resources().finish(sync,queue);
}
void OwnedRuntime::backbuffer(gpu::Image image,DXGI_FORMAT format,bool shader_resource){
    impl_->gpu->resources().backbuffer(std::move(image),format,D3D11_BIND_RENDER_TARGET|(shader_resource?D3D11_BIND_SHADER_RESOURCE:0));
}
gpu::ResourceCommands& OwnedRuntime::commands(){return impl_->gpu->resources().commands();}
OwnedRuntime::ShaderStats OwnedRuntime::shader_stats() const{
    const auto& s=impl_->gpu->shader_stats();return {s.compiled,s.translated,s.compile_us,s.translate_us,s.cached};
}
bool OwnedRuntime::backbuffer_targeted() const {return impl_->gpu->resources().backbuffer_targeted();}
}
