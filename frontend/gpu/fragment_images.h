#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace efmi::gpu {
struct ImageBindingMove {
    uint32_t source_set=0,source_binding=0,target_set=0,target_binding=0;
};
// Moves only explicitly named, nonarray float32 sampled 2D color images in a
// fragment-only module. Samplers, buffers, code and all other words are retained.
// Depth=Unknown is admitted only when traced uses avoid depth comparison and
// opaque escapes. Unsupported instruction extensions refuse this narrow path.
// Output is unchanged on refusal. This is not full SPIR-V validation, a material
// slot resolver or a pipeline/layout builder; validate source modules separately.
bool repoint_fragment_images(const std::vector<uint32_t>&,
                             const std::vector<ImageBindingMove>&,
                             std::vector<uint32_t>& output,std::string& reason);
}
