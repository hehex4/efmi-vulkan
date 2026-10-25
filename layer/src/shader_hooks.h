// What the shader hooks of a library stand for on Vulkan, read off the game's SPIR-V (D-113).
#pragma once

#include <cstdint>
#include <vector>

namespace efvk {

// EFMITTL's lit vertex shader: the outputs of the game's lit character vertex shader under DX11 (o1.xy,
// o2.xyz, o3.xyz, o4.xyzw, o5-o8.xyz, o9.x) at locations 0-8, a pipeline leaving out some it does not
// pass on, the UV at 0 and the instance at 8 kept, the instance index read, a storage and a uniform
// buffer bound. Its copy shader: one explicit-LOD sample of one 2D texture at the interpolated UV, its
// LOD from a uniform, stored as it is to the only output.
bool lit_character_vertex(const std::vector<uint32_t>& code);
bool copy_fragment(const std::vector<uint32_t>& code);

} // namespace efvk
