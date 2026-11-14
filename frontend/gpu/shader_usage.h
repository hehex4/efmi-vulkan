#pragma once
#include "hlsl_compiler.h"
#include <cstdint>

namespace efmi::gpu {
struct SampledPair {
    uint32_t image_binding=0,image_element=0,sampler_binding=0,sampler_element=0;
};
struct StorageImageUse {
    uint32_t binding=0,element=0;
    bool read=false,write=false,atomic=false;
};
struct ShaderUsage {
    uint64_t workgroup_bytes=0;
    std::vector<SampledPair> sampled_pairs;
    std::vector<StorageImageUse> storage_images;
};
// Bounded analysis of a validated executable, independent of caller metadata.
// Unknown resource provenance/layout refuses instead of silently losing uses.
ShaderUsage shader_usage(const HlslProgram&);
// Storage operations only; native callers keep unrelated shader interfaces unchanged.
std::vector<StorageImageUse> shader_storage_usage(const HlslProgram&);
// Shared storage only; native modules need no owned-descriptor reflection to check this limit.
uint64_t shader_workgroup_bytes(const HlslProgram&);
}
