#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace efmi::gpu {

// DX11 registers of a game shader's bound resources, read from its SPIR-V alone (D-076,
// docs/source-slots.md). The game builds each shader with FXC and with DXC from one HLSL source;
// both keep the declaration order, and the DX11 build puts read-only buffers ahead of textures.
// A t register is therefore the rank among the module's shader resource views, buffers first, then
// images, each group in declaration order; an array takes one register per element. Constant
// buffers (b), samplers (s) and UAVs (u) are numbered in declaration order; only t is verified
// against observed slots. No DX11 data and no capture of a character is involved.
struct SourceRegister {
    enum Kind : uint8_t { ImageSrv, BufferSrv, TexelSrv, ImageUav, BufferUav, TexelUav, Sampler, ConstantBuffer };
    Kind kind = ImageSrv;
    char letter = 't';
    uint32_t index = 0, count = 1, set = 0, binding = 0;
    uint32_t dim = 0;       // SPIR-V Dim of an image: 0 1D, 1 2D, 2 3D, 3 Cube, 5 Buffer
    bool arrayed = false, multisampled = false;
};

// Every resource variable with a binding, numbered by the rule. False (output unchanged) for input
// this does not read: malformed words, a runtime-sized array, a resource of unknown type.
bool source_registers(const std::vector<uint32_t>& code, std::vector<SourceRegister>& out, std::string& reason);

// The resource that holds a register, or nullptr. A register inside an array returns the array;
// the caller decides whether it can address one element.
const SourceRegister* find_source_register(const std::vector<SourceRegister>& registers, char letter, uint32_t index);

} // namespace efmi::gpu
