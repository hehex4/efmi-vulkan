#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace efmi::gpu {
// The character dither of the game's fragment shaders (D-133, docs/rabbitfx-dither.md): the fade near
// the camera, a kill whose selecting branch depends on interleaved gradient noise of the pixel position
// (a multiplication by 52.9829) and on the character's instance window (the uniform block at set 2
// binding 0, DX11 cb2). How many such branches the module has; a noise kill without the instance window
// (another fade) and an alpha test are not counted. 0 for a malformed module.
uint32_t dither_kills(const std::vector<uint32_t>& code);
// The module with every such branch steered away from its kill (its condition a constant), as RabbitFX's
// replacement skips the discard while IniParams[172].w is set; every other word unchanged. False and the
// output unchanged when the module has none or cannot be read.
bool remove_dither(const std::vector<uint32_t>& code, std::vector<uint32_t>& output, std::string& reason);
}
