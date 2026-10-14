#pragma once
#include "gpu_resources.h"
#include "graphics_state.h"
#include "../gpu/graphics_commands.h"

namespace efmi::frontend {
// Original CustomShader lists with owned Vulkan resources. The first execution
// adapter handles compute and owned VS/GS/PS draws; unsupported device cases refuse.
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
    uint64_t draws() const {return draws_;}
    size_t graphics_pipelines() const {return graphics_pipelines_.size();}
    const SourceGraphicsState& graphics_state() const {return graphics_;}
    void set_graphics_state(const SourceGraphicsState&);
    void set_scissor(const D3D11_RECT& value){scissor_=value;}
    // Explicit host-owned logical source state. Null restores the original
    // D3D11 default sampler; this is not an INI command or game-state import.
    void set_sampler(wchar_t stage,unsigned slot,const VkSamplerCreateInfo*);
private:
    struct Entry {
        std::array<bool,6> overrides{};
        std::array<std::shared_ptr<gpu::HlslProgram>,6> stages;
        gpu::Compute compute;
    };
    void compile(const RuntimeShaderDeclaration&);
    bool run(RunCustomShaderCommand&,CommandListState&,std::wstring&);
    bool draw(DrawCommand&,CommandListState&,std::wstring&);
    bool draw_graphics(DrawCommand&,CommandListState&);
    std::vector<gpu::ShaderDescriptor> descriptors(const std::vector<const gpu::HlslProgram*>&);
    gpu::Sampler sampler(wchar_t,unsigned);
    gpu::Sampler source_sampler(const D3D11_SAMPLER_DESC&);
    GpuResourceRuntime resources_;
    gpu::HlslCompiler compiler_;
    gpu::ShaderCommands commands_;
    uint32_t api_version_;
    std::filesystem::path working_directory_;
    DrawPlanAdapter* adapter_=nullptr;
    std::map<CustomShader*,Entry> shaders_;
    gpu::Compute compute_;
    std::array<std::shared_ptr<gpu::HlslProgram>,5> stages_;
    SourceGraphicsState graphics_=source_graphics_defaults();
    D3D11_RECT scissor_{};
    std::map<std::vector<uint64_t>,gpu::Graphics> graphics_pipelines_;
    std::map<std::vector<uint64_t>,gpu::Sampler> source_samplers_;
    std::map<std::pair<wchar_t,unsigned>,gpu::Sampler> samplers_;
    gpu::Sampler default_sampler_;
    uint64_t dispatches_=0,shader_calls_=0,draws_=0;
};
}
