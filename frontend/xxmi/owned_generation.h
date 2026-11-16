#pragma once
#include "runtime_host.h"
#include "gpu_shaders.h"

namespace efmi::frontend {
// One original configuration served by an optional host extension and the GPU
// command adapter. Constants/Present use ordinary scopes; draw interception must
// explicitly open a prepared transaction with complete application source facts.
// There are no substituted Core lists or character loading boundaries.
std::shared_ptr<RuntimeExtension> owned_generation(std::shared_ptr<RuntimeExtension> native,std::shared_ptr<GpuShaderRuntime> gpu);
}
