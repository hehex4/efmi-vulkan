#pragma once
#include "shader_commands.h"

namespace efmi::gpu {
struct NativeStorageUse {
    uint32_t set=0,binding=0,element=0;
    bool read=false,write=false,atomic=false;
};
// Conservative executable access provenance, preserving original native descriptor identities.
bool native_shader_storage_usage(const std::vector<uint32_t>& words,
                                 std::vector<NativeStorageUse>& uses,std::string& reason);
// Image bindings statically accessed by an entry point, for copies at its known descriptor layout.
bool native_shader_image_bindings(const std::vector<uint32_t>& words, const std::string& entry,
                                  std::vector<std::pair<uint32_t,uint32_t>>& bindings, std::string& reason);
// Native Vulkan modules keep their descriptor sets and entry points. The original application's
// module supplies the interface contract; no DX11 register remapping is performed here.
bool validate_native_shader(const std::vector<uint32_t>& words, VkShaderStageFlagBits stage,
                            const ShaderHost& host, const VkPhysicalDeviceFeatures& features, uint32_t api, std::string& reason);
bool native_shader_interface(const std::vector<uint32_t>& original, const std::vector<uint32_t>& replacement,
                             VkShaderStageFlagBits stage, const std::string& entry, std::string& reason);
}
