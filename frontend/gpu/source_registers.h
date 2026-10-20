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
    uint32_t bytes = 0;     // a constant buffer: its block's size (last member's offset plus its size)
    uint32_t stride = 0;    // a buffer view: the array stride of its block's last member, 0 when none
};

// Every resource variable with a binding, numbered by the rule. False (output unchanged) for input
// this does not read: malformed words, a runtime-sized array, a resource of unknown type.
bool source_registers(const std::vector<uint32_t>& code, std::vector<SourceRegister>& out, std::string& reason);

// The resource that holds a register, or nullptr. A register inside an array returns the array;
// the caller decides whether it can address one element.
const SourceRegister* find_source_register(const std::vector<SourceRegister>& registers, char letter, uint32_t index);

// What the original EFMIv1 Core reads off a draw's vertex shader before it touches a mesh (D-079),
// decided by its own rules on the registers above: Component_ReadConfig takes the first of vs-cb2,
// vs-cb1, vs-cb3 that holds 4096 vectors (the per-instance window), and
// MergedSkeleton_DetectBoneDataSource the rolling bone buffer when vs-t0 is a buffer of 16-byte
// elements and such a window exists. A size the Core reads from the bound view is the declared
// block here; the caller checks the block against what it binds.
struct CoreSourceFacts {
    int32_t vs_config_slot = -1;  // 1, 2 or 3; -1 none
    const SourceRegister* config = nullptr, *bones = nullptr;
    bool rolling_bones = false;
};
CoreSourceFacts core_source_facts(const std::vector<SourceRegister>& vertex_registers);

} // namespace efmi::gpu
