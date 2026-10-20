#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace efmi::gpu {

// RabbitFX's texture roles in a game draw (D-082, docs/rabbitfx-roles.md): the diffuse map, the
// lightmap and the normal map a RabbitFX Mod's textures replace, found from the fragment module and
// the textures bound to it, as server/efmi_vk/texture_roles.py does. No shader table.

// One image sample of a fragment module: the DX11 t register of its image (D-076), where that image
// is bound, whether its result is decoded as a tangent-space normal and its instruction index.
struct RoleSample {
    uint32_t t = 0, set = 0, binding = 0, at = 0;
    bool decoded = false;
};
// The samples in instruction order. False (output unchanged) for a module the register rule does
// not read.
bool role_samples(const std::vector<uint32_t>& code, std::vector<RoleSample>& out, std::string& reason);

// The texture bound at a sample: Vulkan format, size and mip levels; known false when the layer
// cannot tell.
struct RoleTexture {
    bool known = false;
    uint32_t format = 0, width = 0, height = 0, levels = 0;
};
// 'S' an sRGB asset texture, 'N' a two-channel (BC5) one, 'U' another linear block-compressed one,
// 'o' anything else; an asset texture is square with a full mip chain.
char texture_class(const RoleTexture&);

struct TextureRoles {
    int32_t diffuse = -1, lightmap = -1, normal = -1; // DX11 t registers; -1 for a role not found
};
TextureRoles texture_roles(const std::vector<RoleSample>& samples, const std::function<RoleTexture(const RoleSample&)>& texture);

} // namespace efmi::gpu
