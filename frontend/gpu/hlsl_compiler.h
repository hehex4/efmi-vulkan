#pragma once
#include <vulkan/vulkan.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace efmi::gpu {
struct HlslRequest {
    std::filesystem::path source;
    // Explicit legacy fallback directory; compilation never changes process CWD.
    std::filesystem::path working_directory;
    char stage='c';
    uint32_t flags=1u<<15; // Original D3DCOMPILE_OPTIMIZATION_LEVEL3.
    uint32_t vulkan_version=VK_API_VERSION_1_1;
    int recursive_include=0;
};
struct ShaderDependency {std::filesystem::path path;std::vector<uint8_t> bytes;};
struct ShaderBinding {
    std::string name;
    char source_type='t';
    uint32_t slot=0,count=1,binding=0;
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t image_dimension=0,image_format=0,image_depth=0,block_size=0,stride=0;
    uint32_t scalar_type=0,scalar_width=0; // 1 float, 2 unsigned, 3 signed.
    bool image_array=false,image_multisample=false,counter=false;
};
struct ShaderInterface {
    std::string name,semantic;
    uint32_t location=UINT32_MAX,builtin=UINT32_MAX;
    VkFormat format=VK_FORMAT_UNDEFINED;
};
struct HlslProgram {
    VkShaderStageFlagBits stage=VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t vulkan_version=VK_API_VERSION_1_1;
    std::vector<uint32_t> capabilities;
    std::vector<uint32_t> spirv;
    // Original shader-model-5 reference and reflection, not device execution.
    std::vector<uint8_t> dxbc;
    std::vector<ShaderBinding> bindings;
    std::vector<ShaderInterface> inputs,outputs;
    std::vector<ShaderDependency> dependencies;
    uint32_t threads[3]={0,0,0};
    std::string diagnostics;
};
// CPU compilation only. The DLL path is explicit and absolute, dependencies are
// read-only and there are no on-disk shader caches or global environment changes.
class HlslCompiler {
public:
    explicit HlslCompiler(const std::filesystem::path& dxcompiler);
    ~HlslCompiler();
    HlslCompiler(const HlslCompiler&)=delete;
    HlslCompiler& operator=(const HlslCompiler&)=delete;
    bool compile(const HlslRequest&,HlslProgram&,std::string& reason);
    static uint32_t binding(char stage,char type,uint32_t slot,bool counter=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
