#pragma once
#include "gpu_resources.h"
#include "../gpu/shader_commands.h"

namespace efmi::frontend {
// Original CustomShader lists with owned Vulkan resources. The first execution
// adapter handles direct compute; unimplemented graphics/device cases refuse.
class GpuShaderRuntime final : public RuntimeExtension {
public:
    GpuShaderRuntime(gpu::ResourceDevice,gpu::ShaderHost,const std::filesystem::path& compiler,gpu::ResourceBudgets={},
                     std::filesystem::path working_directory=std::filesystem::current_path());
    bool custom_shader_execution() const override {return true;}
    std::vector<std::wstring> install(DrawPlanAdapter&) override;
    bool initialize_parameters(CommandListState& s,std::wstring& why) override {return resources_.initialize_parameters(s,why);}
    void execution_failed() noexcept override {resources_.execution_failed();}
    GpuResourceRuntime& resources(){return resources_;}
    uint64_t dispatches() const {return dispatches_;}
    uint64_t shader_calls() const {return shader_calls_;}
private:
    struct Entry {
        std::array<bool,6> overrides{};
        std::array<std::shared_ptr<gpu::HlslProgram>,6> stages;
        gpu::Compute compute;
    };
    void compile(const RuntimeShaderDeclaration&);
    bool run(RunCustomShaderCommand&,CommandListState&,std::wstring&);
    bool draw(DrawCommand&,CommandListState&,std::wstring&);
    GpuResourceRuntime resources_;
    gpu::HlslCompiler compiler_;
    gpu::ShaderCommands commands_;
    uint32_t api_version_;
    std::filesystem::path working_directory_;
    DrawPlanAdapter* adapter_=nullptr;
    std::map<CustomShader*,Entry> shaders_;
    gpu::Compute compute_;
    uint64_t dispatches_=0,shader_calls_=0;
};
}
