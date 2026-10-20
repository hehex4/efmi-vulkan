#pragma once
#include "runtime.h"
#include "../gpu/shader_commands.h"
#include "../gpu/dxbc_compiler.h"
#include <dxgiformat.h>

namespace efmi::frontend {
class RuntimeExtension;
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
    };
    OwnedRuntime(gpu::ResourceDevice, gpu::ShaderHost, const Tools&, std::filesystem::path working);
    ~OwnedRuntime();
    OwnedRuntime(const OwnedRuntime&) = delete;
    OwnedRuntime& operator=(const OwnedRuntime&) = delete;
    // The extension of one generation: `native` with this runtime behind the host scope.
    std::shared_ptr<RuntimeExtension> combine(std::shared_ptr<RuntimeExtension> native);
    // Scopes: one on the presenting queue, or a deferred one while a generation loads in the
    // background, suspended after loading and submitted with the first scope's finish().
    void begin(VkQueue);
    void begin_deferred();
    // A replaced draw's plan (P2, D-087): its owned work is recorded without a queue and stays
    // pending until a scope on the queue the work belongs to finishes.
    void begin_draw();
    bool pending() const;
    // The queue the runtime's work is bound to; null until its first submission.
    VkQueue queue() const;
    // Moves later work to another queue of the family; the host waited for every earlier submission.
    void rebind(VkQueue);
    void suspend();
    void discard();
    bool scope_active() const;
    std::shared_ptr<gpu::ResourceSubmission> finish(const gpu::ResourceCommands::SubmitSync&, VkQueue);
    // bb, r_bb and f_bb: the host's current back buffer. Null makes them read as null.
    void backbuffer(gpu::Image, DXGI_FORMAT, bool shader_resource);
    gpu::ResourceCommands& commands();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
