#pragma once
#include <vulkan/vulkan.h>
#include <spirv/unified1/spirv.h>
#include <cstdint>
#include <vector>

namespace efmi::gpu {
// Known faults of drivers on SPIR-V this project produces (D-088), applied to the words a shader
// module is created from. NVIDIA (vendor 0x10de; seen with driver 591.86 on a GeForce RTX 5060
// Laptop GPU): creating a pipeline from a module whose entry point sets RoundingModeRTE for 32-bit
// floats and which has an OpFDiv, OpFRem or OpFMod ends the process inside the driver's shader
// compiler. Such a module loses that execution mode; NVIDIA's 32-bit float arithmetic rounds to
// nearest even without it, which is all the mode asks for. Every other module is used as it is.
inline bool nvidia_division_rounding(const std::vector<uint32_t>& spirv, const VkPhysicalDeviceProperties& device)
{
    if (device.vendorID != 0x10de || spirv.size() < 5) return false;
    bool division = false, rounding = false;
    for (size_t at = 5; at < spirv.size();) {
        const uint32_t count = spirv[at] >> 16, op = spirv[at] & 0xffff;
        if (!count || count > spirv.size() - at) return false;
        if (op == SpvOpFDiv || op == SpvOpFRem || op == SpvOpFMod) division = true;
        if (op == SpvOpExecutionMode && count == 4 && spirv[at + 2] == SpvExecutionModeRoundingModeRTE && spirv[at + 3] == 32) rounding = true;
        at += count;
    }
    return division && rounding;
}
inline std::vector<uint32_t> driver_module_words(const std::vector<uint32_t>& spirv, const VkPhysicalDeviceProperties& device)
{
    if (!nvidia_division_rounding(spirv, device)) return spirv;
    std::vector<uint32_t> words(spirv.begin(), spirv.begin() + 5);
    for (size_t at = 5; at < spirv.size();) {
        const uint32_t count = spirv[at] >> 16, op = spirv[at] & 0xffff;
        if (!(op == SpvOpExecutionMode && count == 4 && spirv[at + 2] == SpvExecutionModeRoundingModeRTE && spirv[at + 3] == 32))
            words.insert(words.end(), spirv.begin() + at, spirv.begin() + at + count);
        at += count;
    }
    return words;
}
}
