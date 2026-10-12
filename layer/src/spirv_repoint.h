// The narrow vertex-buffer rewrite decided in D-022, not a general SPIR-V optimizer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace efvk {

struct VertexRepoint {
    uint32_t bone_set = 0, bone_binding = 0;
    uint32_t own_set = 0;
    std::vector<uint32_t> words;
    std::string reason;
};

// Find exactly one storage block and the uniform block at (2, 0). Change only their
// Binding/DescriptorSet literals to (own_set, 0/1), preserving all fragment code.
bool repoint_vertex(const std::vector<uint32_t>& code, uint32_t own_set, VertexRepoint& out);

} // namespace efvk
