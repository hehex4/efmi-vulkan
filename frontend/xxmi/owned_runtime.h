#pragma once
#include "runtime.h"
#include "../gpu/shader_commands.h"
#include "../gpu/dxbc_compiler.h"
#include <dxgiformat.h>

namespace efmi::frontend {
class RuntimeExtension;
struct ApplicationResourceState;
// Source register addresses resolved by the native host from the current
// pipeline. Unknown source descriptions stay unknown, never become null slots.
struct NativeApplicationBinding {
    NativeBinding address;
    NativeResource resource;
    bool source_known=false;
    uint32_t structure_stride=0;
    bool raw=false;
    std::function<bool(uint32_t,uint32_t,uint32_t&)> region_hash;
    std::function<std::shared_ptr<const std::vector<uint8_t>>()> cached_data;
    gpu::ApplicationImage image;
};
struct NativeApplicationState {
    decltype(RuntimeDraw::source_declaration) source_declaration;
    std::vector<NativeApplicationBinding> bindings;
    bool independent_initialization=false;
    std::function<bool(uint64_t,const gpu::Image&,gpu::ApplicationImage&,std::string&)> observe_image;
};
// Retains the original registry until its speculative changes are admitted or
// cancelled. The caller serializes this entire lifetime with Present/reload and
// other commands of the same configuration. Destruction cancels an unadmitted
// draw; after admission the host retains plan packets with the executable CB.
class PreparedConfigurationDraw {
public:
    ~PreparedConfigurationDraw();
    PreparedConfigurationDraw(const PreparedConfigurationDraw&)=delete;
    PreparedConfigurationDraw& operator=(const PreparedConfigurationDraw&)=delete;
    const assets::FilePlan& plan() const;
    // Only after complete host resource/pipeline/pass/recording preflight.
    // Admission emits no command and does not authorize queue submission.
    bool admit(std::string& reason);
private:
    friend class OwnedRuntime;
    PreparedConfigurationDraw();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// The owned GPU runtime as a host renderer sees it (D-072): made on the application's device, it
// joins one generation with that generation's native extension and runs the original GPU
// commands only inside scopes the host opens. This interface keeps the original engine's headers
// out of the host.
class OwnedRuntime {
public:
    struct Tools {
        std::filesystem::path compiler;  // DXC, for CustomShader HLSL
        std::filesystem::path bytecode;  // vkd3d-shader, for the shipped shader caches (D-070)
        gpu::DxbcFloat32Mode mode = gpu::DxbcFloat32Mode::Software;
        std::filesystem::path cache; // the shader cache's folder (D-126); empty: none
    };
    OwnedRuntime(gpu::ResourceDevice, gpu::ShaderHost, const Tools&, std::filesystem::path working);
    ~OwnedRuntime();
    OwnedRuntime(const OwnedRuntime&) = delete;
    OwnedRuntime& operator=(const OwnedRuntime&) = delete;
    // The extension of one generation: `native` with this runtime behind the host scope.
    std::shared_ptr<RuntimeExtension> combine(std::shared_ptr<RuntimeExtension> native);
    // The matching combined registry, complete application source state and
    // original pre/caller/post execution form one cancellable host transaction.
    // No command is recorded/submitted here and no first-use work is waited.
    std::unique_ptr<PreparedConfigurationDraw> prepare_draw(std::shared_ptr<IniRuntime>,
        const RuntimeDraw&,const ApplicationResourceState&,bool shader_only,std::string& reason);
    std::unique_ptr<PreparedConfigurationDraw> prepare_native_draw(std::shared_ptr<IniRuntime>,
        const RuntimeDraw&,const NativeApplicationState&,bool shader_only,std::string& reason);
    // Scopes: one on the presenting queue, or a deferred one while a generation loads in the
    // background, suspended after loading and submitted with the first scope's finish().
    void begin(VkQueue);
    void begin_deferred();
    bool pending() const;
    // The queue the runtime's work is bound to; null until its first submission.
    VkQueue queue() const;
    // Moves later work to another queue of the family; the host waited for every earlier submission.
    void rebind(VkQueue);
    void suspend();
    void discard();
    bool scope_active() const;
    // The active scope's work is to be dropped (GpuResourceRuntime::failed): discard it instead of finishing.
    bool failed() const;
    std::shared_ptr<gpu::ResourceSubmission> finish(const gpu::ResourceCommands::SubmitSync&, VkQueue);
    // bb, r_bb and f_bb: the host's current back buffer. Null makes them read as null.
    void backbuffer(gpu::Image, DXGI_FORMAT, bool shader_resource);
    // Whether the scope since the last begin() drew into or copied into the back buffer (BUG-036).
    bool backbuffer_targeted() const;
    gpu::ResourceCommands& commands();
    // What the CustomShaders of this generation cost at load (GpuShaderRuntime::ShaderStats).
    struct ShaderStats {uint64_t compiled=0,translated=0,compile_us=0,translate_us=0,cached=0;};
    ShaderStats shader_stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
