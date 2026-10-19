#pragma once
#include "runtime_host.h"
#include "gpu_shaders.h"

namespace efmi::frontend {
// One generation served by two extensions. `native` owns the named Core boundaries and the
// replaced-draw file plan; `gpu` executes the original GPU commands, but only while its host
// has an owned scope open (Present, Constants, key and reload lists). Replaced draws open no
// scope, so they keep the plan path unchanged. The native extension's own preparation runs
// with the GPU callbacks closed as well. Declaration references the GPU side executes are
// mirrored into the plan; references made by replaced draws are not visible to it.
std::shared_ptr<RuntimeExtension> owned_generation(std::shared_ptr<RuntimeExtension> native,std::shared_ptr<GpuShaderRuntime> gpu);
}
