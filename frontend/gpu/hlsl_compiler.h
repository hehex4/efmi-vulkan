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
enum class ShaderBufferKind {Unspecified,Typed,Raw,Structured};
struct ShaderBinding {
    std::string name;
    char source_type='t';
    uint32_t slot=0,count=1,binding=0;
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t image_dimension=0,image_format=0,image_depth=0,block_size=0,stride=0;
    uint32_t scalar_type=0,scalar_width=0; // 1 float, 2 unsigned, 3 signed.
    bool image_array=false,image_multisample=false,counter=false;
    // Exact DXBC declarations, distinct from the executable's scalar stride.
    ShaderBufferKind source_buffer=ShaderBufferKind::Unspecified;
    uint32_t source_stride=0;
};
struct ShaderInterface {
    std::string name,semantic;
    uint32_t location=UINT32_MAX,builtin=UINT32_MAX;
    VkFormat format=VK_FORMAT_UNDEFINED;
    // Stable executable identity, retained before reflection annotations vanish.
    uint32_t spirv_id=0;
};
struct ShaderSignature {
    std::string semantic;
    uint32_t index=0,register_index=0,component_type=0,system_value=0,stream=0;
    uint32_t mask=0,read_write_mask=0;
};
// serialize_program (shader_cache.cpp) stores every field: a new field goes there too, with kFormat bumped.
struct HlslProgram {
    VkShaderStageFlagBits stage=VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t vulkan_version=VK_API_VERSION_1_1;
    std::vector<uint32_t> capabilities;
    std::vector<uint32_t> spirv;
    // Original shader-model-5 reference and reflection, not device execution.
    std::vector<uint8_t> dxbc;
    std::vector<ShaderBinding> bindings;
    std::vector<ShaderInterface> inputs,outputs;
    // Native SM5 register/component contracts, including tessellation patch IO.
    std::vector<ShaderSignature> input_signature,output_signature,patch_signature;
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
    // When DXC refuses a source the original compiler took, `reference` receives that compiler's
    // bytecode (what 3DMigoto runs) for the caller's bytecode translation; otherwise it is empty.
    bool compile(const HlslRequest&,HlslProgram&,std::string& reason,std::vector<uint8_t>* reference=nullptr);
    static uint32_t binding(char stage,char type,uint32_t slot,bool counter=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
