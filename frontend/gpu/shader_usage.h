#pragma once
#include "hlsl_compiler.h"
#include <cstdint>

namespace efmi::gpu {
struct SampledPair {
    uint32_t image_binding=0,image_element=0,sampler_binding=0,sampler_element=0;
};
struct ShaderUsage {
    uint64_t workgroup_bytes=0;
    std::vector<SampledPair> sampled_pairs;
};
// Bounded analysis of a validated executable, independent of caller metadata.
// Unknown resource provenance/layout refuses instead of silently losing uses.
ShaderUsage shader_usage(const HlslProgram&);
}
